#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "TestHarness.hpp"
#include "patch/PatchIO.hpp"
#include "patch/Presets.hpp"
#ifdef NOCTURNE_HAS_SESSION
#include "app/Session.hpp"
#endif

using namespace nocturne;
namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  std::string s = ss.str();
  s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
  return s;
}

fs::path presetsDir() { return fs::path(NOCTURNE_SOURCE_DIR) / "presets"; }

float param(const Patch& p, const char* module, const char* id) {
  const int mi = p.findModule(module);
  if (mi < 0) return -9999.0f;
  const ModuleSpec* spec = p.specOf(mi);
  const int pi = spec->paramIndex(id);
  return pi < 0 ? -9999.0f : p.modules[static_cast<size_t>(mi)].params[static_cast<size_t>(pi)];
}

bool hasCode(const PatchLoadResult& r, const std::string& code) {
  return std::any_of(r.diagnostics.begin(), r.diagnostics.end(), [&](const Diagnostic& d) { return d.code == code; });
}

}  // namespace

TEST(presets_embedded_match_canonical_json_files) {
  const PresetInfo* euro = findFactoryPreset("euro_hoover");
  CHECK(euro != nullptr);
  if (euro) CHECK(euro->json.size() > 100 && readFile(presetsDir() / "euro_hoover.json") == [&] {
    std::string s = euro->json;
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
  }());
  // Every presets/*.json is embedded and byte-identical; nothing extra is embedded.
  size_t files = 0;
  for (const auto& de : fs::directory_iterator(presetsDir())) {
    if (de.path().extension() != ".json") continue;
    ++files;
    const PresetInfo* p = findFactoryPreset(de.path().stem().string());
    CHECK(p != nullptr);
    if (p) CHECK(readFile(de.path()) == [&] {
      std::string s = p->json;
      s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
      return s;
    }());
  }
  CHECK(files == factoryPresets().size());
  CHECK(findFactoryPreset("euro_hoover_v1") != nullptr);
  CHECK(factoryPresets().front().id == "init");
}

TEST(presets_all_factory_presets_load_cleanly) {
  for (const auto& p : factoryPresets()) {
    CHECK(p.load.ok());
    CHECK(p.load.count(Diagnostic::Severity::Warning) == 0);
    CHECK(p.load.count(Diagnostic::Severity::Error) == 0);
  }
}

TEST(presets_euro_hoover_has_current_oscillators_and_params) {
  const Patch p = findFactoryPreset("euro_hoover")->build();
  CHECK(p.meta.name == "EURO HOOVER");
  const ModuleSpec* osc = findSpec("osc");
  const int wave = osc->paramIndex("wave");
  CHECK(osc->params[static_cast<size_t>(wave)].choices[6] == "SAWPWM");
  CHECK(param(p, "OSC1", "wave") == 6.0f);
  CHECK(param(p, "OSC2", "wave") == 6.0f);
  CHECK(param(p, "OSC3", "wave") == 4.0f);  // PULSE
  CHECK_NEAR(param(p, "OSC1", "range"), 24.0, 1e-6);
  CHECK(p.findModule("ADSR3") >= 0);
  // The superseded patch is a different, still available preset.
  const Patch v1 = findFactoryPreset("euro_hoover_v1")->build();
  CHECK(v1.meta.name == "EURO HOOVER V1");
  CHECK(param(v1, "OSC1", "wave") == 2.0f);  // SAW
}

TEST(presets_euro_hoover_connections_retained) {
  const PresetInfo* info = findFactoryPreset("euro_hoover");
  const auto j = nlohmann::json::parse(info->json);
  const Patch p = info->build();
  CHECK(p.connections.size() == j["connections"].size());
  for (const auto& c : j["connections"]) {
    const std::string from = c["from"], to = c["to"];
    const bool found = std::any_of(p.connections.begin(), p.connections.end(), [&](const Connection& k) {
      return k.from + "." + k.fromPort == from && k.to + "." + k.toPort == to &&
             std::fabs(k.amount - c.value("amount", 1.0f)) < 1e-4;
    });
    CHECK(found);
  }
}

TEST(presets_unsupported_content_is_reported_not_silent) {
  const auto r = patchFromString(R"({"format":"nocturne-patch","version":1,
    "modules":[{"name":"MASTER","type":"master"},
               {"name":"OSC1","type":"osc","params":{"level":99,"wave":"banana","bogus":1,"fine":"x"}}],
    "connections":[{"from":"OSC1.OUT","to":"MASTER.IN"},{"from":"OSC1.OUT","to":"MASTER.NOPE"},
                   {"from":"GHOST.OUT","to":"MASTER.IN"},{"from":"nodot","to":"MASTER.IN"},
                   {"from":"OSC1.OUT","to":"MASTER.IN","amount":5}]})");
  CHECK(r.ok());
  CHECK(hasCode(r, "unknown-param"));
  CHECK(hasCode(r, "param-clamped"));
  CHECK(hasCode(r, "bad-value"));
  CHECK(hasCode(r, "dropped-connection"));
  CHECK(hasCode(r, "malformed-connection"));
  CHECK(r.warnings.size() >= 6);  // all findings, not just the first
  CHECK(r.patch && r.patch->connections.size() == 1);
}

