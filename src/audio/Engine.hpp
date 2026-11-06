#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "audio/LockFreeQueue.hpp"
#include "audio/Rack.hpp"
#include "audio/VoiceAllocator.hpp"

namespace nocturne {

struct EngineEvent {
  enum class Type : std::uint8_t {
    NoteOn,         // a = note, f = velocity 0..1, g = auto-release time in s (0 = until NoteOff)
    NoteOff,        // a = note
    NoteExtend,     // a = note, g = new auto-release time (key repeat)
    AllNotesOff,    // release every held note
    Panic,          // silence every voice quickly
    SetParam,       // a = patch module index, b = param index, f = value
    SetWireAmount,  // a = connection index, f = amount
    SwapRack,       // rack = new compiled patch (ownership transfers to the engine)
    SetVolume,      // f = output volume 0..1
  };
  Type type = Type::NoteOff;
  int a = 0, b = 0;
  float f = 0.0f, g = 0.0f;
  Rack* rack = nullptr;
};

// Real-time synthesis engine. Everything below `render()` runs on the audio
// thread. The UI thread talks to it only through the lock-free event queue.
class Engine {
 public:
  explicit Engine(double sampleRate = 48000.0);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // ---- control-thread API (thread-safe, wait-free) ----
  bool post(const EngineEvent& e);
  bool postSwap(std::unique_ptr<Rack> rack);
  bool noteOn(int note, float velocity, float holdSeconds = 0.0f);
  bool noteOff(int note);
  bool noteExtend(int note, float holdSeconds);
  bool allNotesOff();
  bool panic();
  bool setVolume(float v);
  void collectGarbage();  // free racks retired by the audio thread

  // Only while the audio device is stopped (no audio thread running):
  void setSampleRate(double sr);
  void flushQueue();      // apply pending events on the calling thread
  double sampleRate() const { return sr_; }

  // ---- audio thread ----
  void render(float* interleavedStereo, int frames);

  // ---- meters / state (lock-free reads) ----
  float peakLeft() const { return peakL_.load(std::memory_order_relaxed); }
  float peakRight() const { return peakR_.load(std::memory_order_relaxed); }
  float cpuLoad() const { return cpu_.load(std::memory_order_relaxed); }
  int activeVoices() const { return activeVoices_.load(std::memory_order_relaxed); }
  std::uint32_t clipCount() const { return clips_.load(std::memory_order_relaxed); }
  static constexpr int kScopeSize = 4096;
  // Copies the most recent samples (oldest first). Returns number copied.
  int readScope(float* dst, int count) const;

 private:
  void drainEvents();
  void handle(const EngineEvent& e);
  void applyAction(const VoiceAction& a);
  void finalizePending();
  void renderChunk(float* l, float* r, int n);
  void retire(Rack* r);
  void releaseAllVoices();

  double sr_;
  LockFreeQueue<EngineEvent, 4096> queue_;
  LockFreeQueue<Rack*, 64> garbage_;

  // audio-thread-owned
  Rack* rack_ = nullptr;
  Rack* pending_ = nullptr;
  bool fadeIn_ = false;
  VoiceAllocator alloc_;
  struct VoiceState {
    float pitchTarget = 0.0f, pitchCur = 0.0f, velocity = 0.0f;
    int note = 60;
  };
  std::array<VoiceState, kMaxVoices> vstate_{};
  std::array<double, 128> timers_{};  // auto-release countdown in samples, < 0 = none
  int activeTimers_ = 0;
  float volume_ = 0.8f, volumeTarget_ = 0.8f;
  float glide_ = 0.0f;
  float chunkL_[kBlock] = {}, chunkR_[kBlock] = {};

  std::atomic<float> peakL_{0}, peakR_{0}, cpu_{0};
  std::atomic<int> activeVoices_{0};
  std::atomic<std::uint32_t> clips_{0};
  std::array<std::atomic<float>, kScopeSize> scope_{};
  std::atomic<std::uint32_t> scopeWrite_{0};
};

}  // namespace nocturne
