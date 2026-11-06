#pragma once
#include <cstddef>

namespace nocturne::embedded {

// One preset file compiled into the executable. The table is generated at build
// time from presets/*.json (see cmake/EmbedPresets.cmake).
struct Asset {
  const char* id;    // file name stem, e.g. "euro_hoover"
  const char* data;  // exact bytes of presets/<id>.json
  unsigned long size;
};

extern const Asset kPresetAssets[];
extern const unsigned long kPresetAssetCount;

}  // namespace nocturne::embedded
