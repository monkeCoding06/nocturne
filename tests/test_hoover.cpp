// Hoover / deep-bass / LFO / chorus tests. Everything is measured from the real engine output.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "TestHarness.hpp"
#include "audio/Engine.hpp"
#include "dsp/Effects.hpp"
#include "dsp/Lfo.hpp"
#include "input/NotePlayer.hpp"
#include "patch/Patch.hpp"
#include "patch/Presets.hpp"

using namespace nocturne;

namespace {
constexpr double kSr = 48000.0;

std::vector<float> renderMono(Engine& e, int frames) {
  std::vector<float> buf(static_cast<size_t>(frames) * 2), mono(static_cast<size_t>(frames));
  int pos = 0;
  while (pos < frames) {
    const int n = std::min(frames - pos, 256);
    e.render(buf.data() + pos * 2, n);
    pos += n;
  }
  for (int i = 0; i < frames; ++i) mono[static_cast<size_t>(i)] = 0.5f * (buf[static_cast<size_t>(i) * 2] + buf[static_cast<size_t>(i) * 2 + 1]);
  return mono;
}

struct Stereo { std::vector<float> l, r; };
Stereo renderStereo(Engine& e, int frames) {
  std::vector<float> buf(static_cast<size_t>(frames) * 2);
  int pos = 0;
  while (pos < frames) {
    const int n = std::min(frames - pos, 256);
    e.render(buf.data() + pos * 2, n);
    pos += n;
  }
  Stereo s;
  for (int i = 0; i < frames; ++i) { s.l.push_back(buf[static_cast<size_t>(i) * 2]); s.r.push_back(buf[static_cast<size_t>(i) * 2 + 1]); }
  return s;
}

void loadPatch(Engine& e, const Patch& p) {
  auto rack = Rack::build(p, kSr, nullptr);
  CHECK(rack != nullptr);
  if (rack) CHECK(e.postSwap(std::move(rack)));
  e.flushQueue();
}

void setp(Patch& p, const char* mod, const char* par, float v) {
  const int mi = p.findModule(mod);
  CHECK(mi >= 0);
  const ModuleSpec* spec = p.specOf(mi);
  const int pi = spec->paramIndex(par);
  CHECK(pi >= 0);
  p.modules[static_cast<size_t>(mi)].params[static_cast<size_t>(pi)] = spec->params[static_cast<size_t>(pi)].clamp(v);
}

// OSC(sine) -> AMP(ADSR) -> MASTER, with sustain at full level.
Patch sinePatch() {
  Patch p = makeEmptyPatch();
  std::string n;
  for (const char* t : {"osc", "adsr", "amp"}) CHECK(addModule(p, t, &n).empty());
  setp(p, "OSC1", "wave", 0);
  setp(p, "ADSR1", "attack", 0.01f); setp(p, "ADSR1", "sustain", 1.0f); setp(p, "ADSR1", "release", 0.1f);
  CHECK(connect(p, "OSC1", "OUT", "AMP1", "IN", 1.0f).empty());
  CHECK(connect(p, "ADSR1", "OUT", "AMP1", "GAIN", 1.0f).empty());
  CHECK(connect(p, "AMP1", "OUT", "MASTER", "IN", 1.0f).empty());
  return p;
}

double rising(const std::vector<float>& x, size_t from, size_t to) {
  int c = 0;
  for (size_t i = from + 1; i < to; ++i) if (x[i - 1] < 0 && x[i] >= 0) ++c;
  return c * kSr / static_cast<double>(to - from);
}

// Fundamental by autocorrelation within +-25% of the expected frequency.
double fundamental(const std::vector<float>& x, size_t from, size_t len, double expect) {
  const int lo = static_cast<int>(kSr / (expect * 1.25)), hi = static_cast<int>(kSr / (expect * 0.8));
  double best = -1e30;
  int bestLag = lo;
  for (int lag = lo; lag <= hi; ++lag) {
    double s = 0;
    for (size_t i = from; i + static_cast<size_t>(lag) < from + len; ++i) s += static_cast<double>(x[i]) * x[i + static_cast<size_t>(lag)];
    if (s > best) { best = s; bestLag = lag; }
  }
  return kSr / bestLag;
}

double rmsOf(const std::vector<float>& x, size_t from, size_t to) {
  double s = 0;
  for (size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
  return std::sqrt(s / static_cast<double>(to - from));
}
double meanOf(const std::vector<float>& x, size_t from, size_t to) {
  double s = 0;
  for (size_t i = from; i < to; ++i) s += x[i];
  return s / static_cast<double>(to - from);
}
float peakOf(const std::vector<float>& x) {
  float p = 0;
  for (float v : x) p = std::max(p, std::fabs(v));
  return p;
}

void writeWav(const std::string& name, const Stereo& s) {
  std::filesystem::create_directories("wav");
  std::ofstream f("wav/" + name, std::ios::binary);
  const uint32_t frames = static_cast<uint32_t>(s.l.size()), dataBytes = frames * 4, sr = 48000;
  const auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
  const auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
  f.write("RIFF", 4); w32(36 + dataBytes); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16);
  f.write("data", 4); w32(dataBytes);
  for (uint32_t i = 0; i < frames; ++i) {
    const auto q = [](float v) { return static_cast<int16_t>(std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f)); };
    const int16_t a = q(s.l[i]), b = q(s.r[i]);
    f.write(reinterpret_cast<const char*>(&a), 2);
    f.write(reinterpret_cast<const char*>(&b), 2);
  }
}

