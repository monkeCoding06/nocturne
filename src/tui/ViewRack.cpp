#include <algorithm>
#include <cmath>
#include <cstdio>

#include "tui/Ui.hpp"
#include "tui/Widgets.hpp"

namespace nocturne::tui {

using namespace ftxui;

namespace {
constexpr int kCardWidth = 34;

Color accentFor(const ModuleSpec& spec) {
  if (spec.type == "master") return theme::green;
  if (spec.type == "lfo" || spec.type == "adsr" || spec.type == "keys") return theme::amber;  // control sources
  return theme::cyan;
}
}  // namespace

Element Ui::moduleCard(int index, int width, bool selected) {
  const Patch& p = s_.patch();
  const ModuleInstance& m = p.modules[static_cast<size_t>(index)];
  const ModuleSpec* spec = p.specOf(index);
  const Color accent = accentFor(*spec);
  const int barW = 8;

  Elements rows;
  rows.push_back(hbox({text(m.name) | bold | color(selected ? theme::purple : accent), filler(),
                       text(spec->label) | color(theme::dim)}));
  rows.push_back(text(repeatStr("┄", width - 4)) | color(theme::border));
  for (size_t i = 0; i < spec->params.size(); ++i) {
    const ParamSpec& ps = spec->params[i];
    const bool cur = selected && static_cast<int>(i) == selParam_;
    Element row = hbox({text(cur ? "▸" : " ") | color(theme::purple),
                        text(fixedText(ps.label, 7)) | color(cur ? theme::text : theme::dim),
                        text(" "), paramBar(ps, m.params[i], barW), text(" "),
                        text(fixedText(ps.format(m.params[i]), width - 4 - 1 - 7 - 1 - barW - 1)) |
                            color(cur ? theme::amber : theme::text)});
    if (cur) row = row | bold;
    rows.push_back(row);
  }
  int shown = 0;
  for (const Connection& c : p.connections) {
    if (c.to != m.name) continue;
    if (shown == 0) rows.push_back(text(repeatStr("┄", width - 4)) | color(theme::border));
    if (++shown > 4) break;
    char amt[16];
    std::snprintf(amt, sizeof amt, " x%.2f", c.amount);
    rows.push_back(text(fixedText(" " + c.toPort + " ◀ " + c.from + "." + c.fromPort + amt, width - 4)) | color(theme::dim));
  }
  return vbox(std::move(rows)) | borderStyled(ROUNDED, selected ? theme::purple : theme::border) | size(WIDTH, EQUAL, width);
}

Element Ui::renderRack(int width, int height) {
  const Patch& patch = s_.patch();
  const int n = static_cast<int>(patch.modules.size());
  const int cols = std::max(1, width / kCardWidth);
  const int rowsTotal = (n + cols - 1) / cols;

  auto cardHeight = [&](int idx) {
    const ModuleSpec* spec = patch.specOf(idx);
    int conn = 0;
    for (const Connection& c : patch.connections)
      if (c.to == patch.modules[static_cast<size_t>(idx)].name) ++conn;
    conn = std::min(conn, 4);
    return 4 + static_cast<int>(spec->params.size()) + (conn ? conn + 1 : 0);
  };
  std::vector<int> rowH(static_cast<size_t>(std::max(rowsTotal, 1)), 0);
  for (int i = 0; i < n; ++i) rowH[static_cast<size_t>(i / cols)] = std::max(rowH[static_cast<size_t>(i / cols)], cardHeight(i));

  const int detailH = 6;
  const int avail = std::max(6, height - detailH);
  const int selRow = selModule_ / cols;
  int first = selRow, used = rowH[static_cast<size_t>(selRow)];
  while (first > 0 && used + rowH[static_cast<size_t>(first - 1)] <= avail) used += rowH[static_cast<size_t>(--first)];

  Elements rows;
  int shownH = 0;
  int lastRow = first;
  for (int r = first; r < rowsTotal && shownH + rowH[static_cast<size_t>(r)] <= avail; ++r) {
    Elements cards;
    for (int c = 0; c < cols; ++c) {
      const int idx = r * cols + c;
      if (idx >= n) break;
      cards.push_back(moduleCard(idx, kCardWidth, idx == selModule_));
    }
    cards.push_back(filler());
    rows.push_back(hbox(std::move(cards)));
    shownH += rowH[static_cast<size_t>(r)];
    lastRow = r;
  }
  if (n <= 1) {
    rows.push_back(text(""));
    rows.push_back(text("  Empty patch. Press [A] to add an OSCILLATOR, ENVELOPE and AMPLIFIER,") | color(theme::amber));
    rows.push_back(text("  then wire them in the BAY view (F2). MASTER is the output.") | color(theme::amber));
  }
  if (first > 0 || lastRow + 1 < rowsTotal) rows.push_back(text(" ... more modules: [Tab]") | color(theme::dim));

  Element detail = text("");
  if (n > 0) {
    const ModuleInstance& m = patch.modules[static_cast<size_t>(selModule_)];
    const ModuleSpec* spec = patch.specOf(selModule_);
    if (!spec->params.empty()) {
      const ParamSpec& ps = spec->params[static_cast<size_t>(selParam_)];
      const float v = m.params[static_cast<size_t>(selParam_)];
      const int sliderW = std::min(60, width - 40);
      detail = vbox({
          hbox({text(" " + m.name + " ▸ " + ps.label + "  ") | bold | color(theme::purple),
                text(ps.format(v)) | bold | color(theme::amber), filler(),
                text("default " + ps.format(ps.def) + " ") | color(theme::dim)}),
          hbox({text(" "), paramSlider(ps, v, sliderW, theme::purple), text("  "),
                (ps.kind == ParamKind::Enum ? text("") : text(ps.format(ps.min) + " … " + ps.format(ps.max)) | color(theme::dim))}),
          text(ps.kind == ParamKind::Enum ? " [←→] choose" : " [←→] coarse  [-/=] fine  [Enter] type a value  [Bksp] default") |
              color(theme::dim),
      });
    } else {
      detail = text(" " + m.name + " has no parameters") | color(theme::dim);
    }
  }
  return vbox({vbox(std::move(rows)) | flex, detail | borderStyled(ROUNDED, theme::border)});
}

bool Ui::handleRack(const Event& e) {
  const int n = static_cast<int>(s_.patch().modules.size());
  if (n == 0) return false;
  const ModuleSpec* spec = s_.patch().specOf(selModule_);
  const int pc = static_cast<int>(spec->params.size());
  auto selectModule = [&](int d) { selModule_ = (selModule_ + d + n) % n; selParam_ = 0; };

  if (e == Event::Tab || e == Event::PageDown) { selectModule(+1); return true; }
  if (e == Event::TabReverse || e == Event::PageUp) { selectModule(-1); return true; }
  if (e == Event::ArrowUp) { selParam_ = std::max(0, selParam_ - 1); return true; }
  if (e == Event::ArrowDown) { selParam_ = std::min(std::max(0, pc - 1), selParam_ + 1); return true; }
  if (e == Event::Home) { selParam_ = 0; return true; }
  if (e == Event::End) { selParam_ = std::max(0, pc - 1); return true; }
  if (pc > 0) {
    if (e == Event::ArrowLeft) { s_.adjustParam(selModule_, selParam_, -1, false); return true; }
    if (e == Event::ArrowRight) { s_.adjustParam(selModule_, selParam_, +1, false); return true; }
    if (e == Event::ArrowLeftCtrl) { s_.adjustParam(selModule_, selParam_, -1, true); return true; }
    if (e == Event::ArrowRightCtrl) { s_.adjustParam(selModule_, selParam_, +1, true); return true; }
    if (e == Event::Backspace) { s_.resetParam(selModule_, selParam_); return true; }
    if (e == Event::Return) { openNumeric(ModalKind::Numeric, selModule_, selParam_); return true; }
  }
  if (e == Event::Delete) {
    if (spec->unique) s_.setStatus("MASTER is the output and cannot be removed", true);
    else { modal_ = ModalKind::ConfirmModule; modalA_ = selModule_; }
    return true;
  }
  if (e.is_character()) {
    const std::string& c = e.character();
    if (c == "-" && pc > 0) { s_.adjustParam(selModule_, selParam_, -1, true); return true; }
    if ((c == "=" || c == "+") && pc > 0) { s_.adjustParam(selModule_, selParam_, +1, true); return true; }
    if (c == "a" || c == "A") { openAddModule(); return true; }
  }
  return false;
}

}  // namespace nocturne::tui
