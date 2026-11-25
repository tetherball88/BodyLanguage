#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace RE {
    class Actor;
}

/**
 * BodyLanguage Public C/C++ API — loaded at runtime via GetModuleHandleW + GetProcAddress.
 *
 * Drop this single header into any SKSE plugin project to query real-time 3D body measurements,
 * anthropometric ratios, body-part size tiers, genital metrics, height, naked/beast state, and
 * natural-language descriptions from BodyLanguage.dll — with or without SkyrimNet installed.
 *
 * ABI Guarantee:
 *   All exported functions use pure C ABI types (POD structs, fixed char arrays, caller-owned
 *   buffers) and are noexcept + SEH-protected. Safe to call across different MSVC toolsets and
 *   CRT linkage modes (/MT vs /MD).
 *
 * Quick Start (C++):
 * @code
 *   // In SKSE kPostPostLoad or kDataLoaded:
 *   if (BodyLanguage::API::Load()) {
 *       SKSE::log::info("BodyLanguage API v{} loaded (SkyrimNet active: {})",
 *           BodyLanguage::API::GetVersion(),
 *           BodyLanguage::API::IsSkyrimNetActive());
 *   }
 *
 *   // Anywhere at runtime (thread-safe, backed by a 15-second per-actor cache):
 *   if (auto data = BodyLanguage::API::GetActorData(actor); data && data->valid) {
 *       // Size tiers: data->parts.breasts ("large"), data->parts.butt ("average"), ...
 *       // Measurements: data->measurements.chestCm, data->measurements.waistCm, ...
 *       // Ratios: data->ratios.waistToHipRatio, data->ratios.toneIndex, ...
 *   }
 * @endcode
 */

inline constexpr std::uint32_t kBodyLanguageApiVersion = 1;

enum BodyLanguage_BeastForm : std::uint8_t {
    kBodyLanguageBeastNone = 0,
    kBodyLanguageBeastWerewolf = 1,
    kBodyLanguageBeastVampireLord = 2,
    kBodyLanguageBeastWerebear = 3
};

struct BodyLanguage_Measurements {
    float shoulderWidthCm{0.0f};    // Bideltoid shoulder span (cm)
    float chestCm{0.0f};            // Bust / chest circumference (cm)
    float underbustCm{0.0f};        // Ribcage underbust circumference (cm)
    float waistCm{0.0f};            // Natural waist circumference (cm)
    float bellyCm{0.0f};            // Mid-belly / umbilicus circumference (cm)
    float hipsCm{0.0f};             // Hips / glutes circumference (cm)
    float thighCm{0.0f};            // Upper thigh circumference (cm)
    float calfCm{0.0f};             // Calf circumference (cm)
    float armCm{0.0f};              // Upper arm circumference (cm)
    float heightScale{1.0f};        // Effective actor height scale (1.0 = 182.88 cm)
    float estimatedHeightCm{0.0f};  // Estimated standing height (cm)
};

struct BodyLanguage_Ratios {
    float bodyMassIndex{0.0f};        // Systemic body mass index (mean height-normalized girths)
    float shoulderToHeight{0.0f};     // Shoulder width / reference height
    float vTaperRatio{1.25f};         // Shoulder width / waist width
    float waistToHipRatio{0.75f};     // Waist girth / hip girth (WHR)
    float bustToUnderbust{1.0f};      // Chest girth / underbust girth
    float hipsToHeight{0.0f};         // Hip girth / reference height
    float upperThighGirth{0.0f};      // Upper thigh girth / reference height
    float calfGirth{0.0f};            // Calf girth / reference height
    float upperArmGirth{0.0f};        // Upper arm girth / reference height
    float sagittalBellyDrop{0.0f};    // Positive = tucked/flat, negative = protruding
    float flankFlareRatio{1.0f};      // Belly width / waist width
    float geoToneSignal{0.0f};        // Geometric muscularity signal [-1.0, +1.0]
    float texToneScore{0.0f};         // Normal-map Laplacian muscle sharpness [0.0, 1.0]
    float toneIndex{0.0f};            // Combined muscularity vs. body fat index [-1.0, +1.0]
    float pregnancyBellyScore{0.0f};  // Isolated anterior belly dome score [0.0, 1.0]
};

struct BodyLanguage_BodyParts {
    char frame[32]{};      // Female: small..massive | Male: slight..massive
    char shoulders[32]{};  // Female: small..massive | Male: narrow..expansive
    char chest[32]{};      // Female: flat..impossible | Male: flat/average/broad/sculpted/heavyset
    char breasts[32]{};    // Female: flat/small/average/large/huge/impossible | Male: ""
    char waist[32]{};      // Female: tiny..thick | Male: ripped/tapered/straight/thick/potbellied/distended
    char belly[32]{};      // Female: ripped/toned/flat/soft/chubby/large/huge/pregnant_* | Male: midsection tier
    char butt[32]{};       // Female: flat..huge(_muscular) | Male: flat/firm/large/huge(_muscular)
    char thighs[32]{};     // Female: thin..colossal(_muscular) | Male: spindly/athletic/pillars/immense(_soft)
    char arms[32]{};       // Female: thin..colossal | Male: thin/athletic/brawny/colossal
    char condition[32]{};  // Overall physique archetype (e.g. slender, curvy, athletic, muscular, plump, ...)
};

