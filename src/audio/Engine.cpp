#include "audio/Engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace nocturne {

Engine::Engine(double sr) : sr_(sr) {
  timers_.fill(-1.0);
  for (auto& s : scope_) s.store(0.0f, std::memory_order_relaxed);
  alloc_.configure(8, VoiceMode::Poly, true);
}

Engine::~Engine() {
  EngineEvent e;
  while (queue_.pop(e))
    if (e.type == EngineEvent::Type::SwapRack) delete e.rack;
  Rack* r;
  while (garbage_.pop(r)) delete r;
  delete pending_;
  delete rack_;
}

// ------------------------------------------------------- control thread
bool Engine::post(const EngineEvent& e) { return queue_.push(e); }

bool Engine::postSwap(std::unique_ptr<Rack> rack) {
  EngineEvent e;
  e.type = EngineEvent::Type::SwapRack;
  e.rack = rack.get();
  if (!queue_.push(e)) return false;
  rack.release();
  return true;
}

bool Engine::noteOn(int note, float velocity, float holdSeconds) {
  EngineEvent e;
  e.type = EngineEvent::Type::NoteOn;
  e.a = note; e.f = velocity; e.g = holdSeconds;
  return post(e);
}
bool Engine::noteOff(int note) {
  EngineEvent e;
  e.type = EngineEvent::Type::NoteOff;
  e.a = note;
  return post(e);
}
bool Engine::noteExtend(int note, float holdSeconds) {
  EngineEvent e;
  e.type = EngineEvent::Type::NoteExtend;
  e.a = note; e.g = holdSeconds;
  return post(e);
}
bool Engine::allNotesOff() {
  EngineEvent e;
  e.type = EngineEvent::Type::AllNotesOff;
  return post(e);
}
bool Engine::panic() {
  EngineEvent e;
  e.type = EngineEvent::Type::Panic;
  return post(e);
}
bool Engine::setVolume(float v) {
  EngineEvent e;
  e.type = EngineEvent::Type::SetVolume;
  e.f = v;
  return post(e);
}

void Engine::collectGarbage() {
  Rack* r;
  while (garbage_.pop(r)) delete r;
}

void Engine::setSampleRate(double sr) { sr_ = sr; }

void Engine::flushQueue() {
  drainEvents();
  if (pending_) finalizePending();
}

// ---------------------------------------------------------- audio thread
void Engine::drainEvents() {
  EngineEvent e;
  while (queue_.pop(e)) handle(e);
}

void Engine::retire(Rack* r) {
  if (!r) return;
  if (!garbage_.push(r)) delete r;  // only if the UI stopped collecting: practically never
}

void Engine::releaseAllVoices() {
  for (int v = 0; v < alloc_.voiceCount(); ++v) applyAction(alloc_.releaseVoice(v));
  alloc_.clearStack();
  timers_.fill(-1.0);
  activeTimers_ = 0;
}

void Engine::applyAction(const VoiceAction& a) {
  if (a.kind == VoiceAction::Kind::None || !rack_ || a.voice < 0) return;
  VoiceState& vs = vstate_[static_cast<size_t>(a.voice)];
  if (a.kind == VoiceAction::Kind::Release) return;  // gate is derived from the allocator slot
  vs.note = a.note;
  vs.velocity = a.velocity;
  vs.pitchTarget = static_cast<float>(a.note - 60) / 12.0f;
  if (a.kind == VoiceAction::Kind::Trigger) {
    triggers_.fetch_add(1, std::memory_order_relaxed);
    if (a.cold || glide_ <= 0.0f) vs.pitchCur = vs.pitchTarget;
    VoiceCtx ctx;
    ctx.pitch = vs.pitchCur; ctx.gate = 1.0f; ctx.velocity = a.velocity; ctx.note = a.note;
    rack_->triggerVoice(a.voice, ctx, a.cold);
  }
}

