#include "input/NotePlayer.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace nocturne {

const char* keyLayoutName(KeyLayout l) {
  switch (l) {
    case KeyLayout::Qwertz: return "QWERTZ";
    case KeyLayout::Azerty: return "AZERTY";
    default: return "QWERTY";
  }
}

const char* keyRow(KeyLayout l, bool upper) {
  switch (l) {
    case KeyLayout::Qwertz:
      return upper ? "q2w3er5t6z7ui9o0p" : "ysxdcvgbhnjm,l.\x01\x01";
    case KeyLayout::Azerty:  // accented digit-row keys cannot be read as single characters
      return upper ? "a\x01z\"er(t\x01y\x01ui\x01o\x01p" : "wsxdcvgbhnj,;l:m!";
    default:
      return upper ? "q2w3er5t6y7ui9o0p" : "zsxdcvgbhnjm,l.;/";
  }
}

std::optional<KeyNote> keyToNote(char c, KeyLayout layout) {
  const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
  const char k = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (k == '\0' || k == '\x01') return std::nullopt;
  const char* lowerRow = keyRow(layout, false);  // semitones 0..16
  const char* upperRow = keyRow(layout, true);   // semitones 12..28
  if (const char* p = std::strchr(lowerRow, k)) return KeyNote{static_cast<int>(p - lowerRow), upper};
  if (const char* p = std::strchr(upperRow, k)) return KeyNote{12 + static_cast<int>(p - upperRow), upper};
  return std::nullopt;
}

void NotePlayer::reset() {
  on_.fill(false);
  repeating_.fill(false);
  lastEvent_.fill(Clock::time_point{});
  offAt_.fill(Clock::time_point{});
}

bool NotePlayer::isSounding(int note, Clock::time_point now) const {
  if (note < 0 || note > 127 || !on_[static_cast<size_t>(note)]) return false;
  return mode_ == NoteMode::Latch || now < offAt_[static_cast<size_t>(note)];
}

bool NotePlayer::handleChar(char c, Clock::time_point now) {
  const auto key = keyToNote(c, layout_);
  if (!key) return false;
  const int note = 12 * (octave_ + 1) + key->semitone;
  if (note < 0 || note > 127) return true;
  const size_t n = static_cast<size_t>(note);
  const float vel = key->accent ? 1.0f : velocity_;

  // Timed: a held key arrives as one press, a pause (the OS initial repeat delay,
  // typically 250-700 ms) and then fast auto-repeat. Events of the same key inside
  // those windows belong to one physical press, so they extend the note instead of
  // retriggering it. To bridge the initial delay without a gap, a fresh note is
  // kept for at least kInitialDelay even if the note length is shorter.
  constexpr auto kInitialDelay = std::chrono::milliseconds(700);
  constexpr auto kRepeatGap = std::chrono::milliseconds(150);
  constexpr auto kMinExtend = std::chrono::milliseconds(250);
  const auto window = repeating_[n] ? kRepeatGap : kInitialDelay;
  const bool sameKey = lastEvent_[n] != Clock::time_point{} && (now - lastEvent_[n]) < window;

  if (mode_ == NoteMode::Latch) {
    if (sameKey) {  // auto-repeat of a held key must not toggle the note back off
      repeating_[n] = true;
      lastEvent_[n] = now;
      return true;
    }
    repeating_[n] = false;
    lastEvent_[n] = now;
    if (on_[n]) {
      on_[n] = false;
      engine_.noteOff(note);
    } else {
      on_[n] = true;
      engine_.noteOn(note, vel, 0.0f);
    }
    return true;
  }

  const auto toDur = [](float s) { return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(s)); };
  const bool repeat = on_[n] && now < offAt_[n] && sameKey;
  Clock::duration holdDur;
  if (repeat) {
    repeating_[n] = true;
    const float extend = std::max(hold_, std::chrono::duration<float>(kMinExtend).count());
    engine_.noteExtend(note, extend);
    holdDur = toDur(extend);
  } else {
    repeating_[n] = false;
    const float first = std::max(hold_, std::chrono::duration<float>(kInitialDelay).count() + 0.1f);
    engine_.noteOn(note, vel, first);
    holdDur = toDur(first);
  }
  on_[n] = true;
  lastEvent_[n] = now;
  offAt_[n] = now + holdDur;
  return true;
}

void NotePlayer::allOff() {
  engine_.allNotesOff();
  on_.fill(false);
}

void NotePlayer::panic() {
  engine_.panic();
  on_.fill(false);
}

}  // namespace nocturne
