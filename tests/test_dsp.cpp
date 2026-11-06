#include <cmath>
#include <vector>

#include "TestHarness.hpp"
#include "dsp/Adsr.hpp"
#include "dsp/DspUtil.hpp"
#include "dsp/Effects.hpp"
#include "dsp/Filter.hpp"
#include "dsp/Lfo.hpp"
#include "dsp/Oscillator.hpp"

using namespace nocturne::dsp;

namespace {
constexpr double kSr = 48000.0;

std::vector<float> renderOsc(Wave w, float freq, int n, float pw = 0.5f) {
  Oscillator o;
  o.seedNoise(1);
  std::vector<float> out(static_cast<size_t>(n));
  const float dt = freq / static_cast<float>(kSr);
  for (auto& s : out) s = o.next(w, dt, pw);
  return out;
}

// Magnitude of a single frequency component (Goertzel).
double tone(const std::vector<float>& x, double freq) {
  double re = 0, im = 0;
  const double w = 2.0 * 3.14159265358979 * freq / kSr;
  for (size_t i = 0; i < x.size(); ++i) {
    const double win = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * static_cast<double>(i) / static_cast<double>(x.size()));
    re += x[i] * win * std::cos(w * static_cast<double>(i));
    im += x[i] * win * std::sin(w * static_cast<double>(i));
  }
  return std::sqrt(re * re + im * im) / static_cast<double>(x.size());
}

int risingZeroCrossings(const std::vector<float>& x) {
  int c = 0;
  for (size_t i = 1; i < x.size(); ++i)
    if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++c;
  return c;
}
}  // namespace

TEST(oscillator_frequency_is_accurate) {
  for (Wave w : {Wave::Sine, Wave::Triangle, Wave::Saw, Wave::Square}) {
    const auto x = renderOsc(w, 440.0f, 48000);
    CHECK_NEAR(risingZeroCrossings(x), 440, 2);
  }
}

TEST(oscillator_levels_and_dc) {
  for (Wave w : {Wave::Sine, Wave::Triangle, Wave::Saw, Wave::Square, Wave::Pulse}) {
    const auto x = renderOsc(w, 220.0f, 48000, 0.3f);
    double sum = 0;
    float peak = 0;
    for (float s : x) { sum += s; peak = std::max(peak, std::fabs(s)); }
    CHECK(std::fabs(sum / static_cast<double>(x.size())) < 0.02);
    CHECK(peak > 0.8f && peak < 1.25f);
  }
}

TEST(oscillator_pulse_width) {
  const auto x = renderOsc(Wave::Pulse, 100.0f, 48000, 0.25f);
  int high = 0;
  for (float s : x) high += s > 0.0f;
  // Pulse is DC-compensated, so the positive fraction is the *short* side.
  const double frac = static_cast<double>(high) / static_cast<double>(x.size());
  CHECK(std::fabs(frac - 0.25) < 0.02 || std::fabs(frac - 0.75) < 0.02);
}

TEST(oscillator_sine_is_pure) {
  const auto x = renderOsc(Wave::Sine, 1000.0f, 48000);
  const double fund = tone(x, 1000.0);
  CHECK_NEAR(fund, 0.25, 0.01);  // Hann window halves the amplitude/2
  CHECK(tone(x, 3000.0) < fund * 0.001);
}

TEST(oscillator_saw_is_band_limited) {
  // 3 kHz saw @ 48 kHz: the 9th harmonic (27 kHz) folds back to 21 kHz.
  const int n = 48000;
  const float f = 3000.0f;
  const auto blep = renderOsc(Wave::Saw, f, n);
  std::vector<float> naive(static_cast<size_t>(n));
  float ph = 0.0f;
  for (auto& s : naive) { s = 2.0f * ph - 1.0f; ph += f / static_cast<float>(kSr); if (ph >= 1) ph -= 1; }
  const double aliasBlep = tone(blep, 21000.0);
  const double aliasNaive = tone(naive, 21000.0);
  CHECK(aliasBlep < aliasNaive * 0.25);  // at least 12 dB better than naive
  const double fundB = tone(blep, 3000.0), fundN = tone(naive, 3000.0);
  CHECK_NEAR(fundB / fundN, 1.0, 0.1);   // the wanted tone is preserved
}