void Engine::handle(const EngineEvent& e) {
  using T = EngineEvent::Type;
  switch (e.type) {
    case T::NoteOn: {
      if (!rack_ || e.a < 0 || e.a > 127) break;
      applyAction(alloc_.noteOn(e.a, std::min(std::max(e.f, 0.0f), 1.0f)));
      if (e.g > 0.0f) {
        if (timers_[static_cast<size_t>(e.a)] < 0.0) ++activeTimers_;
        timers_[static_cast<size_t>(e.a)] = e.g * sr_;
      } else if (timers_[static_cast<size_t>(e.a)] >= 0.0) {
        timers_[static_cast<size_t>(e.a)] = -1.0;
        --activeTimers_;
      }
      break;
    }
    case T::NoteOff: {
      if (!rack_ || e.a < 0 || e.a > 127) break;
      if (timers_[static_cast<size_t>(e.a)] >= 0.0) {
        timers_[static_cast<size_t>(e.a)] = -1.0;
        --activeTimers_;
      }
      applyAction(alloc_.noteOff(e.a));
      break;
    }
    case T::NoteExtend:
      if (e.a >= 0 && e.a <= 127 && timers_[static_cast<size_t>(e.a)] >= 0.0 && e.g > 0.0f)
        timers_[static_cast<size_t>(e.a)] = e.g * sr_;
      break;
    case T::AllNotesOff:
      releaseAllVoices();
      break;
    case T::Panic:
      releaseAllVoices();
      if (rack_)
        for (int v = 0; v < alloc_.voiceCount(); ++v) rack_->killVoice(v);
      break;
    case T::SetParam:
      if (Rack* r = pending_ ? pending_ : rack_) r->setParam(e.a, e.b, e.f);
      break;
    case T::SetWireAmount:
      if (Rack* r = pending_ ? pending_ : rack_) r->setWireAmount(e.a, e.f);
      break;
    case T::SetVolume:
      volumeTarget_ = std::min(std::max(e.f, 0.0f), 1.0f);
      break;
    case T::SwapRack:
      if (pending_) finalizePending();
      if (!rack_) {
        rack_ = e.rack;
        const auto& s = rack_->settings();
        alloc_.configure(rack_->voiceCount(), s.mode, s.legato);
        glide_ = s.glide;
      } else {
        pending_ = e.rack;  // swapped in after a short fade-out (see renderChunk)
      }
      break;
  }
}

void Engine::finalizePending() {
  Rack* next = pending_;
  pending_ = nullptr;
  next->adoptState(*rack_);
  const auto& s = next->settings();
  const bool voicingChanged = next->voiceCount() != alloc_.voiceCount() ||
                              (s.mode == VoiceMode::Mono) != (rack_->settings().mode == VoiceMode::Mono) ||
                              s.legato != rack_->settings().legato;
  retire(rack_);
  rack_ = next;
  glide_ = s.glide;
  if (voicingChanged) {
    alloc_.configure(rack_->voiceCount(), s.mode, s.legato);
    timers_.fill(-1.0);
    activeTimers_ = 0;
    for (int v = 0; v < rack_->voiceCount(); ++v) rack_->killVoice(v);
  }
}