TEST(presets_fatal_patches_report_every_error_and_yield_no_patch) {
  const auto r = patchFromString(R"({"format":"nocturne-patch","version":1,
    "modules":[{"name":"X1","type":"warp"},{"name":"Y1","type":"flux"}]})");
  CHECK(!r.ok());
  CHECK(hasCode(r, "unknown-module-type"));
  CHECK(hasCode(r, "no-master"));
  CHECK(r.count(Diagnostic::Severity::Error) == 3);
  CHECK(!r.error.empty());
  CHECK(hasCode(patchFromString("{oops"), "malformed-json"));
  CHECK(hasCode(patchFromString(R"({"format":"nocturne-patch","version":2,"modules":[]})"), "unsupported-version"));
  CHECK(hasCode(patchFromString(R"({"format":"x"})"), "bad-format"));
  CHECK(hasCode(loadPatchFile(fs::path(NOCTURNE_SOURCE_DIR) / "no_such_file.json"), "missing-file"));
}

TEST(presets_corrupt_factory_asset_throws_instead_of_falling_back) {
  PresetInfo bad;
  bad.id = "broken";
  bad.json = "{not json";
  bad.load = patchFromString(bad.json);
  bool threw = false;
  try { bad.build(); } catch (const std::runtime_error& e) { threw = std::string(e.what()).find("broken") != std::string::npos; }
  CHECK(threw);
}

TEST(presets_older_patch_without_newer_params_still_loads) {
  const PresetInfo* v1 = findFactoryPreset("euro_hoover_v1");
  CHECK(v1 && v1->load.ok());
  if (!v1) return;
  // Older files predate the OSC `range` parameter: strip it from the V1 patch to simulate one.
  auto j = nlohmann::ordered_json::parse(v1->json);
  for (auto& m : j["modules"])
    if (m["type"] == "osc") m["params"].erase("range");
  const auto old = patchFromJson(j);
  CHECK(old.ok());
  if (!old.ok()) return;
  CHECK(hasCode(old, "param-default"));  // visible as a note, not an error
  CHECK(old.count(Diagnostic::Severity::Warning) == 0);
  CHECK_NEAR(param(*old.patch, "OSC1", "range"), 12.0, 1e-6);
  CHECK(param(*old.patch, "OSC1", "wave") == param(*v1->load.patch, "OSC1", "wave"));
  // A minimal old file with no version field also loads.
  const auto r = patchFromString(R"({"format":"nocturne-patch","modules":[{"name":"MASTER","type":"master"}]})");
  CHECK(r.ok());
}

TEST(presets_export_round_trip_preserves_effective_config) {
  for (const auto& info : factoryPresets()) {
    const Patch a = info.build();
    const std::string exported = patchToString(a);
    const auto r = patchFromString(exported);
    CHECK(r.ok());
    if (!r.ok()) continue;
    CHECK(r.count(Diagnostic::Severity::Warning) == 0);
    CHECK(patchToString(*r.patch) == exported);
  }
}

TEST(presets_work_from_any_working_directory) {
  const fs::path old = fs::current_path();
  const fs::path elsewhere = fs::temp_directory_path();
  fs::current_path(elsewhere);
  const PresetInfo* euro = findFactoryPreset("euro_hoover");
  CHECK(euro != nullptr && euro->load.ok());
  if (euro) CHECK(param(euro->build(), "OSC1", "wave") == 6.0f);
  CHECK(!factoryPresets().empty());
  fs::current_path(old);
}

#ifdef NOCTURNE_HAS_SESSION
TEST(presets_fatal_load_keeps_current_rack_and_patch) {
  const fs::path dir = fs::temp_directory_path() / "nocturne_test_bad_patch";
  fs::create_directories(dir);
  const fs::path bad = dir / "bad.json";
  { std::ofstream(bad) << R"({"format":"nocturne-patch","version":1,"modules":[{"name":"X","type":"warp"}]})"; }
  const fs::path good = dir / "good.json";
  { std::ofstream(good) << R"({"format":"nocturne-patch","version":1,"meta":{"name":"GOODONE"},
     "modules":[{"name":"MASTER","type":"master"}],"connections":[{"from":"A.OUT","to":"MASTER.IN"}]})"; }

  Session s;
  PatchEntry factory;
  factory.factory = true;
  factory.id = "euro_hoover";
  factory.name = "EURO HOOVER";
  CHECK(s.loadEntry(factory));
  CHECK(s.identity().kind == PatchIdentity::Kind::Factory);
  CHECK(s.identity().source.find("embedded:euro_hoover") == 0);
  const std::string before = s.currentJson();

  PatchEntry badEntry;
  badEntry.name = "bad";
  badEntry.path = bad;
  CHECK(!s.loadEntry(badEntry));
  CHECK(s.currentJson() == before);
  CHECK(s.identity().id == "euro_hoover");
  CHECK(!s.lastLoad().ok() && s.lastLoad().count(Diagnostic::Severity::Error) >= 1);

  PatchEntry missing;
  missing.name = "missing";
  missing.path = dir / "missing.json";
  CHECK(!s.loadEntry(missing));
  CHECK(s.currentJson() == before);

  PatchEntry goodEntry;
  goodEntry.name = "good";
  goodEntry.path = good;
  CHECK(s.loadEntry(goodEntry));  // loads, with a visible dropped-connection warning
  CHECK(s.patch().meta.name == "GOODONE");
  CHECK(s.identity().kind == PatchIdentity::Kind::User);
  CHECK(s.lastLoad().count(Diagnostic::Severity::Warning) == 1);

  // Edits mark the patch modified; the exported JSON is what is in the rack.
  s.setParam(0, 0, s.patch().modules[0].params[0] * 0.5f);
  CHECK(s.dirty());
  CHECK(s.currentJson() == patchToString(s.patch()));
  fs::remove_all(dir);
}
#endif
