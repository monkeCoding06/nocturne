#include <algorithm>
#include <sstream>

#include "patch/PatchIO.hpp"
#include "patch/Presets.hpp"
#include "tui/Ui.hpp"
#include "tui/Widgets.hpp"

namespace nocturne::tui {

using namespace ftxui;

namespace {

Color severityColor(Diagnostic::Severity s) {
  return s == Diagnostic::Severity::Error ? theme::red : (s == Diagnostic::Severity::Warning ? theme::amber : theme::dim);
}

std::string summary(const PatchLoadResult& r) {
  return std::to_string(r.count(Diagnostic::Severity::Error)) + " error(s), " +
         std::to_string(r.count(Diagnostic::Severity::Warning)) + " warning(s), " +
         std::to_string(r.count(Diagnostic::Severity::Info)) + " note(s)";
}

// Every diagnostic, one wrapped paragraph each (nothing is truncated or hidden).
void appendDiagnostics(Elements& out, const PatchLoadResult& r) {
  for (const auto& d : r.diagnostics)
    out.push_back(paragraph(" " + d.text()) | color(severityColor(d.severity)));
}

}  // namespace

std::string Ui::describePatchSummary(const Patch& p) const {
  std::string path;
  for (int idx : processingOrder(p)) {
    const ModuleInstance& m = p.modules[static_cast<size_t>(idx)];
    path += (path.empty() ? "" : " → ") + m.name;
  }
  return path;
}

Element Ui::renderPatches(int width, int height) {
  (void)width;
  if (entries_.empty()) refreshPatches();
  const int n = static_cast<int>(entries_.size());
  const int rows = std::max(3, height - 3);
  const int first = std::max(0, std::min(patchSel_ - rows / 2, n - rows));
  const PatchIdentity& id = s_.identity();

  Elements list;
  for (int i = first; i < n && i < first + rows; ++i) {
    const PatchEntry& e = entries_[static_cast<size_t>(i)];
    const bool cur = i == patchSel_;
    const bool loaded = s_.config().lastPatch == e.key();
    const std::string tag = e.factory ? "FACTORY" : (e.shadowsFactory ? "USER*" : "USER");
    Element row = hbox({text(cur ? " ▸ " : "   "), text(e.factory ? "◆ " : "● ") | color(e.factory ? theme::purple : theme::cyan),
                        text(fixedText(e.name, 20)), text(fixedText(tag, 8)) | color(e.factory ? theme::purple : theme::cyan),
                        text(loaded ? (s_.dirty() ? "loaded*" : "loaded") : "") | color(theme::green)});
    if (cur) row = row | inverted;
    list.push_back(row);
  }
  if (list.empty()) list.push_back(text(" no patches"));

  // What is in the rack right now, whatever is selected in the list.
  Elements info;
  info.push_back(hbox({text(" LOADED: ") | color(theme::dim), text(s_.patch().meta.name) | bold | color(theme::amber),
                       text(std::string("  ") + id.kindName()) | color(id.kind == PatchIdentity::Kind::Factory ? theme::purple : theme::cyan),
                       text(s_.dirty() ? "  MODIFIED (not saved)" : "  unmodified") | color(s_.dirty() ? theme::red : theme::green)}));
  info.push_back(paragraph(" source: " + (id.source.empty() ? std::string("(none)") : id.source)) | color(theme::dim));
  info.push_back(text(" last load: " + (s_.lastLoadLabel().empty() ? std::string("-") : s_.lastLoadLabel() + ", " + summary(s_.lastLoad()))) |
                 color(s_.lastLoad().count(Diagnostic::Severity::Error) + s_.lastLoad().count(Diagnostic::Severity::Warning) > 0 ? theme::red : theme::dim));
  info.push_back(separator() | color(theme::border));

  const int bodyRows = std::max(3, height - 9);
  std::string title = " PREVIEW ";
  if (patchPanel_ == 1) {
    title = " DIAGNOSTICS (last load) ";
    Elements lines;
    if (s_.lastLoad().diagnostics.empty()) lines.push_back(text(" no findings") | color(theme::green));
    appendDiagnostics(lines, s_.lastLoad());
    if (!s_.presetDirectory().empty()) {
      lines.push_back(text(" preset dir: " + s_.presetDirectory()) | color(theme::dim));
      if (s_.presetMismatches().empty()) lines.push_back(text(" matches the built-in presets") | color(theme::green));
      for (const auto& m : s_.presetMismatches()) lines.push_back(paragraph(" MISMATCH (embedded wins): " + m) | color(theme::red));
    }
    const int total = static_cast<int>(lines.size());
    panelScroll_ = std::max(0, std::min(panelScroll_, total - 1));
    for (int i = panelScroll_; i < total && i < panelScroll_ + bodyRows; ++i) info.push_back(lines[static_cast<size_t>(i)]);
    info.push_back(text(" [↑↓] scroll  " + std::to_string(total) + " line(s)") | color(theme::dim));
  } else if (patchPanel_ == 2) {
    title = " LOADED PATCH JSON ";
    std::vector<std::string> lines;
    std::istringstream in(s_.currentJson());
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    const int total = static_cast<int>(lines.size());
    panelScroll_ = std::max(0, std::min(panelScroll_, total - bodyRows));
    for (int i = panelScroll_; i < total && i < panelScroll_ + bodyRows; ++i)
      info.push_back(text(" " + lines[static_cast<size_t>(i)]) | color(theme::text));
    info.push_back(text(" [↑↓] scroll  line " + std::to_string(panelScroll_ + 1) + "/" + std::to_string(total) + "  [Ctrl+E] export to file") |
                   color(theme::dim));
  } else if (n > 0) {
    const PatchEntry& e = entries_[static_cast<size_t>(patchSel_)];
    if (previewKey_ != e.key()) {
      previewKey_ = e.key();
      if (e.factory) {
        const PresetInfo* f = findFactoryPreset(e.id);
        if (f) previewLoad_ = f->load;
        else previewLoad_ = PatchLoadResult{}, previewLoad_.add(Diagnostic::Severity::Error, "missing-asset", e.id, "not embedded in this build");
      } else {
        previewLoad_ = loadPatchFile(e.path);
      }
      previewOk_ = previewLoad_.ok();
      if (previewOk_) preview_ = *previewLoad_.patch;
    }
    info.push_back(text(" " + e.name + (e.factory ? "  [FACTORY, read-only]" : "  [USER]")) | bold | color(theme::amber));
    info.push_back(paragraph(" " + (e.factory ? "embedded:" + e.id : e.path.string())) | color(theme::dim));
    if (e.shadowsFactory)
      info.push_back(paragraph(" note: named like a factory preset, but it is a separate user file and does not replace the factory patch") | color(theme::amber));
    if (previewOk_) {
      if (!preview_.meta.description.empty()) info.push_back(paragraph(" " + preview_.meta.description) | color(theme::text));
      if (!preview_.meta.author.empty()) info.push_back(text(" by " + preview_.meta.author) | color(theme::dim));
      info.push_back(text(" " + std::to_string(preview_.modules.size()) + " modules, " + std::to_string(preview_.connections.size()) +
                          " connections, " + (preview_.global.mode == VoiceMode::Mono ? std::string("mono") : std::to_string(preview_.global.voices) + " voices")) |
                     color(theme::cyan));
      info.push_back(paragraph(" SIGNAL ORDER: " + describePatchSummary(preview_)) | color(theme::dim));
    } else {
      info.push_back(text(" CANNOT BE LOADED:") | bold | color(theme::red));
    }
    Elements diag;
    for (const auto& d : previewLoad_.diagnostics)
      if (d.severity != Diagnostic::Severity::Info || !previewOk_) diag.push_back(paragraph(" " + d.text()) | color(severityColor(d.severity)));
    const int total = static_cast<int>(diag.size());
    if (total > 0) info.push_back(text(" " + summary(previewLoad_)) | color(theme::amber));
    // Show as many as fit; the Tab panel lists all of them for the loaded patch.
    for (int i = 0; i < total && i < bodyRows - 6; ++i) info.push_back(diag[static_cast<size_t>(i)]);
    if (total > bodyRows - 6) info.push_back(text(" ... " + std::to_string(total - (bodyRows - 6)) + " more finding(s); load it and press Tab") | color(theme::dim));
  }
  return hbox({window(text(" PATCHES ") | color(theme::purple), vbox(std::move(list)) | flex) | color(theme::border) | size(WIDTH, EQUAL, 44),
               window(text(title) | color(theme::purple), vbox(std::move(info)) | flex) | color(theme::border) | flex});
}

bool Ui::handlePatches(const Event& e) {
  const int n = static_cast<int>(entries_.size());
  if (e == Event::Tab) { patchPanel_ = (patchPanel_ + 1) % 3; panelScroll_ = 0; return true; }
  if (e == Event::TabReverse) { patchPanel_ = (patchPanel_ + 2) % 3; panelScroll_ = 0; return true; }
  if (e == Event::CtrlE) {
    std::filesystem::path out;
    const std::string err = s_.exportCurrent(&out);
    if (err.empty()) s_.setStatus("Exported loaded patch JSON to " + out.string());
    else s_.setStatus("Export failed: " + err, true);
    return true;
  }
  if (patchPanel_ != 0) {
    if (e == Event::ArrowUp) { panelScroll_ = std::max(0, panelScroll_ - 1); return true; }
    if (e == Event::ArrowDown) { ++panelScroll_; return true; }
    if (e == Event::PageUp) { panelScroll_ = std::max(0, panelScroll_ - 8); return true; }
    if (e == Event::PageDown) { panelScroll_ += 8; return true; }
  }
  if (e == Event::ArrowUp && n > 0) { patchSel_ = (patchSel_ + n - 1) % n; return true; }
  if (e == Event::ArrowDown && n > 0) { patchSel_ = (patchSel_ + 1) % n; return true; }
  if (e == Event::PageUp && n > 0) { patchSel_ = std::max(0, patchSel_ - 8); return true; }
  if (e == Event::PageDown && n > 0) { patchSel_ = std::min(n - 1, patchSel_ + 8); return true; }
  if (e == Event::Return && n > 0) {
    if (s_.loadEntry(entries_[static_cast<size_t>(patchSel_)])) {
      selModule_ = selParam_ = 0;
      armed_ = false;
      panelScroll_ = 0;
      if (s_.lastLoad().count(Diagnostic::Severity::Warning) == 0)
        s_.setStatus("Loaded " + s_.patch().meta.name + " [" + s_.identity().kindName() + "] - play a key!");
    } else {
      patchPanel_ = 1;  // a failed load: show why (the previous patch is still in the rack)
      panelScroll_ = 0;
    }
    return true;
  }
  if ((e == Event::Delete || e == Event::Backspace) && n > 0) {
    if (entries_[static_cast<size_t>(patchSel_)].factory) s_.setStatus("Factory patches cannot be deleted", true);
    else modal_ = ModalKind::ConfirmPatch;
    return true;
  }
  return false;
}

}  // namespace nocturne::tui
