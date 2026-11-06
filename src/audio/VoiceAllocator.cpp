#include "audio/VoiceAllocator.hpp"

#include <algorithm>

namespace nocturne {

void VoiceAllocator::configure(int voices, VoiceMode mode, bool legato) {
  mode_ = mode;
  legato_ = legato;
  count_ = mode == VoiceMode::Mono ? 1 : std::min(std::max(voices, 1), kMaxVoices);
  clear();
}

void VoiceAllocator::clear() {
  for (auto& s : slots_) s = Slot{};
  stackSize_ = 0;
}

int VoiceAllocator::activeVoices() const {
  int n = 0;
  for (int i = 0; i < count_; ++i) n += slots_[static_cast<size_t>(i)].active ? 1 : 0;
  return n;
}

int VoiceAllocator::gatedVoices() const {
  int n = 0;
  for (int i = 0; i < count_; ++i) n += slots_[static_cast<size_t>(i)].gate ? 1 : 0;
  return n;
}

VoiceAction VoiceAllocator::noteOn(int note, float velocity) {
  return mode_ == VoiceMode::Mono ? monoOn(note, velocity) : polyOn(note, velocity);
}

VoiceAction VoiceAllocator::noteOff(int note) {
  if (mode_ == VoiceMode::Mono) return monoOff(note);
  for (int i = 0; i < count_; ++i) {
    auto& s = slots_[static_cast<size_t>(i)];
    if (s.gate && s.note == note) return releaseVoice(i);
  }
  return {};
}

VoiceAction VoiceAllocator::releaseVoice(int voice) {
  auto& s = slots_[static_cast<size_t>(voice)];
  if (!s.gate) return {};
  s.gate = false;
  return {VoiceAction::Kind::Release, voice, s.note, s.velocity, false, false};
}

void VoiceAllocator::voiceFinished(int voice) {
  auto& s = slots_[static_cast<size_t>(voice)];
  s.active = false;
  s.gate = false;
}

VoiceAction VoiceAllocator::polyOn(int note, float vel) {
  int chosen = -1;
  bool stolen = false;
  // 1. same note already on a voice (held or still releasing): reuse it
  for (int i = 0; i < count_; ++i) {
    const auto& s = slots_[static_cast<size_t>(i)];
    if (s.active && s.note == note) { chosen = i; stolen = false; break; }
  }
  // 2. oldest silent voice
  if (chosen < 0) {
    std::uint64_t best = ~0ull;
    for (int i = 0; i < count_; ++i) {
      const auto& s = slots_[static_cast<size_t>(i)];
      if (!s.active && s.stamp < best) { best = s.stamp; chosen = i; }
    }
  }
  // 3. oldest releasing voice
  if (chosen < 0) {
    std::uint64_t best = ~0ull;
    for (int i = 0; i < count_; ++i) {
      const auto& s = slots_[static_cast<size_t>(i)];
      if (s.active && !s.gate && s.stamp < best) { best = s.stamp; chosen = i; stolen = true; }
    }
  }
  // 4. oldest held voice
  if (chosen < 0) {
    std::uint64_t best = ~0ull;
    for (int i = 0; i < count_; ++i) {
      const auto& s = slots_[static_cast<size_t>(i)];
      if (s.stamp < best) { best = s.stamp; chosen = i; stolen = true; }
    }
  }
  auto& s = slots_[static_cast<size_t>(chosen)];
  const bool cold = !s.active;
  s.active = true;
  s.gate = true;
  s.note = note;
  s.velocity = vel;
  s.stamp = ++counter_;
  return {VoiceAction::Kind::Trigger, chosen, note, vel, cold, stolen};
}

void VoiceAllocator::removeFromStack(int note) {
  int w = 0;
  for (int i = 0; i < stackSize_; ++i)
    if (stack_[i] != note) stack_[w++] = stack_[i];
  stackSize_ = w;
}

VoiceAction VoiceAllocator::monoOn(int note, float vel) {
  removeFromStack(note);
  if (stackSize_ >= kStack) {
    for (int i = 1; i < stackSize_; ++i) stack_[i - 1] = stack_[i];
    --stackSize_;
  }
  stack_[stackSize_++] = note;
  auto& s = slots_[0];
  const bool wasGated = s.gate;
  const bool cold = !s.active;
  s.active = true;
  s.gate = true;
  s.note = note;
  s.velocity = vel;
  s.stamp = ++counter_;
  if (wasGated && legato_) return {VoiceAction::Kind::Retarget, 0, note, vel, false, false};
  return {VoiceAction::Kind::Trigger, 0, note, vel, cold, false};
}

VoiceAction VoiceAllocator::monoOff(int note) {
  auto& s = slots_[0];
  const bool wasCurrent = s.gate && s.note == note;
  removeFromStack(note);
  if (!wasCurrent) return {};
  if (stackSize_ == 0) return releaseVoice(0);
  const int prev = stack_[stackSize_ - 1];
  s.note = prev;
  if (legato_) return {VoiceAction::Kind::Retarget, 0, prev, s.velocity, false, false};
  return {VoiceAction::Kind::Trigger, 0, prev, s.velocity, false, false};
}

}  // namespace nocturne
