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
    std::ofstream out(dir / (p.id + ".json"));
    if (!out) {
      std::fprintf(stderr, "cannot write %s\n", (dir / (p.id + ".json")).string().c_str());
      return 1;
    }
    out << nocturne::patchToString(p.build());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--export-presets") && i + 1 < argc) return exportPresets(argv[i + 1]);
    if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
      std::puts("nocturne - terminal modular synthesizer\n  nocturne                     start the instrument\n  nocturne --export-presets D  write factory patches as JSON into D");
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

  screen.Loop(component);
  running = false;
  ticker.join();
  session.shutdown();
  return 0;
}
