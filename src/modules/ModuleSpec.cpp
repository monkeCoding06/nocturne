#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "modules/Module.hpp"
#include "modules/ModuleRegistry.hpp"

namespace nocturne {

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}
std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}
}  // namespace

float ParamSpec::clamp(float v) const {
  if (!(v == v)) return def;  // NaN
  v = std::min(std::max(v, min), max);
  if (kind != ParamKind::Float) v = std::round(v);
  return v;
}

float ParamSpec::toNormalized(float v) const {
  v = clamp(v);
  if (max <= min) return 0.0f;
  if (scale == ParamScale::Log && min > 0.0f)
    return std::log(v / min) / std::log(max / min);
  return (v - min) / (max - min);
}

float ParamSpec::fromNormalized(float n) const {
  n = std::min(std::max(n, 0.0f), 1.0f);
  float v;
  if (scale == ParamScale::Log && min > 0.0f)
    v = min * std::pow(max / min, n);
  else
    v = min + n * (max - min);
  return clamp(v);
}

float ParamSpec::adjust(float v, int direction, bool fine) const {
  if (kind == ParamKind::Enum) {
    const int count = static_cast<int>(choices.size());
    if (count <= 0) return v;
    int i = static_cast<int>(std::lround(v)) + direction;
    i = ((i % count) + count) % count;  // enums wrap around
    return static_cast<float>(i);
  }
  if (kind == ParamKind::Int) return clamp(std::round(v) + static_cast<float>(direction));
  const float step = fine ? 0.0025f : 0.025f;
  return fromNormalized(toNormalized(v) + step * static_cast<float>(direction));
}

std::string ParamSpec::format(float v) const {
  v = clamp(v);
  char buf[64];
  if (kind == ParamKind::Enum) {
    const size_t i = static_cast<size_t>(std::lround(v));
    return i < choices.size() ? choices[i] : "?";
  }
  if (kind == ParamKind::Int) {
    std::snprintf(buf, sizeof buf, min < 0 ? "%+d%s%s" : "%d%s%s", static_cast<int>(std::lround(v)),
                  unit.empty() ? "" : " ", unit.c_str());
    return buf;
  }
  if (unit == "Hz") {
    if (v >= 1000.0f) std::snprintf(buf, sizeof buf, "%.2f kHz", v / 1000.0f);
    else if (v >= 100.0f) std::snprintf(buf, sizeof buf, "%.0f Hz", v);
    else std::snprintf(buf, sizeof buf, "%.2f Hz", v);
  } else if (unit == "s") {
    if (v < 1.0f) std::snprintf(buf, sizeof buf, "%.0f ms", v * 1000.0f);
    else std::snprintf(buf, sizeof buf, "%.2f s", v);
  } else if (unit == "ms") {
    std::snprintf(buf, sizeof buf, "%.0f ms", v);
  } else {
    std::snprintf(buf, sizeof buf, min < 0 ? "%+.*f%s%s" : "%.*f%s%s", decimals, v,
                  unit.empty() ? "" : " ", unit.c_str());
  }
  return buf;
}

bool ParamSpec::parse(const std::string& textIn, float& out) const {
  const std::string text = lower(trim(textIn));
  if (text.empty()) return false;
  if (kind == ParamKind::Enum) {
    for (size_t i = 0; i < choices.size(); ++i)
      if (lower(choices[i]) == text) { out = static_cast<float>(i); return true; }
    for (size_t i = 0; i < choices.size(); ++i)
      if (lower(choices[i]).rfind(text, 0) == 0) { out = static_cast<float>(i); return true; }
    char* end = nullptr;
    const long idx = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str() && *end == '\0' && idx >= 1 && idx <= static_cast<long>(choices.size())) {
      out = static_cast<float>(idx - 1);
      return true;
    }
    return false;
  }
  char* end = nullptr;
  double v = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || !std::isfinite(v)) return false;
  const std::string suffix = trim(std::string(end));
  if (suffix == "k" || suffix == "khz") v *= 1000.0;
  else if (suffix == "ms") { if (unit == "s") v /= 1000.0; }
  else if (!(suffix.empty() || suffix == "hz" || suffix == "s" || suffix == "ct" || suffix == "st" || suffix == "%" ||
             suffix == "deg" || suffix == "db"))
    return false;
  out = clamp(static_cast<float>(v));
  return true;
}

int ModuleSpec::inputIndex(std::string_view id) const {
  for (size_t i = 0; i < inputs.size(); ++i) if (inputs[i].id == id) return static_cast<int>(i);
  return -1;
}
int ModuleSpec::outputIndex(std::string_view id) const {
  for (size_t i = 0; i < outputs.size(); ++i) if (outputs[i].id == id) return static_cast<int>(i);
  return -1;
}
int ModuleSpec::paramIndex(std::string_view id) const {
  for (size_t i = 0; i < params.size(); ++i) if (params[i].id == id) return static_cast<int>(i);
  return -1;
}

const std::vector<const ModuleSpec*>& allSpecs() {
  static const std::vector<ModuleSpec> storage = [] {
    std::vector<ModuleSpec> v;
    registerSourceModules(v);
    registerProcessModules(v);
    registerEffectModules(v);
    return v;
  }();
  static const std::vector<const ModuleSpec*> ptrs = [] {
    std::vector<const ModuleSpec*> p;
    for (const auto& s : storage) p.push_back(&s);
    return p;
  }();
  return ptrs;
}

const ModuleSpec* findSpec(std::string_view type) {
  for (const auto* s : allSpecs())
    if (s->type == type) return s;
  return nullptr;
}

}  // namespace nocturne
