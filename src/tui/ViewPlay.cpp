#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <ftxui/dom/canvas.hpp>

#include "tui/Ui.hpp"
#include "tui/Widgets.hpp"

namespace nocturne::tui {

using namespace ftxui;

namespace {
constexpr std::array<int, 10> kWhite = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16};
constexpr std::array<int, 10> kBlackAfter = {1, 3, -1, 6, 8, 10, -1, 13, 15, -1};
constexpr const char* kWhiteNames[10] = {"C", "D", "E", "F", "G", "A", "B", "C", "D", "E"};
constexpr int kCell = 6;

}  // namespace

Element Ui::piano(int baseOctave, bool upperRow) {
  const char* whiteRow = keyRow(s_.player().layout(), upperRow);
  const char* blackRow = whiteRow;
  const int baseNote = 12 * (baseOctave + 1);
  NotePlayer& pl = s_.player();
  const auto now = NotePlayer::Clock::now();

  const Color whiteBg = Color::RGB(188, 188, 200), blackBg = Color::RGB(34, 34, 46), gapBg = Color::RGB(20, 20, 28);
  const Color hot = theme::purple;

  Elements rows;
  for (int r = 0; r < 4; ++r) {
    Elements cells;
    for (int c = 0; c < kCell * 10; ++c) {
      const int i = c / kCell, k = c % kCell;
      Color bg = whiteBg, fg = Color::RGB(30, 30, 40);
      std::string ch = " ";
      int blackIdx = -1;
      if (r < 2) {
        // Black key j straddles the boundary after white key j: columns [6j+4, 6j+8).
        for (int j = 0; j < 9; ++j)
          if (kBlackAfter[static_cast<size_t>(j)] >= 0 && c >= kCell * j + 4 && c < kCell * j + 8) blackIdx = j;
      }
      if (blackIdx >= 0) {
        const int note = baseNote + kBlackAfter[static_cast<size_t>(blackIdx)];
        const bool on = pl.isSounding(note, now);
        bg = on ? hot : blackBg;
        fg = on ? Color::RGB(255, 255, 255) : Color::RGB(200, 200, 215);
        if (r == 0 && c == kCell * blackIdx + 5) {
          const char b = blackRow[kBlackAfter[static_cast<size_t>(blackIdx)]];
          ch = b == '\x01' ? " " : std::string(1, static_cast<char>(std::toupper(b)));
        }
      } else if (k == kCell - 1) {
        bg = gapBg;
      } else {
        const int note = baseNote + kWhite[static_cast<size_t>(i)];
        const bool on = pl.isSounding(note, now);
        if (on) { bg = hot; fg = Color::RGB(255, 255, 255); }
        if (r == 2 && k == 2) {
          const char w = whiteRow[kWhite[static_cast<size_t>(i)]];
          ch = w == '\x01' ? " " : std::string(1, static_cast<char>(std::toupper(w)));
        }
        if (r == 3) {
          const std::string name = kWhiteNames[i] + (i == 0 || i == 7 ? std::to_string(baseOctave + (i == 7 ? 1 : 0)) : std::string());
          if (name.size() == 3 && k >= 1 && k <= 3) ch = name.substr(static_cast<size_t>(k - 1), 1);
          else if (name.size() == 2 && k == 1) ch = name.substr(0, 1);
          else if (name.size() == 2 && k == 2) ch = name.substr(1, 1);
          else if (name.size() == 1 && k == 2) ch = name;
        }
      }
      cells.push_back(text(ch) | bgcolor(bg) | color(fg));
    }
    rows.push_back(hbox(std::move(cells)));
  }
  return vbox(std::move(rows));
}

