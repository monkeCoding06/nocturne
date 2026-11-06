#pragma once
#include <bitset>
#include <memory>
#include <string>

namespace nocturne {

// The 34 piano keys by physical position, independent of the keyboard layout:
// slots 0..16 are the lower row (semitones 0..16 from the octave's C), slots
// 17..33 the upper row (semitones 12..28). The same slot is the same key cap on
// QWERTY, QWERTZ and AZERTY.
constexpr int kPianoSlots = 34;
constexpr int slotSemitone(int slot) { return slot < 17 ? slot : 12 + (slot - 17); }

struct KeySample {
  std::bitset<kPianoSlots> down;    // physically held right now
  std::bitset<kPianoSlots> tapped;  // went down and up since the previous sample (too fast to see)
  bool shift = false;               // accent
  bool blocked = false;             // Ctrl/Alt held: a shortcut, not a note
  bool focused = true;              // our terminal window has the keyboard focus
};

// A source of true key-up/key-down state. Terminals normally only deliver typed
// characters, so this is the one place that knows how to observe physical keys.
class KeyStateSource {
 public:
  virtual ~KeyStateSource() = default;
  virtual bool available() const = 0;
  // Human readable explanation shown in the UI (why unavailable, or what is used).
  virtual std::string description() const = 0;
  virtual void sample(KeySample& out) = 0;
};

// Windows: polls the physical key state of the console's keyboard. Elsewhere: a
// source that reports itself unavailable (plain terminals have no key-up events).
std::unique_ptr<KeyStateSource> makePlatformKeySource();

// Raise the OS timer resolution (Windows: ~1 ms instead of ~15 ms) so a key poll
// thread sleeping a few ms really wakes that often. No-ops elsewhere.
void beginFineTimer();
void endFineTimer();

}  // namespace nocturne
