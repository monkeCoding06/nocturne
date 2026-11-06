#include "input/KeyState.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace nocturne {

namespace {

#ifdef _WIN32

// Set-1 scan codes of the piano keys per slot (physical positions of a US keyboard).
// Scan codes are mapped to virtual keys through the *current* layout at poll time, so
// the physical key under the "Z" position is read whether it is labelled Z, Y or W.
constexpr unsigned char kScan[kPianoSlots] = {
    // lower row: Z S X D C V G B H N J M , L . ; /
    0x2C, 0x1F, 0x2D, 0x20, 0x2E, 0x2F, 0x22, 0x30, 0x23, 0x31, 0x24, 0x32, 0x33, 0x26, 0x34, 0x27, 0x35,
    // upper row: Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P
    0x10, 0x03, 0x11, 0x04, 0x12, 0x13, 0x06, 0x14, 0x07, 0x15, 0x08, 0x16, 0x17, 0x0A, 0x18, 0x0B, 0x19};

class WindowsKeyState final : public KeyStateSource {
 public:
  WindowsKeyState() {
    DWORD mode = 0;
    const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    consoleInput_ = in != INVALID_HANDLE_VALUE && in != nullptr && GetConsoleMode(in, &mode) != 0;
    if (consoleInput_ && GetConsoleWindow() != nullptr) {
      ok_ = true;
      collectAncestors();
    }
  }

  bool available() const override { return ok_; }

  std::string description() const override {
    if (ok_) return "physical key state (GetAsyncKeyState); works only while this terminal window has focus";
    if (!consoleInput_) return "LIVE needs an interactive Windows console (stdin is not a console)";
    return "LIVE needs a console window (none found, e.g. remote/SSH session)";
  }

  void sample(KeySample& out) override {
    out = KeySample{};
    // Always read the state so the "pressed since last call" bits are consumed even
    // while unfocused; otherwise keys typed elsewhere would play phantom notes later.
    for (int i = 0; i < kPianoSlots; ++i) {
      const UINT vk = MapVirtualKeyW(kScan[i], MAPVK_VSC_TO_VK_EX);
      if (vk == 0) continue;
      const SHORT st = GetAsyncKeyState(static_cast<int>(vk));
      const bool d = (st & 0x8000) != 0;
      out.down[static_cast<size_t>(i)] = d;
      out.tapped[static_cast<size_t>(i)] = !d && (st & 1) != 0;
    }
    out.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    out.blocked = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 || (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
                  (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
    out.focused = hasFocus();
    if (!out.focused) {
      out.down.reset();
      out.tapped.reset();
    }
  }

 private:
  // Under conhost the console window is the foreground window. Under Windows Terminal
  // (and other ConPTY hosts) GetConsoleWindow() is a hidden pseudo window owned by the
  // terminal window, and the foreground window belongs to an ancestor process.
  bool hasFocus() const {
    const HWND fg = GetForegroundWindow();
    if (!fg) return false;
    const HWND con = GetConsoleWindow();
    if (con && (fg == con || GetAncestor(con, GA_ROOTOWNER) == fg)) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid != 0 && (pid == GetCurrentProcessId() || ancestors_.count(pid) != 0);
  }

  void collectAncestors() {
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    struct P { DWORD parent; std::string exe; };
    std::map<DWORD, P> procs;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof pe;
    if (Process32First(snap, &pe)) {
      do {
        std::string exe = pe.szExeFile;
        std::transform(exe.begin(), exe.end(), exe.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        procs[pe.th32ProcessID] = {pe.th32ParentProcessID, exe};
      } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    DWORD pid = GetCurrentProcessId();
    for (int depth = 0; depth < 16; ++depth) {
      const auto it = procs.find(pid);
      if (it == procs.end() || it->second.parent == 0) break;
      pid = it->second.parent;
      const auto parent = procs.find(pid);
      if (parent == procs.end()) break;
      // explorer.exe owns the desktop and taskbar: never treat it as "our" window.
      if (parent->second.exe == "explorer.exe") break;
      ancestors_.insert(pid);
    }
  }

  bool consoleInput_ = false;
  bool ok_ = false;
  std::set<DWORD> ancestors_;
};

std::unique_ptr<KeyStateSource> makeImpl() { return std::make_unique<WindowsKeyState>(); }

#else

class UnavailableKeyState final : public KeyStateSource {
 public:
  bool available() const override { return false; }
  std::string description() const override {
    return "LIVE needs key-release events, which this terminal does not provide "
           "(kitty keyboard protocol is not implemented)";
  }
  void sample(KeySample& out) override { out = KeySample{}; }
};

std::unique_ptr<KeyStateSource> makeImpl() { return std::make_unique<UnavailableKeyState>(); }

#endif

}  // namespace

std::unique_ptr<KeyStateSource> makePlatformKeySource() { return makeImpl(); }

#ifdef _WIN32
namespace {
using TimePeriodFn = UINT(WINAPI*)(UINT);
TimePeriodFn timerFn(const char* name) {
  HMODULE m = LoadLibraryA("winmm.dll");
  return m ? reinterpret_cast<TimePeriodFn>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
}
}  // namespace
void beginFineTimer() { if (auto f = timerFn("timeBeginPeriod")) f(1); }
void endFineTimer() { if (auto f = timerFn("timeEndPeriod")) f(1); }
#else
void beginFineTimer() {}
void endFineTimer() {}
#endif

}  // namespace nocturne
