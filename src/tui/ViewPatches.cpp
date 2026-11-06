#include <algorithm>

#include "patch/PatchIO.hpp"
#include "patch/Presets.hpp"
#include "tui/Ui.hpp"
#include "tui/Widgets.hpp"

namespace nocturne::tui {

using namespace ftxui;

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

  Elements list;
  for (int i = first; i < n && i < first + rows; ++i) {
    const PatchEntry& e = entries_[static_cast<size_t>(i)];
    const bool cur = i == patchSel_;
    const bool loaded = s_.config().lastPatch == e.key();
    Element row = hbox({text(cur ? " ▸ " : "   "), text(e.factory ? "◆ " : "● ") | color(e.factory ? theme::purple : theme::cyan),
                        text(fixedText(e.name, 22)), text(loaded ? "loaded" : "") | color(theme::green)});
    if (cur) row = row | inverted;
    list.push_back(row);
  }
  if (list.empty()) list.push_back(text(" no patches"));

  Elements info;
  if (n > 0) {
    const PatchEntry& e = entries_[static_cast<size_t>(patchSel_)];
    if (previewKey_ != e.key()) {
      previewKey_ = e.key();
      previewOk_ = false;
      if (e.factory) {
        preview_ = factoryPresets()[static_cast<size_t>(e.factoryIndex)].build();
        previewOk_ = true;
      } else {
        auto res = loadPatchFile(e.path);
        if (res.patch) { preview_ = *res.patch; previewOk_ = true; }
      }
    }
    info.push_back(text(" " + e.name) | bold | color(theme::amber));
    info.push_back(text(e.factory ? " factory patch (read-only)" : " " + e.path.string()) | color(theme::dim));
    info.push_back(text(" "));
    if (previewOk_) {
      if (!preview_.meta.description.empty()) info.push_back(paragraph(" " + preview_.meta.description) | color(theme::text));
      if (!preview_.meta.author.empty()) info.push_back(text(" by " + preview_.meta.author) | color(theme::dim));
      info.push_back(text(" "));
      info.push_back(text(" " + std::to_string(preview_.modules.size()) + " modules, " + std::to_string(preview_.connections.size()) +
                          " connections, " + (preview_.global.mode == VoiceMode::Mono ? std::string("mono") : std::to_string(preview_.global.voices) + " voices")) |
                     color(theme::cyan));
      info.push_back(text(" ") );
      info.push_back(text(" SIGNAL ORDER") | color(theme::purple));
      info.push_back(paragraph(" " + describePatchSummary(preview_)) | color(theme::dim));
    } else {
      info.push_back(text(" cannot be read") | color(theme::red));
    }
  }
  return hbox({window(text(" PATCHES ") | color(theme::purple), vbox(std::move(list)) | flex) | color(theme::border) | size(WIDTH, EQUAL, 40),
               window(text(" PREVIEW ") | color(theme::purple), vbox(std::move(info)) | flex) | color(theme::border) | flex});
}

bool Ui::handlePatches(const Event& e) {
  const int n = static_cast<int>(entries_.size());
  if (e == Event::ArrowUp && n > 0) { patchSel_ = (patchSel_ + n - 1) % n; return true; }
  if (e == Event::ArrowDown && n > 0) { patchSel_ = (patchSel_ + 1) % n; return true; }
  if (e == Event::PageUp && n > 0) { patchSel_ = std::max(0, patchSel_ - 8); return true; }
  if (e == Event::PageDown && n > 0) { patchSel_ = std::min(n - 1, patchSel_ + 8); return true; }
  if (e == Event::Return && n > 0) {
    if (s_.loadEntry(entries_[static_cast<size_t>(patchSel_)])) {
      selModule_ = selParam_ = 0;
      armed_ = false;
      s_.setStatus("Loaded " + s_.patch().meta.name + " - play a key!");
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