Patch euro() {
  for (const auto& info : factoryPresets()) if (info.id == "euro_hoover") return info.build();
  CHECK(false);
  return makeEmptyPatch();
}
}  // namespace

TEST(bass_midi_note_zero_is_8_hz) {
  Engine e(kSr);
  loadPatch(e, sinePatch());
  e.noteOn(0, 1.0f);
  renderMono(e, static_cast<int>(kSr));  // settle
  const auto x = renderMono(e, static_cast<int>(kSr * 10));
  CHECK(peakOf(x) > 0.3f);
  CHECK_NEAR(rising(x, 0, x.size()), 8.176, 0.11);  // 81 or 82 cycles in 10 s
}

TEST(bass_negative_octave_offsets_are_exact) {
  Patch p = sinePatch();
  setp(p, "OSC1", "oct", -4);
  Engine e(kSr);
  loadPatch(e, p);
  e.noteOn(60, 1.0f);  // C4 / 16
  renderMono(e, static_cast<int>(kSr));
  const auto x = renderMono(e, static_cast<int>(kSr * 8));
  CHECK_NEAR(rising(x, 0, x.size()), 261.6256 / 16.0, 0.1);
}

TEST(bass_low_notes_do_not_vanish) {
  for (int note : {0, 12, 24, 36}) {
    Engine e(kSr);
    loadPatch(e, euro());
    e.noteOn(note, 1.0f);
    const auto x = renderMono(e, static_cast<int>(kSr * 2));
    CHECK(rmsOf(x, static_cast<size_t>(kSr), x.size()) > 0.01);
  }
}

TEST(pitch_envelope_sweeps_and_amount_scales) {
  double early[2] = {0, 0}, late = 0;
  const float amounts[2] = {1.0f, 0.5f};
  for (int k = 0; k < 2; ++k) {
    Patch p = sinePatch();
    std::string n;
    CHECK(addModule(p, "adsr", &n).empty());
    setp(p, "ADSR2", "attack", 0.001f); setp(p, "ADSR2", "decay", 0.5f); setp(p, "ADSR2", "sustain", 0.0f);
    setp(p, "ADSR2", "velocity", 0.0f);
    CHECK(connect(p, "ADSR2", "OUT", "OSC1", "PITCH", amounts[k]).empty());
    Engine e(kSr);
    loadPatch(e, p);
    e.noteOn(60, 1.0f);
    const auto x = renderMono(e, static_cast<int>(kSr * 2));
    early[k] = rising(x, 240, 1200);          // 5..25 ms
    late = rising(x, static_cast<size_t>(kSr * 1.5), x.size());
  }
  CHECK_NEAR(late, 261.6, 2.0);
  CHECK(early[0] / late > 1.5);  // clear dive onto the note
  const double o0 = std::log2(early[0] / late), o1 = std::log2(early[1] / late);
  CHECK_NEAR(o1 / o0, 0.5, 0.12);  // half the connection amount, half the interval
}

