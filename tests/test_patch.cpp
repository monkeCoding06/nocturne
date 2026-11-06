#include <cmath>

#include "TestHarness.hpp"
#include "audio/VoiceAllocator.hpp"
#include "modules/ModuleSpec.hpp"
#include "patch/Patch.hpp"
#include "patch/PatchIO.hpp"
#include "patch/Presets.hpp"

using namespace nocturne;

TEST(registry_contains_all_module_types) {
  for (const char* t : {"osc", "lfo", "keys", "filter", "adsr", "amp", "mixer", "drive", "delay", "reverb", "master"})
    CHECK(findSpec(t) != nullptr);
  CHECK(findSpec("nope") == nullptr);
}

TEST(parameter_limits_and_defaults) {
  for (const ModuleSpec* spec : allSpecs()) {
    for (const ParamSpec& p : spec->params) {
      CHECK(p.min < p.max);
      CHECK(p.def >= p.min && p.def <= p.max);
      CHECK(p.clamp(p.min - 1000.0f) == p.min);
      CHECK(p.clamp(p.max + 1000.0f) == p.max);
      CHECK(p.clamp(std::nanf("")) >= p.min);
      CHECK_NEAR(p.fromNormalized(p.toNormalized(p.def)), p.def, (p.max - p.min) * 0.01);
      CHECK_NEAR(p.toNormalized(p.min), 0.0, 1e-5);
      CHECK_NEAR(p.toNormalized(p.max), 1.0, 1e-5);
      // stepping never leaves the range, even when hammered
      float v = p.def;
      for (int i = 0; i < 500; ++i) v = p.adjust(v, +1, false);
      CHECK(v <= p.max && v >= p.min);
      for (int i = 0; i < 1000; ++i) v = p.adjust(v, -1, true);
      CHECK(v <= p.max && v >= p.min);
    }
  }
}

TEST(frequency_parameters_are_logarithmic) {
  const ParamSpec& cutoff = findSpec("filter")->params[static_cast<size_t>(findSpec("filter")->paramIndex("cutoff"))];
  CHECK(cutoff.scale == ParamScale::Log);
  // Halfway along the control is the geometric mean, not the arithmetic one.
  CHECK_NEAR(cutoff.fromNormalized(0.5f), std::sqrt(cutoff.min * cutoff.max), 5.0);
}

TEST(parameter_coarse_fine_and_parse) {
  const ModuleSpec* f = findSpec("filter");
  const ParamSpec& cutoff = f->params[static_cast<size_t>(f->paramIndex("cutoff"))];
  const float c = cutoff.adjust(1000.0f, +1, false);
  const float fi = cutoff.adjust(1000.0f, +1, true);
  CHECK(c > 1000.0f && fi > 1000.0f && (fi - 1000.0f) < (c - 1000.0f) * 0.5f);
  float v = 0;
  CHECK(cutoff.parse("2.4k", v));
  CHECK_NEAR(v, 2400.0, 0.5);
  CHECK(cutoff.parse("440", v));
  CHECK_NEAR(v, 440.0, 0.01);
  CHECK(cutoff.parse("440 hz", v));
  CHECK(!cutoff.parse("banana", v));
  const ModuleSpec* o = findSpec("osc");
  const ParamSpec& wave = o->params[static_cast<size_t>(o->paramIndex("wave"))];
  CHECK(wave.parse("square", v));
  CHECK_NEAR(v, 3.0, 0.01);
  CHECK(wave.format(2.0f) == "SAW");
}

namespace {
Patch basic() {
  Patch p = makeEmptyPatch();
  std::string n;
  for (const char* t : {"osc", "filter", "amp", "adsr", "lfo"}) CHECK(addModule(p, t, &n).empty());
  return p;
}
}  // namespace

