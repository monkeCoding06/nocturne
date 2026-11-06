#include "patch/PatchIO.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace nocturne {

using json = nlohmann::ordered_json;  // keeps keys in written order: readable files

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

double round4(double v) { return std::round(v * 10000.0) / 10000.0; }

bool splitRef(const std::string& ref, std::string& module, std::string& port) {
  const auto dot = ref.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= ref.size()) return false;
  module = ref.substr(0, dot);
  port = ref.substr(dot + 1);
  return true;
}

}  // namespace

json patchToJson(const Patch& patch) {
  json j;
  j["format"] = "nocturne-patch";
  j["version"] = 1;
  j["meta"] = {{"name", patch.meta.name}, {"author", patch.meta.author}, {"description", patch.meta.description}};
  j["global"] = {{"mode", patch.global.mode == VoiceMode::Mono ? "mono" : "poly"},
                 {"voices", patch.global.voices},
                 {"glide", round4(patch.global.glide)},
                 {"legato", patch.global.legato}};
  json mods = json::array();
  for (const auto& m : patch.modules) {
    const ModuleSpec* spec = findSpec(m.type);
    json params = json::object();
    if (spec) {
      for (size_t i = 0; i < spec->params.size() && i < m.params.size(); ++i) {
        const auto& ps = spec->params[i];
        if (ps.kind == ParamKind::Enum) params[ps.id] = lower(ps.format(m.params[i]));
        else if (ps.kind == ParamKind::Int) params[ps.id] = static_cast<int>(std::lround(m.params[i]));
        else params[ps.id] = round4(m.params[i]);
      }
    }
    mods.push_back({{"name", m.name}, {"type", m.type}, {"params", params}});
  }
  j["modules"] = mods;
  json conns = json::array();
  for (const auto& c : patch.connections)
    conns.push_back({{"from", c.from + "." + c.fromPort}, {"to", c.to + "." + c.toPort}, {"amount", round4(c.amount)}});
  j["connections"] = conns;
  return j;
}

std::string patchToString(const Patch& patch) { return patchToJson(patch).dump(2) + "\n"; }

PatchLoadResult patchFromJson(const json& j) {
  PatchLoadResult res;
  auto fail = [&](const std::string& msg) {
    res.error = msg;
    res.patch.reset();
    return res;
  };
  if (!j.is_object()) return fail("patch file is not a JSON object");
  if (j.value("format", "") != "nocturne-patch") return fail("not a NOCTURNE patch file");
  if (j.value("version", 0) != 1) res.warnings.push_back("unknown patch version, trying anyway");

  Patch p;
  if (j.contains("meta") && j["meta"].is_object()) {
    const auto& m = j["meta"];
    p.meta.name = m.value("name", "untitled");
    p.meta.author = m.value("author", "");
    p.meta.description = m.value("description", "");
  }
  if (j.contains("global") && j["global"].is_object()) {
    const auto& g = j["global"];
    p.global.mode = g.value("mode", "poly") == "mono" ? VoiceMode::Mono : VoiceMode::Poly;
    p.global.voices = g.value("voices", 8);
    p.global.glide = g.value("glide", 0.0f);
    p.global.legato = g.value("legato", true);
  }

  if (!j.contains("modules") || !j["modules"].is_array()) return fail("patch has no module list");
  for (const auto& mj : j["modules"]) {
    if (!mj.is_object()) continue;
    const std::string name = mj.value("name", "");
    const std::string type = mj.value("type", "");
    const ModuleSpec* spec = findSpec(type);
    if (!spec) { res.warnings.push_back("skipped module '" + name + "' of unknown type '" + type + "'"); continue; }
    if (name.empty() || p.findModule(name) >= 0) { res.warnings.push_back("skipped module with missing/duplicate name"); continue; }
    if (spec->unique && std::any_of(p.modules.begin(), p.modules.end(), [&](const ModuleInstance& m) { return m.type == type; })) {
      res.warnings.push_back("skipped extra " + spec->label);
      continue;
    }
    if (static_cast<int>(p.modules.size()) >= kMaxModules) { res.warnings.push_back("too many modules"); break; }
    ModuleInstance inst{name, type, defaultParams(*spec)};
    if (mj.contains("params") && mj["params"].is_object()) {
      for (const auto& [key, val] : mj["params"].items()) {
        const int pi = spec->paramIndex(key);
        if (pi < 0) { res.warnings.push_back(name + ": unknown parameter '" + key + "'"); continue; }
        const auto& ps = spec->params[static_cast<size_t>(pi)];
        float v = ps.def;
        if (val.is_number()) v = val.get<float>();
        else if (val.is_string()) { if (!ps.parse(val.get<std::string>(), v)) { res.warnings.push_back(name + "." + key + ": bad value"); continue; } }
        else if (val.is_boolean()) v = val.get<bool>() ? 1.0f : 0.0f;
        inst.params[static_cast<size_t>(pi)] = ps.clamp(v);
      }
    }
    p.modules.push_back(std::move(inst));
  }
  if (p.findModule("MASTER") < 0 && std::none_of(p.modules.begin(), p.modules.end(), [](const ModuleInstance& m) { return m.type == "master"; })) {
    addModule(p, "master");
    res.warnings.push_back("added missing MASTER module");
  }

  if (j.contains("connections") && j["connections"].is_array()) {
    for (const auto& cj : j["connections"]) {
      std::string fm, fp, tm, tp;
      if (!cj.is_object() || !splitRef(cj.value("from", ""), fm, fp) || !splitRef(cj.value("to", ""), tm, tp)) {
        res.warnings.push_back("skipped malformed connection");
        continue;
      }
      const std::string err = connect(p, fm, fp, tm, tp, cj.value("amount", 1.0f));
      if (!err.empty()) res.warnings.push_back("dropped " + fm + "." + fp + " -> " + tm + "." + tp + ": " + err);
    }
  }
  normalizePatch(p);
  res.patch = std::move(p);
  return res;
}

PatchLoadResult patchFromString(const std::string& text) {
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded()) {
    PatchLoadResult r;
    r.error = "invalid JSON";
    return r;
  }
  return patchFromJson(j);
}

std::string savePatchFile(const Patch& patch, const std::filesystem::path& path) {
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return "cannot write " + path.string();
  f << patchToString(patch);
  return f.good() ? std::string() : "write error: " + path.string();
}

PatchLoadResult loadPatchFile(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    PatchLoadResult r;
    r.error = "cannot open " + path.string();
    return r;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return patchFromString(ss.str());
}

std::string sanitizePatchFileName(const std::string& name) {
  std::string out;
  for (unsigned char c : name) {
    if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    else if (c == ' ' || c == '_' || c == '-') out += '_';
  }
  while (!out.empty() && out.front() == '_') out.erase(out.begin());
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out.empty() ? "patch" : out;
}

}  // namespace nocturne
