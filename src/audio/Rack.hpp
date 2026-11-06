#pragma once
#include <memory>
#include <string>
#include <vector>

#include "modules/Module.hpp"
#include "patch/Patch.hpp"

namespace nocturne {

// A Patch compiled into DSP objects: N voice instances of every voice-scope
// module plus one instance of every global module, wired according to the patch
// connections and sorted in processing order. Built on the UI thread, then handed
// to the audio thread, which is the only thread that touches it afterwards.
class Rack {
 public:
  static std::unique_ptr<Rack> build(const Patch& patch, double sampleRate, std::string* error = nullptr);

  const GlobalSettings& settings() const { return settings_; }
  int voiceCount() const { return static_cast<int>(voices_.size()); }
  double sampleRate() const { return sr_; }

  // ---- audio thread API ----
  void setParam(int patchModuleIndex, int param, float value);
  void setWireAmount(int connectionIndex, float amount);

  // Start a note on a voice. cold = clean module state (voice was silent).
  void triggerVoice(int voice, const VoiceCtx& ctx, bool cold);
  void killVoice(int voice);  // fast fade-out, ignoring envelopes

  void beginChunk();  // clears global inputs
  // Renders one chunk of a voice and adds it to the global inputs.
  void processVoice(int voice, const VoiceCtx& ctx, int n);
  void processGlobal(int n, float* left, float* right);
  bool voiceFinished(int voice) const;

  // Take over live module objects (filter memory, delay lines, envelope levels...)
  // from the rack being replaced so edits do not interrupt the sound.
  void adoptState(Rack& old);

 private:
  struct Wire {
    int srcNode = 0, srcPort = 0, dstNode = 0, dstPort = 0;
    float amount = 1.0f;
    int connIdx = 0;
  };
  struct Node {
    std::string name;
    const ModuleSpec* spec = nullptr;
    std::unique_ptr<Module> mod;
    int nIn = 0, nOut = 0;
    bool connected[kMaxPorts] = {};
    std::vector<float> in, out;       // [port * kBlock + sample]
    std::vector<Wire> incoming;       // wires from nodes of the same instance
    std::vector<float> params;        // patch values, reapplied after adoption
  };
  struct Instance {
    std::vector<Node> nodes;  // processing order
    bool hasEnvelope = false;
    float gain = 0.0f;        // per-voice declick gain
    bool killed = false;
    bool holding = false;     // gate held or an envelope still running
    double sinceRelease = 0.0;
  };
  struct Loc { bool global = false; int node = -1; };

  void runNodes(Instance& inst, int n, const VoiceCtx* ctx);

  double sr_ = 48000.0;
  GlobalSettings settings_;
  std::vector<Instance> voices_;
  Instance global_;
  std::vector<Wire> cross_;  // voice node -> global node
  std::vector<Loc> loc_;     // patch module index -> location
  int masterNode_ = -1;
};

}  // namespace nocturne