TEST(patch_add_remove_modules) {
  Patch p = makeEmptyPatch();
  std::string n1, n2;
  CHECK(addModule(p, "osc", &n1).empty());
  CHECK(addModule(p, "osc", &n2).empty());
  CHECK(n1 == "OSC1" && n2 == "OSC2");
  CHECK(!addModule(p, "master").empty());  // MASTER is unique
  CHECK(!addModule(p, "bogus").empty());
  CHECK(!removeModule(p, "MASTER").empty());
  CHECK(connect(p, "OSC1", "OUT", "MASTER", "IN").empty());
  CHECK(removeModule(p, "OSC1").empty());
  CHECK(p.connections.empty());  // dangling connections are removed with the module
  CHECK(p.findModule("OSC1") < 0 && p.findModule("OSC2") >= 0);
}

TEST(connection_validation_rules) {
  Patch p = basic();
  CHECK(connect(p, "OSC1", "OUT", "FILTER1", "IN").empty());
  CHECK(connect(p, "FILTER1", "OUT", "AMP1", "IN").empty());
  CHECK(connect(p, "ADSR1", "OUT", "AMP1", "GAIN").empty());
  CHECK(connect(p, "LFO1", "OUT", "FILTER1", "CUTOFF").empty());
  CHECK(connect(p, "AMP1", "OUT", "MASTER", "IN").empty());
  CHECK(!connect(p, "OSC1", "OUT", "FILTER1", "IN").empty());      // duplicate
  CHECK(!connect(p, "OSC1", "OUT", "OSC1", "FM").empty() ||        // self connection
        p.connections.size() == 5);
  CHECK(!connect(p, "LFO1", "OUT", "AMP1", "IN").empty());         // control -> audio input
  CHECK(!connect(p, "OSC1", "OUT", "FILTER1", "RES").empty());     // audio -> plain control input
  CHECK(!connect(p, "OSC1", "NOPE", "FILTER1", "IN").empty());     // unknown port
  CHECK(!connect(p, "GHOST", "OUT", "FILTER1", "IN").empty());     // unknown module
  CHECK(!connect(p, "FILTER1", "IN", "AMP1", "IN").empty());       // input used as source
  CHECK(connect(p, "OSC1", "OUT", "OSC1", "FM").size() > 0);
  CHECK(validatePatch(p).empty());
}

TEST(voice_to_global_scope_rules) {
  Patch p = basic();
  std::string n;
  CHECK(addModule(p, "delay", &n).empty());
  CHECK(connect(p, "AMP1", "OUT", "DELAY1", "IN").empty());          // voices sum into a global effect
  CHECK(!connect(p, "DELAY1", "OUT", "FILTER1", "IN").empty());      // global -> per-voice is impossible
  CHECK(!connect(p, "LFO1", "OUT", "DELAY1", "IN").empty());         // control -> audio
}

TEST(cycle_detection) {
  Patch p = basic();
  CHECK(connect(p, "OSC1", "OUT", "FILTER1", "IN").empty());
  CHECK(connect(p, "FILTER1", "OUT", "AMP1", "IN").empty());
  CHECK(!hasCycle(p));
  CHECK(!processingOrder(p).empty());
  // Closing the loop through the audio-accepting FM input is rejected.
  CHECK(!connect(p, "AMP1", "OUT", "OSC1", "FM").empty());
  CHECK(!connect(p, "AMP1", "OUT", "FILTER1", "CUTOFF").empty());
  CHECK(!hasCycle(p));
  // A cycle forced into the data (e.g. by a hand-edited file) is detected.
  Connection c;
  c.from = "AMP1"; c.fromPort = "OUT"; c.to = "FILTER1"; c.toPort = "CUTOFF";
  p.connections.push_back(c);
  CHECK(hasCycle(p));
  CHECK(processingOrder(p).empty());
  CHECK(!validatePatch(p).empty());
}

