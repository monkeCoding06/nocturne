#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "patch/Patch.hpp"
#include "patch/PatchIO.hpp"

namespace nocturne {

// A factory preset. The embedded JSON text (generated at build time from
// presets/<id>.json) is the single source of truth; everything else is derived.
struct PresetInfo {
  std::string id;      // file name stem, e.g. "euro_hoover"
  std::string name;    // display name (meta.name of the JSON; the id if it is unreadable)
  std::string source;  // e.g. "embedded:euro_hoover (presets/euro_hoover.json)"
  std::string json;    // exact embedded JSON text
  PatchLoadResult load;  // result of parsing `json`, including all diagnostics

  // The parsed patch. Throws std::runtime_error with the diagnostics if the
  // embedded asset is corrupt: a factory preset never falls back to another one.
  Patch build() const;
};

// All factory patches in menu order (the first is the startup patch).
const std::vector<PresetInfo>& factoryPresets();
const PresetInfo* findFactoryPreset(const std::string& id);

// Where the on-disk presets/ directory is looked for, in order:
// $NOCTURNE_PRESET_DIR, <exe dir>/presets, <exe dir>/../share/nocturne/presets.
// Returns an empty path when none exists. The disk copy is never used to
// replace embedded data; it is only compared against it.
std::filesystem::path findPresetDirectory();

struct PresetDiskCheck {
  std::filesystem::path dir;               // empty = no on-disk presets directory found
  std::vector<std::string> mismatches;     // one line per difference (embedded wins)
};
PresetDiskCheck checkPresetsOnDisk();

}  // namespace nocturne