TEST(oscillator_triangle_and_pulse_are_band_limited) {
  const int n = 48000;
  const auto tri = renderOsc(Wave::Triangle, 7000.0f, n);
  const auto sq = renderOsc(Wave::Square, 4100.0f, n);
  // naive square @4100: harmonic 7 = 28.7k folds to 19.3k
  std::vector<float> naive(static_cast<size_t>(n));
  float ph = 0.0f;
  for (auto& s : naive) { s = ph < 0.5f ? 1.0f : -1.0f; ph += 4100.0f / static_cast<float>(kSr); if (ph >= 1) ph -= 1; }
  CHECK(tone(sq, 19300.0) < tone(naive, 19300.0) * 0.3);
  CHECK(tone(tri, 7000.0) > 0.1);
}

TEST(oscillator_noise_is_white_and_bounded) {
  const auto x = renderOsc(Wave::Noise, 440.0f, 96000);
  double sum = 0, sq = 0;
  for (float s : x) { CHECK(std::fabs(s) <= 1.0f); sum += s; sq += static_cast<double>(s) * s; }
  CHECK(std::fabs(sum / 96000.0) < 0.02);
  CHECK(sq / 96000.0 > 0.15);  // genuinely noisy, not silent
}

TEST(adsr_stage_transitions_and_timing) {
  Adsr e;
  e.setSampleRate(kSr);
  e.setParams(0.1f, 0.2f, 0.5f, 0.3f);
  CHECK(!e.active());
  e.noteOn();
  CHECK(e.stage() == Adsr::Stage::Attack);
  int i = 0;
  float v = 0;
  while (e.stage() == Adsr::Stage::Attack && i < 100000) { v = e.next(); ++i; }
  CHECK_NEAR(i, 0.1 * kSr, 0.002 * kSr);  // attack ~ set time
  CHECK(v >= 0.99f);
  CHECK(e.stage() == Adsr::Stage::Decay);
  for (i = 0; i < static_cast<int>(kSr * 1.5); ++i) v = e.next();
  CHECK(e.stage() == Adsr::Stage::Sustain);
  CHECK_NEAR(v, 0.5, 0.01);
  e.noteOff();
  CHECK(e.stage() == Adsr::Stage::Release);
  for (i = 0; i < static_cast<int>(0.3 * kSr); ++i) v = e.next();
  CHECK(v < 0.01f);  // -60 dB at release time
  for (i = 0; i < static_cast<int>(kSr); ++i) v = e.next();
  CHECK(!e.active());
  CHECK(v == 0.0f);
}

TEST(adsr_retrigger_is_continuous) {
  Adsr e;
  e.setSampleRate(kSr);
  e.setParams(0.05f, 0.1f, 0.6f, 0.5f);
  e.noteOn();
  for (int i = 0; i < 20000; ++i) e.next();
  e.noteOff();
  float last = 0;
  for (int i = 0; i < 2000; ++i) last = e.next();
  e.noteOn();
  const float after = e.next();
  CHECK(std::fabs(after - last) < 0.02f);  // no jump back to zero
}

TEST(filter_lowpass_highpass_bandpass_response) {
  auto gain = [](FilterType t, float cutoff, float freq) {
    StateVariableFilter f;
    f.setSampleRate(kSr);
    const auto x = renderOsc(Wave::Sine, freq, 48000);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = f.process(x[i], t, cutoff, 0.1f, 0.0f);
    std::vector<float> tail(y.begin() + 24000, y.end());
    return tone(tail, freq) / 0.25 * (24000.0 / 24000.0) * 1.0;
  };
  CHECK(gain(FilterType::LowPass, 1000.0f, 100.0f) > 0.85);
  CHECK(gain(FilterType::LowPass, 1000.0f, 8000.0f) < 0.05);
  CHECK(gain(FilterType::HighPass, 1000.0f, 8000.0f) > 0.85);
  CHECK(gain(FilterType::HighPass, 1000.0f, 100.0f) < 0.05);
  CHECK(gain(FilterType::BandPass, 1000.0f, 1000.0f) > gain(FilterType::BandPass, 1000.0f, 100.0f) * 3.0);
  CHECK(gain(FilterType::BandPass, 1000.0f, 1000.0f) > gain(FilterType::BandPass, 1000.0f, 9000.0f) * 3.0);
}

