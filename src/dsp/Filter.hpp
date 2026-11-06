#pragma once
#include "dsp/DspUtil.hpp"

namespace nocturne::dsp {

enum class FilterType { LowPass, HighPass, BandPass };

// Zavalishin trapezoidal-integration state variable filter. Unconditionally
// stable for any cutoff below Nyquist and well behaved under fast modulation.
class StateVariableFilter {
 public:
  void setSampleRate(double sr) { sr_ = static_cast<float>(sr); }
  void reset() { ic1_ = ic2_ = 0.0f; }

  // cutoff in Hz, resonance 0..1, drive 0..1.
  float process(float x, FilterType type, float cutoffHz, float resonance, float drive) {
    const float fc = clampv(cutoffHz, 10.0f, sr_ * 0.45f);
    const float g = std::tan(kPi * fc / sr_);
    // k = 1/Q. resonance 1 -> k = 0.04 (near self oscillation, still stable).
    const float res = clampv(resonance, 0.0f, 1.0f);
    const float k = 2.0f - 1.96f * res;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    if (drive > 0.001f) {
      const float gain = 1.0f + drive * 7.0f;
      x = std::tanh(x * gain) / std::tanh(gain * 0.5f + 0.5f);
    }
    const float v3 = x - ic2_;
    const float v1 = a1 * ic1_ + a2 * v3;
    const float v2 = ic2_ + a2 * ic1_ + a3 * v3;
    ic1_ = sanitize(2.0f * v1 - ic1_);
    ic2_ = sanitize(2.0f * v2 - ic2_);

    switch (type) {
      case FilterType::LowPass: return v2;
      case FilterType::BandPass: return k * v1;
      case FilterType::HighPass: return x - k * v1 - v2;
    }
    return v2;
  }

 private:
  float sr_ = 48000.0f;
  float ic1_ = 0.0f, ic2_ = 0.0f;
};

}  // namespace nocturne::dsp
