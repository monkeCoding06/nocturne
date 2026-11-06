#include "tui/Ui.hpp"
#include "tui/Widgets.hpp"

namespace nocturne::tui {

using namespace ftxui;

namespace {
Element key(const std::string& k, const std::string& what) {
  return hbox({text("  " + fixedText(k, 18)) | color(theme::amber), text(what) | color(theme::text)});
}
Element head(const std::string& t) { return text(" " + t) | bold | color(theme::purple); }
}  // namespace

Element Ui::renderHelp(int width, int height) {
  (void)width;
  (void)height;
  Element left = vbox({
      head("VIEWS"),
      key("F1 .. F6", "Rack, Patch bay, Play, Patches, Settings, Help"),
      key("`  (backtick)", "cycle views"),
      key("F7 / Ctrl+S", "save patch"),
      key("F8 / Ctrl+N", "new empty patch"),
      key("F10 / Ctrl+Q", "quit"),
      text(" "),
      head("PLAYING (any view)"),
      key("Z S X D C V ...", "lower piano row (S D G H J L = sharps)"),
      key("Q 2 W 3 E R ...", "upper piano row (digits = sharps)"),
      key("CAPS", "accent: full velocity"),
      key("[  ]", "octave down / up"),
      key("Space", "PANIC: silence everything"),
      key("Esc", "release all notes"),
      text(" "),
      head("RACK"),
      key("Tab / Shift+Tab", "select module"),
      key("Up / Down", "select parameter"),
      key("Left / Right", "adjust (coarse)"),
      key("Ctrl+Arrows - =", "adjust (fine)"),
      key("Enter", "type a value  (e.g. 2.4k, 440, saw)"),
      key("Backspace", "reset to default"),
      key("A / Delete", "add / remove module"),
      text(" "),
      head("PATCHES (F4)"),
      key("Enter", "load selected (FACTORY = built in, USER = file)"),
      key("Tab", "right panel: preview / diagnostics / loaded JSON"),
      key("Ctrl+E", "export loaded patch JSON to <config>/exports"),
      key("Up / Down", "scroll the diagnostics / JSON panel"),
  });
  Element right = vbox({
      head("PATCH BAY"),
      key("Tab", "switch list: sources, destinations, connections"),
      key("Enter", "arm source, then Enter on a destination"),
      key("Left / Right", "connection amount (on CONNECTIONS)"),
      key("Delete", "disconnect"),
      key("Esc", "cancel armed source"),
      text(" ") ,
      paragraph("  Cyan = audio, amber = control. Control sources (LFO, ADSR) can go to any control input; audio can go to audio inputs and FM-capable ones. Cycles are refused. Each connection has an amount: negative inverts.") |
          color(theme::dim),
      text(" "),
      head("PLAY VIEW"),
      key("Up / Down", "velocity"),
      key("Left / Right", "octave"),
      text(" "),
      head("NOTE MODES  (Tab in Play view)"),
      paragraph("  Terminals report key presses but normally no releases. TIMED plays each note for the NOTE LENGTH (key repeat extends it). LATCH: press to start, press again to stop. LIVE: the note sounds exactly while the key is physically held and releases on key-up; it reads the real key state (Windows: GetAsyncKeyState, only while this window has focus; Ctrl/Alt ignored; Shift = accent) and is shown UNAVAILABLE where no key-up info exists. Effect tails always ring out.") |
          color(theme::dim),
      text(" "),
      head("ACCEPTANCE TOUR"),
      paragraph("  Play a key. Change OSC WAVE. Sweep FILTER CUTOFF. Edit ADSR. Patch LFO1.OUT into FILTER1.CUTOFF in the bay. Add an oscillator (A). Raise DELAY/REVERB mix. F7 to save, F4 to reload.") |
          color(theme::dim),
  });
  return hbox({window(text(" KEYS ") | color(theme::purple), left | flex) | color(theme::border) | size(WIDTH, EQUAL, 58),
               window(text(" PATCHING & PERFORMANCE ") | color(theme::purple), right | flex) | color(theme::border) | flex});
}

}  // namespace nocturne::tui
