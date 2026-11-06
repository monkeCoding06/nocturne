#pragma once
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "audio/Engine.hpp"
#include "input/KeyState.hpp"

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
  Live,   // a note sounds exactly as long as its key is physically held (needs a KeyStateSource)
};
const char* noteModeName(NoteMode m);
std::string noteName(int midiNote);  // "C4"

// Turns typed keys into engine note events. Terminals deliver key presses (and
// auto-repeat) but normally no key releases, so Timed and Latch work from typed
// characters only. Live mode instead polls a KeyStateSource for real key-up/down
// state (pollKeys) and ignores typed characters, so there is no double trigger and
// no auto-repeat. It is unavailable when no such source exists: it never falls back
// to a timer. A MIDI input would call Engine::noteOn/noteOff directly instead.
// All methods are thread-safe (the UI thread and a key poll thread both use it).
class NotePlayer {
 public:
  using Clock = std::chrono::steady_clock;

  explicit NotePlayer(Engine& engine) : engine_(engine), keys_(makePlatformKeySource()) { reset(); }

  // The source used by Live mode (replaceable for tests).
  void setKeySource(std::shared_ptr<KeyStateSource> src);
  bool liveAvailable() const;
  std::string liveDescription() const;
  // Live keys are only acted on while enabled (the UI disables them during text entry).
  void setInputEnabled(bool on);
  // Observe physical keys and emit note on/off. Call every few ms; no-op outside Live.
  void pollKeys();
  std::vector<int> heldNotes() const;  // notes with at least one physical key held (Live)

  KeyLayout layout() const { return layout_; }
  void setLayout(KeyLayout l);
  int octave() const { return octave_.load(); }
  void setOctave(int o) { octave_ = o < -1 ? -1 : (o > 8 ? 8 : o); }  // held Live notes keep their pitch
  float velocity() const { return velocity_; }
  void setVelocity(float v) { velocity_ = v < 0.05f ? 0.05f : (v > 1.0f ? 1.0f : v); }
  NoteMode mode() const { return mode_.load(); }
  // Returns false (and keeps the old mode) when Live is requested but unavailable.
  bool setMode(NoteMode m);
  // Timed -> Latch -> Live -> Timed; Live is skipped while unavailable. Returns the new mode.
  NoteMode cycleMode();
  float holdSeconds() const { return hold_; }
  void setHoldSeconds(float s) { hold_ = s < 0.05f ? 0.05f : (s > 8.0f ? 8.0f : s); }

  // Returns true when the character was a piano key (in Live mode it is swallowed:
  // the physical key state, not the typed character, drives the note).
  bool handleChar(char c, Clock::time_point now = Clock::now());
  void allOff();   // release all notes (soft)
  void panic();    // silence everything now

  bool isSounding(int note, Clock::time_point now = Clock::now()) const;

 private:
  void reset();
  void allOffLocked();
  void markDownKeysIgnored();
  void pressSlot(int slot, bool accent);
  void releaseSlot(int slot);
  void releaseAllHeld();

  mutable std::mutex mu_;
  Engine& engine_;
  std::shared_ptr<KeyStateSource> keys_;
  bool inputEnabled_ = true;
  std::bitset<kPianoSlots> heldSlot_, ignored_;
  std::array<int, kPianoSlots> slotNote_{};
  std::array<int, 128> noteRefs_{};
  KeyLayout layout_ = KeyLayout::Qwerty;
  std::atomic<int> octave_{4};
  std::atomic<float> velocity_{0.8f};
  std::atomic<NoteMode> mode_{NoteMode::Timed};
  std::atomic<float> hold_{0.5f};
  std::array<Clock::time_point, 128> lastEvent_{};
  std::array<Clock::time_point, 128> offAt_{};
  std::array<bool, 128> on_{};
  std::array<bool, 128> repeating_{};
};

}  // namespace nocturne
