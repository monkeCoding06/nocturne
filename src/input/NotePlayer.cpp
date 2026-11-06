#include "input/NotePlayer.hpp"

#include <cctype>
#include <cstring>

namespace nocturne {

std::optional<KeyNote> keyToNote(char c) {
  const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
  const char k = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static const char* lowerRow = "zsxdcvgbhnjm,l.;/";   // semitones 0..16
  static const char* upperRow = "q2w3er5t6y7ui9o0p";   // semitones 12..28
  if (k == '\0') return std::nullopt;
  if (const char* p = std::strchr(lowerRow, k)) return KeyNote{static_cast<int>(p - lowerRow), upper};
  if (const char* p = std::strchr(upperRow, k)) return KeyNote{12 + static_cast<int>(p - upperRow), upper};
  return std::nullopt;
}

void NotePlayer::reset() {
  on_.fill(false);
  lastEvent_.fill(Clock::time_point{});
  offAt_.fill(Clock::time_point{});
}

bool NotePlayer::isSounding(int note, Clock::time_point now) const {
  if (note < 0 || note > 127 || !on_[static_cast<size_t>(note)]) return false;
  return mode_ == NoteMode::Latch || now < offAt_[static_cast<size_t>(note)];
}

bool NotePlayer::handleChar(char c, Clock::time_point now) {
  const auto key = keyToNote(c);
  if (!key) return false;
  const int note = 12 * (octave_ + 1) + key->semitone;
  if (note < 0 || note > 127) return true;
  const size_t n = static_cast<size_t>(note);
  const float vel = key->accent ? 1.0f : velocity_;

  if (mode_ == NoteMode::Latch) {
    if (on_[n]) {
      on_[n] = false;
      engine_.noteOff(note);
    } else {
      on_[n] = true;
      engine_.noteOn(note, vel, 0.0f);
    }
    return true;
  }

  // Timed: events of the same key arriving faster than the OS auto-repeat gap
  // belong to one held key, so they extend the note instead of retriggering it.
  constexpr auto kRepeatGap = std::chrono::milliseconds(110);
  const auto holdDur = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(hold_));
  const bool repeat = on_[n] && now < offAt_[n] && (now - lastEvent_[n]) < kRepeatGap;
  if (repeat) engine_.noteExtend(note, hold_);
  else engine_.noteOn(note, vel, hold_);
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
