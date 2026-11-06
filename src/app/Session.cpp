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
  engine_.setVolume(cfg_.volume);
  engine_.flushQueue();

  bool loaded = false;
  for (const auto& e : listPatches())
    if (e.key() == cfg_.lastPatch) { loaded = loadEntry(e); break; }
  if (!loaded) adopt(factoryPresets().front().build(), "INIT");
  applyAudioSettings();
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

void Session::adopt(Patch p, const std::string& label) {
  normalizePatch(p);
  patch_ = std::move(p);
  dirty_ = false;
  engine_.allNotesOff();
  rebuild();
  if (!label.empty()) setStatus("Loaded " + label);
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
  int i = 0;
  for (const auto& f : factoryPresets()) {
    PatchEntry e;
    e.name = f.name;
    e.factory = true;
    e.factoryIndex = i++;
    out.push_back(e);
  }
  std::error_code ec;
  std::vector<PatchEntry> user;
  for (const auto& de : fs::directory_iterator(patchDirectory(), ec)) {
    if (!de.is_regular_file() || de.path().extension() != ".json") continue;
    PatchEntry e;
    e.name = de.path().stem().string();
    e.path = de.path();
    user.push_back(e);
  }
  std::sort(user.begin(), user.end(), [](const PatchEntry& a, const PatchEntry& b) { return a.name < b.name; });
  out.insert(out.end(), user.begin(), user.end());
  return out;
}

bool Session::loadEntry(const PatchEntry& e) {
  if (e.factory) {
    if (e.factoryIndex < 0 || e.factoryIndex >= static_cast<int>(factoryPresets().size())) return false;
    adopt(factoryPresets()[static_cast<size_t>(e.factoryIndex)].build(), e.name);
    cfg_.lastPatch = e.key();
    return true;
  }
  auto res = loadPatchFile(e.path);
  if (!res.patch) {
    setStatus("Cannot load " + e.name + ": " + res.error, true);
    return false;
  }
  adopt(std::move(*res.patch), e.name);
  if (!res.warnings.empty()) setStatus("Loaded " + e.name + " (" + res.warnings.front() + ")");
  cfg_.lastPatch = e.key();
  return true;
}

void Session::newPatch() {
  adopt(makeEmptyPatch(), "");
  patch_.meta.name = "untitled";
  setStatus("New empty patch: press A to add modules");
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
    cfg_.noteMode = j.value("noteMode", std::string("timed")) == "latch" ? NoteMode::Latch : NoteMode::Timed;
    cfg_.holdSeconds = std::min(std::max(j.value("holdSeconds", cfg_.holdSeconds), 0.05f), 8.0f);
    cfg_.velocity = std::min(std::max(j.value("velocity", cfg_.velocity), 0.05f), 1.0f);
    cfg_.octave = std::min(std::max(j.value("octave", cfg_.octave), 0), 8);
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
  j["noteMode"] = cfg_.noteMode == NoteMode::Latch ? "latch" : "timed";
  j["holdSeconds"] = player_.holdSeconds();
  j["velocity"] = player_.velocity();
  j["octave"] = player_.octave();
  j["lastPatch"] = cfg_.lastPatch;
  std::ofstream f(configDirectory() / "config.json");
  if (f) f << j.dump(2) << "\n";
}

}  // namespace nocturne