void Engine::renderChunk(float* l, float* r, int n) {
  if (!rack_) {
    std::fill(l, l + n, 0.0f);
    std::fill(r, r + n, 0.0f);
    return;
  }
  if (activeTimers_ > 0) {
    for (int note = 0; note < 128; ++note) {
      double& t = timers_[static_cast<size_t>(note)];
      if (t < 0.0) continue;
      t -= n;
      if (t <= 0.0) {
        t = -1.0;
        --activeTimers_;
        applyAction(alloc_.noteOff(note));
      }
    }
  }

  rack_->beginChunk();
  const float glideCoef = glide_ > 0.0f ? 1.0f - std::exp(-static_cast<float>(n) / (glide_ * static_cast<float>(sr_))) : 1.0f;
  for (int v = 0; v < alloc_.voiceCount(); ++v) {
    const auto& slot = alloc_.slot(v);
    if (!slot.active) continue;
    VoiceState& vs = vstate_[static_cast<size_t>(v)];
    vs.pitchCur += (vs.pitchTarget - vs.pitchCur) * glideCoef;
    VoiceCtx ctx;
    ctx.pitch = vs.pitchCur;
    ctx.gate = slot.gate ? 1.0f : 0.0f;
    ctx.velocity = slot.velocity;
    ctx.note = slot.note;
    rack_->processVoice(v, ctx, n);
    if (rack_->voiceFinished(v)) alloc_.voiceFinished(v);
  }
  rack_->processGlobal(n, l, r);

  if (pending_) {
    // Edit in progress: duck this chunk to silence, then swap racks (state is adopted).
    for (int i = 0; i < n; ++i) {
      const float g = 1.0f - static_cast<float>(i + 1) / static_cast<float>(n);
      l[i] *= g;
      r[i] *= g;
    }
    finalizePending();
    fadeIn_ = true;
  } else if (fadeIn_) {
    for (int i = 0; i < n; ++i) {
      const float g = static_cast<float>(i + 1) / static_cast<float>(n);
      l[i] *= g;
      r[i] *= g;
    }
    fadeIn_ = false;
  }
}

void Engine::render(float* out, int frames) {
  const auto t0 = std::chrono::steady_clock::now();
  drainEvents();

  float peakL = 0.0f, peakR = 0.0f;
  std::uint32_t sw = scopeWrite_.load(std::memory_order_relaxed);
  std::uint32_t clips = 0;
  int pos = 0;
  while (pos < frames) {
    const int n = std::min(kBlock, frames - pos);
    renderChunk(chunkL_, chunkR_, n);
    for (int i = 0; i < n; ++i) {
      volume_ += (volumeTarget_ - volume_) * 0.002f;
      float l = chunkL_[i] * volume_;
      float r = chunkR_[i] * volume_;
      if (!(l == l)) l = 0.0f;
      if (!(r == r)) r = 0.0f;
      if (std::fabs(l) > 1.0f || std::fabs(r) > 1.0f) ++clips;
      l = std::min(std::max(l, -1.0f), 1.0f);
      r = std::min(std::max(r, -1.0f), 1.0f);
      out[(pos + i) * 2] = l;
      out[(pos + i) * 2 + 1] = r;
      peakL = std::max(peakL, std::fabs(l));
      peakR = std::max(peakR, std::fabs(r));
      if (((pos + i) & 1) == 0) {
        scope_[sw & (kScopeSize - 1)].store(0.5f * (l + r), std::memory_order_relaxed);
        ++sw;
      }
    }
    pos += n;
  }
  scopeWrite_.store(sw, std::memory_order_release);
  if (clips) clips_.fetch_add(clips, std::memory_order_relaxed);

  const float fall = std::exp(-static_cast<float>(frames) / static_cast<float>(0.35 * sr_));
  peakL_.store(std::max(peakL, peakL_.load(std::memory_order_relaxed) * fall), std::memory_order_relaxed);
  peakR_.store(std::max(peakR, peakR_.load(std::memory_order_relaxed) * fall), std::memory_order_relaxed);
  activeVoices_.store(alloc_.activeVoices(), std::memory_order_relaxed);
  gatedVoices_.store(alloc_.gatedVoices(), std::memory_order_relaxed);

  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const float load = static_cast<float>(elapsed / (frames / sr_));
  cpu_.store(cpu_.load(std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
}

int Engine::readScope(float* dst, int count) const {
  count = std::min(count, kScopeSize);
  const std::uint32_t w = scopeWrite_.load(std::memory_order_acquire);
  for (int i = 0; i < count; ++i) {
    const std::uint32_t idx = w - static_cast<std::uint32_t>(count) + static_cast<std::uint32_t>(i);
    dst[i] = scope_[idx & (kScopeSize - 1)].load(std::memory_order_relaxed);
  }
  return count;
}

}  // namespace nocturne
