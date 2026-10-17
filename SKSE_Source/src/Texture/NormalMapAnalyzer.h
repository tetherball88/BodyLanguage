#pragma once

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Geometry/BodyTemplateCache.h"

namespace BodyLanguage {

    class NormalMapAnalyzer {
    public:
        static NormalMapAnalyzer& GetInstance();

        // Loads SmoothBaseline and RippedBaseline from Data/SKSE/Plugins/BodyLanguage.ini
        void LoadConfig(const std::filesystem::path& dataRoot);

        // Resolves the actor's active body normal map (_msn.dds) path in priority order:
        // 1. Live 3D body geometry shader texture set (slot kNormal)
        // 2. Naked TESObjectARMA -> BGSTextureSet (slot kNormal)
        // 3. Fallback BSShaderTextureSet path from the base .nif
        std::string ResolveActiveNormalMapPath(
            RE::Actor* actor,
            RE::TESObjectARMA* nakedArma,
            bool isFemale,
            std::string_view fallbackNifMsn) const;

        // Returns normalized muscularity texture score S_tex_tone in [0.0, 1.0].
        // Thread-safe and cached by normalized texture path.
        float EvaluateNormalMapTone(
            std::string_view msnPath,
            const UVRect& abdomenUV,
            const UVRect& deltoidUV);

    private:
        NormalMapAnalyzer() = default;

        float m_smoothBaseline{0.018f};
        float m_rippedBaseline{0.045f};
        mutable std::shared_mutex m_cacheMutex;
        std::unordered_map<std::string, float> m_scoreCache;
    };

}  // namespace BodyLanguage

