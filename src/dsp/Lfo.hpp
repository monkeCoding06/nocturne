#pragma once
#include "dsp/DspUtil.hpp"

namespace nocturne::dsp {

enum class LfoWave { Sine, Triangle, Square, Saw };

class Lfo {
 public:
  void setSampleRate(double sr) { sr_ = static_cast<float>(sr); }
  void reset(float phase = 0.0f) { phase_ = phase; }

  // Returns -1..1.
  float next(LfoWave wave, float rateHz) {
    const float p = phase_;
    float out = 0.0f;
    switch (wave) {
      case LfoWave::Sine: out = std::sin(kTwoPi * p); break;
      case LfoWave::Triangle: out = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; break;
      case LfoWave::Square: out = p < 0.5f ? 1.0f : -1.0f; break;
      case LfoWave::Saw: out = 2.0f * p - 1.0f; break;
    }
    phase_ += clampv(rateHz, 0.0f, sr_ * 0.25f) / sr_;
    if (phase_ >= 1.0f) phase_ -= std::floor(phase_);
    return out;
  }

 private:
  float sr_ = 48000.0f;
  float phase_ = 0.0f;
};

}  // namespace nocturne::dsp
