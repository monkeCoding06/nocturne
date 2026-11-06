#include "app/Session.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "patch/PatchIO.hpp"
#include "patch/Presets.hpp"

namespace nocturne {

namespace fs = std::filesystem;

fs::path configDirectory() {
#ifdef _WIN32
  if (const char* a = std::getenv("APPDATA")) return fs::path(a) / "nocturne";
#else
  if (const char* x = std::getenv("XDG_CONFIG_HOME")) return fs::path(x) / "nocturne";
  if (const char* h = std::getenv("HOME")) return fs::path(h) / ".config" / "nocturne";
#endif
  return fs::path(".nocturne");
}

Session::Session() : engine_(48000.0), device_(engine_), player_(engine_), patch_(makeEmptyPatch()) {}

Session::~Session() { shutdown(); }

fs::path Session::patchDirectory() const { return configDirectory() / "patches"; }

// ------------------------------------------------------------ lifecycle
void Session::startup() {
  loadConfig();
  player_.setOctave(cfg_.octave);
  player_.setVelocity(cfg_.velocity);
  player_.setHoldSeconds(cfg_.holdSeconds);
  player_.setMode(cfg_.noteMode);
  cfg_.noteMode = player_.mode();  // Live falls back to the previous mode when unavailable
  player_.setLayout(cfg_.keyLayout);
  engine_.setVolume(cfg_.volume);
  engine_.flushQueue();

  presetDir_.clear();
  presetMismatches_.clear();
  const PresetDiskCheck disk = checkPresetsOnDisk();
  if (!disk.dir.empty()) {
    presetDir_ = disk.dir.string();
    presetMismatches_ = disk.mismatches;
  }

  bool loaded = false;
  for (const auto& e : listPatches()) {
    // Older configs stored the display name ("factory:EURO HOOVER") instead of the id.
    if (e.key() == cfg_.lastPatch || (e.factory && "factory:" + e.name == cfg_.lastPatch)) {
      loaded = loadEntry(e);
      break;
    }
  }
  if (!loaded) {
    const std::string why = cfg_.lastPatch.empty() ? std::string() : "could not restore '" + cfg_.lastPatch + "': ";
    const auto& list = factoryPresets();
    if (!list.empty()) {
      const bool ok = loadEntry(listPatches().front());
      if (ok && !why.empty()) setStatus(why + "started with " + list.front().name, true);
    } else {
      PatchLoadResult r;
      r.add(Diagnostic::Severity::Error, "missing-asset", "factory presets", "no factory presets are embedded in this build");
      recordLoad(std::move(r), "factory presets");
    }
  }
  applyAudioSettings();
  if (!presetMismatches_.empty())
    setStatus("Preset files in " + presetDir_ + " differ from the built-in ones (embedded wins) - see F4", true);
}

void Session::shutdown() {
  if (device_.running() || !cfg_.lastPatch.empty()) saveConfig();
  device_.stop();
  engine_.collectGarbage();
}

void Session::tick() {
  engine_.collectGarbage();
  if (resync_) {
    resync_ = false;
    rebuild();
  }
}

// ------------------------------------------------------------ engine sync
void Session::post(const EngineEvent& e) {
  if (!engine_.post(e)) resync_ = true;
  if (!device_.running()) engine_.flushQueue();
}

void Session::rebuild() {
  std::string err;
  auto rack = Rack::build(patch_, engine_.sampleRate(), &err);
  if (!rack) {
    setStatus("Patch error: " + err, true);
    return;
  }
  if (!engine_.postSwap(std::move(rack))) resync_ = true;
  if (!device_.running()) engine_.flushQueue();
}

bool Session::adopt(Patch p, const std::string& label) {
  normalizePatch(p);
  // Build the rack first: if that fails, the current patch and rack stay untouched.
  std::string err;
  auto rack = Rack::build(p, engine_.sampleRate(), &err);
  if (!rack) {
    setStatus("Patch not loaded (current patch kept): " + err, true);
    return false;
  }
  patch_ = std::move(p);
  dirty_ = false;
  engine_.allNotesOff();
  if (!engine_.postSwap(std::move(rack))) resync_ = true;
  if (!device_.running()) engine_.flushQueue();
  if (!label.empty()) setStatus("Loaded " + label);
  return true;
}

void Session::recordLoad(PatchLoadResult r, const std::string& label) {
  lastLoad_ = std::move(r);
  lastLoadLabel_ = label;
}

// ------------------------------------------------------------ editing
void Session::setParam(int module, int param, float value) {
  if (module < 0 || module >= static_cast<int>(patch_.modules.size())) return;
  const ModuleSpec* spec = patch_.specOf(module);
  if (!spec || param < 0 || param >= static_cast<int>(spec->params.size())) return;
  const float v = spec->params[static_cast<size_t>(param)].clamp(value);
  patch_.modules[static_cast<size_t>(module)].params[static_cast<size_t>(param)] = v;
  dirty_ = true;
  EngineEvent e;
  e.type = EngineEvent::Type::SetParam;
  e.a = module; e.b = param; e.f = v;
  post(e);
}

void Session::adjustParam(int module, int param, int dir, bool fine) {
  const ModuleSpec* spec = patch_.specOf(module);
  if (!spec || param < 0 || param >= static_cast<int>(spec->params.size())) return;
  const float cur = patch_.modules[static_cast<size_t>(module)].params[static_cast<size_t>(param)];
  setParam(module, param, spec->params[static_cast<size_t>(param)].adjust(cur, dir, fine));
}

void Session::resetParam(int module, int param) {
  const ModuleSpec* spec = patch_.specOf(module);
  if (!spec || param < 0 || param >= static_cast<int>(spec->params.size())) return;
  setParam(module, param, spec->params[static_cast<size_t>(param)].def);
}

bool Session::parseParam(int module, int param, const std::string& text) {
  const ModuleSpec* spec = patch_.specOf(module);
  if (!spec || param < 0 || param >= static_cast<int>(spec->params.size())) return false;
  float v = 0.0f;
  if (!spec->params[static_cast<size_t>(param)].parse(text, v)) return false;
  setParam(module, param, v);
  return true;
}

std::string Session::addModule(const std::string& type) {
  std::string name;
  const std::string err = nocturne::addModule(patch_, type, &name);
  if (!err.empty()) return err;
  dirty_ = true;
  rebuild();
  setStatus("Added " + name);
  return {};
}

std::string Session::removeModule(int module) {
  if (module < 0 || module >= static_cast<int>(patch_.modules.size())) return "no such module";
  const std::string name = patch_.modules[static_cast<size_t>(module)].name;
  const std::string err = nocturne::removeModule(patch_, name);
  if (!err.empty()) return err;
  dirty_ = true;
  rebuild();
  setStatus("Removed " + name);
  return {};
}

std::string Session::connect(const std::string& from, const std::string& fromPort, const std::string& to,
                             const std::string& toPort) {
  const std::string err = nocturne::connect(patch_, from, fromPort, to, toPort, 1.0f);
  if (!err.empty()) return err;
  dirty_ = true;
  rebuild();
  setStatus("Connected " + from + "." + fromPort + " > " + to + "." + toPort);
  return {};
}

void Session::disconnect(size_t index) {
  if (index >= patch_.connections.size()) return;
  nocturne::disconnect(patch_, index);
  dirty_ = true;
  rebuild();
  setStatus("Disconnected");
}

void Session::setWireAmount(size_t index, float amount) {
  if (index >= patch_.connections.size()) return;
  amount = std::min(std::max(amount, -1.0f), 1.0f);
  patch_.connections[index].amount = amount;
  dirty_ = true;
  EngineEvent e;
  e.type = EngineEvent::Type::SetWireAmount;
  e.a = static_cast<int>(index);
  e.f = amount;
  post(e);
}

void Session::setGlobal(const GlobalSettings& g) {
  patch_.global = g;
  patch_.global.voices = std::min(std::max(patch_.global.voices, 1), kMaxVoices);
  patch_.global.glide = std::min(std::max(patch_.global.glide, 0.0f), 2.0f);
  dirty_ = true;
  rebuild();
}

void Session::setPatchName(const std::string& name) {
  patch_.meta.name = name;
  dirty_ = true;
}

// ------------------------------------------------------------ patches
std::vector<PatchEntry> Session::listPatches() const {
  std::vector<PatchEntry> out;
  for (const auto& f : factoryPresets()) {
    PatchEntry e;
    e.name = f.name;
    e.factory = true;
    e.id = f.id;
    out.push_back(e);
  }
  std::error_code ec;
  std::vector<PatchEntry> user;
  for (const auto& de : fs::directory_iterator(patchDirectory(), ec)) {
    if (!de.is_regular_file() || de.path().extension() != ".json") continue;
    PatchEntry e;
    e.name = de.path().stem().string();
    e.path = de.path();
    const std::string stem = sanitizePatchFileName(e.name);
    for (const auto& f : factoryPresets())
      if (stem == f.id || stem == sanitizePatchFileName(f.name)) e.shadowsFactory = true;
    user.push_back(e);
  }
  std::sort(user.begin(), user.end(), [](const PatchEntry& a, const PatchEntry& b) { return a.name < b.name; });
  out.insert(out.end(), user.begin(), user.end());
  return out;
}

bool Session::loadEntry(const PatchEntry& e) {
  PatchLoadResult res;
  PatchIdentity id;
  if (e.factory) {
    const PresetInfo* f = findFactoryPreset(e.id);
    if (!f) {
      res.add(Diagnostic::Severity::Error, "missing-asset", e.id, "factory preset '" + e.id + "' is not embedded in this build");
    } else {
      res = f->load;
      id = {PatchIdentity::Kind::Factory, f->id, f->source};
    }
  } else {
    res = loadPatchFile(e.path);
    id = {PatchIdentity::Kind::User, e.path.filename().string(), e.path.string()};
  }
  const std::string label = e.name + (e.factory ? " [FACTORY]" : " [USER]");
  Patch loaded;
  const bool ok = res.ok();
  if (ok) loaded = *res.patch;
  recordLoad(std::move(res), e.name);
  if (!ok) {
    setStatus("Cannot load " + e.name + " (current patch kept): " + lastLoad_.error +
                  (lastLoad_.count(Diagnostic::Severity::Error) > 1 ? " (+ more, see F4)" : ""),
              true);
    return false;
  }
  if (!adopt(std::move(loaded), label)) return false;
  identity_ = id;
  const size_t warn = lastLoad_.count(Diagnostic::Severity::Warning);
  if (warn > 0)
    setStatus("Loaded " + label + " with " + std::to_string(warn) + " warning(s) - see F4 diagnostics", true);
  cfg_.lastPatch = e.key();
  return true;
}

void Session::newPatch() {
  adopt(makeEmptyPatch(), "");
  patch_.meta.name = "untitled";
  identity_ = {};
  recordLoad({}, "");
  setStatus("New empty patch: press A to add modules");
}

std::string Session::exportCurrent(fs::path* written) {
  const fs::path dir = configDirectory() / "exports";
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path path = dir / (sanitizePatchFileName(patch_.meta.name) + ".json");
  const std::string err = savePatchFile(patch_, path);
  if (!err.empty()) return err;
  if (written) *written = path;
  return {};
}

std::string Session::saveCurrent(const std::string& name) {
  const std::string stem = sanitizePatchFileName(name);
  if (stem.empty()) return "invalid name";
  std::error_code ec;
  fs::create_directories(patchDirectory(), ec);
  Patch p = patch_;
  p.meta.name = name;
  const fs::path path = patchDirectory() / (stem + ".json");
  const std::string err = savePatchFile(p, path);
  if (!err.empty()) return err;
  patch_.meta.name = name;
  dirty_ = false;
  identity_ = {PatchIdentity::Kind::User, path.filename().string(), path.string()};
  PatchEntry e;
  e.path = path;
  cfg_.lastPatch = e.key();
  setStatus("Saved " + path.string());
  return {};
}

bool Session::deleteEntry(const PatchEntry& e) {
  if (e.factory) return false;
  std::error_code ec;
  return fs::remove(e.path, ec);
}

// ------------------------------------------------------------ audio
bool Session::applyAudioSettings() {
  AudioConfig ac;
  ac.sampleRate = cfg_.sampleRate;
  ac.bufferFrames = cfg_.bufferFrames;
  if (!cfg_.deviceName.empty()) {
    for (const auto& d : device_.listDevices())
      if (d.name == cfg_.deviceName) ac.deviceIndex = d.index;
  }
  const bool ok = device_.start(ac);
  rebuild();  // the rack depends on the sample rate
  if (ok)
    setStatus("Audio: " + device_.deviceName() + " @ " + std::to_string(device_.actualSampleRate()) + " Hz (" +
              device_.backendName() + ")");
  else
    setStatus(device_.lastError() + " - running silent, see SETTINGS", true);
  return ok;
}

void Session::setVolume(float v) {
  cfg_.volume = std::min(std::max(v, 0.0f), 1.0f);
  engine_.setVolume(cfg_.volume);
}

void Session::applyAndSaveSettings() {
  const bool ok = applyAudioSettings();
  saveConfig();
  if (ok) setStatus("Settings applied and saved - " + status_);
  else setStatus("Settings saved, but audio failed: " + device_.lastError(), true);
}

// ------------------------------------------------------------ config
void Session::loadConfig() {
  std::ifstream f(configDirectory() / "config.json");
  if (!f) return;
  try {
    const auto j = nlohmann::json::parse(f, nullptr, true, true);
    cfg_.sampleRate = j.value("sampleRate", cfg_.sampleRate) == 44100 ? 44100 : 48000;
    cfg_.bufferFrames = std::min(std::max(j.value("bufferFrames", cfg_.bufferFrames), 64), 4096);
    cfg_.deviceName = j.value("device", cfg_.deviceName);
    cfg_.volume = std::min(std::max(j.value("volume", cfg_.volume), 0.0f), 1.0f);
    {
      const std::string nm = j.value("noteMode", std::string("timed"));
      cfg_.noteMode = nm == "latch" ? NoteMode::Latch : (nm == "live" ? NoteMode::Live : NoteMode::Timed);
    }
    cfg_.holdSeconds = std::min(std::max(j.value("holdSeconds", cfg_.holdSeconds), 0.05f), 8.0f);
    {
      const std::string kl = j.value("keyLayout", std::string("qwerty"));
      cfg_.keyLayout = kl == "qwertz" ? KeyLayout::Qwertz : (kl == "azerty" ? KeyLayout::Azerty : KeyLayout::Qwerty);
    }
    cfg_.velocity = std::min(std::max(j.value("velocity", cfg_.velocity), 0.05f), 1.0f);
    cfg_.octave = std::min(std::max(j.value("octave", cfg_.octave), -1), 8);
    cfg_.lastPatch = j.value("lastPatch", cfg_.lastPatch);
  } catch (...) {
    // A damaged config file must never prevent startup; defaults apply.
  }
}

void Session::saveConfig() const {
  std::error_code ec;
  fs::create_directories(configDirectory(), ec);
  nlohmann::ordered_json j;
  j["sampleRate"] = cfg_.sampleRate;
  j["bufferFrames"] = cfg_.bufferFrames;
  j["device"] = cfg_.deviceName;
  j["volume"] = cfg_.volume;
  j["noteMode"] = cfg_.noteMode == NoteMode::Latch ? "latch" : (cfg_.noteMode == NoteMode::Live ? "live" : "timed");
  j["holdSeconds"] = player_.holdSeconds();
  j["keyLayout"] = cfg_.keyLayout == KeyLayout::Qwertz ? "qwertz" : (cfg_.keyLayout == KeyLayout::Azerty ? "azerty" : "qwerty");
  j["velocity"] = player_.velocity();
  j["octave"] = player_.octave();
  j["lastPatch"] = cfg_.lastPatch;
  std::ofstream f(configDirectory() / "config.json");
  if (f) f << j.dump(2) << "\n";
}

}  // namespace nocturne