TEST(pwm_changes_the_waveform) {
  Patch p = sinePatch();
  std::string n;
  CHECK(addModule(p, "lfo", &n).empty());
  setp(p, "OSC1", "wave", 4);
  setp(p, "LFO1", "rate", 0.5f); setp(p, "LFO1", "depth", 1.0f); setp(p, "LFO1", "wave", 0);
  CHECK(connect(p, "LFO1", "OUT", "OSC1", "PWM", 1.0f).empty());
  Engine e(kSr);
  loadPatch(e, p);
  e.noteOn(48, 1.0f);
  const auto x = renderMono(e, static_cast<int>(kSr * 2.2));
  const auto duty = [&](double t) {
    const size_t a = static_cast<size_t>(t * kSr), b = a + 4800;
    int pos = 0;
    for (size_t i = a; i < b; ++i) pos += x[i] > 0.0f;
    return static_cast<double>(pos) / static_cast<double>(b - a);
  };
  CHECK(std::fabs(duty(0.5) - duty(1.5)) > 0.3);  // LFO peak vs trough
}

TEST(lfo_core_waves_phase_and_sample_hold) {
  dsp::Lfo a, b;
  a.setSampleRate(1000.0); b.setSampleRate(1000.0);
  CHECK_NEAR(a.next(dsp::LfoWave::Sine, 1.0f, 0.25f), 1.0, 1e-4);  // phase offset
  a.reset(); b.reset();
  for (int i = 0; i < 200; ++i) {
    const float s = a.next(dsp::LfoWave::Saw, 1.0f), r = b.next(dsp::LfoWave::ReverseSaw, 1.0f);
    CHECK_NEAR(s, -r, 1e-5);
  }
  dsp::Lfo sh; sh.setSampleRate(1000.0); sh.seed(7);
  float first = sh.next(dsp::LfoWave::SampleHold, 2.0f);
  bool held = true;
  for (int i = 1; i < 400; ++i) held = held && sh.next(dsp::LfoWave::SampleHold, 2.0f) == first;
  CHECK(held);  // stays constant within a cycle
  float seen = first;
  bool changed = false;
  for (int i = 0; i < 2000; ++i) changed = changed || sh.next(dsp::LfoWave::SampleHold, 2.0f) != seen;
  CHECK(changed);
  // 0.01 Hz: one cycle takes 100 s
  dsp::Lfo slow; slow.setSampleRate(1000.0);
  for (int i = 0; i < 25000; ++i) slow.next(dsp::LfoWave::Saw, 0.01f);
  CHECK_NEAR(slow.next(dsp::LfoWave::Saw, 0.01f), -0.5, 0.01);
}

TEST(lfo_module_delay_and_fade_in) {
  Patch p = makeEmptyPatch();
  std::string n;
  for (const char* t : {"osc", "amp", "lfo"}) CHECK(addModule(p, t, &n).empty());
  setp(p, "OSC1", "wave", 0);
  setp(p, "LFO1", "wave", 2); setp(p, "LFO1", "polarity", 1); setp(p, "LFO1", "rate", 0.01f); setp(p, "LFO1", "depth", 1.0f);
  setp(p, "LFO1", "delay", 0.5f); setp(p, "LFO1", "fade", 0.5f);
  CHECK(connect(p, "OSC1", "OUT", "AMP1", "IN", 1.0f).empty());
  CHECK(connect(p, "LFO1", "OUT", "AMP1", "GAIN", 1.0f).empty());  // unipolar square = constant 1, shaped by delay/fade
  CHECK(connect(p, "AMP1", "OUT", "MASTER", "IN", 1.0f).empty());
  Engine e(kSr);
  loadPatch(e, p);
  e.noteOn(60, 1.0f);
  const auto x = renderMono(e, static_cast<int>(kSr * 2));
  const double before = rmsOf(x, 0, 18000), mid = rmsOf(x, 33000, 39000), after = rmsOf(x, 60000, 90000);
  CHECK(before < 0.01);
  CHECK(mid > before && mid < after);
  CHECK(after > 0.1);
}

