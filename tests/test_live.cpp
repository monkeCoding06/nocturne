#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "TestHarness.hpp"
#include "audio/Engine.hpp"
#include "input/NotePlayer.hpp"
#include "patch/Patch.hpp"
#include "patch/Presets.hpp"

using namespace nocturne;

namespace {
constexpr double kSr = 48000.0;
constexpr int kC = 0, kE = 4, kG = 7, kD = 2;  // lower-row slots (semitones above C)

class FakeKeys final : public KeyStateSource {
 public:
  bool ok = true;
  KeySample state;
  bool available() const override { return ok; }
  std::string description() const override { return ok ? "fake" : "fake: unavailable"; }
  void sample(KeySample& out) override {
    out = state;
    state.tapped.reset();
  }
  void press(int slot) { state.down[static_cast<size_t>(slot)] = true; }
  void release(int slot) { state.down[static_cast<size_t>(slot)] = false; }
};

struct Rig {
  Engine engine{kSr};
  NotePlayer player{engine};
  std::shared_ptr<FakeKeys> keys = std::make_shared<FakeKeys>();

  explicit Rig(const Patch& p, int octave = 3) {
    auto rack = Rack::build(p, kSr, nullptr);
    CHECK(rack != nullptr);
    if (rack) engine.postSwap(std::move(rack));
    engine.flushQueue();
    player.setKeySource(keys);
    CHECK(player.setMode(NoteMode::Live));
    player.setOctave(octave);
  }
  // One key-poll interval followed by `seconds` of audio; returns the rms of that audio.
  double step(double seconds = 0.02) {
    player.pollKeys();
    return render(seconds);
  }
  double render(double seconds) {
    const int frames = static_cast<int>(seconds * kSr);
    std::vector<float> buf(static_cast<size_t>(frames) * 2);
    int pos = 0;
    while (pos < frames) {
      const int n = std::min(frames - pos, 251);
      engine.render(buf.data() + pos * 2, n);
      pos += n;
    }
    double sq = 0;
    for (int i = 0; i < frames * 2; ++i) sq += static_cast<double>(buf[static_cast<size_t>(i)]) * buf[static_cast<size_t>(i)];
    return std::sqrt(sq / (frames * 2));
  }
};

Patch initPatch() { return factoryPresets().front().build(); }

Patch presetNamed(const std::string& name) {
  for (const auto& p : factoryPresets())
    if (p.name == name) return p.build();
  CHECK(false);
  return initPatch();
}
}  // namespace

TEST(live_single_note_sustains_until_key_up) {
  Rig r(initPatch());
  r.keys->press(kC);
  r.step();
  const std::uint32_t t0 = r.engine.triggerCount();
  // 4 s with no auto-off and no further events
  for (int i = 0; i < 20; ++i) r.step(0.2);
  CHECK(r.engine.activeVoices() == 1);
  CHECK(r.engine.gatedVoices() == 1);
  CHECK(r.step(0.1) > 0.02);
  CHECK(r.engine.triggerCount() == t0);
  r.keys->release(kC);
  r.step();
  CHECK(r.engine.gatedVoices() == 0);  // released immediately
  r.render(1.5);                       // ADSR release is 0.4 s
  CHECK(r.engine.activeVoices() == 0);
  CHECK(r.render(0.1) < 1e-4);
}

TEST(live_overlapping_notes_and_transitions_poly) {
  Rig r(initPatch());
  r.keys->press(kC);
  r.step();
  r.keys->press(kE);
  r.step();
  CHECK(r.engine.gatedVoices() == 2);
  r.keys->release(kC);  // transition: C released while E is still held
  r.step();
  CHECK(r.engine.gatedVoices() == 1);
  for (int i = 0; i < 10; ++i) r.step(0.2);
  CHECK(r.engine.gatedVoices() == 1);
  CHECK(r.step(0.1) > 0.02);
  CHECK(r.player.heldNotes() == std::vector<int>{52});
  r.keys->release(kE);
  r.step();
  r.render(1.5);
  CHECK(r.engine.activeVoices() == 0);
}

