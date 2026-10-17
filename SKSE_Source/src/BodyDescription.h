#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "RE/Skyrim.h"

namespace BodyLanguage {

    // Called at kDataLoaded to load BodyLanguage.ini, geometryThresholds.json,
    // FemaleParts.json, and MaleParts.json.
    void InitBodyDescriptions(const std::filesystem::path& dataRoot);

    // Clears the per-actor runtime description cache (called on game load / new game).
    void ClearActorDescriptionCache();

    // Evaluates the actor's 3D body landmark rings, SKEE morphs, XPMSE bone scales,
    // and _msn.dds normal map Laplacian roughness to build a natural language description.
    // Results are cached per actor (FormID) with state & TTL validation.
    // mode == "short" : returns a concise 1-clause summary.
    // mode == "full"  : returns a complete multi-region anatomical description.
    std::string GetBodyDescription(RE::Actor* actor, std::string_view mode);

    // Returns a JSON string {"hasGenitals": bool, "size": "XS..XL", "addon": "..."} for SkyrimNet.
    std::string GetActorGenitalsJson(RE::Actor* actor);

}  // namespace BodyLanguage
