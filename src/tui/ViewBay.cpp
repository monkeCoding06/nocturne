#include <algorithm>
#include <cmath>
#include <cstdio>

#include "tui/Ui.hpp"
#include "tui/Widgets.hpp"

namespace nocturne::tui {

using namespace ftxui;

std::vector<Endpoint> Ui::sources() const {
  std::vector<Endpoint> out;
  const Patch& p = s_.patch();
  for (size_t i = 0; i < p.modules.size(); ++i) {
    const ModuleSpec* spec = p.specOf(static_cast<int>(i));
    if (!spec) continue;
    for (const PortSpec& port : spec->outputs)
      if (!port.internal) out.push_back({static_cast<int>(i), p.modules[i].name, &port});
  }
  return out;
}

std::vector<Endpoint> Ui::destinations() const {
  std::vector<Endpoint> out;
  const Patch& p = s_.patch();
  for (size_t i = 0; i < p.modules.size(); ++i) {
    const ModuleSpec* spec = p.specOf(static_cast<int>(i));
    if (!spec) continue;
    for (const PortSpec& port : spec->inputs)
      if (!port.internal) out.push_back({static_cast<int>(i), p.modules[i].name, &port});
  }
  return out;
}

namespace {
Color kindColor(SignalKind k) { return k == SignalKind::Audio ? theme::cyan : theme::amber; }

// Range of `rows` list entries kept visible around the selection.
void visibleRange(int total, int sel, int rows, int& first, int& last) {
  first = std::max(0, std::min(sel - rows / 2, total - rows));
  last = std::min(total, first + rows);
}

const PortSpec* findOutPort(const Patch& p, const std::string& mod, const std::string& port) {
  const int mi = p.findModule(mod);
  if (mi < 0) return nullptr;
  const ModuleSpec* spec = p.specOf(mi);
  const int pi = spec->outputIndex(port);
  return pi < 0 ? nullptr : &spec->outputs[static_cast<size_t>(pi)];
}
}  // namespace

Element Ui::renderBay(int width, int height) {
  const Patch& patch = s_.patch();
  const auto src = sources();
  const auto dst = destinations();
  const int rows = std::max(3, height - 6);
  srcSel_ = std::min(srcSel_, std::max(0, static_cast<int>(src.size()) - 1));
  dstSel_ = std::min(dstSel_, std::max(0, static_cast<int>(dst.size()) - 1));
  if (armed_ && patch.findModule(armedSrc_.module_name) < 0) armed_ = false;

  const Endpoint* activeSrc = nullptr;
  if (armed_) activeSrc = &armedSrc_;
  else if (!src.empty()) activeSrc = &src[static_cast<size_t>(srcSel_)];

  Elements srcRows;
  {
    int a, b;
    visibleRange(static_cast<int>(src.size()), srcSel_, rows, a, b);
    for (int i = a; i < b; ++i) {
      const Endpoint& ep = src[static_cast<size_t>(i)];
      const bool cur = panel_ == 0 && i == srcSel_;
      const bool isArmed = armed_ && armedSrc_.module_name == ep.module_name && armedSrc_.port->id == ep.port->id;
      Element row = text((isArmed ? "● " : cur ? "▸ " : "  ") + ep.module_name + "." + ep.port->id) | color(kindColor(ep.port->kind));
      if (cur) row = row | inverted;
      srcRows.push_back(row);
    }
    if (src.empty()) srcRows.push_back(text(" (no outputs)") | color(theme::dim));
  }
  Elements dstRows;
  {
    int a, b;
    visibleRange(static_cast<int>(dst.size()), dstSel_, rows, a, b);
    for (int i = a; i < b; ++i) {
      const Endpoint& ep = dst[static_cast<size_t>(i)];
      const bool cur = panel_ == 1 && i == dstSel_;
      bool valid = true;
      if (activeSrc && activeSrc->port)
        valid = checkConnection(patch, activeSrc->module_name, activeSrc->port->id, ep.module_name, ep.port->id).empty();
      int wires = 0;
      for (const Connection& c : patch.connections)
        if (c.to == ep.module_name && c.toPort == ep.port->id) ++wires;
      const std::string label = (cur ? "▸ " : "  ") + ep.module_name + "." + ep.port->id;
      Element line = hbox({text(fixedText(label, 20)) | color(valid ? kindColor(ep.port->kind) : theme::border),
                           text(wires ? " ◀" + std::to_string(wires) : "") | color(theme::purple)});
      if (cur) line = line | inverted;
      dstRows.push_back(line);
    }
    if (dst.empty()) dstRows.push_back(text(" (no inputs)") | color(theme::dim));
  }
  Elements connRows;
  {
    const int total = static_cast<int>(patch.connections.size());
    int a, b;
    visibleRange(total, connSel_, rows, a, b);
    for (int i = a; i < b; ++i) {
      const Connection& c = patch.connections[static_cast<size_t>(i)];
      const PortSpec* sp = findOutPort(patch, c.from, c.fromPort);
      const Color col = sp ? kindColor(sp->kind) : theme::text;
      const bool cur = panel_ == 2 && i == connSel_;
      char amt[16];
      std::snprintf(amt, sizeof amt, " x%+.2f", c.amount);
      Element line = hbox({text(cur ? "▸ " : "  "), text(fixedText(c.from + "." + c.fromPort, 16)) | color(col),
                           text(" ──▶ ") | color(col), text(fixedText(c.to + "." + c.toPort, 16)) | color(col),
                           text(amt) | color(theme::amber)});
      if (cur) line = line | inverted;
      connRows.push_back(line);
    }
    if (total == 0) {
      connRows.push_back(text(" No connections yet.") | color(theme::dim));
      connRows.push_back(text(" 1. pick a source (left list)") | color(theme::dim));
      connRows.push_back(text(" 2. press [Enter]") | color(theme::dim));
      connRows.push_back(text(" 3. pick a destination, [Enter]") | color(theme::dim));
    }
  }

  auto panelBox = [&](const std::string& title, Elements body, bool focus) {
    return ftxui::window(text(" " + title + " ") | bold | color(focus ? theme::purple : theme::dim),
                         vbox(std::move(body)) | flex) |
           color(focus ? theme::purple : theme::border);
  };
  const int srcW = 24, dstW = 28;
  const std::string dstTitle = armed_ ? "TO  (" + armedSrc_.module_name + "." + armedSrc_.port->id + ")" : "DESTINATIONS";
  Element top = hbox({panelBox("SOURCES", std::move(srcRows), panel_ == 0) | size(WIDTH, EQUAL, srcW),
                      panelBox(dstTitle, std::move(dstRows), panel_ == 1) | size(WIDTH, EQUAL, dstW),
                      panelBox("CONNECTIONS", std::move(connRows), panel_ == 2) | flex});

  std::string order;
  for (int idx : processingOrder(patch)) order += (order.empty() ? "" : " → ") + patch.modules[static_cast<size_t>(idx)].name;
  if (order.empty() && !patch.modules.empty()) order = "(cycle!)";
  Element legend = hbox({text(" ■ ") | color(theme::cyan), text("audio  ") | color(theme::dim), text("■ ") | color(theme::amber),
                         text("control  ") | color(theme::dim), text("x = amount (attenuverter)  ") | color(theme::dim),
                         text("dark destinations are invalid for the chosen source") | color(theme::dim)});
  Element flow = hbox({text(" ORDER ") | bold | color(theme::purple), text(order) | color(theme::text)});
  (void)width;
  return vbox({top | flex, flow, legend});
}

bool Ui::handleBay(const Event& e) {
  const auto src = sources();
  const auto dst = destinations();
  const int nConn = static_cast<int>(s_.patch().connections.size());

  if (e == Event::Tab) { panel_ = (panel_ + 1) % 3; return true; }
  if (e == Event::TabReverse) { panel_ = (panel_ + 2) % 3; return true; }
  if (e == Event::Escape) { armed_ = false; panel_ = 0; return true; }

  auto move = [&](int& sel, int total, int d) { if (total > 0) sel = std::min(std::max(sel + d, 0), total - 1); };
  if (e == Event::ArrowUp || e == Event::ArrowDown) {
    const int d = e == Event::ArrowUp ? -1 : 1;
    if (panel_ == 0) move(srcSel_, static_cast<int>(src.size()), d);
    else if (panel_ == 1) move(dstSel_, static_cast<int>(dst.size()), d);
    else move(connSel_, nConn, d);
    return true;
  }
  if (e == Event::ArrowRight && panel_ == 0) { panel_ = 1; return true; }
  if (e == Event::ArrowLeft && panel_ == 1) { panel_ = 0; return true; }
  if (e == Event::ArrowRight && panel_ == 1) { panel_ = 2; return true; }

  if (e == Event::Return) {
    if (panel_ == 0 && !src.empty()) {
      armedSrc_ = src[static_cast<size_t>(srcSel_)];
      armed_ = true;
      panel_ = 1;
      s_.setStatus("Source " + armedSrc_.module_name + "." + armedSrc_.port->id + " armed: choose a destination");
    } else if (panel_ == 1 && !dst.empty()) {
      if (!armed_ && !src.empty()) { armedSrc_ = src[static_cast<size_t>(srcSel_)]; armed_ = true; }
      const Endpoint& d = dst[static_cast<size_t>(dstSel_)];
      const std::string err = s_.connect(armedSrc_.module_name, armedSrc_.port->id, d.module_name, d.port->id);
      if (!err.empty()) s_.setStatus("Cannot connect: " + err, true);
      else connSel_ = static_cast<int>(s_.patch().connections.size()) - 1;
    } else if (panel_ == 2 && nConn > 0) {
      openNumeric(ModalKind::WireAmount, connSel_, 0);
    }
    return true;
  }
  if ((e == Event::Delete || e == Event::Backspace) && panel_ == 2 && nConn > 0) {
    s_.disconnect(static_cast<size_t>(connSel_));
    return true;
  }
  if (panel_ == 2 && nConn > 0) {
    const float cur = s_.patch().connections[static_cast<size_t>(connSel_)].amount;
    float step = 0.05f;
    int dir = 0;
    if (e == Event::ArrowLeft) dir = -1;
    else if (e == Event::ArrowRight) dir = 1;
    else if (e == Event::ArrowLeftCtrl) { dir = -1; step = 0.01f; }
    else if (e == Event::ArrowRightCtrl) { dir = 1; step = 0.01f; }
    else if (e.is_character() && e.character() == "-") { dir = -1; step = 0.01f; }
    else if (e.is_character() && (e.character() == "=" || e.character() == "+")) { dir = 1; step = 0.01f; }
    if (dir != 0) {
      s_.setWireAmount(static_cast<size_t>(connSel_), std::round((cur + static_cast<float>(dir) * step) * 100.0f) / 100.0f);
      return true;
    }
  }
  if (e.is_character() && (e.character() == "a" || e.character() == "A")) { openAddModule(); return true; }
  return false;
}

}  // namespace nocturne::tui