TEST(chorus_is_stable_wide_and_bounded) {
  dsp::Chorus c;
  c.prepare(kSr);
  double diff = 0, wetEnergy = 0;
  float peak = 0;
  for (int i = 0; i < 96000; ++i) {
    const float x = std::sin(6.2831853f * 220.0f * static_cast<float>(i) / 48000.0f);
    float l, r;
    c.process(x, 0.7f, 4.0f, 12.0f, l, r);
    CHECK(std::isfinite(l) && std::isfinite(r));
    peak = std::max({peak, std::fabs(l), std::fabs(r)});
    if (i > 4800) { diff += std::fabs(l - r); wetEnergy += std::fabs(l); }
  }
  CHECK(peak <= 1.05f);
  CHECK(diff / wetEnergy > 0.02);  // channels differ: real stereo detune
}

TEST(euro_hoover_loads_plays_releases_and_stays_clean) {
  const Patch p = euro();
  CHECK(p.meta.name == "EURO HOOVER");
  CHECK(p.meta.author == "NOCTURNE");

  // every factory preset (old and new) builds into a rack
  for (const auto& info : factoryPresets()) CHECK(Rack::build(info.build(), kSr, nullptr) != nullptr);

  struct Case { const char* file; int note; };
  for (const Case c : {Case{"euro_hoover_C2.wav", 36}, Case{"euro_hoover_C3.wav", 48}}) {
    Engine e(kSr);
    loadPatch(e, p);
    e.noteOn(c.note, 1.0f);
    Stereo s = renderStereo(e, static_cast<int>(kSr * 3));
    const double f0 = 440.0 * std::exp2((c.note - 69) / 12.0);
    std::vector<float> mono(s.l.size());
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (s.l[i] + s.r[i]);
    const size_t from = static_cast<size_t>(kSr * 1.5), to = static_cast<size_t>(kSr * 2.8);
    CHECK_NEAR(fundamental(mono, from, to - from, f0 / 2), f0 / 2, f0 * 0.015);  // octave-down layer defines the composite period
    CHECK(rmsOf(mono, from, to) > 0.03);                                // strong sustain
    const double dcv = std::fabs(meanOf(mono, from, to)), rv = rmsOf(mono, from, to);
    std::printf("    %s mean %.4f rms %.4f peak %.3f\n", c.file, dcv, rv, peakOf(mono));
    CHECK(dcv < 0.1 * rv);  // no DC (loose: the window holds a non-integer number of bass cycles)
    CHECK(peakOf(mono) < 1.0f);                                         // no clipping
    // the dive: pitch at the start is clearly above the final pitch
    CHECK(fundamental(mono, 240, 2400, f0 * 1.6) > f0 * 1.15);
    // stereo image
    double d = 0;
    for (size_t i = from; i < to; ++i) d += std::fabs(s.l[i] - s.r[i]);
    CHECK(d / static_cast<double>(to - from) > 0.002);
    e.noteOff(c.note);
    const Stereo tail = renderStereo(e, static_cast<int>(kSr * 1.5));
    CHECK(e.activeVoices() == 0);
    CHECK(rmsOf(tail.l, tail.l.size() - 4800, tail.l.size()) < 1e-3);  // release really ends
    for (const auto& x : {tail.l}) for (float v : x) CHECK(std::isfinite(v));
    writeWav(c.file, s);
  }
}

