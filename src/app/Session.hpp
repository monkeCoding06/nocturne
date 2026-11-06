#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "audio/AudioDevice.hpp"
#include "audio/Engine.hpp"
#include "input/NotePlayer.hpp"
#include "patch/Patch.hpp"

namespace nocturne {

struct AppConfig {
  int sampleRate = 48000;
  int bufferFrames = 256;
  std::string deviceName;  // empty = system default
  float volume = 0.8f;
  NoteMode noteMode = NoteMode::Timed;
  float holdSeconds = 0.5f;
  float velocity = 0.8f;
  int octave = 4;
  std::string lastPatch;   // patch list key, see PatchEntry::key
};

struct PatchEntry {
  std::string name;
  bool factory = false;
  int factoryIndex = -1;
  std::filesystem::path path;
  std::string key() const { return factory ? "factory:" + name : "user:" + path.filename().string(); }
};

std::filesystem::path configDirectory();

// The application model: owns the patch being edited, the engine and the audio
// device, and keeps the engine in sync with every edit. UI-thread only.
class Session {
 public:
  Session();
  ~Session();

  Engine& engine() { return engine_; }
  AudioDevice& device() { return device_; }
  NotePlayer& player() { return player_; }
  const Patch& patch() const { return patch_; }
  AppConfig& config() { return cfg_; }
  bool dirty() const { return dirty_; }

  // ---- lifecycle ----
  void startup();     // load config, patch, open the audio device
  void shutdown();    // save config, stop audio
  void tick();        // call regularly from the UI thread (garbage, resync)

  // ---- status line ----
  const std::string& status() const { return status_; }
  bool statusIsError() const { return statusError_; }
  void setStatus(const std::string& s, bool error = false) { status_ = s; statusError_ = error; }

  // ---- patch editing ----
  void setParam(int module, int param, float value);
  void adjustParam(int module, int param, int dir, bool fine);
  void resetParam(int module, int param);
  bool parseParam(int module, int param, const std::string& text);
  std::string addModule(const std::string& type);
  std::string removeModule(int module);
  std::string connect(const std::string& from, const std::string& fromPort, const std::string& to,
                      const std::string& toPort);
  void disconnect(size_t index);
  void setWireAmount(size_t index, float amount);
  void setGlobal(const GlobalSettings& g);
  void setPatchName(const std::string& name);

  // ---- patches ----
  std::vector<PatchEntry> listPatches() const;
  bool loadEntry(const PatchEntry& e);
  void newPatch();
  std::string saveCurrent(const std::string& name);  // user patch dir; returns error or empty
  bool deleteEntry(const PatchEntry& e);
  std::filesystem::path patchDirectory() const;

  // ---- audio ----
  bool applyAudioSettings();  // (re)starts the device with cfg_
  void setVolume(float v);
  std::vector<DeviceInfo> devices() { return device_.listDevices(); }

 private:
  void rebuild();
  void post(const EngineEvent& e);
  void loadConfig();
  void saveConfig() const;
  void adopt(Patch p, const std::string& label);

  Engine engine_;
  AudioDevice device_;
  NotePlayer player_;
  Patch patch_;
  AppConfig cfg_;
  bool dirty_ = false;
  bool resync_ = false;
  std::string status_;
  bool statusError_ = false;
};

}  // namespace nocturne
