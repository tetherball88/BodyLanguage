#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <RE/Skyrim.h>

namespace BodyLanguage {

    // Must be called at kDataLoaded (after ScanAndGeneratePresetDescriptions).
    // Loads the per-body-type description JSONs and the parts phrase tables.
    void InitBodyDescriptions(const std::filesystem::path& dataRoot);

    // Reloads the RSV normal map hash cache from disk.
    // Safe to call from a background thread after ScanRsvNormalMaps completes.
    void ReloadRsvHashes(const std::filesystem::path& dataRoot);

    // Reloads the RSV diffuse hash cache from disk.
    // Safe to call from a background thread after ScanRsvDiffuseMaps completes.
    void ReloadRsvDiffuseHashes(const std::filesystem::path& dataRoot);

    // Builds a body description string for actor.
    // mode == "short" : returns just the preset name (or "unknown preset").
    // mode == "full"  : returns a longer sentence-form description.
    std::string GetBodyDescription(RE::Actor* actor, std::string_view mode);

}  // namespace BodyLanguage
