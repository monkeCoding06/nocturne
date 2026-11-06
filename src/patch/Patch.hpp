#pragma once
#include <optional>
#include <string>
#include <vector>

#include "modules/ModuleSpec.hpp"

namespace nocturne {

enum class VoiceMode { Poly, Mono };

struct PatchMeta {
  std::string name = "init";
  std::string author;
  std::string description;
};

// Settings that belong to the patch (saved with it).
struct GlobalSettings {
  VoiceMode mode = VoiceMode::Poly;
  int voices = 8;         // polyphony in poly mode (1..16)
  float glide = 0.0f;     // portamento time in seconds (mono and poly)
  bool legato = true;     // mono mode: overlapping notes do not retrigger envelopes
};

struct ModuleInstance {
  std::string name;   // unique instance name, e.g. "OSC1"
  std::string type;   // key into the module registry
  std::vector<float> params;  // same order as ModuleSpec::params
};

struct Connection {
  std::string from, fromPort;  // module name + output port id
  std::string to, toPort;      // module name + input port id
  float amount = 1.0f;         // attenuverter, -1..1
};

struct Patch {
  PatchMeta meta;
  GlobalSettings global;
  std::vector<ModuleInstance> modules;
  std::vector<Connection> connections;

  int findModule(const std::string& name) const;
  const ModuleSpec* specOf(int moduleIndex) const;
};

constexpr int kMaxModules = 32;
constexpr int kMaxVoices = 16;

// ---- construction / editing (all return an error string, empty on success) ----
std::vector<float> defaultParams(const ModuleSpec& spec);
Patch makeEmptyPatch();                                  // MASTER only
std::string addModule(Patch& patch, const std::string& type, std::string* createdName = nullptr);
std::string removeModule(Patch& patch, const std::string& name);

// Validates a prospective connection: existence, signal kind rules, voice/global
// scope rules, duplicates, and feedback cycles. Empty string = connection is valid.
std::string checkConnection(const Patch& patch, const std::string& from, const std::string& fromPort,
                            const std::string& to, const std::string& toPort);
std::string connect(Patch& patch, const std::string& from, const std::string& fromPort, const std::string& to,
                    const std::string& toPort, float amount = 1.0f);
void disconnect(Patch& patch, size_t connectionIndex);

// ---- graph analysis ----
bool hasCycle(const Patch& patch);
// Module indices in a valid processing order (sources first). Empty on cycle.
std::vector<int> processingOrder(const Patch& patch);
// Whole-patch sanity check (master present, no cycles, valid connections, ranges).
std::vector<std::string> validatePatch(const Patch& patch);

// Clamp all parameters to their limits and resize to the spec.
void normalizePatch(Patch& patch);

}  // namespace nocturne
