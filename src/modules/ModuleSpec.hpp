#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nocturne {

class Module;

// Voice modules are instantiated once per polyphonic voice. Global modules exist
// once and receive the sum of all voices (effects, master).
enum class Scope { Voice, Global };

// Audio ports carry audio signals; control ports carry modulation (CV, gates).
enum class SignalKind { Audio, Control };

enum class ParamKind { Float, Int, Enum };
enum class ParamScale { Linear, Log };

struct ParamSpec {
  std::string id;      // key in patch files, e.g. "cutoff"
  std::string label;   // displayed, e.g. "CUTOFF"
  ParamKind kind = ParamKind::Float;
  float min = 0.0f, max = 1.0f, def = 0.0f;
  ParamScale scale = ParamScale::Linear;
  std::string unit;                  // "Hz", "s", "ct", ...
  std::vector<std::string> choices;  // for Enum
  int decimals = 2;

  float clamp(float v) const;
  float toNormalized(float v) const;      // 0..1 on the control's travel
  float fromNormalized(float n) const;
  std::string format(float v) const;      // "2400 Hz", "SAW", "+3 st"
  float adjust(float v, int direction, bool fine) const;  // one coarse/fine step
  bool parse(const std::string& text, float& out) const;  // direct numeric entry
};

struct PortSpec {
  std::string id;      // "OUT", "CUTOFF", ...
  SignalKind kind = SignalKind::Audio;
  // Inputs only: a control-kind input that also accepts audio-rate sources
  // (FM, filter FM, AM). Audio sources cannot patch into other control inputs.
  bool acceptsAudio = false;
  bool internal = false;  // not user patchable
  std::string hint;       // short description shown in the patch bay
};

struct ModuleSpec {
  std::string type;    // "osc"
  std::string label;   // "OSCILLATOR"
  std::string prefix;  // "OSC" -> instance names OSC1, OSC2
  Scope scope = Scope::Voice;
  bool isEnvelope = false;   // keeps a voice alive while it is running
  bool unique = false;       // at most one per patch (MASTER)
  std::vector<PortSpec> inputs;
  std::vector<PortSpec> outputs;
  std::vector<ParamSpec> params;
  std::unique_ptr<Module> (*create)() = nullptr;

  int inputIndex(std::string_view id) const;
  int outputIndex(std::string_view id) const;
  int paramIndex(std::string_view id) const;
};

const ModuleSpec* findSpec(std::string_view type);
const std::vector<const ModuleSpec*>& allSpecs();

constexpr int kMaxPorts = 8;
constexpr int kMaxParams = 12;

}  // namespace nocturne
