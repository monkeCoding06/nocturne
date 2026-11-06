#pragma once
#include <array>

#include "modules/ModuleSpec.hpp"

namespace nocturne {

constexpr int kBlock = 32;  // internal processing chunk (samples)

// Per-voice state handed to voice modules every chunk.
struct VoiceCtx {
  float pitch = 0.0f;     // octaves relative to middle C (MIDI 60), glide applied
  float gate = 0.0f;      // 0 or 1
  float velocity = 1.0f;  // 0..1
  int note = 60;
};

struct ModuleIO {
  const float* in[kMaxPorts] = {};  // nullptr when nothing is patched in
  float* out[kMaxPorts] = {};
  int n = 0;
  const VoiceCtx* voice = nullptr;  // null for global modules
};

// DSP module base. All methods except construction/prepare run on the audio
// thread and must not allocate, lock, or block.
class Module {
 public:
  virtual ~Module() = default;

  // Called once after creation, off the audio thread (may allocate).
  virtual void prepare(double sampleRate) { sr_ = sampleRate; }
  virtual void reset() {}
  // Voice (re)start: new note on this voice.
  virtual void trigger(const VoiceCtx&) {}
  virtual void gateOff() {}
  virtual void process(const ModuleIO& io) = 0;
  virtual bool busy() const { return false; }

  void setParam(int idx, float v) {
    if (idx >= 0 && idx < kMaxParams) params_[static_cast<size_t>(idx)] = v;
  }
  void setAllParams(const float* p, int count) {
    for (int i = 0; i < count && i < kMaxParams; ++i) params_[static_cast<size_t>(i)] = p[i];
  }
  float param(int idx) const { return params_[static_cast<size_t>(idx)]; }

 protected:
  double sr_ = 48000.0;
  std::array<float, kMaxParams> params_{};
};

}  // namespace nocturne