struct BodyLanguage_Genitals {
    bool hasGenitals{false};
    bool isErect{false};
    float lengthCm{0.0f};
    float girthCm{0.0f};
    char sizeTier[8]{};  // "XS", "S", "M", "L", "XL", "XXL", "XXXL", or ""
    char state[16]{};    // "flaccid", "hard", or ""
    char addon[64]{};    // Resolved TNG/SOS addon key
};

struct BodyLanguage_ActorData {
    std::uint32_t structSize{sizeof(BodyLanguage_ActorData)};
    bool valid{false};
    bool isFemale{true};
    bool isNaked{false};
    bool isExcluded{false};
    std::uint8_t beastForm{kBodyLanguageBeastNone};
    BodyLanguage_Measurements measurements{};
    BodyLanguage_Ratios ratios{};
    BodyLanguage_BodyParts parts{};
    BodyLanguage_Genitals genitals{};
};

#ifndef BODYLANGUAGE_BUILD_DLL

namespace BodyLanguage::API {

    namespace detail {

        using GetApiVersion_t = std::uint32_t (*)() noexcept;
        using IsSkyrimNetActive_t = bool (*)() noexcept;
        using GetActorData_t = bool (*)(RE::Actor*, BodyLanguage_ActorData*) noexcept;
        using GetActorGenitals_t = bool (*)(RE::Actor*, BodyLanguage_Genitals*) noexcept;
        using IsActorNaked_t = bool (*)(RE::Actor*) noexcept;
        using IsActorExcluded_t = bool (*)(RE::Actor*) noexcept;
        using GetActorBeastForm_t = std::uint8_t (*)(RE::Actor*) noexcept;
        using GetActorHeightScale_t = float (*)(RE::Actor*) noexcept;
        using GetActorJson_t = std::size_t (*)(RE::Actor*, char*, std::size_t) noexcept;
        using GetBodyDescription_t = std::size_t (*)(RE::Actor*, bool, char*, std::size_t) noexcept;
        using InvalidateActorCache_t = void (*)(RE::Actor*) noexcept;

        struct Functions {
            GetApiVersion_t getApiVersion{nullptr};
            IsSkyrimNetActive_t isSkyrimNetActive{nullptr};
            GetActorData_t getActorData{nullptr};
            GetActorGenitals_t getActorGenitals{nullptr};
            IsActorNaked_t isActorNaked{nullptr};
            IsActorExcluded_t isActorExcluded{nullptr};
            GetActorBeastForm_t getActorBeastForm{nullptr};
            GetActorHeightScale_t getActorHeightScale{nullptr};
            GetActorJson_t getActorDataJson{nullptr};
            GetActorJson_t getActorBodyPartsJson{nullptr};
            GetActorJson_t getActorGenitalsJson{nullptr};
            GetBodyDescription_t getBodyDescription{nullptr};
            InvalidateActorCache_t invalidateActorCache{nullptr};
            bool loaded{false};
        };

        inline Functions& GetFunctions() noexcept {
            static Functions fns{};
            return fns;
        }

        template <typename Fn>
        inline Fn Resolve(HMODULE hMod, const char* name) noexcept {
            return reinterpret_cast<Fn>(GetProcAddress(hMod, name));
        }

        inline std::string CallBufferFn(GetActorJson_t fn, RE::Actor* actor, std::size_t initialCap = 2048) {
            if (!fn || !actor) {
                return {};
            }
            std::string buf(initialCap, '\0');
            const std::size_t written = fn(actor, buf.data(), buf.size());
            if (written == 0) {
                return {};
            }
            buf.resize(written);
            return buf;
        }

    }  // namespace detail