TEST(processing_order_respects_dependencies) {
  Patch p = basic();
  CHECK(connect(p, "LFO1", "OUT", "FILTER1", "CUTOFF").empty());
  CHECK(connect(p, "OSC1", "OUT", "FILTER1", "IN").empty());
  CHECK(connect(p, "FILTER1", "OUT", "AMP1", "IN").empty());
  CHECK(connect(p, "ADSR1", "OUT", "AMP1", "GAIN").empty());
  CHECK(connect(p, "AMP1", "OUT", "MASTER", "IN").empty());
  const auto order = processingOrder(p);
  CHECK(order.size() == p.modules.size());
  auto pos = [&](const char* n) {
    for (size_t i = 0; i < order.size(); ++i)
      if (p.modules[static_cast<size_t>(order[i])].name == n) return static_cast<int>(i);
    return -1;
  };
  CHECK(pos("OSC1") < pos("FILTER1"));
  CHECK(pos("LFO1") < pos("FILTER1"));
  CHECK(pos("FILTER1") < pos("AMP1"));
  CHECK(pos("ADSR1") < pos("AMP1"));
  CHECK(pos("AMP1") < pos("MASTER"));
}

TEST(patch_json_round_trip_all_factory_presets) {
  for (const auto& info : factoryPresets()) {
    const Patch a = info.build();
    CHECK(validatePatch(a).empty());
    const std::string text = patchToString(a);
    const auto res = patchFromString(text);
    CHECK(res.patch.has_value());
    if (!res.patch) continue;
    const Patch& b = *res.patch;
    CHECK(res.warnings.empty());
    CHECK(b.meta.name == a.meta.name);
    CHECK(b.modules.size() == a.modules.size());
    CHECK(b.connections.size() == a.connections.size());
    CHECK(b.global.voices == a.global.voices && b.global.mode == a.global.mode);
    CHECK_NEAR(b.global.glide, a.global.glide, 1e-6);
    for (size_t i = 0; i < a.modules.size() && i < b.modules.size(); ++i) {
      CHECK(a.modules[i].name == b.modules[i].name && a.modules[i].type == b.modules[i].type);
      CHECK(a.modules[i].params.size() == b.modules[i].params.size());
      for (size_t k = 0; k < a.modules[i].params.size() && k < b.modules[i].params.size(); ++k)
        CHECK_NEAR(a.modules[i].params[k], b.modules[i].params[k], 1e-4 * (1.0 + std::fabs(a.modules[i].params[k])));
    }
    for (size_t i = 0; i < a.connections.size() && i < b.connections.size(); ++i) {
      CHECK(a.connections[i].from == b.connections[i].from && a.connections[i].toPort == b.connections[i].toPort);
      CHECK_NEAR(a.connections[i].amount, b.connections[i].amount, 1e-5);
    }
  }
}

TEST(patch_json_is_human_readable) {
  const std::string text = patchToString(factoryPresets().front().build());
  CHECK(text.find("\"wave\": \"sine\"") != std::string::npos);
  CHECK(text.find("\"OSC1.OUT\"") != std::string::npos);
  CHECK(text.find('\n') != std::string::npos);
}

TEST(patch_loader_rejects_garbage_without_throwing) {
  CHECK(!patchFromString("").patch.has_value());
  CHECK(!patchFromString("{not json").patch.has_value());
  CHECK(!patchFromString("[1,2,3]").patch.has_value());
  CHECK(!patchFromString("{\"format\":\"other\"}").patch.has_value());
  // Unknown module types, bad connections and out-of-range values degrade gracefully.
  const std::string text = R"({"format":"nocturne-patch","version":1,
    "modules":[{"name":"MASTER","type":"master"},{"name":"X1","type":"warp"},
               {"name":"OSC1","type":"osc","params":{"level":99,"wave":"banana","bogus":1}}],
    "connections":[{"from":"OSC1.OUT","to":"MASTER.IN"},{"from":"X1.OUT","to":"MASTER.IN"},
                   {"from":"OSC1.OUT","to":"MASTER.NOPE"}]})";
  const auto res = patchFromString(text);
  CHECK(res.patch.has_value());
  if (res.patch) {
    CHECK(res.patch->connections.size() == 1);
    CHECK(res.patch->findModule("X1") < 0);
    CHECK(!res.warnings.empty());
    const ModuleSpec* o = findSpec("osc");
    CHECK(res.patch->modules[static_cast<size_t>(res.patch->findModule("OSC1"))].params[static_cast<size_t>(o->paramIndex("level"))] <= 1.0f);
  }
}

