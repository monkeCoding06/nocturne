#pragma once
#include <array>
#include <chrono>
#include <optional>

#include "audio/Engine.hpp"

namespace nocturne {

// Maps a typed character to a semitone offset from the current octave's C.
// Two piano rows, tracker style:
//    2 3   5 6 7   9 0          (black keys, octave+1)
//   Q W E R T Y U I O P         (white keys, octave+1)
//     S D   G H J   L           (black keys)
//    Z X C V B N M , . /        (white keys)
// Uppercase letters play the same note as an accent (full velocity).
struct KeyNote {
  int semitone = 0;
  bool accent = false;
};

// The piano keys sit at the same physical positions on every layout; only the
// characters the terminal reports differ.
enum class KeyLayout { Qwerty, Qwertz, Azerty };
constexpr int kKeyLayoutCount = 3;
const char* keyLayoutName(KeyLayout l);
// 17 characters each, semitone offsets 0..16 from the row's C. '\x01' = no typable key.
const char* keyRow(KeyLayout l, bool upper);
std::optional<KeyNote> keyToNote(char c, KeyLayout layout = KeyLayout::Qwerty);

enum class NoteMode {
  Timed,  // note sounds for `hold` seconds after the last key event (key repeat extends it)
  Latch,  // press once to start the note, press again to release it
};

// Turns typed keys into engine note events. Terminals deliver key presses (and
// auto-repeat) but never key releases, so notes are either timed or latched.
// A MIDI input would call Engine::noteOn/noteOff directly instead.
class NotePlayer {
 public:
  using Clock = std::chrono::steady_clock;

  explicit NotePlayer(Engine& engine) : engine_(engine) { reset(); }

  KeyLayout layout() const { return layout_; }
  void setLayout(KeyLayout l) { allOff(); layout_ = l; }
  int octave() const { return octave_; }
  void setOctave(int o) { octave_ = o < -1 ? -1 : (o > 8 ? 8 : o); }
  float velocity() const { return velocity_; }
  void setVelocity(float v) { velocity_ = v < 0.05f ? 0.05f : (v > 1.0f ? 1.0f : v); }
  NoteMode mode() const { return mode_; }
  void setMode(NoteMode m) { allOff(); mode_ = m; }
  float holdSeconds() const { return hold_; }
  void setHoldSeconds(float s) { hold_ = s < 0.05f ? 0.05f : (s > 8.0f ? 8.0f : s); }

  // Returns true when the character was a piano key.
  bool handleChar(char c, Clock::time_point now = Clock::now());
  void allOff();   // release all notes (soft)
  void panic();    // silence everything now

  bool isSounding(int note, Clock::time_point now = Clock::now()) const;

 private:
  void reset();

  Engine& engine_;
  KeyLayout layout_ = KeyLayout::Qwerty;
  int octave_ = 4;
  float velocity_ = 0.8f;
  NoteMode mode_ = NoteMode::Timed;
  float hold_ = 0.5f;
  std::array<Clock::time_point, 128> lastEvent_{};
  std::array<Clock::time_point, 128> offAt_{};
  std::array<bool, 128> on_{};
  std::array<bool, 128> repeating_{};
};

}  // namespace nocturne