    // Resolves exported functions from BodyLanguage.dll (must be called at kPostPostLoad or later).
    // Returns true if BodyLanguage.dll is loaded and compatible.
    inline bool Load() noexcept {
        auto& fns = detail::GetFunctions();
        if (fns.loaded) {
            return true;
        }

        HMODULE hMod = GetModuleHandleW(L"BodyLanguage.dll");
        if (!hMod) {
            return false;
        }

        fns.getApiVersion = detail::Resolve<detail::GetApiVersion_t>(hMod, "BodyLanguage_GetApiVersion");
        if (!fns.getApiVersion) {
            return false;
        }

        fns.isSkyrimNetActive = detail::Resolve<detail::IsSkyrimNetActive_t>(hMod, "BodyLanguage_IsSkyrimNetActive");
        fns.getActorData = detail::Resolve<detail::GetActorData_t>(hMod, "BodyLanguage_GetActorData");
        fns.getActorGenitals = detail::Resolve<detail::GetActorGenitals_t>(hMod, "BodyLanguage_GetActorGenitals");
        fns.isActorNaked = detail::Resolve<detail::IsActorNaked_t>(hMod, "BodyLanguage_IsActorNaked");
        fns.isActorExcluded = detail::Resolve<detail::IsActorExcluded_t>(hMod, "BodyLanguage_IsActorExcluded");
        fns.getActorBeastForm = detail::Resolve<detail::GetActorBeastForm_t>(hMod, "BodyLanguage_GetActorBeastForm");
        fns.getActorHeightScale = detail::Resolve<detail::GetActorHeightScale_t>(hMod, "BodyLanguage_GetActorHeightScale");
        fns.getActorDataJson = detail::Resolve<detail::GetActorJson_t>(hMod, "BodyLanguage_GetActorDataJson");
        fns.getActorBodyPartsJson = detail::Resolve<detail::GetActorJson_t>(hMod, "BodyLanguage_GetActorBodyPartsJson");
        fns.getActorGenitalsJson = detail::Resolve<detail::GetActorJson_t>(hMod, "BodyLanguage_GetActorGenitalsJson");
        fns.getBodyDescription = detail::Resolve<detail::GetBodyDescription_t>(hMod, "BodyLanguage_GetBodyDescription");
        fns.invalidateActorCache = detail::Resolve<detail::InvalidateActorCache_t>(hMod, "BodyLanguage_InvalidateActorCache");

        fns.loaded = (fns.getActorData != nullptr);
        return fns.loaded;
    }

    inline bool IsLoaded() noexcept {
        return detail::GetFunctions().loaded;
    }

    inline std::uint32_t GetVersion() noexcept {
        const auto& fns = detail::GetFunctions();
        return fns.getApiVersion ? fns.getApiVersion() : 0;
    }

    inline bool IsSkyrimNetActive() noexcept {
        const auto& fns = detail::GetFunctions();
        return fns.isSkyrimNetActive ? fns.isSkyrimNetActive() : false;
    }

    inline std::optional<BodyLanguage_ActorData> GetActorData(RE::Actor* actor) noexcept {
        const auto& fns = detail::GetFunctions();
        if (!fns.getActorData || !actor) {
            return std::nullopt;
        }
        BodyLanguage_ActorData data{};
        if (!fns.getActorData(actor, &data)) {
            return std::nullopt;
        }
        return data;
    }

    inline std::optional<BodyLanguage_Genitals> GetActorGenitals(RE::Actor* actor) noexcept {
        const auto& fns = detail::GetFunctions();
        if (!fns.getActorGenitals || !actor) {
            return std::nullopt;
        }
        BodyLanguage_Genitals data{};
        if (!fns.getActorGenitals(actor, &data)) {
            return std::nullopt;
        }
        return data;
    }

    inline bool IsActorNaked(RE::Actor* actor) noexcept {
        const auto& fns = detail::GetFunctions();
        return (fns.isActorNaked && actor) ? fns.isActorNaked(actor) : false;
    }

    inline bool IsActorExcluded(RE::Actor* actor) noexcept {
        const auto& fns = detail::GetFunctions();
        return (fns.isActorExcluded && actor) ? fns.isActorExcluded(actor) : false;
    }

    inline std::uint8_t GetActorBeastForm(RE::Actor* actor) noexcept {
        const auto& fns = detail::GetFunctions();
        return (fns.getActorBeastForm && actor) ? fns.getActorBeastForm(actor) : kBodyLanguageBeastNone;
    }

    inline float GetActorHeightScale(RE::Actor* actor) noexcept {
        const auto& fns = detail::GetFunctions();
        return (fns.getActorHeightScale && actor) ? fns.getActorHeightScale(actor) : 1.0f;
    }

    inline std::string GetActorDataJson(RE::Actor* actor) {
        return detail::CallBufferFn(detail::GetFunctions().getActorDataJson, actor, 4096);
    }

    inline std::string GetActorBodyPartsJson(RE::Actor* actor) {
        return detail::CallBufferFn(detail::GetFunctions().getActorBodyPartsJson, actor, 512);
    }

    inline std::string GetActorGenitalsJson(RE::Actor* actor) {
        return detail::CallBufferFn(detail::GetFunctions().getActorGenitalsJson, actor, 512);
    }

    inline std::string GetBodyDescription(RE::Actor* actor, bool shortMode = false) {
        const auto& fns = detail::GetFunctions();
        if (!fns.getBodyDescription || !actor) {
            return {};
        }
        std::string buf(2048, '\0');
        const std::size_t written = fns.getBodyDescription(actor, shortMode, buf.data(), buf.size());
        if (written == 0) {
            return {};
        }
        buf.resize(written);
        return buf;
    }

    inline void InvalidateActorCache(RE::Actor* actor = nullptr) noexcept {
        const auto& fns = detail::GetFunctions();
        if (fns.invalidateActorCache) {
            fns.invalidateActorCache(actor);
        }
    }

}  // namespace BodyLanguage::API

#endif  // BODYLANGUAGE_BUILD_DLL
