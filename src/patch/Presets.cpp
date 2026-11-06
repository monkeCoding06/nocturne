#include "patch/Presets.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "patch/EmbeddedPresets.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace nocturne {

namespace fs = std::filesystem;

namespace {

fs::path executableDirectory() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof buf / sizeof buf[0]));
  if (n > 0 && n < sizeof buf / sizeof buf[0]) return fs::path(buf).parent_path();
  return {};
#else
  std::error_code ec;
  const fs::path p = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path() : p.parent_path();
#endif
}

std::string normalizeEol(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
  return s;
}

std::vector<PresetInfo> buildList() {
  std::vector<PresetInfo> list;
  for (unsigned long i = 0; i < embedded::kPresetAssetCount; ++i) {
    const embedded::Asset& a = embedded::kPresetAssets[i];
    PresetInfo info;
    info.id = a.id;
    info.json.assign(a.data, a.size);
    info.source = "embedded:" + info.id + " (presets/" + info.id + ".json)";
    info.load = patchFromString(info.json);
    info.load.source = info.source;
    info.name = info.load.ok() ? info.load.patch->meta.name : info.id;
    list.push_back(std::move(info));
  }
  return list;
}

}  // namespace

Patch PresetInfo::build() const {
  if (!load.ok()) {
    std::string msg = "factory preset '" + id + "' is corrupt (" + source + "):";
    for (const auto& d : load.diagnostics)
      if (d.severity == Diagnostic::Severity::Error) msg += "\n  " + d.text();
    throw std::runtime_error(msg);
  }
  return *load.patch;
}

const std::vector<PresetInfo>& factoryPresets() {
  static const std::vector<PresetInfo> list = buildList();
  return list;
}

const PresetInfo* findFactoryPreset(const std::string& id) {
  for (const auto& p : factoryPresets())
    if (p.id == id) return &p;
  return nullptr;
}

fs::path findPresetDirectory() {
  std::vector<fs::path> candidates;
  if (const char* env = std::getenv("NOCTURNE_PRESET_DIR"); env && *env) candidates.emplace_back(env);
  const fs::path exe = executableDirectory();
  if (!exe.empty()) {
    candidates.push_back(exe / "presets");
    candidates.push_back(exe.parent_path() / "share" / "nocturne" / "presets");
  }
  std::error_code ec;
  for (const auto& c : candidates)
    if (fs::is_directory(c, ec)) return c;
  return {};
}

PresetDiskCheck checkPresetsOnDisk() {
  PresetDiskCheck r;
  r.dir = findPresetDirectory();
  if (r.dir.empty()) return r;
  std::vector<std::string> known;
  for (const auto& p : factoryPresets()) {
    known.push_back(p.id);
    std::ifstream f(r.dir / (p.id + ".json"), std::ios::binary);
    if (!f) {
      r.mismatches.push_back(p.id + ".json missing on disk");
      continue;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (normalizeEol(ss.str()) != normalizeEol(p.json))
      r.mismatches.push_back(p.id + ".json on disk differs from the embedded copy");
  }
  std::error_code ec;
  for (const auto& de : fs::directory_iterator(r.dir, ec)) {
    if (!de.is_regular_file() || de.path().extension() != ".json") continue;
    if (std::find(known.begin(), known.end(), de.path().stem().string()) == known.end())
      r.mismatches.push_back(de.path().filename().string() + " on disk is not an embedded preset");
  }
  return r;
}

}  // namespace nocturne
