#include <algorithm>
#include <cmath>
#include <vector>

#include "TestHarness.hpp"
#include "audio/Engine.hpp"
#include "input/NotePlayer.hpp"
#include "patch/Patch.hpp"
#include "patch/Presets.hpp"

using namespace nocturne;

namespace {
constexpr double kSr = 48000.0;

struct Audio {
  std::vector<float> l, r;
  float peak = 0;
  double rms = 0;
  bool finite = true;
  int risingCrossings() const {
    int c = 0;
    for (size_t i = 1; i < l.size(); ++i) if (l[i - 1] < 0 && l[i] >= 0) ++c;
    return c;
  }
};

void load(Engine& e, const Patch& p) {
  auto rack = Rack::build(p, kSr, nullptr);
  CHECK(rack != nullptr);
  if (rack) CHECK(e.postSwap(std::move(rack)));
  e.flushQueue();
}

Audio render(Engine& e, int frames) {
  Audio a;
  std::vector<float> buf(static_cast<size_t>(frames) * 2);
  int pos = 0;
  while (pos < frames) {  // odd block sizes exercise the chunking
    const int n = std::min(frames - pos, 251);
    e.render(buf.data() + pos * 2, n);
    pos += n;
  }
  double sq = 0;
  for (int i = 0; i < frames; ++i) {
    const float L = buf[static_cast<size_t>(i) * 2], R = buf[static_cast<size_t>(i) * 2 + 1];
    if (!std::isfinite(L) || !std::isfinite(R)) a.finite = false;
    a.peak = std::max({a.peak, std::fabs(L), std::fabs(R)});
    sq += static_cast<double>(L) * L;
    a.l.push_back(L);
    a.r.push_back(R);
  }
  a.rms = std::sqrt(sq / frames);
  return a;
}
}  // namespace

TEST(engine_is_silent_without_notes) {
  Engine e(kSr);
  load(e, factoryPresets().front().build());
  const Audio a = render(e, 4800);
  CHECK(a.peak == 0.0f);
}

TEST(engine_plays_the_correct_pitch) {
  Engine e(kSr);
  load(e, factoryPresets().front().build());  // INIT: sine
  e.noteOn(69, 1.0f);                         // A4 = 440 Hz
  render(e, 4800);                            // skip attack
  const Audio a = render(e, 48000);
  CHECK_NEAR(a.risingCrossings(), 440, 3);
  CHECK(a.peak > 0.1f && a.peak <= 1.0f);
  e.noteOn(81, 1.0f);  // A5
  const Audio b = render(e, 48000);
  CHECK(b.risingCrossings() > 600);  // two notes: fundamentals 440 and 880
}

TEST(engine_envelope_gates_the_sound) {
  Engine e(kSr);
  load(e, factoryPresets().front().build());
  e.noteOn(60, 1.0f);
  const Audio on = render(e, 24000);
  CHECK(on.rms > 0.05);
  e.noteOff(60);
  render(e, static_cast<int>(kSr * 1.5));  // release 0.4 s
  CHECK(e.activeVoices() == 0);
  CHECK(render(e, 2400).peak < 1e-4f);
}

TEST(engine_timed_notes_release_by_themselves) {
  Engine e(kSr);
  load(e, factoryPresets().front().build());
  e.noteOn(60, 1.0f, 0.25f);
  CHECK(render(e, 4800).rms > 0.05);
  render(e, static_cast<int>(kSr * 2.0));
  CHECK(e.activeVoices() == 0);
}

TEST(engine_polyphony_and_panic) {
  Engine e(kSr);
  load(e, factoryPresets().front().build());
  for (int n : {60, 64, 67, 72}) e.noteOn(n, 0.9f);
  render(e, 4800);
  CHECK(e.activeVoices() == 4);
  e.panic();
  render(e, 2400);
  CHECK(e.activeVoices() == 0);
  CHECK(render(e, 1000).peak < 1e-4f);
}

TEST(engine_output_is_always_clipped_safely) {
  Engine e(kSr);
  Patch p = factoryPresets().front().build();
  load(e, p);
  e.setVolume(1.0f);
  // Hammer many notes into a patch with everything maxed.
  Patch loud = p;
  loud.modules[static_cast<size_t>(loud.findModule("AMP1"))].params[0] = 1.0f;
  load(e, loud);
  for (int n = 36; n < 60; ++n) e.noteOn(n, 1.0f);
  const Audio a = render(e, 24000);
  CHECK(a.finite);
  CHECK(a.peak <= 1.0f);
}

