#include "dsp/Oscillator.hpp"

namespace nocturne::dsp {

float polyBlep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

float polyBlamp(float tau) {
  const float a = std::fabs(tau);
  if (a >= 1.0f) return 0.0f;
  const float u = 1.0f - a;
  return u * u * u * (1.0f / 6.0f);
}

namespace {
inline float wrap01(float x) {
  x -= std::floor(x);
  return x;
}
}  // namespace

float Oscillator::next(Wave wave, float dt, float pw, float phaseOffset) {
  dt = clampv(dt, 1e-7f, 0.49f);
  pw = clampv(pw, 0.02f, 0.98f);
  const float p = wrap01(static_cast<float>(phase_) + phaseOffset);
  float out = 0.0f;

  switch (wave) {
    case Wave::Sine:
      out = std::sin(kTwoPi * p);
      break;
    case Wave::Saw:
      out = 2.0f * p - 1.0f - polyBlep(p, dt);
      break;
    case Wave::Square:
    case Wave::Pulse: {
      const float w = wave == Wave::Square ? 0.5f : pw;
      float v = p < w ? 1.0f : -1.0f;
      v += polyBlep(p, dt);
      v -= polyBlep(wrap01(p - w), dt);
      const float dc = 2.0f * w - 1.0f;  // remove the DC offset of asymmetric pulses
      out = (v - dc) / (1.0f + std::fabs(dc));
      break;
    }
    case Wave::Triangle: {
      float v = p < 0.5f ? -1.0f + 4.0f * p : 3.0f - 4.0f * p;
      // Slope discontinuities at p = 0 (+8/phase) and p = 0.5 (-8/phase).
      float tau0 = p < 0.5f ? p / dt : (p - 1.0f) / dt;
      v += 8.0f * dt * polyBlamp(tau0);
      v -= 8.0f * dt * polyBlamp((p - 0.5f) / dt);
      out = v;
      break;
    }
    case Wave::Noise:
      out = noise_.next();
      break;
  }

  phase_ += static_cast<double>(dt);
  if (phase_ >= 1.0) phase_ -= std::floor(phase_);
  return out;
}

}  // namespace nocturne::dsp
