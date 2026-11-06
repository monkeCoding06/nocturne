#pragma once
#include <string>
#include <vector>

#include "patch/Patch.hpp"

namespace nocturne {

struct PresetInfo {
  std::string id;    // file name stem, e.g. "init"
  std::string name;  // display name
  Patch (*build)();
};

// Factory patches. Each one is a real signal graph, built with the same
// editing functions the UI uses.
const std::vector<PresetInfo>& factoryPresets();

}  // namespace nocturne
