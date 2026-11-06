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

const char* noteModeName(NoteMode m) {
  switch (m) {
    case NoteMode::Latch: return "LATCH";
    case NoteMode::Live: return "LIVE";
    default: return "TIMED";
  }
}

std::string noteName(int n) {
  static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (n < 0 || n > 127) return "?";
  return std::string(names[n % 12]) + std::to_string(n / 12 - 1);
}

void NotePlayer::reset() {
  on_.fill(false);
  repeating_.fill(false);
  lastEvent_.fill(Clock::time_point{});
  offAt_.fill(Clock::time_point{});
  noteRefs_.fill(0);
  slotNote_.fill(-1);
}

void NotePlayer::setLayout(KeyLayout l) {
  std::lock_guard<std::mutex> lk(mu_);
  allOffLocked();
  layout_ = l;
}

void NotePlayer::setKeySource(std::shared_ptr<KeyStateSource> src) {
  std::lock_guard<std::mutex> lk(mu_);
  allOffLocked();
  keys_ = std::move(src);
  if (mode_ == NoteMode::Live && !(keys_ && keys_->available())) mode_ = NoteMode::Timed;
}

bool NotePlayer::liveAvailable() const {
  std::lock_guard<std::mutex> lk(mu_);
  return keys_ && keys_->available();
}

std::string NotePlayer::liveDescription() const {
  std::lock_guard<std::mutex> lk(mu_);
  return keys_ ? keys_->description() : "no key source";
}

bool NotePlayer::setMode(NoteMode m) {
  std::lock_guard<std::mutex> lk(mu_);
  if (m == NoteMode::Live && !(keys_ && keys_->available())) return false;
  allOffLocked();
  mode_ = m;
  if (m == NoteMode::Live) markDownKeysIgnored();  // keys already down when switching do not play
  return true;
}

NoteMode NotePlayer::cycleMode() {
  NoteMode next = mode() == NoteMode::Timed ? NoteMode::Latch : (mode() == NoteMode::Latch ? NoteMode::Live : NoteMode::Timed);
  if (!setMode(next)) {
    next = NoteMode::Timed;
    setMode(next);
  }
  return next;
}

void NotePlayer::setInputEnabled(bool on) {
  std::lock_guard<std::mutex> lk(mu_);
  inputEnabled_ = on;
}

bool NotePlayer::isSounding(int note, Clock::time_point now) const {
  if (note < 0 || note > 127) return false;
  std::lock_guard<std::mutex> lk(mu_);
  if (mode_ == NoteMode::Live) return noteRefs_[static_cast<size_t>(note)] > 0;
  if (!on_[static_cast<size_t>(note)]) return false;
  return mode_ == NoteMode::Latch || now < offAt_[static_cast<size_t>(note)];
}

std::vector<int> NotePlayer::heldNotes() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<int> out;
  for (int n = 0; n < 128; ++n)
    if (noteRefs_[static_cast<size_t>(n)] > 0) out.push_back(n);
  return out;
}

// ------------------------------------------------------------ Live (real key state)
void NotePlayer::markDownKeysIgnored() {
  KeySample s;
  if (keys_) keys_->sample(s);
  ignored_ = s.down;
}

void NotePlayer::pressSlot(int slot, bool accent) {
  const int note = 12 * (octave_.load() + 1) + slotSemitone(slot);
  const size_t sl = static_cast<size_t>(slot);
  if (note < 0 || note > 127) { ignored_[sl] = true; return; }
  if (!engine_.noteOn(note, accent ? 1.0f : velocity_.load(), 0.0f)) return;  // queue full: retried next poll
  heldSlot_[sl] = true;
  slotNote_[sl] = note;
  ++noteRefs_[static_cast<size_t>(note)];
}

void NotePlayer::releaseSlot(int slot) {
  const size_t sl = static_cast<size_t>(slot);
  const size_t note = static_cast<size_t>(slotNote_[sl]);
  // Two physical keys can map to the same note (the rows overlap): release it with the last one.
  if (noteRefs_[note] <= 1 && !engine_.noteOff(static_cast<int>(note))) return;  // retried next poll
  --noteRefs_[note];
  heldSlot_[sl] = false;
  slotNote_[sl] = -1;
}

void NotePlayer::releaseAllHeld() {
  for (int i = 0; i < kPianoSlots; ++i)
    if (heldSlot_[static_cast<size_t>(i)]) releaseSlot(i);
}

void NotePlayer::pollKeys() {
  std::lock_guard<std::mutex> lk(mu_);
  if (mode_ != NoteMode::Live || !keys_) return;
  KeySample s;
  keys_->sample(s);
  if (!inputEnabled_ || !s.focused) {
    releaseAllHeld();
    ignored_ = s.down;
    return;
  }
  ignored_ &= (s.down | s.tapped);  // a key stays ignored until it is physically released
  for (int i = 0; i < kPianoSlots; ++i) {
    const size_t sl = static_cast<size_t>(i);
    // A tap shorter than one poll interval counts as down for this poll, up on the next.
    const bool down = s.down[sl] || s.tapped[sl];
    if (heldSlot_[sl]) {
      if (!down) releaseSlot(i);
    } else if (down && !ignored_[sl]) {
      if (s.blocked) ignored_[sl] = true;  // Ctrl/Alt shortcut, not a note
      else pressSlot(i, s.shift);
    }
  }
}

// ------------------------------------------------------------ typed characters
bool NotePlayer::handleChar(char c, Clock::time_point now) {
  const auto key = keyToNote(c, layout_);
  if (!key) return false;
  std::lock_guard<std::mutex> lk(mu_);
  if (mode_ == NoteMode::Live) return true;
  const int note = 12 * (octave_.load() + 1) + key->semitone;
  if (note < 0 || note > 127) return true;
  const size_t n = static_cast<size_t>(note);
  const float vel = key->accent ? 1.0f : velocity_.load();

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
    const float extend = std::max(hold_.load(), std::chrono::duration<float>(kMinExtend).count());
    engine_.noteExtend(note, extend);
    holdDur = toDur(extend);
  } else {
    repeating_[n] = false;
    const float first = std::max(hold_.load(), std::chrono::duration<float>(kInitialDelay).count() + 0.1f);
    engine_.noteOn(note, vel, first);
    holdDur = toDur(first);
  }
  on_[n] = true;
  lastEvent_[n] = now;
  offAt_[n] = now + holdDur;
  return true;
}

void NotePlayer::allOffLocked() {
  engine_.allNotesOff();
  on_.fill(false);
  heldSlot_.reset();
  slotNote_.fill(-1);
  noteRefs_.fill(0);
  if (mode_ == NoteMode::Live) markDownKeysIgnored();  // keys still down must be released and re-pressed
}

void NotePlayer::allOff() {
  std::lock_guard<std::mutex> lk(mu_);
  allOffLocked();
}

void NotePlayer::panic() {
  std::lock_guard<std::mutex> lk(mu_);
  engine_.panic();
  on_.fill(false);
  heldSlot_.reset();
  slotNote_.fill(-1);
  noteRefs_.fill(0);
  if (mode_ == NoteMode::Live) markDownKeysIgnored();
}


}  // namespace nocturne
