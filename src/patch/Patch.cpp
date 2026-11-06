#include "patch/Patch.hpp"

#include <algorithm>
#include <functional>

namespace nocturne {

int Patch::findModule(const std::string& name) const {
  for (size_t i = 0; i < modules.size(); ++i)
    if (modules[i].name == name) return static_cast<int>(i);
  return -1;
}

const ModuleSpec* Patch::specOf(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(modules.size())) return nullptr;
  return findSpec(modules[static_cast<size_t>(idx)].type);
}

std::vector<float> defaultParams(const ModuleSpec& spec) {
  std::vector<float> p;
  for (const auto& ps : spec.params) p.push_back(ps.def);
  return p;
}

Patch makeEmptyPatch() {
  Patch p;
  p.meta.name = "empty";
  addModule(p, "master");
  return p;
}

std::string addModule(Patch& patch, const std::string& type, std::string* createdName) {
  const ModuleSpec* spec = findSpec(type);
  if (!spec) return "unknown module type '" + type + "'";
  if (static_cast<int>(patch.modules.size()) >= kMaxModules) return "module limit reached";
  if (spec->unique) {
    for (const auto& m : patch.modules)
      if (m.type == type) return spec->label + " already exists";
  }
  std::string name = spec->prefix;
  if (!spec->unique) {
    int n = 1;
    while (patch.findModule(spec->prefix + std::to_string(n)) >= 0) ++n;
    name = spec->prefix + std::to_string(n);
  }
  patch.modules.push_back({name, type, defaultParams(*spec)});
  if (createdName) *createdName = name;
  return {};
}

std::string removeModule(Patch& patch, const std::string& name) {
  const int idx = patch.findModule(name);
  if (idx < 0) return "no such module";
  const ModuleSpec* spec = patch.specOf(idx);
  if (spec && spec->unique) return spec->label + " cannot be removed";
  patch.connections.erase(std::remove_if(patch.connections.begin(), patch.connections.end(),
                                         [&](const Connection& c) { return c.from == name || c.to == name; }),
                          patch.connections.end());
  patch.modules.erase(patch.modules.begin() + idx);
  return {};
}

namespace {

// Adjacency over module indices; optionally with one extra edge.
std::vector<std::vector<int>> buildAdjacency(const Patch& p, int extraFrom = -1, int extraTo = -1) {
  std::vector<std::vector<int>> adj(p.modules.size());
  for (const auto& c : p.connections) {
    const int a = p.findModule(c.from), b = p.findModule(c.to);
    if (a >= 0 && b >= 0) adj[static_cast<size_t>(a)].push_back(b);
  }
  if (extraFrom >= 0 && extraTo >= 0) adj[static_cast<size_t>(extraFrom)].push_back(extraTo);
  return adj;
}

bool reaches(const std::vector<std::vector<int>>& adj, int start, int target) {
  std::vector<char> seen(adj.size(), 0);
  std::vector<int> stack{start};
  while (!stack.empty()) {
    const int n = stack.back();
    stack.pop_back();
    if (n == target) return true;
    if (seen[static_cast<size_t>(n)]) continue;
    seen[static_cast<size_t>(n)] = 1;
    for (int m : adj[static_cast<size_t>(n)]) stack.push_back(m);
  }
  return false;
}

}  // namespace

std::string checkConnection(const Patch& patch, const std::string& from, const std::string& fromPort,
                            const std::string& to, const std::string& toPort) {
  const int a = patch.findModule(from), b = patch.findModule(to);
  if (a < 0) return "unknown source module " + from;
  if (b < 0) return "unknown destination module " + to;
  const ModuleSpec* sa = patch.specOf(a);
  const ModuleSpec* sb = patch.specOf(b);
  if (!sa || !sb) return "unknown module type";
  const int po = sa->outputIndex(fromPort);
  const int pi = sb->inputIndex(toPort);
  if (po < 0 || sa->outputs[static_cast<size_t>(po)].internal) return from + " has no output " + fromPort;
  if (pi < 0 || sb->inputs[static_cast<size_t>(pi)].internal) return to + " has no input " + toPort;
  const PortSpec& out = sa->outputs[static_cast<size_t>(po)];
  const PortSpec& in = sb->inputs[static_cast<size_t>(pi)];

  if (a == b) return "a module cannot patch into itself";
  for (const auto& c : patch.connections)
    if (c.from == from && c.fromPort == fromPort && c.to == to && c.toPort == toPort)
      return "already connected";

  if (out.kind == SignalKind::Control && in.kind == SignalKind::Audio)
    return "control signal cannot feed an audio input";
  if (out.kind == SignalKind::Audio && in.kind == SignalKind::Control && !in.acceptsAudio)
    return "audio signal cannot feed " + toPort + " (control input)";
  if (sa->scope == Scope::Global && sb->scope == Scope::Voice)
    return "effects/global modules cannot feed per-voice modules";
  if (sa->scope == Scope::Voice && sb->scope == Scope::Global && out.kind != SignalKind::Audio)
    return "only audio can leave the voice section";

  if (reaches(buildAdjacency(patch), b, a)) return "would create a feedback loop";
  return {};
}

