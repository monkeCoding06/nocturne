#pragma once
#include <array>
#include <cstdint>

#include "patch/Patch.hpp"

namespace nocturne {

// What the engine must do to a voice after a note event.
struct VoiceAction {
  enum class Kind { None, Trigger, Retarget, Release };
  Kind kind = Kind::None;
  int voice = -1;
  int note = -1;
  float velocity = 0.0f;
  bool cold = false;   // Trigger: voice was silent, modules start from a clean state
  bool stolen = false; // Trigger: a sounding voice was taken over
};

// Polyphonic / monophonic voice assignment. Pure logic, no audio dependencies.
class VoiceAllocator {
 public:
  struct Slot {
    bool active = false;  // sounding (gated or releasing)
    bool gate = false;
    int note = -1;
    float velocity = 0.0f;
    std::uint64_t stamp = 0;
  };

  void configure(int voices, VoiceMode mode, bool legato);
  int voiceCount() const { return count_; }
  const Slot& slot(int i) const { return slots_[static_cast<size_t>(i)]; }

  VoiceAction noteOn(int note, float velocity);
  VoiceAction noteOff(int note);
  // Release the gate of a voice (used by timers/all-notes-off); returns the action.
  VoiceAction releaseVoice(int voice);
  void voiceFinished(int voice);
  void clear();
  void clearStack() { stackSize_ = 0; }

  int activeVoices() const;
  int gatedVoices() const;  // voices whose key is still held (not yet releasing)

 private:
  VoiceAction polyOn(int note, float vel);
  VoiceAction monoOn(int note, float vel);
  VoiceAction monoOff(int note);
  void removeFromStack(int note);

  std::array<Slot, kMaxVoices> slots_{};  // fixed storage: configure() is audio-thread safe
  int count_ = 8;
  VoiceMode mode_ = VoiceMode::Poly;
  bool legato_ = true;
  std::uint64_t counter_ = 0;
  static constexpr int kStack = 32;
  int stack_[kStack] = {};
  int stackSize_ = 0;
};

}  // namespace nocturne