TEST(live_chord_releases_keys_independently) {
  Rig r(initPatch());
  for (int s : {kC, kE, kG}) r.keys->press(s);
  r.step();
  CHECK(r.engine.gatedVoices() == 3);
  r.keys->release(kE);
  r.step();
  CHECK(r.engine.gatedVoices() == 2);
  r.render(1.0);
  CHECK(r.engine.activeVoices() == 2);
  CHECK((r.player.heldNotes() == std::vector<int>{48, 55}));
  r.keys->release(kC);
  r.keys->release(kG);
  r.step();
  r.render(1.5);
  CHECK(r.engine.activeVoices() == 0);
}

TEST(live_rapid_retaps_retrigger_every_time) {
  Rig r(initPatch());
  const std::uint32_t t0 = r.engine.triggerCount();
  for (int i = 0; i < 5; ++i) {
    r.keys->press(kC);
    r.step(0.03);
    r.keys->release(kC);
    r.step(0.03);  // release shorter than the ADSR release: the voice is reused
  }
  CHECK(r.engine.triggerCount() - t0 == 5);
  r.render(1.5);
  CHECK(r.engine.activeVoices() == 0);
}

TEST(live_tap_faster_than_a_poll_is_not_missed) {
  Rig r(initPatch());
  const std::uint32_t t0 = r.engine.triggerCount();
  r.keys->state.tapped[kC] = true;  // down and up between two polls
  r.step();
  CHECK(r.engine.triggerCount() - t0 == 1);
  r.step();  // released by the following poll
  CHECK(r.engine.gatedVoices() == 0);
}

TEST(live_key_autorepeat_and_typed_chars_do_not_retrigger) {
  Rig r(initPatch());
  r.keys->press(kC);
  r.step();
  const std::uint32_t t0 = r.engine.triggerCount();
  for (int i = 0; i < 40; ++i) {
    CHECK(r.player.handleChar('z'));  // typed character for the same key: swallowed
    r.step(0.05);                     // key state still "down"
  }
  CHECK(r.engine.triggerCount() == t0);
  CHECK(r.engine.gatedVoices() == 1);
  // typed characters alone never start a note in Live mode
  CHECK(r.player.handleChar('c'));
  r.step();
  CHECK(r.engine.triggerCount() == t0);
}

TEST(live_mono_legato_follows_note_priority) {
  Patch p = initPatch();
  p.global.mode = VoiceMode::Mono;
  p.global.legato = true;
  Rig r(p);
  r.keys->press(kC);
  r.step();
  const std::uint32_t t0 = r.engine.triggerCount();
  r.keys->press(kD);  // legato: pitch changes, envelope not retriggered
  r.step(0.1);
  CHECK(r.engine.triggerCount() == t0);
  CHECK(r.engine.gatedVoices() == 1);
  r.keys->release(kD);  // falls back to the still-held C
  r.step(0.1);
  CHECK(r.engine.gatedVoices() == 1);
  CHECK(r.step(0.2) > 0.02);
  r.keys->press(kD);
  r.step();
  r.keys->release(kC);  // transition: D survives
  r.step(0.1);
  CHECK(r.engine.gatedVoices() == 1);
  CHECK(r.step(0.2) > 0.02);
  r.keys->release(kD);
  r.step();
  r.render(1.5);
  CHECK(r.engine.activeVoices() == 0);
}

TEST(live_mono_non_legato_retriggers_on_fallback) {
  Patch p = initPatch();
  p.global.mode = VoiceMode::Mono;
  p.global.legato = false;
  Rig r(p);
  r.keys->press(kC);
  r.step();
  r.keys->press(kD);
  r.step();
  const std::uint32_t t0 = r.engine.triggerCount();
  r.keys->release(kD);
  r.step();
  CHECK(r.engine.triggerCount() - t0 == 1);
  CHECK(r.engine.gatedVoices() == 1);
}

