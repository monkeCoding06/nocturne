#include "patch/PatchIO.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace nocturne {

using json = nlohmann::ordered_json;  // keeps keys in written order: readable files

namespace {

using Sev = Diagnostic::Severity;

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

const char* sevName(Sev s) { return s == Sev::Error ? "ERROR" : (s == Sev::Warning ? "WARNING" : "INFO"); }

std::string fmtNum(double v) {
  std::ostringstream o;
  o << v;
  return o.str();
}

}  // namespace

std::string Diagnostic::text() const {
  return std::string(sevName(severity)) + " [" + code + "] " + (where.empty() ? "" : where + ": ") + message;
}

size_t PatchLoadResult::count(Diagnostic::Severity s) const {
  return static_cast<size_t>(std::count_if(diagnostics.begin(), diagnostics.end(),
                                           [&](const Diagnostic& d) { return d.severity == s; }));
}

void PatchLoadResult::add(Diagnostic::Severity s, std::string code, std::string where, std::string message) {
  Diagnostic d;
  d.severity = s;
  d.code = std::move(code);
  d.where = std::move(where);
  d.message = std::move(message);
  if (s == Sev::Error && error.empty()) error = d.where.empty() ? d.message : d.where + ": " + d.message;
  if (s == Sev::Warning) warnings.push_back(d.where.empty() ? d.message : d.where + ": " + d.message);
  diagnostics.push_back(std::move(d));
}

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
  auto finish = [&]() -> PatchLoadResult {
    if (res.count(Sev::Error) > 0) res.patch.reset();
    return std::move(res);
  };
  auto strOf = [](const json& o, const char* key, std::string& out) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
  };

  if (!j.is_object()) {
    res.add(Sev::Error, "not-object", "", "patch file is not a JSON object");
    return finish();
  }
  std::string format;
  if (!strOf(j, "format", format) || format != "nocturne-patch")
    res.add(Sev::Error, "bad-format", "", "not a NOCTURNE patch file (missing \"format\": \"nocturne-patch\")");
  if (!j.contains("version")) {
    res.add(Sev::Info, "missing-version", "", "no version field, assuming version 1");
  } else if (!j["version"].is_number_integer() || j["version"].get<long long>() != 1) {
    res.add(Sev::Error, "unsupported-version", "",
            "unsupported patch version " + j["version"].dump() + " (this build reads version 1)");
  }

  Patch p;
  if (auto it = j.find("meta"); it != j.end()) {
    if (it->is_object()) {
      const auto& m = *it;
      p.meta.name = "untitled";
      strOf(m, "name", p.meta.name);
      strOf(m, "author", p.meta.author);
      strOf(m, "description", p.meta.description);
    } else {
      res.add(Sev::Warning, "bad-meta", "meta", "meta is not an object, ignored");
    }
  }
  if (auto it = j.find("global"); it != j.end()) {
    if (!it->is_object()) {
      res.add(Sev::Warning, "bad-global", "global", "global is not an object, defaults used");
    } else {
      const auto& g = *it;
      std::string mode;
      if (strOf(g, "mode", mode)) {
        if (lower(mode) == "mono") p.global.mode = VoiceMode::Mono;
        else if (lower(mode) == "poly") p.global.mode = VoiceMode::Poly;
        else res.add(Sev::Warning, "bad-value", "global.mode", "unknown mode '" + mode + "', using poly");
      } else if (g.contains("mode")) {
        res.add(Sev::Warning, "bad-value", "global.mode", "mode must be \"poly\" or \"mono\"");
      }
      if (auto v = g.find("voices"); v != g.end()) {
        if (v->is_number()) {
          const double d = v->get<double>();
          p.global.voices = static_cast<int>(std::lround(std::min(std::max(d, -1000.0), 1000.0)));
          if (p.global.voices < 1 || p.global.voices > kMaxVoices) {
            res.add(Sev::Warning, "param-clamped", "global.voices",
                    "value " + fmtNum(d) + " outside 1.." + std::to_string(kMaxVoices) + ", clamped");
            p.global.voices = std::min(std::max(p.global.voices, 1), kMaxVoices);
          }
        } else {
          res.add(Sev::Warning, "bad-value", "global.voices", "voices must be a number");
        }
      }
      if (auto v = g.find("glide"); v != g.end()) {
        if (v->is_number()) {
          const double d = v->get<double>();
          p.global.glide = static_cast<float>(std::min(std::max(d, 0.0), 5.0));
          if (p.global.glide != static_cast<float>(d))
            res.add(Sev::Warning, "param-clamped", "global.glide", "value " + fmtNum(d) + " outside 0..5, clamped");
        } else {
          res.add(Sev::Warning, "bad-value", "global.glide", "glide must be a number");
        }
      }
      if (auto v = g.find("legato"); v != g.end()) {
        if (v->is_boolean()) p.global.legato = v->get<bool>();
        else res.add(Sev::Warning, "bad-value", "global.legato", "legato must be true or false");
      }
    }
  }

  std::set<std::string> failedModules;
  if (!j.contains("modules") || !j["modules"].is_array()) {
    res.add(Sev::Error, "no-modules", "", "patch has no module list");
  } else {
    size_t idx = 0;
    for (const auto& mj : j["modules"]) {
      const std::string slot = "modules[" + std::to_string(idx++) + "]";
      if (!mj.is_object()) {
        res.add(Sev::Error, "bad-module", slot, "module entry is not an object");
        continue;
      }
      std::string name, type;
      strOf(mj, "name", name);
      strOf(mj, "type", type);
      if (name.empty()) {
        res.add(Sev::Error, "bad-module-name", slot, "module has no name");
        continue;
      }
      const ModuleSpec* spec = findSpec(type);
      if (!spec) {
        res.add(Sev::Error, "unknown-module-type", name, "unsupported module type '" + type + "'");
        failedModules.insert(name);
        continue;
      }
      if (p.findModule(name) >= 0) {
        res.add(Sev::Error, "duplicate-module", name, "duplicate module name");
        continue;
      }
      if (spec->unique &&
          std::any_of(p.modules.begin(), p.modules.end(), [&](const ModuleInstance& m) { return m.type == type; })) {
        res.add(Sev::Warning, "extra-module", name, "only one " + spec->label + " allowed, module skipped");
        failedModules.insert(name);
        continue;
      }
      if (static_cast<int>(p.modules.size()) >= kMaxModules) {
        res.add(Sev::Error, "too-many-modules", name, "more than " + std::to_string(kMaxModules) + " modules");
        failedModules.insert(name);
        continue;
      }
      ModuleInstance inst{name, type, defaultParams(*spec)};
      std::vector<char> seen(spec->params.size(), 0);
      if (auto pit = mj.find("params"); pit != mj.end()) {
        if (!pit->is_object()) {
          res.add(Sev::Warning, "bad-params", name, "params is not an object, defaults used");
        } else {
          for (const auto& [key, val] : pit->items()) {
            const int pi = spec->paramIndex(key);
            if (pi < 0) {
              res.add(Sev::Warning, "unknown-param", name, "unknown parameter '" + key + "' ignored");
              continue;
            }
            const auto& ps = spec->params[static_cast<size_t>(pi)];
            const std::string where = name + "." + key;
            float v = ps.def;
            if (val.is_number()) {
              v = val.get<float>();
            } else if (val.is_string()) {
              if (!ps.parse(val.get<std::string>(), v)) {
                std::string choices;
                for (const auto& c : ps.choices) choices += (choices.empty() ? "" : ", ") + lower(c);
                res.add(Sev::Warning, "bad-value", where,
                        "cannot read " + val.dump() + (choices.empty() ? "" : " (choices: " + choices + ")") +
                            ", default used");
                continue;
              }
            } else if (val.is_boolean()) {
              v = val.get<bool>() ? 1.0f : 0.0f;
            } else {
              res.add(Sev::Warning, "bad-value", where, "unsupported value " + val.dump() + ", default used");
              continue;
            }
            const float c = ps.clamp(v);
            if (val.is_number() && std::fabs(c - v) > 1e-6f * (1.0f + std::fabs(v)))
              res.add(Sev::Warning, "param-clamped", where,
                      "value " + fmtNum(v) + " outside " + fmtNum(ps.min) + ".." + fmtNum(ps.max) + ", clamped to " + fmtNum(c));
            inst.params[static_cast<size_t>(pi)] = c;
            seen[static_cast<size_t>(pi)] = 1;
          }
        }
      }
      std::string missing;
      for (size_t k = 0; k < seen.size(); ++k)
        if (!seen[k]) missing += (missing.empty() ? "" : ", ") + spec->params[k].id;
      if (!missing.empty()) res.add(Sev::Info, "param-default", name, "not in file, default used: " + missing);
      p.modules.push_back(std::move(inst));
    }
  }
  if (std::none_of(p.modules.begin(), p.modules.end(), [](const ModuleInstance& m) { return m.type == "master"; }))
    res.add(Sev::Error, "no-master", "", "patch has no MASTER module");

  if (auto cit = j.find("connections"); cit != j.end()) {
    if (!cit->is_array()) {
      res.add(Sev::Error, "bad-connections", "connections", "connections is not an array");
    } else {
      size_t idx = 0;
      for (const auto& cj : *cit) {
        const std::string slot = "connections[" + std::to_string(idx++) + "]";
        std::string from, to, fm, fp, tm, tp;
        if (!cj.is_object() || !strOf(cj, "from", from) || !strOf(cj, "to", to) || !splitRef(from, fm, fp) ||
            !splitRef(to, tm, tp)) {
          res.add(Sev::Warning, "malformed-connection", slot, "dropped: needs \"from\": \"MODULE.PORT\" and \"to\": \"MODULE.PORT\"");
          continue;
        }
        const std::string label = from + " -> " + to;
        if (failedModules.count(fm) || failedModules.count(tm)) {
          res.add(Sev::Warning, "dropped-connection", label, "dropped: refers to a module that did not load");
          continue;
        }
        float amount = 1.0f;
        if (auto a = cj.find("amount"); a != cj.end()) {
          if (a->is_number()) {
            amount = a->get<float>();
            if (amount < -1.0f || amount > 1.0f) {
              res.add(Sev::Warning, "param-clamped", label, "amount " + fmtNum(amount) + " outside -1..1, clamped");
              amount = std::min(std::max(amount, -1.0f), 1.0f);
            }
          } else {
            res.add(Sev::Warning, "bad-value", label, "amount is not a number, 1 used");
          }
        }
        const std::string err = connect(p, fm, fp, tm, tp, amount);
        if (!err.empty()) res.add(Sev::Warning, "dropped-connection", label, "dropped: " + err);
      }
    }
  }

  if (res.count(Sev::Error) == 0) {
    normalizePatch(p);
    for (const auto& issue : validatePatch(p)) res.add(Sev::Warning, "validation", "", issue);
    res.patch = std::move(p);
  }
  return finish();
}

PatchLoadResult patchFromString(const std::string& text) {
  json j;
  try {
    // A UTF-8 byte-order mark (Windows editors) is not part of the JSON.
    const size_t skip = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    j = json::parse(text.begin() + static_cast<std::ptrdiff_t>(skip), text.end());
  } catch (const std::exception& e) {
    PatchLoadResult r;
    r.add(Sev::Error, "malformed-json", "", e.what());
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
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    PatchLoadResult r;
    r.source = path.string();
    r.add(Sev::Error, "missing-file", path.string(), "file does not exist");
    return r;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    PatchLoadResult r;
    r.source = path.string();
    r.add(Sev::Error, "unreadable-file", path.string(), "cannot open file");
    return r;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  PatchLoadResult r = patchFromString(ss.str());
  r.source = path.string();
  return r;
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
