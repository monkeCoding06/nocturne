#include "audio/AudioDevice.hpp"

#include <cstring>

#include "miniaudio.h"

namespace nocturne {

struct AudioDevice::Impl {
  ma_context context{};
  bool contextInit = false;
  ma_device device{};
  bool deviceInit = false;
  std::vector<ma_device_id> ids;
};

static void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
  auto* engine = static_cast<Engine*>(dev->pUserData);
  engine->render(static_cast<float*>(out), static_cast<int>(frames));
}

AudioDevice::AudioDevice(Engine& engine) : engine_(engine), impl_(new Impl) {}

AudioDevice::~AudioDevice() {
  stop();
  if (impl_->contextInit) ma_context_uninit(&impl_->context);
}

static bool ensureContext(AudioDevice::Impl& i, std::string& err) {
  if (i.contextInit) return true;
  if (ma_context_init(nullptr, 0, nullptr, &i.context) != MA_SUCCESS) {
    err = "No audio backend available";
    return false;
  }
  i.contextInit = true;
  return true;
}

std::vector<DeviceInfo> AudioDevice::listDevices() {
  std::vector<DeviceInfo> out;
  if (!ensureContext(*impl_, error_)) return out;
  ma_device_info* infos = nullptr;
  ma_uint32 count = 0;
  if (ma_context_get_devices(&impl_->context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) return out;
  impl_->ids.clear();
  for (ma_uint32 k = 0; k < count; ++k) {
    impl_->ids.push_back(infos[k].id);
    DeviceInfo d;
    d.name = infos[k].name;
    d.index = static_cast<int>(k);
    d.isDefault = infos[k].isDefault != 0;
    out.push_back(d);
  }
  return out;
}

bool AudioDevice::start(const AudioConfig& cfg) {
  stop();
  error_.clear();
  if (!ensureContext(*impl_, error_)) return false;
  backend_ = ma_get_backend_name(impl_->context.backend);

  ma_device_config dc = ma_device_config_init(ma_device_type_playback);
  dc.playback.format = ma_format_f32;
  dc.playback.channels = 2;
  dc.sampleRate = static_cast<ma_uint32>(cfg.sampleRate);
  dc.periodSizeInFrames = static_cast<ma_uint32>(cfg.bufferFrames);
  dc.periods = 3;
  dc.performanceProfile = ma_performance_profile_low_latency;
  dc.dataCallback = dataCallback;
  dc.pUserData = &engine_;

  if (cfg.deviceIndex >= 0) {
    listDevices();
    if (cfg.deviceIndex < static_cast<int>(impl_->ids.size()))
      dc.playback.pDeviceID = &impl_->ids[static_cast<size_t>(cfg.deviceIndex)];
  }

  // Render at the rate the device actually runs at.
  engine_.setSampleRate(cfg.sampleRate);
  ma_result r = ma_device_init(&impl_->context, &dc, &impl_->device);
  if (r != MA_SUCCESS) {
    error_ = std::string("Could not open audio device: ") + ma_result_description(r);
    return false;
  }
  impl_->deviceInit = true;
  sampleRate_ = static_cast<int>(impl_->device.sampleRate);
  bufferFrames_ = static_cast<int>(impl_->device.playback.internalPeriodSizeInFrames);
  deviceName_ = impl_->device.playback.name;
  engine_.setSampleRate(sampleRate_);

  r = ma_device_start(&impl_->device);
  if (r != MA_SUCCESS) {
    error_ = std::string("Could not start audio device: ") + ma_result_description(r);
    ma_device_uninit(&impl_->device);
    impl_->deviceInit = false;
    return false;
  }
  return true;
}

void AudioDevice::stop() {
  if (impl_->deviceInit) {
    ma_device_uninit(&impl_->device);  // blocks until the callback has finished
    impl_->deviceInit = false;
  }
}

bool AudioDevice::running() const {
  return impl_->deviceInit && ma_device_is_started(&impl_->device);
}

}  // namespace nocturne