TEST(euro_hoover_polyphony_and_riff_render) {
  Engine e(kSr);
  loadPatch(e, euro());
  for (int n : {36, 43, 48}) e.noteOn(n, 1.0f);
  const auto chord = renderMono(e, static_cast<int>(kSr * 2));
  CHECK(e.activeVoices() == 3);
  CHECK(peakOf(chord) < 1.0f);
  e.allNotesOff();
  renderMono(e, static_cast<int>(kSr * 2));

  // short bass riff, timed notes
  Stereo riff;
  const int notes[8] = {36, 36, 39, 36, 41, 36, 43, 34};
  for (int i = 0; i < 8; ++i) {
    e.noteOn(notes[i], 1.0f, 0.2f);
    const Stereo s = renderStereo(e, static_cast<int>(kSr * 0.25));
    riff.l.insert(riff.l.end(), s.l.begin(), s.l.end());
    riff.r.insert(riff.r.end(), s.r.begin(), s.r.end());
  }
  CHECK(rmsOf(riff.l, 0, riff.l.size()) > 0.03);
  CHECK(peakOf(riff.l) < 1.0f && peakOf(riff.r) < 1.0f);
  writeWav("euro_hoover_riff.wav", riff);
}

TEST(bass_reference_wavs_sub_and_pitch_sweep) {
  {  // deep sub-bass: sine at C1 (32.7 Hz)
    Engine e(kSr);
    loadPatch(e, sinePatch());
    e.noteOn(24, 1.0f);
    const Stereo s = renderStereo(e, static_cast<int>(kSr * 2));
    CHECK_NEAR(rising(s.l, 24000, s.l.size()), 32.70, 0.5);
    CHECK(std::fabs(meanOf(s.l, 24000, s.l.size())) < 0.02);
    writeWav("sub_bass_C1_sine.wav", s);
  }
  {  // pitch envelope sweep on a saw
    Patch p = sinePatch();
    std::string n;
    CHECK(addModule(p, "adsr", &n).empty());
    setp(p, "OSC1", "wave", 2);
    setp(p, "ADSR2", "attack", 0.001f); setp(p, "ADSR2", "decay", 0.4f); setp(p, "ADSR2", "sustain", 0.0f);
    setp(p, "ADSR2", "velocity", 0.0f);
    CHECK(connect(p, "ADSR2", "OUT", "OSC1", "PITCH", 1.0f).empty());
    Engine e(kSr);
    loadPatch(e, p);
    e.noteOn(48, 1.0f);
    const Stereo s = renderStereo(e, static_cast<int>(kSr * 2));
    CHECK(peakOf(s.l) < 1.0f);
    writeWav("pitch_sweep_C3.wav", s);
  }
}

TEST(keyboard_octave_range_reaches_midi_zero) {
  Engine e(kSr);
  NotePlayer pl(e);
  pl.setOctave(-5);
  CHECK(pl.octave() == -1);  // note = 12 * (octave + 1) + semitone -> 0 for C
  pl.setOctave(99);
  CHECK(pl.octave() == 8);
}

namespace {
// OSC(wave) -> AMP(ADSR sustain 1) -> MASTER, no pitch/PWM modulation.
Patch oscPatch(int wave, float pw) {
  Patch p = sinePatch();
  setp(p, "OSC1", "wave", static_cast<float>(wave));
  setp(p, "OSC1", "pw", pw);
  return p;
}
}  // namespace

