#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "patch/Patch.hpp"

namespace nocturne {

struct PatchLoadResult {
  std::optional<Patch> patch;
  std::string error;                  // set when patch is empty
  std::vector<std::string> warnings;  // recoverable problems (dropped connections, unknown params...)
};

nlohmann::ordered_json patchToJson(const Patch& patch);
std::string patchToString(const Patch& patch);
PatchLoadResult patchFromJson(const nlohmann::ordered_json& j);
PatchLoadResult patchFromString(const std::string& text);

std::string savePatchFile(const Patch& patch, const std::filesystem::path& path);   // error or empty
PatchLoadResult loadPatchFile(const std::filesystem::path& path);

// "My Patch!" -> "my_patch"
std::string sanitizePatchFileName(const std::string& name);

}  // namespace nocturne
