#include <algorithm>
#include <cstdio>

#include "tui/Ui.hpp"
#include "tui/Widgets.hpp"

namespace nocturne::tui {

using namespace ftxui;

namespace {
constexpr int kBuffers[] = {64, 128, 256, 512, 1024, 2048};
constexpr int kRates[] = {44100, 48000};
constexpr int kItems = 11;

std::string fmt(const char* f, double v) {
  char b[48];
  std::snprintf(b, sizeof b, f, v);
  return b;
}
}  // namespace

Element Ui::renderSettings(int width, int height) {
  (void)width;
  (void)height;
  const AppConfig& c = s_.config();
  const GlobalSettings& g = s_.patch().global;
  std::string dev = c.deviceName.empty() ? "System default" : c.deviceName;
  struct Item { const char* group; const char* label; std::string value; };
  const Item items[kItems] = {
      {"AUDIO", "DEVICE", dev},
      {"AUDIO", "SAMPLE RATE", std::to_string(c.sampleRate) + " Hz"},
      {"AUDIO", "BUFFER", std::to_string(c.bufferFrames) + " frames (" + fmt("%.1f", 1000.0 * c.bufferFrames / c.sampleRate) + " ms)"},
      {"AUDIO", "VOLUME", fmt("%.0f%%", c.volume * 100.0)},
      {"PATCH", "VOICE MODE", g.mode == VoiceMode::Poly ? "POLY" : "MONO"},
      {"PATCH", "VOICES", std::to_string(g.voices)},
      {"PATCH", "GLIDE", fmt("%.2f s", g.glide)},
      {"PATCH", "LEGATO", g.legato ? "ON" : "OFF"},
      {"PLAY", "NOTE MODE", c.noteMode == NoteMode::Latch ? "LATCH" : "TIMED"},
      {"PLAY", "NOTE LENGTH", fmt("%.2f s", s_.player().holdSeconds())},
      {"PLAY", "VELOCITY", fmt("%.2f", s_.player().velocity())},
  };
  Elements rows;
  const char* last = "";
  for (int i = 0; i < kItems; ++i) {
    if (std::string(last) != items[i].group) {
      last = items[i].group;
      rows.push_back(text(std::string(" ") + last) | bold | color(theme::purple));
    }
    const bool cur = i == setSel_;
    Element row = hbox({text(cur ? " ▸ " : "   "), text(fixedText(items[i].label, 14)) | color(theme::dim),
                        text("◀ ") | color(theme::border), text(items[i].value) | color(theme::amber), text(" ▶") | color(theme::border)});
    if (cur) row = row | inverted;
    rows.push_back(row);
  }

  Engine& en = s_.engine();
  AudioDevice& d = s_.device();
  Elements st;
  st.push_back(text(" STATUS") | bold | color(theme::purple));
  st.push_back(hbox({text(" stream   ") | color(theme::dim), d.running() ? text("running") | color(theme::green) : text("STOPPED") | color(theme::red)}));
  st.push_back(hbox({text(" backend  ") | color(theme::dim), text(d.running() ? d.backendName() : "-")}));
  st.push_back(hbox({text(" device   ") | color(theme::dim), text(d.running() ? d.deviceName() : "-")}));
  if (d.running()) {
    st.push_back(hbox({text(" rate     ") | color(theme::dim), text(std::to_string(d.actualSampleRate()) + " Hz")}));
    st.push_back(hbox({text(" latency  ") | color(theme::dim),
                       text("~" + fmt("%.1f", 1000.0 * d.actualBufferFrames() * 3 / std::max(1, d.actualSampleRate())) + " ms (3 periods)")}));
  }
  st.push_back(hbox({text(" cpu      ") | color(theme::dim), text(fmt("%.0f%%", en.cpuLoad() * 100.0))}));
  st.push_back(hbox({text(" clips    ") | color(theme::dim), text(std::to_string(en.clipCount()))}));
  if (!d.running() && !d.lastError().empty()) {
    st.push_back(text(" "));
    st.push_back(paragraph(" " + d.lastError()) | color(theme::red));
    st.push_back(text(" [Enter] on DEVICE to retry") | color(theme::dim));
  }
  st.push_back(text(" "));
  st.push_back(text(" devices") | bold | color(theme::purple));
  for (const DeviceInfo& di : devices_)
    st.push_back(text("  " + std::string(di.isDefault ? "* " : "  ") + di.name) | color(theme::dim));
  st.push_back(text(" "));
  st.push_back(text(" config: " + configDirectory().string()) | color(theme::dim));

  return hbox({window(text(" SETTINGS ") | color(theme::purple), vbox(std::move(rows)) | flex) | color(theme::border) | size(WIDTH, EQUAL, 56),
               window(text(" AUDIO ENGINE ") | color(theme::purple), vbox(std::move(st)) | flex) | color(theme::border) | flex});
}

void Ui::adjustSetting(int dir, bool fine) {
  AppConfig& c = s_.config();
  GlobalSettings g = s_.patch().global;
  auto cycle = [&](const int* arr, int n, int cur) {
    int idx = 0;
    for (int i = 0; i < n; ++i) if (arr[i] == cur) idx = i;
    return arr[std::min(std::max(idx + dir, 0), n - 1)];
  };
  switch (setSel_) {
    case 0: {
      if (devices_.empty()) refreshDevices();
      // index 0 = system default, then each enumerated device
      int cur = 0;
      for (size_t i = 0; i < devices_.size(); ++i) if (devices_[i].name == c.deviceName) cur = static_cast<int>(i) + 1;
      const int total = static_cast<int>(devices_.size()) + 1;
      cur = (cur + dir + total) % total;
      c.deviceName = cur == 0 ? "" : devices_[static_cast<size_t>(cur - 1)].name;
      s_.applyAudioSettings();
      break;
    }
    case 1: c.sampleRate = cycle(kRates, 2, c.sampleRate); s_.applyAudioSettings(); break;
    case 2: c.bufferFrames = cycle(kBuffers, 6, c.bufferFrames); s_.applyAudioSettings(); break;
    case 3: s_.setVolume(c.volume + static_cast<float>(dir) * (fine ? 0.01f : 0.05f)); break;
    case 4: g.mode = g.mode == VoiceMode::Poly ? VoiceMode::Mono : VoiceMode::Poly; s_.setGlobal(g); break;
    case 5: g.voices = std::min(kMaxVoices, std::max(1, g.voices + dir)); s_.setGlobal(g); break;
    case 6: g.glide = std::min(2.0f, std::max(0.0f, g.glide + static_cast<float>(dir) * (fine ? 0.01f : 0.05f))); s_.setGlobal(g); break;
    case 7: g.legato = !g.legato; s_.setGlobal(g); break;
    case 8:
      c.noteMode = c.noteMode == NoteMode::Timed ? NoteMode::Latch : NoteMode::Timed;
      s_.player().setMode(c.noteMode);
      break;
    case 9:
      s_.player().setHoldSeconds(s_.player().holdSeconds() + static_cast<float>(dir) * (fine ? 0.05f : 0.25f));
      c.holdSeconds = s_.player().holdSeconds();
      break;
    case 10:
      s_.player().setVelocity(s_.player().velocity() + static_cast<float>(dir) * (fine ? 0.01f : 0.05f));
      c.velocity = s_.player().velocity();
      break;
    default: break;
  }
}

void Ui::activateSetting() {
  if (setSel_ <= 2) {
    refreshDevices();
    s_.applyAudioSettings();
  } else {
    adjustSetting(1, false);
  }
}

bool Ui::handleSettings(const Event& e) {
  if (e == Event::ArrowUp) { setSel_ = (setSel_ + kItems - 1) % kItems; return true; }
  if (e == Event::ArrowDown || e == Event::Tab) { setSel_ = (setSel_ + 1) % kItems; return true; }
  if (e == Event::ArrowLeft) { adjustSetting(-1, false); return true; }
  if (e == Event::ArrowRight) { adjustSetting(1, false); return true; }
  if (e == Event::ArrowLeftCtrl) { adjustSetting(-1, true); return true; }
  if (e == Event::ArrowRightCtrl) { adjustSetting(1, true); return true; }
  if (e == Event::Return) { activateSetting(); return true; }
  if (e.is_character()) {
    if (e.character() == "-") { adjustSetting(-1, true); return true; }
    if (e.character() == "=" || e.character() == "+") { adjustSetting(1, true); return true; }
  }
  return false;
}

}  // namespace nocturne::tui