TEST(saw_pwm_is_clean_continuous_and_tracks_pitch) {
  const int kSawPwm = 6, kSaw = 2;
  std::vector<float> prev;
  for (float pw : {0.05f, 0.2f, 0.5f, 0.8f, 0.95f}) {
    Engine e(kSr);
    loadPatch(e, oscPatch(kSawPwm, pw));
    e.noteOn(36, 1.0f);
    renderMono(e, 4800);
    const auto x = renderMono(e, static_cast<int>(kSr));
    CHECK(peakOf(x) <= 1.001f);
    CHECK(peakOf(x) > 0.2f);
    CHECK(std::fabs(meanOf(x, 0, x.size())) < 0.02);       // DC removed at every width
    CHECK_NEAR(rising(x, 0, x.size()), 65.406, 1.0);        // pw does not change the pitch
    for (float v : x) CHECK(std::isfinite(v));
    if (!prev.empty()) {                                    // shape changes smoothly with pw
      double d = 0;
      for (size_t i = 0; i < x.size(); ++i) d += std::fabs(x[i] - prev[i]);
      CHECK(d / static_cast<double>(x.size()) > 0.0);
    }
    prev = x;
  }
  // unlike plain SAW, pw changes the waveform
  Engine a(kSr), b(kSr);
  loadPatch(a, oscPatch(kSaw, 0.1f)); loadPatch(b, oscPatch(kSaw, 0.9f));
  a.noteOn(36, 1.0f); b.noteOn(36, 1.0f);
  const auto sa = renderMono(a, 9600), sb = renderMono(b, 9600);
  double plain = 0;
  for (size_t i = 0; i < sa.size(); ++i) plain += std::fabs(sa[i] - sb[i]);
  CHECK(plain < 1e-6);
  Engine c(kSr), d(kSr);
  loadPatch(c, oscPatch(kSawPwm, 0.1f)); loadPatch(d, oscPatch(kSawPwm, 0.9f));
  c.noteOn(36, 1.0f); d.noteOn(36, 1.0f);
  const auto sc = renderMono(c, 9600), sd = renderMono(d, 9600);
  double pwmDiff = 0;
  for (size_t i = 0; i < sc.size(); ++i) pwmDiff += std::fabs(sc[i] - sd[i]);
  CHECK(pwmDiff / static_cast<double>(sc.size()) > 0.05);
}

TEST(saw_pwm_survives_extreme_modulation_at_high_notes) {
  Patch p = oscPatch(6, 0.95f);
  std::string n;
  CHECK(addModule(p, "lfo", &n).empty());
  setp(p, "LFO1", "wave", 2); setp(p, "LFO1", "rate", 40.0f); setp(p, "LFO1", "depth", 1.0f);
  CHECK(connect(p, "LFO1", "OUT", "OSC1", "PWM", 1.0f).empty());
  for (int note : {24, 84, 120}) {
    Engine e(kSr);
    loadPatch(e, p);
    e.noteOn(note, 1.0f);
    const auto x = renderMono(e, static_cast<int>(kSr));
    for (float v : x) CHECK(std::isfinite(v));
    CHECK(peakOf(x) < 1.01f);
  }
}

TEST(osc_pitch_range_defines_semitones_per_unit_amount) {
  // ADSR sustain 1.0 holds a constant CV of 1, so PITCH amount 1 shifts by exactly `range` semitones.
  struct Case { float range; float amount; double ratio; };
  for (const Case c : {Case{12.0f, 1.0f, 2.0}, Case{24.0f, 1.0f, 4.0}, Case{24.0f, 0.5f, 2.0}, Case{24.0f, -0.5f, 0.5},
                       Case{12.0f, 0.1f, std::exp2(0.1)}}) {
    Patch p = sinePatch();
    std::string n;
    CHECK(addModule(p, "adsr", &n).empty());
    setp(p, "ADSR2", "attack", 0.001f); setp(p, "ADSR2", "sustain", 1.0f); setp(p, "ADSR2", "velocity", 0.0f);
    setp(p, "OSC1", "range", c.range);
    CHECK(connect(p, "ADSR2", "OUT", "OSC1", "PITCH", c.amount).empty());
    Engine e(kSr);
    loadPatch(e, p);
    e.noteOn(48, 1.0f);
    renderMono(e, 24000);
    const auto x = renderMono(e, static_cast<int>(kSr * 2));
    CHECK_NEAR(rising(x, 0, x.size()), 130.8128 * c.ratio, 130.8128 * c.ratio * 0.01);
  }
}

