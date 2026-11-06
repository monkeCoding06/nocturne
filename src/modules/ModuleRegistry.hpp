#pragma once
#include <vector>

#include "modules/ModuleSpec.hpp"

namespace nocturne {

void registerSourceModules(std::vector<ModuleSpec>& out);   // OSC, LFO, KEYS
void registerProcessModules(std::vector<ModuleSpec>& out);  // FILTER, ADSR, AMP, MIXER
void registerEffectModules(std::vector<ModuleSpec>& out);   // DRIVE, DELAY, REVERB, MASTER

// Small helpers to keep the module tables readable.
inline ParamSpec floatParam(const char* id, const char* label, float min, float max, float def,
                            const char* unit = "", ParamScale scale = ParamScale::Linear, int decimals = 2) {
  ParamSpec p;
  p.id = id; p.label = label; p.kind = ParamKind::Float;
  p.min = min; p.max = max; p.def = def; p.unit = unit; p.scale = scale; p.decimals = decimals;
  return p;
}
inline ParamSpec intParam(const char* id, const char* label, int min, int max, int def, const char* unit = "") {
  ParamSpec p;
  p.id = id; p.label = label; p.kind = ParamKind::Int;
  p.min = static_cast<float>(min); p.max = static_cast<float>(max); p.def = static_cast<float>(def);
  p.unit = unit;
  return p;
}
inline ParamSpec enumParam(const char* id, const char* label, std::vector<std::string> choices, int def) {
  ParamSpec p;
  p.id = id; p.label = label; p.kind = ParamKind::Enum;
  p.min = 0.0f; p.max = static_cast<float>(choices.size() - 1); p.def = static_cast<float>(def);
  p.choices = std::move(choices);
  return p;
}
inline PortSpec audioIn(const char* id, const char* hint = "") { return {id, SignalKind::Audio, false, false, hint}; }
inline PortSpec audioOut(const char* id, const char* hint = "") { return {id, SignalKind::Audio, false, false, hint}; }
inline PortSpec cvIn(const char* id, const char* hint = "", bool acceptsAudio = false) {
  return {id, SignalKind::Control, acceptsAudio, false, hint};
}
inline PortSpec cvOut(const char* id, const char* hint = "") { return {id, SignalKind::Control, false, false, hint}; }

}  // namespace nocturne
