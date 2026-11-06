#include "tui/Ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <ftxui/screen/terminal.hpp>

#include "tui/Widgets.hpp"

namespace nocturne::tui {

using namespace ftxui;

namespace {
const char* kViewNames[] = {"RACK", "BAY", "PLAY", "PATCHES", "SETTINGS", "HELP"};
}

Ui::Ui(Session& session) : s_(session) {
  refreshPatches();
  refreshDevices();
}

// ---------------------------------------------------------------- helpers
void Ui::setView(View v) {
  view_ = v;
  if (v == View::Patches) refreshPatches();
  if (v == View::Settings) refreshDevices();
}

void Ui::refreshPatches() {
  entries_ = s_.listPatches();
  patchSel_ = std::min(std::max(patchSel_, 0), std::max(0, static_cast<int>(entries_.size()) - 1));
  previewKey_.clear();
}

void Ui::refreshDevices() { devices_ = s_.devices(); }

void Ui::clampSelection() {
  const int n = static_cast<int>(s_.patch().modules.size());
  selModule_ = std::min(std::max(selModule_, 0), std::max(0, n - 1));
  if (n > 0) {
    const ModuleSpec* spec = s_.patch().specOf(selModule_);
    const int pc = spec ? static_cast<int>(spec->params.size()) : 0;
    selParam_ = std::min(std::max(selParam_, 0), std::max(0, pc - 1));
  }
  const int c = static_cast<int>(s_.patch().connections.size());
  connSel_ = std::min(std::max(connSel_, 0), std::max(0, c - 1));
}

// ---------------------------------------------------------------- frame
Element Ui::header() {
  Elements row;
  row.push_back(text(" NOCTURNE ") | bold | color(theme::purple));
  row.push_back(text(" "));
  for (int i = 0; i < 6; ++i) {
    const bool on = static_cast<int>(view_) == i;
    const std::string label = " F" + std::to_string(i + 1) + " " + kViewNames[i] + " ";
    row.push_back(on ? (text(label) | bold | inverted | color(theme::purple)) : (text(label) | color(theme::dim)));
  }
  row.push_back(filler());
  const std::string name = s_.patch().meta.name + (s_.dirty() ? " *" : "");
  row.push_back(text("PATCH: ") | color(theme::dim));
  row.push_back(text(name + " ") | bold | color(theme::amber));
  return hbox(std::move(row));
}

Element Ui::statusStrip() {
  Engine& e = s_.engine();
  const float pl = e.peakLeft(), pr = e.peakRight();
  char cpu[16];
  std::snprintf(cpu, sizeof cpu, "%3.0f%%", e.cpuLoad() * 100.0f);
  const bool latch = s_.player().mode() == NoteMode::Latch;
  char hold[24];
  std::snprintf(hold, sizeof hold, "%s %.1fs", latch ? "LATCH" : "TIMED", s_.player().holdSeconds());
  const int total = s_.patch().global.mode == VoiceMode::Mono ? 1 : s_.patch().global.voices;
  const bool audioOk = s_.device().running();
  return hbox({
      text(" L ") | color(theme::dim), levelMeter(pl, 10), text(" R ") | color(theme::dim), levelMeter(pr, 10),
      text(" " + dbString(std::max(pl, pr))) | color(theme::text),
      text("  │ ") | color(theme::border), text("VOICES ") | color(theme::dim),
      text(std::to_string(e.activeVoices()) + "/" + std::to_string(total)) | color(theme::cyan),
      text("  │ ") | color(theme::border), text("CPU ") | color(theme::dim), text(cpu) | color(theme::text),
      text("  │ ") | color(theme::border), text("OCT ") | color(theme::dim),
      text(std::to_string(s_.player().octave())) | color(theme::amber),
      text("  │ ") | color(theme::border), text(hold) | color(theme::amber),
      filler(),
      audioOk ? (text("AUDIO ON ") | color(theme::green)) : (text("AUDIO OFF ") | color(theme::red) | bold),
  });
}

Element Ui::footer(const std::string& hints) {
  Element status = s_.status().empty()
                       ? text(" ")
                       : text(" " + s_.status()) | color(s_.statusIsError() ? theme::red : theme::cyan);
  return vbox({status, text(" " + hints) | color(theme::dim)});
}

Element Ui::render() {
  s_.tick();
  clampSelection();
  const auto dims = Terminal::Size();
  termW_ = dims.dimx;
  termH_ = dims.dimy;

  if (termW_ < 78 || termH_ < 22) {
    return vbox({filler(),
                 text("NOCTURNE needs at least 78 x 22 characters") | center | color(theme::amber),
                 text("current: " + std::to_string(termW_) + " x " + std::to_string(termH_)) | center | color(theme::dim),
                 text("(sound keeps playing - resize the window)") | center | color(theme::dim), filler()}) |
           bgcolor(theme::bg);
  }

  const int bodyH = termH_ - 6;
  Element body;
  std::string hints;
  switch (view_) {
    case View::Rack:
      body = renderRack(termW_, bodyH);
      hints = "[Tab] module  [↑↓] param  [←→] adjust  [-/=] fine  [Enter] type value  [Bksp] reset  [A] add  [Del] remove";
      break;
    case View::Bay:
      body = renderBay(termW_, bodyH);
      hints = "[Tab] panel  [↑↓] select  [Enter] pick source / connect  [←→] amount  [Del] disconnect  [Esc] cancel";
      break;
    case View::Play:
      body = renderPlay(termW_, bodyH);
      hints = "Z-row & Q-row = piano   [ ] octave   [Space] panic   [Esc] release all   [Tab] timed/latch   [- =] note length";
      break;
    case View::Patches:
      body = renderPatches(termW_, bodyH);
      hints = "[↑↓] select  [Enter] load  [F7] save current  [F8] new  [Del] delete user patch";
      break;
    case View::Settings:
      body = renderSettings(termW_, bodyH);
      hints = "[↑↓] select  [←→] change  [Enter] apply / restart audio";
      break;
    case View::Help:
      body = renderHelp(termW_, bodyH);
      hints = "[F1-F6] switch views";
      break;
  }

  Element rule = text(repeatStr("─", termW_)) | color(theme::border);
  Element frame = vbox({header(), rule, body | size(HEIGHT, EQUAL, bodyH), rule, statusStrip(), footer(hints)}) |
                  size(HEIGHT, EQUAL, termH_) | size(WIDTH, EQUAL, termW_);
  frame = frame | bgcolor(theme::bg) | color(theme::text);
  if (modal_ != ModalKind::None) return dbox({frame, modalOverlay() | clear_under | center});
  return frame;
}

// ---------------------------------------------------------------- modal
void Ui::openNumeric(ModalKind kind, int a, int b) {
  modal_ = kind;
  modalA_ = a;
  modalB_ = b;
  modalText_.clear();
  modalError_.clear();
}

void Ui::openSave() {
  modal_ = ModalKind::SaveName;
  modalText_ = s_.patch().meta.name;
  modalError_.clear();
}

void Ui::openAddModule() {
  modal_ = ModalKind::AddModule;
  modalError_.clear();
  modalPick_ = 0;
  addList_.clear();
  for (const ModuleSpec* spec : allSpecs()) {
    if (spec->unique) continue;
    addList_.push_back(spec);
  }
}

Element Ui::modalOverlay() {
  Elements lines;
  std::string title;
  switch (modal_) {
    case ModalKind::Numeric: {
      const ModuleSpec* spec = s_.patch().specOf(modalA_);
      const ParamSpec& p = spec->params[static_cast<size_t>(modalB_)];
      title = s_.patch().modules[static_cast<size_t>(modalA_)].name + "  " + p.label;
      lines.push_back(text("Enter a value" + (p.unit.empty() ? std::string() : " in " + p.unit) +
                           (p.kind == ParamKind::Enum ? " or a name" : "") + ":") | color(theme::dim));
      lines.push_back(text("range " + p.format(p.min) + " … " + p.format(p.max)) | color(theme::dim));
      lines.push_back(text(" "));
      lines.push_back(hbox({text("> ") | color(theme::purple), text(modalText_) | bold, text("▏") | color(theme::purple)}));
      break;
    }
    case ModalKind::WireAmount:
      title = "CONNECTION AMOUNT";
      lines.push_back(text("Attenuverter -1.0 … 1.0 (negative inverts):") | color(theme::dim));
      lines.push_back(text(" "));
      lines.push_back(hbox({text("> ") | color(theme::purple), text(modalText_) | bold, text("▏") | color(theme::purple)}));
      break;
    case ModalKind::SaveName:
      title = "SAVE PATCH";
      lines.push_back(text("Patch name (saved to " + s_.patchDirectory().string() + "):") | color(theme::dim));
      lines.push_back(text(" "));
      lines.push_back(hbox({text("> ") | color(theme::purple), text(modalText_) | bold, text("▏") | color(theme::purple)}));
      break;
    case ModalKind::AddModule: {
      title = "ADD MODULE";
      const int maxRows = std::max(4, termH_ - 12);
      const int first = std::max(0, std::min(modalPick_ - maxRows / 2, static_cast<int>(addList_.size()) - maxRows));
      for (int i = first; i < static_cast<int>(addList_.size()) && i < first + maxRows; ++i) {
        const ModuleSpec* sp = addList_[static_cast<size_t>(i)];
        const bool on = i == modalPick_;
        std::string label = fixedText(sp->label, 14) + (sp->scope == Scope::Global ? "global" : "per voice");
        lines.push_back(text((on ? " ▶ " : "   ") + label + " ") | (on ? Decorator(inverted) | color(theme::purple) : Decorator(color(theme::text))));
      }
      break;
    }
    case ModalKind::ConfirmModule:
      title = "REMOVE MODULE";
      lines.push_back(text("Remove " + s_.patch().modules[static_cast<size_t>(modalA_)].name + " and its connections?"));
      lines.push_back(text("[Enter] yes   [Esc] no") | color(theme::dim));
      break;
    case ModalKind::ConfirmPatch:
      title = "DELETE PATCH FILE";
      lines.push_back(text("Delete " + entries_[static_cast<size_t>(patchSel_)].name + " permanently?"));
      lines.push_back(text("[Enter] yes   [Esc] no") | color(theme::dim));
      break;
    case ModalKind::ConfirmNew:
      title = "NEW PATCH";
      lines.push_back(text("Unsaved changes will be lost. Start an empty patch?"));
      lines.push_back(text("[Enter] yes   [Esc] no") | color(theme::dim));
      break;
    case ModalKind::None: break;
  }
  if (!modalError_.empty()) lines.push_back(text(modalError_) | color(theme::red));
  if (modal_ == ModalKind::Numeric || modal_ == ModalKind::WireAmount || modal_ == ModalKind::SaveName)
    lines.push_back(text("[Enter] ok   [Esc] cancel") | color(theme::dim));
  if (modal_ == ModalKind::AddModule) lines.push_back(text("[↑↓] choose  [Enter] add  [Esc] cancel") | color(theme::dim));
  return window(text(" " + title + " ") | bold | color(theme::purple), vbox(std::move(lines))) |
         size(WIDTH, GREATER_THAN, 46) | bgcolor(theme::panel) | color(theme::text);
}

void Ui::confirmModal() {
  switch (modal_) {
    case ModalKind::Numeric:
      if (!s_.parseParam(modalA_, modalB_, modalText_)) { modalError_ = "Cannot read that value"; return; }
      break;
    case ModalKind::WireAmount: {
      char* end = nullptr;
      const double v = std::strtod(modalText_.c_str(), &end);
      if (modalText_.empty() || end == modalText_.c_str()) { modalError_ = "Not a number"; return; }
      s_.setWireAmount(static_cast<size_t>(modalA_), static_cast<float>(v));
      break;
    }
    case ModalKind::SaveName: {
      const std::string err = s_.saveCurrent(modalText_);
      if (!err.empty()) { modalError_ = err; return; }
      refreshPatches();
      break;
    }
    case ModalKind::AddModule: {
      if (addList_.empty()) break;
      const std::string err = s_.addModule(addList_[static_cast<size_t>(modalPick_)]->type);
      if (!err.empty()) { modalError_ = err; return; }
      selModule_ = static_cast<int>(s_.patch().modules.size()) - 1;
      selParam_ = 0;
      break;
    }
    case ModalKind::ConfirmModule: {
      const std::string err = s_.removeModule(modalA_);
      if (!err.empty()) { modalError_ = err; return; }
      break;
    }
    case ModalKind::ConfirmPatch:
      s_.deleteEntry(entries_[static_cast<size_t>(patchSel_)]);
      refreshPatches();
      break;
    case ModalKind::ConfirmNew:
      s_.newPatch();
      selModule_ = selParam_ = 0;
      armed_ = false;
      break;
    case ModalKind::None: break;
  }
  modal_ = ModalKind::None;
}

bool Ui::handleModal(const Event& e) {
  if (e == Event::Escape) { modal_ = ModalKind::None; return true; }
  const bool textual = modal_ == ModalKind::Numeric || modal_ == ModalKind::WireAmount || modal_ == ModalKind::SaveName;
  if (e == Event::Return) { confirmModal(); return true; }
  if (textual) {
    if (e == Event::Backspace) { if (!modalText_.empty()) modalText_.pop_back(); modalError_.clear(); return true; }
    if (e.is_character() && modalText_.size() < 40) { modalText_ += e.character(); modalError_.clear(); return true; }
  } else if (modal_ == ModalKind::AddModule) {
    const int n = static_cast<int>(addList_.size());
    if (e == Event::ArrowDown || e == Event::Tab) modalPick_ = (modalPick_ + 1) % std::max(1, n);
    if (e == Event::ArrowUp || e == Event::TabReverse) modalPick_ = (modalPick_ + n - 1) % std::max(1, n);
  } else if (e.is_character() && (e.character() == "y" || e.character() == "Y")) {
    confirmModal();
  } else if (e.is_character() && (e.character() == "n" || e.character() == "N")) {
    modal_ = ModalKind::None;
  }
  return true;  // modal swallows everything
}

// ---------------------------------------------------------------- events
bool Ui::handleGlobal(const Event& e) {
  if (e == Event::F1) { setView(View::Rack); return true; }
  if (e == Event::F2) { setView(View::Bay); return true; }
  if (e == Event::F3) { setView(View::Play); return true; }
  if (e == Event::F4) { setView(View::Patches); return true; }
  if (e == Event::F5) { setView(View::Settings); return true; }
  if (e == Event::F6) { setView(View::Help); return true; }
  if (e == Event::F7 || e == Event::CtrlS) { openSave(); return true; }
  if (e == Event::F8 || e == Event::CtrlN) {
    if (s_.dirty()) modal_ = ModalKind::ConfirmNew;
    else { s_.newPatch(); selModule_ = selParam_ = 0; }
    return true;
  }
  if (e == Event::F10 || e == Event::CtrlQ) { quit_ = true; return true; }
  if (e == Event::Escape) { s_.player().allOff(); return true; }
  if (e.is_character()) {
    const std::string& c = e.character();
    if (c == "`") { setView(static_cast<View>((static_cast<int>(view_) + 1) % 6)); return true; }
    if (c == "?") { setView(View::Help); return true; }
    if (c == "[") { s_.player().setOctave(s_.player().octave() - 1); return true; }
    if (c == "]") { s_.player().setOctave(s_.player().octave() + 1); return true; }
    if (c == " ") { s_.player().panic(); s_.setStatus("Panic: all notes off"); return true; }
    if (c.size() == 1 && s_.player().handleChar(c[0])) return true;
  }
  return false;
}

bool Ui::onEvent(const Event& e) {
  if (e == Event::Custom) return false;  // animation tick: re-render only
  if (modal_ != ModalKind::None) return handleModal(e);
  // View-specific text-like keys first when they would collide with globals (none do).
  if (handleGlobal(e)) return true;
  switch (view_) {
    case View::Rack: return handleRack(e);
    case View::Bay: return handleBay(e);
    case View::Patches: return handlePatches(e);
    case View::Settings: return handleSettings(e);
    case View::Play:
      if (e == Event::ArrowUp) { s_.player().setVelocity(s_.player().velocity() + 0.05f); return true; }
      if (e == Event::ArrowDown) { s_.player().setVelocity(s_.player().velocity() - 0.05f); return true; }
      if (e == Event::ArrowLeft) { s_.player().setOctave(s_.player().octave() - 1); return true; }
      if (e == Event::ArrowRight) { s_.player().setOctave(s_.player().octave() + 1); return true; }
      if (e == Event::Tab) {
        const NoteMode m = s_.player().mode() == NoteMode::Timed ? NoteMode::Latch : NoteMode::Timed;
        s_.player().setMode(m);
        s_.config().noteMode = m;
        return true;
      }
      if (e.is_character() && e.character() == "-") { s_.player().setHoldSeconds(s_.player().holdSeconds() - 0.1f); return true; }
      if (e.is_character() && (e.character() == "=" || e.character() == "+")) { s_.player().setHoldSeconds(s_.player().holdSeconds() + 0.1f); return true; }
      return false;
    case View::Help: return false;
  }
  return false;
}

}  // namespace nocturne::tui