TEST(euro_hoover_v2_uses_saw_pwm_and_keeps_v1) {
  const Patch p = euro();
  int sawPwm = 0;
  for (size_t i = 0; i < p.modules.size(); ++i) {
    const ModuleSpec* s = p.specOf(static_cast<int>(i));
    if (s->type == "osc" && static_cast<int>(p.modules[i].params[static_cast<size_t>(s->paramIndex("wave"))]) == 6) ++sawPwm;
  }
  CHECK(sawPwm >= 2);
  bool v1 = false;
  for (const auto& info : factoryPresets()) v1 = v1 || info.id == "euro_hoover_v1";
  CHECK(v1);
}

TEST(euro_hoover_v2_renders_c1_to_c4_dry_and_with_effects) {
  struct Variant { const char* tag; bool dry; };
  for (const Variant v : {Variant{"", false}, Variant{"_dry", true}}) {
    for (int note : {24, 36, 48, 60}) {
      Patch p = euro();
      if (v.dry) { setp(p, "CHORUS1", "mix", 0.0f); setp(p, "DRIVE1", "mix", 0.0f); }
      Engine e(kSr);
      loadPatch(e, p);
      e.noteOn(note, 1.0f, 2.0f);
      const Stereo s = renderStereo(e, static_cast<int>(kSr * 3));
      std::vector<float> mono(s.l.size());
      for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (s.l[i] + s.r[i]);
      const double f0 = 440.0 * std::exp2((note - 69) / 12.0);
      const size_t from = static_cast<size_t>(kSr * 1.0), to = static_cast<size_t>(kSr * 1.9);
      CHECK_NEAR(fundamental(mono, from, to - from, f0 / 2), f0 / 2, f0 * 0.015);  // sub layer period; all layers settle on the note
      CHECK(rmsOf(mono, from, to) > 0.05);
      CHECK(peakOf(s.l) < 0.9f && peakOf(s.r) < 0.9f);                     // headroom, no clipping
      CHECK(std::fabs(meanOf(mono, from, to)) < 0.15 * rmsOf(mono, from, to));
      CHECK(rmsOf(mono, 0, 1200) > 0.01);                                  // attack is present at once
      CHECK(rmsOf(mono, mono.size() - 4800, mono.size()) < 1e-3);          // silent after release
      writeWav(std::string("euro_hoover_v2") + v.tag + "_note" + std::to_string(note) + ".wav", s);
    }
  }
}

TEST(euro_hoover_v2_chord_and_riff_stay_in_headroom) {
  Engine e(kSr);
  loadPatch(e, euro());
  for (int n : {36, 43, 48, 51}) e.noteOn(n, 1.0f, 1.5f);
  const Stereo chord = renderStereo(e, static_cast<int>(kSr * 2.5));
  CHECK(peakOf(chord.l) < 1.0f && peakOf(chord.r) < 1.0f);
  CHECK(rmsOf(chord.l, static_cast<size_t>(kSr * 0.5), static_cast<size_t>(kSr * 1.4)) > 0.05);
  CHECK(e.activeVoices() == 0);
  writeWav("euro_hoover_v2_chord.wav", chord);

  Stereo riff;
  const int notes[8] = {36, 36, 39, 36, 41, 36, 43, 34};
  for (int i = 0; i < 8; ++i) {
    e.noteOn(notes[i], 1.0f, 0.2f);
    const Stereo s = renderStereo(e, static_cast<int>(kSr * 0.25));
    riff.l.insert(riff.l.end(), s.l.begin(), s.l.end());
    riff.r.insert(riff.r.end(), s.r.begin(), s.r.end());
  }
  CHECK(peakOf(riff.l) < 1.0f && peakOf(riff.r) < 1.0f);
  writeWav("euro_hoover_v2_riff.wav", riff);
}