Element Ui::renderPlay(int width, int height) {
  NotePlayer& pl = s_.player();
  const int oct = pl.octave();
  Element upper = vbox({text(" OCTAVE " + std::to_string(oct + 1) + "   upper row (" + keyRow(s_.player().layout(), true)[0] + "…) · digit-row sharps") | color(theme::dim), piano(oct + 1, true)});
  Element lower = vbox({text(" OCTAVE " + std::to_string(oct) + "   lower row (" + keyRow(s_.player().layout(), false)[0] + "…) · home-row sharps") | color(theme::dim), piano(oct, false)});

  // --- oscilloscope of the real output
  const int scopeW = std::max(20, width - 44);
  const int scopeH = std::max(3, height - 14);
  Canvas canvas(scopeW * 2, scopeH * 4);
  {
    static float buf[Engine::kScopeSize];
    s_.engine().readScope(buf, Engine::kScopeSize);
    int start = 0;
    for (int i = 1; i < Engine::kScopeSize / 2; ++i)
      if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) { start = i; break; }
    const int span = 1500;
    const int h = scopeH * 4;
    const int mid = h / 2;
    auto yOf = [&](float v) { return std::min(h - 1, std::max(0, mid - static_cast<int>(v * static_cast<float>(mid - 1) * 0.95f))); };
    for (int x = 0; x < scopeW * 2 - 1; ++x) {
      const int i0 = start + x * span / (scopeW * 2), i1 = start + (x + 1) * span / (scopeW * 2);
      canvas.DrawPointLine(x, yOf(buf[i0]), x + 1, yOf(buf[i1]), theme::cyan);
    }
    canvas.DrawPointLine(0, mid, scopeW * 2 - 1, mid, theme::border);
  }
  Element scope = window(text(" OUTPUT SCOPE ") | color(theme::dim), ftxui::canvas(std::move(canvas))) | color(theme::border);

  char buf[64];
  std::snprintf(buf, sizeof buf, "%.2f", pl.velocity());
  char hb[32];
  std::snprintf(hb, sizeof hb, "%.1f s  (- =)", pl.holdSeconds());
  const std::string holdText = hb;
  const NoteMode mode = pl.mode();
  const bool live = mode == NoteMode::Live;
  const bool liveOk = pl.liveAvailable();
  std::string held;
  for (int n : pl.heldNotes()) held += (held.empty() ? "" : " ") + noteName(n);
  if (held.empty()) held = "-";
  std::string lengthText = mode == NoteMode::Latch ? "until pressed again" : (live ? "while key is held" : holdText);
  Elements infoRows = {
      hbox({text(" OCTAVE   ") | color(theme::dim), text(std::to_string(oct) + "  [ ] ← →") | color(theme::amber)}),
      hbox({text(" VELOCITY ") | color(theme::dim), text(std::string(buf) + "  ↑ ↓ (CAPS = 1.0)") | color(theme::amber)}),
      hbox({text(" NOTES    ") | color(theme::dim), text(std::string(noteModeName(mode)) + "  [Tab]") | color(theme::amber)}),
      hbox({text(" LENGTH   ") | color(theme::dim), text(lengthText) | color(theme::amber)}),
      hbox({text(" HELD     ") | color(theme::dim), text(live ? held : "-") | color(theme::cyan)}),
      hbox({text(" VOICES   ") | color(theme::dim), text(std::to_string(s_.engine().activeVoices()) + " active") | color(theme::cyan)}),
  };
  if (live) {
    infoRows.push_back(text(" LIVE: true key up/down (focus needed)") | color(theme::dim));
  } else if (liveOk) {
    infoRows.push_back(text(" LIVE available: [Tab] cycles to it") | color(theme::dim));
  } else {
    infoRows.push_back(text(" LIVE UNAVAILABLE (no key-up events)") | color(theme::red));
    infoRows.push_back(text(" Timed/latch only in this terminal.") | color(theme::dim));
  }
  Element info = window(text(" PERFORMANCE ") | color(theme::dim), vbox(std::move(infoRows))) |
                 color(theme::border) | size(WIDTH, EQUAL, 42);
  return vbox({hbox({filler(), vbox({upper, lower}), filler()}), hbox({scope | flex, info})});
}

}  // namespace nocturne::tui