TEST(live_voice_limit_steals_predictably_and_never_sticks) {
  Patch p = initPatch();
  p.global.mode = VoiceMode::Poly;
  p.global.voices = 2;
  Rig r(p);
  r.keys->press(kC);
  r.step();
  r.keys->press(kE);
  r.step();
  r.keys->press(kG);  // steals the oldest held voice (C)
  r.step();
  CHECK(r.engine.activeVoices() == 2);
  CHECK(r.engine.gatedVoices() == 2);
  r.keys->release(kC);  // its voice was stolen: nothing to release, nothing breaks
  r.step();
  CHECK(r.engine.gatedVoices() == 2);
  CHECK(r.step(0.1) > 0.02);
  r.keys->release(kE);
  r.keys->release(kG);
  r.step();
  r.render(1.5);
  CHECK(r.engine.activeVoices() == 0);
  CHECK(r.player.heldNotes().empty());
}

TEST(live_effect_tails_survive_note_off) {
  Rig r(presetNamed("GLASS"));
  r.keys->press(kC);
  for (int i = 0; i < 10; ++i) r.step(0.1);
  r.keys->release(kC);
  r.step();
  double t = 0;
  while (r.engine.activeVoices() > 0 && t < 10.0) { r.render(0.05); t += 0.05; }
  CHECK(r.engine.activeVoices() == 0);
  // the voice is gone but delay/reverb are still ringing
  const double tail = r.render(0.3);
  std::printf("    tail rms after voice end: %.6f (voice ended %.2fs after key-up)\n", tail, t);
  CHECK(tail > 1e-4);
  CHECK(r.render(0.5) > 0.0);
}

TEST(live_unavailable_never_falls_back_to_a_timer) {
  Rig r(initPatch());
  r.player.setMode(NoteMode::Timed);
  r.keys->ok = false;
  CHECK(!r.player.liveAvailable());
  CHECK(!r.player.setMode(NoteMode::Live));
  CHECK(r.player.mode() == NoteMode::Timed);
  CHECK(r.player.cycleMode() == NoteMode::Latch);
  CHECK(r.player.cycleMode() == NoteMode::Timed);  // Live skipped
  r.keys->ok = true;
  CHECK(r.player.cycleMode() == NoteMode::Latch);
  CHECK(r.player.cycleMode() == NoteMode::Live);
}

TEST(live_ignores_shortcuts_focus_loss_and_text_entry) {
  Rig r(initPatch());
  r.keys->state.blocked = true;  // Ctrl held: Ctrl+C is not a note
  r.keys->press(kC);
  r.step();
  CHECK(r.engine.activeVoices() == 0);
  r.keys->state.blocked = false;
  r.step();  // still ignored until physically released
  CHECK(r.engine.activeVoices() == 0);
  r.keys->release(kC);
  r.step();

  r.keys->press(kE);
  r.step();
  CHECK(r.engine.gatedVoices() == 1);
  r.keys->state.focused = false;  // window lost focus: key-up would never be seen
  r.step();
  CHECK(r.engine.gatedVoices() == 0);
  r.keys->state.focused = true;
  r.keys->release(kE);
  r.step();

  r.player.setInputEnabled(false);  // text entry
  r.keys->press(kG);
  r.step();
  CHECK(r.engine.gatedVoices() == 0);
  r.player.setInputEnabled(true);
  r.step();
  CHECK(r.engine.gatedVoices() == 0);
  r.keys->release(kG);
}

TEST(live_escape_releases_and_held_keys_do_not_replay) {
  Rig r(initPatch());
  r.keys->press(kC);
  r.step();
  r.player.allOff();
  r.step();
  CHECK(r.engine.gatedVoices() == 0);
  r.step();
  CHECK(r.engine.gatedVoices() == 0);  // key is still down but must be re-pressed
  r.keys->release(kC);
  r.step();
  r.keys->press(kC);
  r.step();
  CHECK(r.engine.gatedVoices() == 1);
}