std::string connect(Patch& patch, const std::string& from, const std::string& fromPort, const std::string& to,
                    const std::string& toPort, float amount) {
  std::string err = checkConnection(patch, from, fromPort, to, toPort);
  if (!err.empty()) return err;
  patch.connections.push_back({from, fromPort, to, toPort, std::min(std::max(amount, -1.0f), 1.0f)});
  return {};
}

void disconnect(Patch& patch, size_t i) {
  if (i < patch.connections.size()) patch.connections.erase(patch.connections.begin() + static_cast<long>(i));
}

bool hasCycle(const Patch& patch) { return processingOrder(patch).size() != patch.modules.size(); }

std::vector<int> processingOrder(const Patch& patch) {
  const auto adj = buildAdjacency(patch);
  std::vector<int> indeg(patch.modules.size(), 0);
  for (const auto& edges : adj)
    for (int m : edges) ++indeg[static_cast<size_t>(m)];
  std::vector<int> order;
  std::vector<int> ready;
  for (size_t i = 0; i < indeg.size(); ++i)
    if (indeg[i] == 0) ready.push_back(static_cast<int>(i));
  while (!ready.empty()) {
    // smallest index first for a deterministic order
    auto it = std::min_element(ready.begin(), ready.end());
    const int n = *it;
    ready.erase(it);
    order.push_back(n);
    for (int m : adj[static_cast<size_t>(n)])
      if (--indeg[static_cast<size_t>(m)] == 0) ready.push_back(m);
  }
  if (order.size() != patch.modules.size()) order.clear();
  return order;
}

std::vector<std::string> validatePatch(const Patch& patch) {
  std::vector<std::string> problems;
  int masters = 0;
  for (size_t i = 0; i < patch.modules.size(); ++i) {
    const auto& m = patch.modules[i];
    const ModuleSpec* spec = findSpec(m.type);
    if (!spec) { problems.push_back("unknown module type " + m.type); continue; }
    if (spec->type == "master") ++masters;
    if (m.params.size() != spec->params.size()) problems.push_back(m.name + ": parameter count mismatch");
    for (size_t j = i + 1; j < patch.modules.size(); ++j)
      if (patch.modules[j].name == m.name) problems.push_back("duplicate module name " + m.name);
  }
  if (masters != 1) problems.push_back("patch needs exactly one MASTER module");
  // Re-check each connection against a patch that does not contain it.
  for (size_t i = 0; i < patch.connections.size(); ++i) {
    Patch without = patch;
    const Connection c = patch.connections[i];
    disconnect(without, i);
    const std::string err = checkConnection(without, c.from, c.fromPort, c.to, c.toPort);
    if (!err.empty()) problems.push_back(c.from + "." + c.fromPort + " -> " + c.to + "." + c.toPort + ": " + err);
  }
  if (hasCycle(patch)) problems.push_back("patch contains a feedback loop");
  return problems;
}

void normalizePatch(Patch& patch) {
  patch.global.voices = std::min(std::max(patch.global.voices, 1), kMaxVoices);
  patch.global.glide = std::min(std::max(patch.global.glide, 0.0f), 5.0f);
  for (auto& m : patch.modules) {
    const ModuleSpec* spec = findSpec(m.type);
    if (!spec) continue;
    m.params.resize(spec->params.size(), 0.0f);
    for (size_t i = 0; i < spec->params.size(); ++i) m.params[i] = spec->params[i].clamp(m.params[i]);
  }
  for (auto& c : patch.connections) c.amount = std::min(std::max(c.amount, -1.0f), 1.0f);
}

}  // namespace nocturne