TEST(factory_presets_make_real_sound) {
  int index = 0;
  for (const auto& info : factoryPresets()) {
    Engine e(kSr);
    Patch p = info.build();
    load(e, p);
    for (int n : {48, 55, 60, 64}) e.noteOn(n, 0.9f, 0.0f);
    const Audio a = render(e, static_cast<int>(kSr * 2.5));
    std::printf("    %-6s peak %.3f rms %.3f voices %d\n", info.name.c_str(), a.peak, a.rms, e.activeVoices());
    CHECK(a.finite);
    CHECK(a.rms > 0.003);     // audible
    CHECK(a.peak <= 1.0f);    // protected
    CHECK(a.peak > 0.02f);
    e.allNotesOff();
    const Audio tail = render(e, static_cast<int>(kSr * 12));
    CHECK(tail.finite);
    CHECK(e.activeVoices() == 0);  // every preset ends cleanly
    ++index;
  }
  CHECK(index == 6);
}

TEST(factory_presets_sound_different) {
  std::vector<std::vector<float>> outs;
  for (const auto& info : factoryPresets()) {
    Engine e(kSr);
    load(e, info.build());
    e.noteOn(57, 0.9f);
    outs.push_back(render(e, 24000).l);
  }
  for (size_t i = 0; i < outs.size(); ++i)
    for (size_t j = i + 1; j < outs.size(); ++j) {
      double diff = 0;
      for (size_t k = 0; k < outs[i].size(); ++k) diff += std::fabs(outs[i][k] - outs[j][k]);
      CHECK(diff / static_cast<double>(outs[i].size()) > 0.002);
    }
}

TEST(patch_routing_determines_the_sound) {
  // OSC -> AMP(+ADSR) -> MASTER, then insert a filter that kills the signal.
  Patch p = makeEmptyPatch();
  std::string n;
  for (const char* t : {"osc", "filter", "amp", "adsr"}) CHECK(addModule(p, t, &n).empty());
  const auto setp = [&](const char* mod, const char* par, float v) {
    auto& m = p.modules[static_cast<size_t>(p.findModule(mod))];
    m.params[static_cast<size_t>(p.specOf(p.findModule(mod))->paramIndex(par))] = v;
  };
  setp("OSC1", "wave", 2);
  setp("FILTER1", "cutoff", 20.0f);
  setp("FILTER1", "res", 0.0f);
  CHECK(connect(p, "OSC1", "OUT", "AMP1", "IN").empty());
  CHECK(connect(p, "ADSR1", "OUT", "AMP1", "GAIN").empty());
  CHECK(connect(p, "AMP1", "OUT", "MASTER", "IN").empty());

  Engine e1(kSr);
  load(e1, p);
  e1.noteOn(60, 1.0f);
  const Audio direct = render(e1, 24000);

  Patch filtered = p;
  disconnect(filtered, 0);
  CHECK(connect(filtered, "OSC1", "OUT", "FILTER1", "IN").empty());
  CHECK(connect(filtered, "FILTER1", "OUT", "AMP1", "IN").empty());
  Engine e2(kSr);
  load(e2, filtered);
  e2.noteOn(60, 1.0f);
  const Audio dark = render(e2, 24000);
  CHECK(direct.rms > 0.05);
  CHECK(dark.rms < direct.rms * 0.15);  // the 20 Hz low-pass in the path removes the saw

  // Unpatched amp output: silence, because nothing reaches MASTER.
  Patch cut = p;
  disconnect(cut, 2);
  Engine e3(kSr);
  load(e3, cut);
  e3.noteOn(60, 1.0f);
  CHECK(render(e3, 12000).peak == 0.0f);
}

