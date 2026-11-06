#pragma once
#include <vector>

#include "dsp/DspUtil.hpp"

namespace nocturne::dsp {

// Feedback delay with interpolated, smoothly modulatable delay time. The feedback
// path is low-passed, DC-blocked and soft-clipped so it can never run away.
class Delay {
 public:
  static constexpr float kMaxFeedback = 0.95f;

  void prepare(double sr, float maxSeconds);   // allocates; call off the audio thread
  void reset();

  // timeSamples may change every sample (smoothed by the caller).
  float process(float in, float timeSamples, float feedback, float dampHz, float mix);

 private:
  std::vector<float> buf_;
  int mask_ = 0;
  int write_ = 0;
  float sr_ = 48000.0f;
  float damp_ = 0.0f;
  float dcX_ = 0.0f, dcY_ = 0.0f;
};

// Tanh saturator running at 2x oversampling to keep harmonics from folding back.
class Saturator {
 public:
  void prepare(double sr);
  void reset();
  // drive 0..1, toneHz low-pass after saturation, mix 0..1, level makeup gain.
  float process(float x, float drive, float toneHz, float mix, float level);

 private:
  struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass(float fc, float fs, float q);
    float process(float x) {
      const float y = b0 * x + z1;
      z1 = sanitize(b1 * x - a1 * y + z2);
      z2 = sanitize(b2 * x - a2 * y);
      return y;
    }
    void reset() { z1 = z2 = 0; }
  };
  Biquad up_[2], down_[2];
  float sr_ = 48000.0f;
  float tone_ = 0.0f;
};

// Freeverb-style stereo reverb (8 damped combs + 4 allpasses per channel).
class Reverb {
 public:
  void prepare(double sr);
  void reset();
  // size 0..1, damp 0..1. Returns wet signal only.
  void process(float in, float size, float damp, float& wetL, float& wetR);

 private:
  struct Comb {
    std::vector<float> buf;
    int idx = 0;
    float store = 0.0f;
    float process(float in, float feedback, float damp) {
      const float out = buf[static_cast<size_t>(idx)];
      store = sanitize(out * (1.0f - damp) + store * damp);
      buf[static_cast<size_t>(idx)] = in + store * feedback;
      if (++idx >= static_cast<int>(buf.size())) idx = 0;
      return out;
    }
  };
  struct Allpass {
    std::vector<float> buf;
    int idx = 0;
    float process(float in) {
      const float b = buf[static_cast<size_t>(idx)];
      const float out = -in + b;
      buf[static_cast<size_t>(idx)] = sanitize(in + b * 0.5f);
      if (++idx >= static_cast<int>(buf.size())) idx = 0;
      return out;
    }
  };
  static constexpr int kCombs = 8;
  static constexpr int kAllpass = 4;
  Comb combL_[kCombs], combR_[kCombs];
  Allpass apL_[kAllpass], apR_[kAllpass];
};

}  // namespace nocturne::dsp
