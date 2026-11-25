#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "Description/PhysiqueClassifier.h"
#include "Geometry/BodyTemplateCache.h"
#include "RE/Skyrim.h"

namespace BodyLanguage {

    // Complete evaluated body data snapshot for a single actor (cached per FormID).
    struct ActorBodySnapshot {
        bool valid{false};
        bool isFemale{true};
        bool isNaked{false};
        bool isExcluded{false};
        BeastFormType beastForm{BeastFormType::kNone};

        float heightScale{1.0f};
        float estimatedHeightCm{0.0f};

        // Real-world measurements in centimeters (scaled by actor height scale)
        float shoulderWidthCm{0.0f};
        float chestCm{0.0f};
        float underbustCm{0.0f};
        float waistCm{0.0f};
        float bellyCm{0.0f};
        float hipsCm{0.0f};
        float thighCm{0.0f};
        float calfCm{0.0f};
        float armCm{0.0f};

        // Raw evaluated geometry metrics and tone signals
        ActorBodyMetrics metrics{};
        float texToneScore{0.0f};
        float toneIndex{0.0f};

        // Unified regional size / classification tiers
        std::string frame;      // female: frame | male: stature_frame
        std::string shoulders;  // female: frame | male: shoulder_breadth
        std::string chest;      // female: breasts_volume | male: chest
        std::string breasts;    // female: breasts_volume | male: ""
        std::string waist;      // female: waist | male: midsection
        std::string belly;      // female: belly | male: midsection
        std::string butt;       // female: butt_hips | male: glutes
        std::string thighs;     // female: legs | male: legs
        std::string arms;       // female: arms | male: arms
        std::string condition;  // female/male: physique_condition

        // Classified TNG/SOS genital data (populated when naked and genitals are present)
        ClassifiedGenitals genitals{};

        // Lazily populated natural-language descriptions and JSON strings
        bool descriptionsBuilt{false};
        ActorDescriptionPair descriptions{};
    };

    // Called at kDataLoaded to load BodyLanguage.ini, geometryThresholds.json,
    // FemaleParts.json, MaleParts.json, and BeastParts.json.
    void InitBodyDescriptions(const std::filesystem::path& dataRoot);

    // Clears the per-actor runtime cache (called on game load / new game).
    void ClearActorDescriptionCache();

    // Invalidates a single actor's cache entry, or clears the entire cache if actor == nullptr.
    void InvalidateActorCache(RE::Actor* actor);

    // Returns true if the given actor or base NPC is listed under [Exclusions] in BodyLanguage.ini.
    bool IsActorExcluded(RE::Actor* actor);
    bool IsNpcExcluded(const RE::TESNPC* npc);

    // Evaluates (or retrieves from the 15s cache) the complete structured body data snapshot for an actor.
    // Set buildDescriptions = false to avoid prose generation overhead when only structured data is needed.
    ActorBodySnapshot EvaluateActorSnapshot(RE::Actor* actor, bool buildDescriptions = false);

    // Evaluates (or retrieves from cache) classified genital data for a naked actor.
    ClassifiedGenitals GetActorGenitals(RE::Actor* actor);

    // Evaluates the actor's 3D body landmark rings, SKEE morphs, XPMSE bone scales,
    // and _msn.dds normal map Laplacian roughness to build a natural language description.
    // Results are cached per actor (FormID) with state & TTL validation.
    // mode == "short" : returns a concise 1-clause summary.
    // mode == "full"  : returns a complete multi-region anatomical description.
    std::string GetBodyDescription(RE::Actor* actor, std::string_view mode);

    // Returns a comprehensive JSON string containing state, measurements_cm, measurements_in,
    // ratios, parts, and genitals for the actor.
    std::string GetActorDataJson(RE::Actor* actor);

    // Returns a JSON string {"hasGenitals": bool, "size": "XS..XXXL", "state": "...", "lengthCm": ..., "girthCm": ..., "addon": "..."}.
    std::string GetActorGenitalsJson(RE::Actor* actor);

    // Returns a JSON string {"valid": bool, "isFemale": bool, "breasts": "...", "butt": "...", "thighs": "..."}.
    std::string GetActorBodyPartsJson(RE::Actor* actor);

    // Returns a JSON string {"height": float} with the actor's effective height scale.
    std::string GetActorHeightJson(RE::Actor* actor);

}  // namespace BodyLanguage
