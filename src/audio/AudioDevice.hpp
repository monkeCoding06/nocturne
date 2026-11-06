#pragma once
#include <memory>
#include <string>
#include <vector>

#include "audio/Engine.hpp"

namespace nocturne {

struct DeviceInfo {
  std::string name;
  int index = -1;  // position in the list returned by listDevices(); -1 = system default
  bool isDefault = false;
};

struct AudioConfig {
  int sampleRate = 48000;  // 44100 or 48000
  int bufferFrames = 256;
  int deviceIndex = -1;    // -1 = default device
};

// Owns the miniaudio playback device and pulls audio from an Engine.
class AudioDevice {
 public:
  explicit AudioDevice(Engine& engine);
  ~AudioDevice();
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  std::vector<DeviceInfo> listDevices();
  // Returns false and sets lastError() when the device cannot be started.
  bool start(const AudioConfig& cfg);
  void stop();
  bool running() const;
  const std::string& lastError() const { return error_; }
  const std::string& deviceName() const { return deviceName_; }
  int actualSampleRate() const { return sampleRate_; }
  int actualBufferFrames() const { return bufferFrames_; }
  std::string backendName() const { return backend_; }

  struct Impl;

 private:
  Engine& engine_;
  std::unique_ptr<Impl> impl_;
  std::string error_, deviceName_, backend_;
  int sampleRate_ = 0, bufferFrames_ = 0;
};

}  // namespace nocturne
