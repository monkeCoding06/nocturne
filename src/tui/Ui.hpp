#pragma once
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "app/Session.hpp"
#include "patch/Patch.hpp"

namespace nocturne::tui {

enum class View { Rack, Bay, Play, Patches, Settings, Help };

enum class ModalKind {
  None,
  Numeric,        // direct numeric entry for a parameter
  WireAmount,     // numeric entry for a connection amount
  SaveName,       // patch name
  AddModule,      // pick a module type
  ConfirmModule,  // remove module?
  ConfirmPatch,   // delete patch file?
  ConfirmNew,     // discard unsaved changes?
};

struct Endpoint {
  int module = 0;
  std::string module_name;
  const PortSpec* port = nullptr;
};

// The whole terminal interface: rendering and keyboard handling. It only talks to
// the Session (patch model + engine); it never touches DSP state directly.
class Ui {
 public:
  explicit Ui(Session& session);

  ftxui::Element render();
  bool onEvent(const ftxui::Event& e);
  bool wantsQuit() const { return quit_; }
  bool textEntryActive() const { return modal_ != ModalKind::None; }  // typed keys are text, not notes

 private:
  // ---- frame ----
  ftxui::Element header();
  ftxui::Element statusStrip();
  ftxui::Element footer(const std::string& hints);
  ftxui::Element modalOverlay();
  void clampSelection();

  // ---- views (ViewRack.cpp ...) ----
  ftxui::Element renderRack(int width, int height);
  ftxui::Element renderBay(int width, int height);
  ftxui::Element renderPlay(int width, int height);
  ftxui::Element renderPatches(int width, int height);
  ftxui::Element renderSettings(int width, int height);
  ftxui::Element renderHelp(int width, int height);
  ftxui::Element moduleCard(int index, int width, bool selected);
  ftxui::Element piano(int baseOctave, bool upperRow);

  bool handleGlobal(const ftxui::Event& e);
  bool handleRack(const ftxui::Event& e);
  bool handleBay(const ftxui::Event& e);
  bool handlePatches(const ftxui::Event& e);
  bool handleSettings(const ftxui::Event& e);
  bool handleModal(const ftxui::Event& e);

  void setView(View v);
  void openNumeric(ModalKind kind, int a, int b);
  void openSave();
  void openAddModule();
  void refreshPatches();
  void refreshDevices();
  void adjustSetting(int dir, bool fine);
  void activateSetting();
  void confirmModal();
  std::vector<Endpoint> sources() const;
  std::vector<Endpoint> destinations() const;
  std::string describePatchSummary(const Patch& p) const;

  Session& s_;
  View view_ = View::Rack;
  bool quit_ = false;

  // rack
  int selModule_ = 0, selParam_ = 0;
  // bay
  int panel_ = 0, srcSel_ = 0, dstSel_ = 0, connSel_ = 0;
  bool armed_ = false;
  Endpoint armedSrc_;
  // patches
  std::vector<PatchEntry> entries_;
  int patchSel_ = 0;
  std::string previewKey_;
  Patch preview_;
  bool previewOk_ = false;
  PatchLoadResult previewLoad_;
  int patchPanel_ = 0;     // right side of F4: 0 preview, 1 diagnostics, 2 loaded JSON
  int panelScroll_ = 0;
  // settings
  int setSel_ = 0;
  std::vector<DeviceInfo> devices_;
  // modal
  ModalKind modal_ = ModalKind::None;
  std::string modalText_, modalError_;
  int modalA_ = 0, modalB_ = 0, modalPick_ = 0;
  std::vector<const ModuleSpec*> addList_;
  int termW_ = 100, termH_ = 30;
};

}  // namespace nocturne::tui
