#include "dsp/Effects.hpp"

namespace nocturne::dsp {

// --------------------------------------------------------------- Chorus
void Chorus::prepare(double sr) {
  sr_ = static_cast<float>(sr);
  int n = 1;
  const int needed = static_cast<int>(0.06f * sr_) + 8;
  while (n < needed) n <<= 1;
  buf_.assign(static_cast<size_t>(n), 0.0f);
  mask_ = n - 1;
  reset();
}

void Chorus::reset() {
  std::fill(buf_.begin(), buf_.end(), 0.0f);
  write_ = 0;
}

float Chorus::tap(float t) const {
  const float readPos = static_cast<float>(write_) - t;
  const float fl = std::floor(readPos);
  const float frac = readPos - fl;
  const int i1 = static_cast<int>(fl) & mask_;
  const float ym1 = buf_[static_cast<size_t>((i1 - 1) & mask_)];
  const float y0 = buf_[static_cast<size_t>(i1)];
  const float y1 = buf_[static_cast<size_t>((i1 + 1) & mask_)];
  const float y2 = buf_[static_cast<size_t>((i1 + 2) & mask_)];
  const float c1 = 0.5f * (y1 - ym1);
  const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
  const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
  return ((c3 * frac + c2) * frac + c1) * frac + y0;
}

void Chorus::process(float in, float rateHz, float depthMs, float baseMs, float& wetL, float& wetR) {
  buf_[static_cast<size_t>(write_)] = sanitize(in);
  const float maxT = static_cast<float>(mask_ - 4);
  const float base = baseMs * 0.001f * sr_;
  const float depth = std::min(depthMs, baseMs - 0.5f) * 0.001f * sr_;
  const double incL = static_cast<double>(clampv(rateHz, 0.0f, 10.0f)) / sr_;
  const double incR = incL * 1.13;  // small rate offset between channels
  float l = 0.0f, r = 0.0f;
  for (int k = 0; k < kTaps; ++k) {
    l += tap(clampv(base + depth * std::sin(kTwoPi * static_cast<float>(phaseL_[k])), 2.0f, maxT));
    r += tap(clampv(base + depth * std::sin(kTwoPi * static_cast<float>(phaseR_[k])), 2.0f, maxT));
    phaseL_[k] += incL; if (phaseL_[k] >= 1.0) phaseL_[k] -= 1.0;
    phaseR_[k] += incR; if (phaseR_[k] >= 1.0) phaseR_[k] -= 1.0;
  }
  wetL = l * (1.0f / kTaps);
  wetR = r * (1.0f / kTaps);
  write_ = (write_ + 1) & mask_;
}

// ---------------------------------------------------------------- Delay
void Delay::prepare(double sr, float maxSeconds) {
  sr_ = static_cast<float>(sr);
  int n = 1;
  const int needed = static_cast<int>(maxSeconds * sr_) + 8;
  while (n < needed) n <<= 1;
  buf_.assign(static_cast<size_t>(n), 0.0f);
  mask_ = n - 1;
  reset();
}

void Delay::reset() {
  std::fill(buf_.begin(), buf_.end(), 0.0f);
  write_ = 0;
  damp_ = dcX_ = dcY_ = 0.0f;
}

float Delay::process(float in, float timeSamples, float feedback, float dampHz, float mix) {
  const float maxT = static_cast<float>(mask_ - 4);
  const float t = clampv(timeSamples, 2.0f, maxT);
  const float readPos = static_cast<float>(write_) - t;
  const float fl = std::floor(readPos);
  const float frac = readPos - fl;
  const int i1 = static_cast<int>(fl) & mask_;
  // 4-point Hermite interpolation
  const float ym1 = buf_[static_cast<size_t>((i1 - 1) & mask_)];
  const float y0 = buf_[static_cast<size_t>(i1)];
  const float y1 = buf_[static_cast<size_t>((i1 + 1) & mask_)];
  const float y2 = buf_[static_cast<size_t>((i1 + 2) & mask_)];
  const float c0 = y0;
  const float c1 = 0.5f * (y1 - ym1);
  const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
  const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
  const float delayed = ((c3 * frac + c2) * frac + c1) * frac + c0;

  const float a = std::exp(-kTwoPi * clampv(dampHz, 100.0f, sr_ * 0.45f) / sr_);
  damp_ = sanitize((1.0f - a) * delayed + a * damp_);
  // DC blocker in the loop
  const float dc = damp_ - dcX_ + 0.995f * dcY_;
  dcX_ = damp_;
  dcY_ = sanitize(dc);

  const float fb = clampv(feedback, 0.0f, kMaxFeedback);
  buf_[static_cast<size_t>(write_)] = sanitize(in + std::tanh(dc * fb));
  write_ = (write_ + 1) & mask_;
  return in * (1.0f - mix) + delayed * mix;
}

// ------------------------------------------------------------ Saturator
void Saturator::Biquad::lowpass(float fc, float fs, float q) {
  const float w0 = kTwoPi * fc / fs;
  const float cw = std::cos(w0);
  const float alpha = std::sin(w0) / (2.0f * q);
  const float a0 = 1.0f + alpha;
  b0 = (1.0f - cw) * 0.5f / a0;
  b1 = (1.0f - cw) / a0;
  b2 = b0;
  a1 = -2.0f * cw / a0;
  a2 = (1.0f - alpha) / a0;
}

void Saturator::prepare(double sr) {
  sr_ = static_cast<float>(sr);
  const float fs2 = sr_ * 2.0f;
  const float fc = sr_ * 0.42f;
  const float q[2] = {0.5412f, 1.3066f};  // 4th-order Butterworth
  for (int i = 0; i < 2; ++i) {
    up_[i].lowpass(fc, fs2, q[i]);
    down_[i].lowpass(fc, fs2, q[i]);
  }
  reset();
}

void Saturator::reset() {
  for (auto& b : up_) b.reset();
  for (auto& b : down_) b.reset();
  tone_ = 0.0f;
}

float Saturator::process(float x, float drive, float toneHz, float mix, float level) {
  const float gain = std::exp(clampv(drive, 0.0f, 1.0f) * 4.0f);  // 1x .. ~55x
  float out = 0.0f;
  for (int s = 0; s < 2; ++s) {
    float v = s == 0 ? x * 2.0f : 0.0f;
    v = up_[1].process(up_[0].process(v));
    v = std::tanh(v * gain);
    v = down_[1].process(down_[0].process(v));
    out = v;
  }
  // Compensate level growth so the drive control mostly changes timbre.
  const float comp = 1.0f / std::sqrt(gain);
  float wet = out * (0.5f + 0.5f * comp);
  const float a = std::exp(-kTwoPi * clampv(toneHz, 200.0f, sr_ * 0.45f) / sr_);
  tone_ = sanitize((1.0f - a) * wet + a * tone_);
  wet = tone_ * level;
  return x * (1.0f - mix) + wet * mix;
}

// --------------------------------------------------------------- Reverb
void Reverb::prepare(double sr) {
  static const int combTune[kCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
  static const int apTune[kAllpass] = {556, 441, 341, 225};
  constexpr int kSpread = 23;
  const double scale = sr / 44100.0;
  for (int i = 0; i < kCombs; ++i) {
    combL_[i].buf.assign(static_cast<size_t>(combTune[i] * scale), 0.0f);
    combR_[i].buf.assign(static_cast<size_t>((combTune[i] + kSpread) * scale), 0.0f);
  }
  for (int i = 0; i < kAllpass; ++i) {
    apL_[i].buf.assign(static_cast<size_t>(apTune[i] * scale), 0.0f);
    apR_[i].buf.assign(static_cast<size_t>((apTune[i] + kSpread) * scale), 0.0f);
  }
  reset();
}

void Reverb::reset() {
  for (auto& c : combL_) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.idx = 0; c.store = 0; }
  for (auto& c : combR_) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.idx = 0; c.store = 0; }
  for (auto& a : apL_) { std::fill(a.buf.begin(), a.buf.end(), 0.0f); a.idx = 0; }
  for (auto& a : apR_) { std::fill(a.buf.begin(), a.buf.end(), 0.0f); a.idx = 0; }
}

void Reverb::process(float in, float size, float damp, float& wetL, float& wetR) {
  const float feedback = 0.70f + 0.28f * clampv(size, 0.0f, 1.0f);  // < 1: always decays
  const float d = 0.05f + 0.75f * clampv(damp, 0.0f, 1.0f);
  const float input = in * 0.015f;
  float l = 0.0f, r = 0.0f;
  for (int i = 0; i < kCombs; ++i) {
    l += combL_[i].process(input, feedback, d);
    r += combR_[i].process(input, feedback, d);
  }
  for (int i = 0; i < kAllpass; ++i) {
    l = apL_[i].process(l);
    r = apR_[i].process(r);
  }
  wetL = l * 3.0f;
  wetR = r * 3.0f;
}

}  // namespace nocturne::dsp
