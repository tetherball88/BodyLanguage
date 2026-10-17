#pragma once

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "Geometry/BodyTemplateCache.h"

namespace BodyLanguage {

    struct ActorDescriptionPair {
        std::string full;
        std::string shortDesc;
        std::string genitalsJson{"{\"hasGenitals\":false}"};
    };

    class PhysiqueClassifier {
    public:
        static PhysiqueClassifier& GetInstance();

        // Loads geometryThresholds.json, FemaleParts.json, MaleParts.json, and tngAddons.json
        void LoadConfigs(const std::filesystem::path& dataRoot);

        // Computes unified Muscularity vs. Body Fat Index I_tone in [-1.0, +1.0] purely from 3D geometry and normal map
        static float ComputeToneIndex(
            float geoToneSignal,
            float texToneScore);

        // Classifies regional anatomical categories once and builds both "full" and "short" descriptions
        // for the actor's current context (clothed vs. naked from metrics.isNaked).
        ActorDescriptionPair BuildBothDescriptions(
            const ActorBodyMetrics& metrics,
            float texToneScore) const;

        // Classifies regional anatomical categories and builds the final natural language description
        // for mode ("full" or "short") and context (clothed vs. naked from metrics.isNaked).
        std::string BuildDescription(
            const ActorBodyMetrics& metrics,
            float texToneScore,
            std::string_view mode) const;

        // Builds a JSON string {"hasGenitals": bool, "size": "XS..XL", "addon": "..."} for SkyrimNet.
        std::string BuildGenitalsJson(const ActorGenitalMetrics& genitalMetrics) const;

    private:
        PhysiqueClassifier() = default;

        mutable std::shared_mutex m_configMutex;
        nlohmann::json m_thresholds;
        nlohmann::json m_femaleParts;
        nlohmann::json m_maleParts;
        nlohmann::json m_genitalAddons;
    };

}  // namespace BodyLanguage

