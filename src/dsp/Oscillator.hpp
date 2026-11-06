#pragma once
#include "dsp/DspUtil.hpp"

namespace nocturne::dsp {

enum class Wave { Sine, Triangle, Saw, Square, Pulse, Noise };

// Band-limited oscillator. Discontinuities are corrected with PolyBLEP and the
// triangle's slope discontinuities with PolyBLAMP, so no naive aliasing.
class Oscillator {
 public:
  void seedNoise(std::uint32_t s) { noise_.seed(s); }
  void reset(float phase = 0.0f) { phase_ = phase; }
  float phase() const { return static_cast<float>(phase_); }

  // dt = frequency / sampleRate (clamped internally), pw = pulse width 0..1,
  // phaseOffset = 0..1 added to the read position.
  float next(Wave wave, float dt, float pw = 0.5f, float phaseOffset = 0.0f);

 private:
  double phase_ = 0.0;  // double: sub-20 Hz increments would stall in float
  Noise noise_;
};

// Reference helpers (public for tests).
float polyBlep(float t, float dt);   // t = phase 0..1, discontinuity at t = 0
float polyBlamp(float tau);          // tau = distance from kink in samples (-1..1)

}  // namespace nocturne::dsp
