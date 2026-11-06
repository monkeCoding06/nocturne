#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "patch/Patch.hpp"

namespace nocturne {

// One finding while reading a patch. Errors are fatal (no patch is produced);
// warnings mean the patch loaded but differs from what the file asked for;
// infos are harmless notes (e.g. a newer parameter missing from an old file).
struct Diagnostic {
  enum class Severity { Info, Warning, Error };
  Severity severity = Severity::Warning;
  std::string code;     // stable identifier, e.g. "unknown-param"
  std::string where;    // module / connection / file the finding refers to (may be empty)
  std::string message;
  std::string text() const;  // "ERROR [unknown-module-type] X1: ..."
};

struct PatchLoadResult {
  std::optional<Patch> patch;  // empty when any Error diagnostic exists
  std::string source;          // human description of where the text came from
  std::vector<Diagnostic> diagnostics;  // ALL findings, in discovery order
  std::string error;                    // first fatal message (empty on success)
  std::vector<std::string> warnings;    // text of every Warning diagnostic

  bool ok() const { return patch.has_value(); }
  size_t count(Diagnostic::Severity s) const;
  void add(Diagnostic::Severity s, std::string code, std::string where, std::string message);
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
