#include "audio/Rack.hpp"

#include <algorithm>
#include <cstring>

namespace nocturne {

namespace {
constexpr float kFadeInSec = 0.002f;
constexpr float kFadeOutSec = 0.015f;
constexpr float kKillSec = 0.004f;
constexpr double kMaxReleaseSec = 30.0;
}  // namespace

std::unique_ptr<Rack> Rack::build(const Patch& patch, double sr, std::string* error) {
  auto fail = [&](const std::string& msg) -> std::unique_ptr<Rack> {
    if (error) *error = msg;
    return nullptr;
  };
  const auto problems = validatePatch(patch);
  if (!problems.empty()) return fail(problems.front());
  const auto order = processingOrder(patch);

  auto rack = std::make_unique<Rack>();
  rack->sr_ = sr;
  rack->settings_ = patch.global;
  rack->settings_.voices = std::min(std::max(rack->settings_.voices, 1), kMaxVoices);
  const int voiceCount = patch.global.mode == VoiceMode::Mono ? 1 : rack->settings_.voices;
  rack->voices_.resize(static_cast<size_t>(voiceCount));
  rack->loc_.assign(patch.modules.size(), {});

  auto makeNode = [&](const ModuleInstance& mi, const ModuleSpec& spec) {
    Node n;
    n.name = mi.name;
    n.spec = &spec;
    n.params = mi.params;
    n.mod = spec.create();
    n.mod->setAllParams(mi.params.data(), static_cast<int>(mi.params.size()));
    n.mod->prepare(sr);
    n.mod->reset();
    n.nIn = static_cast<int>(spec.inputs.size());
    n.nOut = static_cast<int>(spec.outputs.size());
    n.in.assign(static_cast<size_t>(std::max(n.nIn, 1) * kBlock), 0.0f);
    n.out.assign(static_cast<size_t>(std::max(n.nOut, 1) * kBlock), 0.0f);
    return n;
  };

  for (int idx : order) {
    const auto& mi = patch.modules[static_cast<size_t>(idx)];
    const ModuleSpec& spec = *findSpec(mi.type);
    if (spec.scope == Scope::Voice) {
      for (auto& v : rack->voices_) {
        v.nodes.push_back(makeNode(mi, spec));
        v.hasEnvelope = v.hasEnvelope || spec.isEnvelope;
      }
      rack->loc_[static_cast<size_t>(idx)] = {false, static_cast<int>(rack->voices_[0].nodes.size()) - 1};
    } else {
      rack->global_.nodes.push_back(makeNode(mi, spec));
      rack->loc_[static_cast<size_t>(idx)] = {true, static_cast<int>(rack->global_.nodes.size()) - 1};
      if (spec.type == "master") rack->masterNode_ = static_cast<int>(rack->global_.nodes.size()) - 1;
    }
  }
  if (rack->masterNode_ < 0) return fail("patch has no MASTER module");

  for (size_t ci = 0; ci < patch.connections.size(); ++ci) {
    const auto& c = patch.connections[ci];
    const int a = patch.findModule(c.from), b = patch.findModule(c.to);
    const ModuleSpec* sa = patch.specOf(a);
    const ModuleSpec* sb = patch.specOf(b);
    const int po = sa->outputIndex(c.fromPort), pi = sb->inputIndex(c.toPort);
    const Loc la = rack->loc_[static_cast<size_t>(a)], lb = rack->loc_[static_cast<size_t>(b)];
    Wire w{la.node, po, lb.node, pi, c.amount, static_cast<int>(ci)};
    if (!la.global && !lb.global) {
      for (auto& v : rack->voices_) {
        v.nodes[static_cast<size_t>(lb.node)].incoming.push_back(w);
        v.nodes[static_cast<size_t>(lb.node)].connected[pi] = true;
      }
    } else if (la.global && lb.global) {
      rack->global_.nodes[static_cast<size_t>(lb.node)].incoming.push_back(w);
      rack->global_.nodes[static_cast<size_t>(lb.node)].connected[pi] = true;
    } else if (!la.global && lb.global) {
      rack->cross_.push_back(w);
      rack->global_.nodes[static_cast<size_t>(lb.node)].connected[pi] = true;
    }
  }
  return rack;
}

void Rack::setParam(int moduleIdx, int param, float value) {
  if (moduleIdx < 0 || moduleIdx >= static_cast<int>(loc_.size())) return;
  const Loc l = loc_[static_cast<size_t>(moduleIdx)];
  auto apply = [&](Instance& inst) {
    Node& n = inst.nodes[static_cast<size_t>(l.node)];
    if (param >= 0 && param < static_cast<int>(n.params.size())) n.params[static_cast<size_t>(param)] = value;
    n.mod->setParam(param, value);
  };
  if (l.global) apply(global_);
  else for (auto& v : voices_) apply(v);
}

void Rack::setWireAmount(int connIdx, float amount) {
  auto upd = [&](std::vector<Wire>& ws) {
    for (auto& w : ws) if (w.connIdx == connIdx) w.amount = amount;
  };
  upd(cross_);
  for (auto& n : global_.nodes) upd(n.incoming);
  for (auto& v : voices_) for (auto& n : v.nodes) upd(n.incoming);
}

void Rack::triggerVoice(int v, const VoiceCtx& ctx, bool cold) {
  if (v < 0 || v >= voiceCount()) return;
  Instance& inst = voices_[static_cast<size_t>(v)];
  if (cold) {
    for (auto& n : inst.nodes) n.mod->reset();
    inst.gain = 0.0f;
  }
  inst.killed = false;
  inst.holding = true;
  inst.sinceRelease = 0.0;
  for (auto& n : inst.nodes) n.mod->trigger(ctx);
}

void Rack::killVoice(int v) {
  if (v >= 0 && v < voiceCount()) voices_[static_cast<size_t>(v)].killed = true;
}

void Rack::runNodes(Instance& inst, int n, const VoiceCtx* ctx) {
  const bool zero = ctx != nullptr;  // global inputs are pre-filled by the voices
  for (auto& node : inst.nodes) {
    if (zero) {
      for (int p = 0; p < node.nIn; ++p)
        if (node.connected[p]) std::memset(&node.in[static_cast<size_t>(p * kBlock)], 0, sizeof(float) * static_cast<size_t>(n));
    }
    for (const Wire& w : node.incoming) {
      const Node& src = inst.nodes[static_cast<size_t>(w.srcNode)];
      const float* s = &src.out[static_cast<size_t>(w.srcPort * kBlock)];
      float* d = &node.in[static_cast<size_t>(w.dstPort * kBlock)];
      const float a = w.amount;
      for (int i = 0; i < n; ++i) d[i] += s[i] * a;
    }
    ModuleIO io;
    io.n = n;
    io.voice = ctx;
    for (int p = 0; p < node.nIn; ++p) io.in[p] = node.connected[p] ? &node.in[static_cast<size_t>(p * kBlock)] : nullptr;
    for (int p = 0; p < node.nOut; ++p) io.out[p] = &node.out[static_cast<size_t>(p * kBlock)];
    node.mod->process(io);
  }
}

void Rack::beginChunk() {
  for (auto& node : global_.nodes) std::fill(node.in.begin(), node.in.end(), 0.0f);
}

void Rack::processVoice(int v, const VoiceCtx& ctx, int n) {
  Instance& inst = voices_[static_cast<size_t>(v)];
  runNodes(inst, n, &ctx);

  bool busy = false;
  if (inst.hasEnvelope)
    for (auto& node : inst.nodes)
      if (node.spec->isEnvelope && node.mod->busy()) busy = true;

  const float sr = static_cast<float>(sr_);
  const float fn = static_cast<float>(n);
  inst.holding = !inst.killed && (ctx.gate > 0.5f || busy);
  if (ctx.gate > 0.5f) inst.sinceRelease = 0.0;
  else {
    inst.sinceRelease += n / sr_;
    if (inst.sinceRelease > kMaxReleaseSec) inst.killed = true;  // runaway voice guard
  }
  const float g0 = inst.gain;
  float g1;
  if (inst.holding) g1 = std::min(1.0f, g0 + fn / (kFadeInSec * sr));
  else g1 = std::max(0.0f, g0 - fn / ((inst.killed ? kKillSec : kFadeOutSec) * sr));
  inst.gain = g1;

  for (const Wire& w : cross_) {
    const float* s = &inst.nodes[static_cast<size_t>(w.srcNode)].out[static_cast<size_t>(w.srcPort * kBlock)];
    float* d = &global_.nodes[static_cast<size_t>(w.dstNode)].in[static_cast<size_t>(w.dstPort * kBlock)];
    const float step = (g1 - g0) / fn;
    float g = g0;
    for (int i = 0; i < n; ++i) {
      g += step;
      d[i] += s[i] * w.amount * g;
    }
  }
}

void Rack::processGlobal(int n, float* left, float* right) {
  runNodes(global_, n, nullptr);
  const Node& m = global_.nodes[static_cast<size_t>(masterNode_)];
  std::memcpy(left, &m.out[0], sizeof(float) * static_cast<size_t>(n));
  std::memcpy(right, &m.out[kBlock], sizeof(float) * static_cast<size_t>(n));
}

bool Rack::voiceFinished(int v) const {
  const Instance& inst = voices_[static_cast<size_t>(v)];
  return !inst.holding && inst.gain <= 1e-5f;
}

void Rack::adoptState(Rack& old) {
  if (old.sr_ != sr_) return;
  auto adopt = [](Instance& to, Instance& from) {
    for (auto& n : to.nodes) {
      for (auto& o : from.nodes) {
        if (o.name == n.name && o.spec == n.spec) {
          std::swap(n.mod, o.mod);
          n.mod->setAllParams(n.params.data(), static_cast<int>(n.params.size()));
          break;
        }
      }
    }
    to.gain = from.gain;
    to.killed = from.killed;
    to.holding = from.holding;
    to.sinceRelease = from.sinceRelease;
  };
  const size_t nv = std::min(voices_.size(), old.voices_.size());
  for (size_t i = 0; i < nv; ++i) adopt(voices_[i], old.voices_[i]);
  adopt(global_, old.global_);
}

}  // namespace nocturne
