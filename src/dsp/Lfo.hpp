#pragma once
#include "dsp/DspUtil.hpp"

namespace nocturne::dsp {

// New shapes are appended so existing patches keep their wave indices.
enum class LfoWave { Sine, Triangle, Square, Saw, ReverseSaw, SampleHold };

class Lfo {
 public:
  void setSampleRate(double sr) { sr_ = sr; }
  void seed(std::uint32_t s) { noise_.seed(s); held_ = noise_.next(); }
  void reset(float phase = 0.0f) { phase_ = phase; }

  // Returns -1..1. phaseOffset is 0..1 of a cycle. The phase is kept in double so
  // rates down to 0.01 Hz stay exact.
  float next(LfoWave wave, float rateHz, float phaseOffset = 0.0f) {
    float p = static_cast<float>(phase_) + phaseOffset;
    p -= std::floor(p);
    float out = 0.0f;
    switch (wave) {
      case LfoWave::Sine: out = std::sin(kTwoPi * p); break;
      case LfoWave::Triangle: out = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; break;
      case LfoWave::Square: out = p < 0.5f ? 1.0f : -1.0f; break;
      case LfoWave::Saw: out = 2.0f * p - 1.0f; break;
      case LfoWave::ReverseSaw: out = 1.0f - 2.0f * p; break;
      case LfoWave::SampleHold: out = held_; break;
    }
    phase_ += static_cast<double>(clampv(rateHz, 0.0f, static_cast<float>(sr_) * 0.25f)) / sr_;
    if (phase_ >= 1.0) {
      phase_ -= std::floor(phase_);
      held_ = noise_.next();  // new random step once per cycle
    }
    return out;
  }

 private:
  double sr_ = 48000.0;
  double phase_ = 0.0;
  float held_ = 0.0f;
  Noise noise_{0xA5A5u};
};

}  // namespace nocturne::dsp