TEST(patch_loader_rejects_cycles) {
  const std::string text = R"({"format":"nocturne-patch","version":1,
    "modules":[{"name":"MASTER","type":"master"},{"name":"FILTER1","type":"filter"},{"name":"AMP1","type":"amp"}],
    "connections":[{"from":"FILTER1.OUT","to":"AMP1.IN"},{"from":"AMP1.OUT","to":"FILTER1.IN"},{"from":"AMP1.OUT","to":"MASTER.IN"}]})";
  const auto res = patchFromString(text);
  CHECK(res.patch.has_value());
  if (res.patch) CHECK(!hasCycle(*res.patch));
}

TEST(file_name_sanitizing) {
  CHECK(sanitizePatchFileName("My Patch!") == "my_patch");
  CHECK(sanitizePatchFileName("../../etc/passwd").find('/') == std::string::npos);
  CHECK(sanitizePatchFileName("///") == "patch");
}

// ------------------------------------------------------------ voices
TEST(voice_allocator_poly_basics) {
  VoiceAllocator a;
  a.configure(4, VoiceMode::Poly, true);
  VoiceAction x = a.noteOn(60, 0.8f);
  CHECK(x.kind == VoiceAction::Kind::Trigger && x.voice >= 0 && x.cold);
  const int v0 = x.voice;
  VoiceAction y = a.noteOn(64, 0.8f);
  CHECK(y.voice != v0);
  CHECK(a.activeVoices() == 2);
  // Repeated note reuses its own voice instead of piling up.
  VoiceAction z = a.noteOn(60, 0.5f);
  CHECK(z.voice == v0);
  CHECK(a.activeVoices() == 2);
  VoiceAction off = a.noteOff(60);
  CHECK(off.kind == VoiceAction::Kind::Release && off.voice == v0);
  CHECK(!a.slot(v0).gate && a.slot(v0).active);  // releasing, still sounding
  a.voiceFinished(v0);
  CHECK(a.activeVoices() == 1);
}

TEST(voice_allocator_stealing_order) {
  VoiceAllocator a;
  a.configure(3, VoiceMode::Poly, true);
  const int v60 = a.noteOn(60, 1).voice;
  const int v62 = a.noteOn(62, 1).voice;
  const int v64 = a.noteOn(64, 1).voice;
  CHECK(v60 != v62 && v62 != v64 && v60 != v64);
  // All busy and held: the oldest held note is stolen.
  VoiceAction s = a.noteOn(65, 1);
  CHECK(s.kind == VoiceAction::Kind::Trigger && s.stolen && s.voice == v60);
  // Prefer a releasing voice over a held one.
  a.noteOff(64);
  VoiceAction s2 = a.noteOn(67, 1);
  CHECK(s2.voice == v64 && s2.stolen);
  // Never exceeds the voice count.
  for (int n = 40; n < 80; ++n) CHECK(a.noteOn(n, 1).voice < 3);
  CHECK(a.activeVoices() == 3);
}

TEST(voice_allocator_mono_legato_and_note_stack) {
  VoiceAllocator a;
  a.configure(1, VoiceMode::Mono, true);
  VoiceAction x = a.noteOn(60, 1);
  CHECK(x.kind == VoiceAction::Kind::Trigger && x.voice == 0);
  VoiceAction y = a.noteOn(64, 1);
  CHECK(y.kind == VoiceAction::Kind::Retarget && y.note == 64);  // legato: no retrigger
  VoiceAction r = a.noteOff(64);
  CHECK(r.kind == VoiceAction::Kind::Retarget && r.note == 60);  // falls back to the held note
  VoiceAction e = a.noteOff(60);
  CHECK(e.kind == VoiceAction::Kind::Release);
  VoiceAllocator b;
  b.configure(1, VoiceMode::Mono, false);
  b.noteOn(60, 1);
  CHECK(b.noteOn(64, 1).kind == VoiceAction::Kind::Trigger);  // legato off: retrigger
}
