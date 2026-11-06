#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "app/Session.hpp"
#include "patch/PatchIO.hpp"
#include "patch/Presets.hpp"
#include "tui/Ui.hpp"

namespace {

int exportPresets(const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  for (const auto& p : nocturne::factoryPresets()) {
    std::ofstream out(dir / (p.id + ".json"), std::ios::binary);
    if (!out) {
      std::fprintf(stderr, "cannot write %s\n", (dir / (p.id + ".json")).string().c_str());
      return 1;
    }
    out << p.json;  // the exact embedded bytes of presets/<id>.json
  }
  return 0;
}

int checkPresets() {
  int bad = 0;
  for (const auto& p : nocturne::factoryPresets()) {
    for (const auto& d : p.load.diagnostics)
      if (d.severity != nocturne::Diagnostic::Severity::Info) {
        std::printf("%s: %s\n", p.id.c_str(), d.text().c_str());
        ++bad;
      }
  }
  const auto disk = nocturne::checkPresetsOnDisk();
  std::printf("%zu embedded presets; disk dir: %s\n", nocturne::factoryPresets().size(),
              disk.dir.empty() ? "(none found)" : disk.dir.string().c_str());
  for (const auto& m : disk.mismatches) std::printf("MISMATCH: %s\n", m.c_str());
  return bad == 0 && disk.mismatches.empty() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--export-presets") && i + 1 < argc) return exportPresets(argv[i + 1]);
    if (!std::strcmp(argv[i], "--check-presets")) return checkPresets();
    if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
      std::puts("nocturne - terminal modular synthesizer\n  nocturne                     start the instrument\n  nocturne --export-presets D  write the built-in factory patches as JSON into D\n  nocturne --check-presets     verify built-in presets (and compare with an on-disk presets/ dir)");
      return 0;
    }
  }

  nocturne::Session session;
  session.startup();
  nocturne::tui::Ui ui(session);

  auto screen = ftxui::ScreenInteractive::Fullscreen();
  auto component = ftxui::Renderer([&] { return ui.render(); });
  component = ftxui::CatchEvent(component, [&](ftxui::Event e) {
    const bool handled = ui.onEvent(e);
    session.player().setInputEnabled(!ui.textEntryActive());
    if (ui.wantsQuit()) screen.Exit();
    return handled;
  });

  // Repaint ~30 times a second for meters and the scope; audio never depends on it.
  std::atomic<bool> running{true};
  std::thread ticker([&] {
    while (running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
      screen.PostEvent(ftxui::Event::Custom);
    }
  });
  // LIVE note mode: observe real key up/down state (no-op in the other modes).
  std::thread keyPoller([&] {
    nocturne::beginFineTimer();
    while (running.load()) {
      session.player().pollKeys();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    nocturne::endFineTimer();
  });

  screen.Loop(component);
  running = false;
  ticker.join();
  keyPoller.join();
  session.player().allOff();
  session.shutdown();
  return 0;
}