TEST(filter_is_stable_under_extreme_settings) {
  for (FilterType t : {FilterType::LowPass, FilterType::HighPass, FilterType::BandPass}) {
    for (float cutoff : {1.0f, 20.0f, 500.0f, 20000.0f, 40000.0f}) {
      for (float res : {0.0f, 0.5f, 1.0f, 2.0f}) {
        StateVariableFilter f;
        f.setSampleRate(kSr);
        Oscillator o;
        o.seedNoise(7);
        float peak = 0.0f;
        for (int i = 0; i < 48000; ++i) {
          // noise with sweeping cutoff: the hardest case for stability
          const float c = cutoff * (1.0f + 0.9f * std::sin(static_cast<float>(i) * 0.001f));
          const float y = f.process(o.next(Wave::Noise, 0.01f), t, c, res, 1.0f);
          CHECK(std::isfinite(y));
          peak = std::max(peak, std::fabs(y));
        }
        CHECK(peak < 30.0f);
      }
    }
  }
}

TEST(filter_resonance_boosts_cutoff_region) {
  auto gainAt = [](float res) {
    StateVariableFilter f;
    f.setSampleRate(kSr);
    const auto x = renderOsc(Wave::Sine, 1000.0f, 48000);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = f.process(x[i], FilterType::LowPass, 1000.0f, res, 0.0f);
    return tone(std::vector<float>(y.begin() + 24000, y.end()), 1000.0);
  };
  CHECK(gainAt(0.9f) > gainAt(0.1f) * 2.0);
}

TEST(lfo_shapes_and_rate) {
  Lfo l;
  l.setSampleRate(kSr);
  l.reset(0.0f);
  int crossings = 0;
  float prev = 0, mn = 9, mx = -9;
  for (int i = 0; i < 48000; ++i) {
    const float v = l.next(LfoWave::Sine, 5.0f);
    if (prev < 0 && v >= 0) ++crossings;
    prev = v; mn = std::min(mn, v); mx = std::max(mx, v);
  }
  CHECK_NEAR(crossings, 5, 1);
  CHECK(mn >= -1.0001f && mx <= 1.0001f && mx > 0.99f && mn < -0.99f);
  for (LfoWave w : {LfoWave::Triangle, LfoWave::Square, LfoWave::Saw}) {
    l.reset(0.0f);
    for (int i = 0; i < 1000; ++i) { const float v = l.next(w, 3.0f); CHECK(v >= -1.0001f && v <= 1.0001f); }
  }
}

TEST(delay_echoes_and_feedback_is_safe) {
  Delay d;
  d.prepare(kSr, 2.0f);
  const float t = 4800.0f;  // 100 ms
  std::vector<float> y(48000);
  for (size_t i = 0; i < y.size(); ++i) y[i] = d.process(i == 0 ? 1.0f : 0.0f, t, 0.5f, 16000.0f, 1.0f);
  // The echo arrives ~100 ms after the impulse, smaller than the original.
  size_t peak = 0;
  for (size_t i = 100; i < y.size(); ++i) if (std::fabs(y[i]) > std::fabs(y[peak < 100 ? 100 : peak])) peak = i;
  CHECK_NEAR(static_cast<double>(peak), 4800.0, 40.0);
  // Max feedback request must decay, never run away.
  Delay d2;
  d2.prepare(kSr, 2.0f);
  float last = 0;
  for (int i = 0; i < 480000; ++i) last = d2.process(i < 100 ? 1.0f : 0.0f, 2000.0f, 5.0f, 8000.0f, 1.0f);
  CHECK(std::isfinite(last) && std::fabs(last) < 2.0f);
}

TEST(saturator_soft_clips) {
  Saturator s;
  s.prepare(kSr);
  float peak = 0;
  for (int i = 0; i < 48000; ++i) {
    const float x = 4.0f * std::sin(0.05f * static_cast<float>(i));
    const float y = s.process(x, 1.0f, 16000.0f, 1.0f, 1.0f);
    CHECK(std::isfinite(y));
    peak = std::max(peak, std::fabs(y));
  }
  CHECK(peak < 1.5f);
  CHECK(peak > 0.5f);
}

TEST(reverb_has_a_tail_and_decays) {
  Reverb r;
  r.prepare(kSr);
  float l = 0, rr = 0;
  double early = 0, late = 0;
  for (int i = 0; i < 48000 * 6; ++i) {
    r.process(i == 0 ? 1.0f : 0.0f, 0.7f, 0.5f, l, rr);
    CHECK(std::isfinite(l) && std::isfinite(rr));
    if (i > 2000 && i < 24000) early += std::fabs(l);
    if (i > 48000 * 5) late += std::fabs(l);
  }
  CHECK(early > 0.01);
  CHECK(late < early * 0.2);
}
