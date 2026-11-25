#pragma once

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "Geometry/BodyTemplateCache.h"

namespace BodyLanguage {

    struct ClassifiedGenitals {
        bool hasGenitals{false};
        bool isErect{false};
        float lengthCm{0.0f};
        float girthCm{0.0f};
        std::string sizeTier;
        std::string state;
        std::string addonKey;
    };

    struct ActorDescriptionPair {
        std::string full;
        std::string shortDesc;
        std::string genitalsJson{"{\"hasGenitals\":false}"};
        std::string bodyPartsJson{"{\"valid\":false}"};
    };

    class PhysiqueClassifier {
    public:
        static PhysiqueClassifier& GetInstance();

        // Loads geometryThresholds.json, FemaleParts.json, MaleParts.json, BeastParts.json, and optional tngAddons.json
        void LoadConfigs(const std::filesystem::path& dataRoot);

        // Builds static "full" and "short" descriptions for an actor in beast form (werewolf, vampire_lord, werebear).
        ActorDescriptionPair BuildBeastBothDescriptions(
            BeastFormType beastForm,
            bool isFemale) const;

        // Computes unified Muscularity vs. Body Fat Index I_tone in [-1.0, +1.0] purely from 3D geometry and normal map
        static float ComputeToneIndex(
            float geoToneSignal,
            float texToneScore);

        // Classifies regional anatomical categories once and returns the region->tier map.
        std::unordered_map<std::string, std::string> ClassifyRegions(
            const ActorBodyMetrics& metrics,
            float texToneScore) const;

        // Builds full and short descriptions from pre-classified regions (avoids re-classifying).
        ActorDescriptionPair BuildBothDescriptionsFromRegions(
            const ActorBodyMetrics& metrics,
            float toneIndex,
            const std::unordered_map<std::string, std::string>& kw) const;

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

        // Classifies TNG/SOS genital metrics into size tier, effective cm, state, and addon key.
        ClassifiedGenitals ClassifyGenitals(const ActorGenitalMetrics& genitalMetrics) const;

        // Builds a JSON string {"hasGenitals": bool, "size": "XS..XXXL", "state": "...", "lengthCm": ..., "girthCm": ..., "addon": "..."}.
        static std::string BuildGenitalsJson(const ClassifiedGenitals& classified);
        std::string BuildGenitalsJson(const ActorGenitalMetrics& genitalMetrics) const;

    private:
        PhysiqueClassifier() = default;

        mutable std::shared_mutex m_configMutex;
        nlohmann::json m_thresholds;
        nlohmann::json m_femaleParts;
        nlohmann::json m_maleParts;
        nlohmann::json m_beastParts;
        nlohmann::json m_genitalAddons;
    };

}  // namespace BodyLanguage

