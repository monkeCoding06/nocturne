#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nocturne::dsp {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

template <typename T>
constexpr T clampv(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

inline float dbToLin(float db) { return std::pow(10.0f, db * 0.05f); }
inline float linToDb(float lin) { return 20.0f * std::log10(std::max(lin, 1e-9f)); }

// Replaces NaN/Inf and flushes denormals; last line of defence for recursive state.
inline float sanitize(float v) {
  if (!(std::fabs(v) < 1e6f)) return 0.0f;
  if (std::fabs(v) < 1e-20f) return 0.0f;
  return v;
}

// Smooth saturation that is linear below `knee` and asymptotically approaches 1.
inline float softLimit(float x, float knee = 0.8f) {
  const float a = std::fabs(x);
  if (a <= knee) return x;
  const float over = (a - knee) / (1.0f - knee);
  const float y = knee + (1.0f - knee) * std::tanh(over);
  return x < 0 ? -y : y;
}

// One-pole smoothing for parameter changes (prevents zipper noise and clicks).
class Smoothed {
 public:
  void setSampleRate(double sr) { sr_ = sr; setTime(timeSec_); }
  void setTime(float sec) {
    timeSec_ = sec;
    coef_ = sec <= 0.0f ? 1.0f : 1.0f - std::exp(-1.0f / (sec * static_cast<float>(sr_)));
  }
  void snap(float v) { value_ = target_ = v; }
  void setTarget(float t) { target_ = t; }
  float target() const { return target_; }
  float value() const { return value_; }
  float next() {
    value_ += (target_ - value_) * coef_;
    return value_;
  }
  bool settled() const { return std::fabs(target_ - value_) < 1e-6f; }

 private:
  double sr_ = 48000.0;
  float timeSec_ = 0.01f;
  float coef_ = 0.01f;
  float value_ = 0.0f;
  float target_ = 0.0f;
};

// xorshift32: allocation-free noise generator for the audio thread.
class Noise {
 public:
  explicit Noise(std::uint32_t seed = 0x1234567u) : s_(seed ? seed : 1u) {}
  void seed(std::uint32_t seed) { s_ = seed ? seed : 1u; }
  float next() {  // -1..1
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return static_cast<float>(static_cast<std::int32_t>(s_)) * (1.0f / 2147483648.0f);
  }

 private:
  std::uint32_t s_;
};

}  // namespace nocturne::dsp