TEST(lfo_modulates_filter_cutoff_audibly) {
  Patch base = makeEmptyPatch();
  std::string n;
  for (const char* t : {"osc", "filter", "amp", "adsr", "lfo"}) CHECK(addModule(base, t, &n).empty());
  const auto setp = [&](Patch& p, const char* mod, const char* par, float v) {
    auto& m = p.modules[static_cast<size_t>(p.findModule(mod))];
    m.params[static_cast<size_t>(p.specOf(p.findModule(mod))->paramIndex(par))] = v;
  };
  setp(base, "OSC1", "wave", 2);
  setp(base, "FILTER1", "cutoff", 800.0f);
  setp(base, "LFO1", "rate", 4.0f);
  setp(base, "LFO1", "depth", 1.0f);
  CHECK(connect(base, "OSC1", "OUT", "FILTER1", "IN").empty());
  CHECK(connect(base, "FILTER1", "OUT", "AMP1", "IN").empty());
  CHECK(connect(base, "ADSR1", "OUT", "AMP1", "GAIN").empty());
  CHECK(connect(base, "AMP1", "OUT", "MASTER", "IN").empty());

  auto brightness = [&](const Patch& p) {
    Engine e(kSr);
    load(e, p);
    e.noteOn(48, 1.0f);
    render(e, 4800);
    const Audio a = render(e, 48000);
    // Standard deviation of short-window RMS = how much the level moves.
    std::vector<double> w;
    for (size_t i = 0; i + 960 <= a.l.size(); i += 960) {
      double s = 0;
      for (size_t k = 0; k < 960; ++k) s += static_cast<double>(a.l[i + k]) * a.l[i + k];
      w.push_back(std::sqrt(s / 960));
    }
    double mean = 0;
    for (double v : w) mean += v;
    mean /= static_cast<double>(w.size());
    double var = 0;
    for (double v : w) var += (v - mean) * (v - mean);
    return std::sqrt(var / static_cast<double>(w.size())) / mean;
  };
  Patch modded = base;
  CHECK(connect(modded, "LFO1", "OUT", "FILTER1", "CUTOFF", 1.0f).empty());
  CHECK(brightness(modded) > brightness(base) * 3.0 + 0.02);
}

TEST(live_edits_swap_the_rack_without_dropping_notes) {
  Engine e(kSr);
  Patch p = factoryPresets().front().build();
  load(e, p);
  e.noteOn(69, 1.0f);
  render(e, 9600);
  // Add a module (forces a rack rebuild) while the note is held.
  std::string n;
  CHECK(addModule(p, "delay", &n).empty());
  disconnect(p, 2);
  CHECK(connect(p, "AMP1", "OUT", "DELAY1", "IN").empty());
  CHECK(connect(p, "DELAY1", "OUT", "MASTER", "IN").empty());
  auto rack = Rack::build(p, kSr, nullptr);
  CHECK(rack != nullptr);
  e.postSwap(std::move(rack));
  const Audio a = render(e, 9600);
  CHECK(a.rms > 0.05);  // note still sounding
  // No click: biggest sample-to-sample jump of a 440 Hz sine stays smooth.
  float maxJump = 0;
  for (size_t i = 1; i < a.l.size(); ++i) maxJump = std::max(maxJump, std::fabs(a.l[i] - a.l[i - 1]));
  CHECK(maxJump < 0.2f);
  e.collectGarbage();
}

TEST(engine_param_changes_are_smooth) {
  Engine e(kSr);
  Patch p = factoryPresets().front().build();
  load(e, p);
  e.noteOn(57, 1.0f);
  render(e, 9600);
  const int mi = p.findModule("OSC1");
  const int pi = p.specOf(mi)->paramIndex("level");
  EngineEvent ev;
  ev.type = EngineEvent::Type::SetParam;
  ev.a = mi; ev.b = pi; ev.f = 0.05f;  // sudden drop in level
  e.post(ev);
  const Audio a = render(e, 4800);
  float maxJump = 0;
  for (size_t i = 1; i < a.l.size(); ++i) maxJump = std::max(maxJump, std::fabs(a.l[i] - a.l[i - 1]));
  CHECK(maxJump < 0.1f);  // no step discontinuity
  CHECK(a.peak < 0.5f);
}

// ------------------------------------------------------------ input
TEST(key_layout_maps_notes) {
  CHECK(keyToNote('z') && keyToNote('z')->semitone == 0);
  CHECK(keyToNote('s')->semitone == 1);
  CHECK(keyToNote('m')->semitone == 11);
  CHECK(keyToNote('q')->semitone == 12);
  CHECK(keyToNote('Z')->accent);
  CHECK(!keyToNote('?').has_value());
  CHECK(!keyToNote('a').has_value());  // letters kept free for commands
}

TEST(note_player_latch_and_timed_modes) {
  Engine e(kSr);
  load(e, factoryPresets().front().build());
  NotePlayer pl(e);
  pl.setOctave(4);
  pl.setMode(NoteMode::Latch);
  CHECK(pl.handleChar('z'));
  CHECK(render(e, 4800).rms > 0.05);
  render(e, 48000);
  CHECK(render(e, 2400).rms > 0.05);  // latched notes sustain until toggled off
  pl.handleChar('z');
  render(e, static_cast<int>(kSr * 1.5));
  CHECK(e.activeVoices() == 0);

  pl.setMode(NoteMode::Timed);
  pl.setHoldSeconds(0.2f);
  CHECK(pl.handleChar('x'));
  CHECK(render(e, 4800).rms > 0.05);
  render(e, static_cast<int>(kSr * 2.0));
  CHECK(e.activeVoices() == 0);
  CHECK(!pl.handleChar('!'));
}
