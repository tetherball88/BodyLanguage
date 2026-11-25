#include "PCH.h"

#include "PapyrusAPI.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#define BODYLANGUAGE_BUILD_DLL
#include "BodyLanguageAPI.h"

#include "BodyDescription.h"
#include "Geometry/ActorBodyEvaluator.h"
#include "SkyrimNetIntegration.h"

namespace BodyLanguage {

    namespace {

        constexpr std::string_view kPapyrusScriptName = "BodyLanguage";

        std::string ToLowerAscii(std::string_view input) {
            std::string out(input);
            std::ranges::transform(out, out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        std::int32_t GetApiVersion(RE::StaticFunctionTag*) {
            return static_cast<std::int32_t>(kBodyLanguageApiVersion);
        }

        bool IsSkyrimNetActivePapyrus(RE::StaticFunctionTag*) {
            return IsSkyrimNetActive();
        }

        bool IsNaked(RE::StaticFunctionTag*, RE::Actor* actor) {
            if (!actor) {
                return false;
            }
            return ActorBodyEvaluator::GetInstance().IsActorNaked(actor);
        }

        bool IsExcluded(RE::StaticFunctionTag*, RE::Actor* actor) {
            if (!actor) {
                return false;
            }
            return IsActorExcluded(actor);
        }

        std::string GetBeastForm(RE::StaticFunctionTag*, RE::Actor* actor) {
            if (!actor) {
                return {};
            }
            const auto form = ActorBodyEvaluator::GetInstance().GetActorBeastForm(actor);
            return std::string(ActorBodyEvaluator::BeastFormToKey(form));
        }

        float GetHeightScale(RE::StaticFunctionTag*, RE::Actor* actor) {
            if (!actor) {
                return 1.0f;
            }
            return ActorBodyEvaluator::GetInstance().GetActorHeightScale(actor);
        }

        float GetEstimatedHeightCm(RE::StaticFunctionTag*, RE::Actor* actor) {
            if (!actor) {
                return 0.0f;
            }
            constexpr float kBaseStandingHeightCm = 128.0f * kSkyrimUnitToCm;
            return ActorBodyEvaluator::GetInstance().GetActorHeightScale(actor) * kBaseStandingHeightCm;
        }

        std::string GetBodyPartTier(RE::StaticFunctionTag*, RE::Actor* actor, std::string part) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            if (!snap.valid) {
                return {};
            }
            const std::string key = ToLowerAscii(part);
            if (key == "frame" || key == "stature" || key == "stature_frame") {
                return snap.frame;
            }
            if (key == "shoulders" || key == "shoulder" || key == "shoulder_breadth") {
                return snap.shoulders;
            }
            if (key == "chest") {
                return snap.chest;
            }
            if (key == "breasts" || key == "breast" || key == "bust" || key == "breasts_volume") {
                return snap.breasts;
            }
            if (key == "waist" || key == "midsection") {
                return snap.waist;
            }
            if (key == "belly" || key == "stomach") {
                return snap.belly;
            }
            if (key == "butt" || key == "hips" || key == "glutes" || key == "butt_hips") {
                return snap.butt;
            }
            if (key == "thighs" || key == "thigh" || key == "legs") {
                return snap.thighs;
            }
            if (key == "arms" || key == "arm") {
                return snap.arms;
            }
            if (key == "condition" || key == "physique" || key == "physique_condition") {
                return snap.condition;
            }
            return {};
        }

        std::string GetBreastSize(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.breasts : std::string{};
        }

        std::string GetButtSize(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.butt : std::string{};
        }

        std::string GetThighSize(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.thighs : std::string{};
        }

        std::string GetWaistSize(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.waist : std::string{};
        }

        std::string GetBellySize(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.belly : std::string{};
        }

        std::string GetArmSize(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.arms : std::string{};
        }

        std::string GetFrameSize(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.frame : std::string{};
        }

        std::string GetShoulderBreadth(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.shoulders : std::string{};
        }

        std::string GetChestSize(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.chest : std::string{};
        }

        std::string GetPhysiqueCondition(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.condition : std::string{};
        }

        float GetBodyMeasurementCm(RE::StaticFunctionTag*, RE::Actor* actor, std::string part) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            const std::string key = ToLowerAscii(part);
            if (key == "height") {
                return snap.estimatedHeightCm;
            }
            if (!snap.valid) {
                return 0.0f;
            }
            if (key == "shoulders" || key == "shoulder" || key == "shoulder_width") {
                return snap.shoulderWidthCm;
            }
            if (key == "chest" || key == "bust" || key == "breasts") {
                return snap.chestCm;
            }
            if (key == "underbust" || key == "ribcage") {
                return snap.underbustCm;
            }
            if (key == "waist") {
                return snap.waistCm;
            }
            if (key == "belly" || key == "stomach") {
                return snap.bellyCm;
            }
            if (key == "hips" || key == "butt" || key == "glutes") {
                return snap.hipsCm;
            }
            if (key == "thigh" || key == "thighs") {
                return snap.thighCm;
            }
            if (key == "calf" || key == "calves") {
                return snap.calfCm;
            }
            if (key == "arm" || key == "arms") {
                return snap.armCm;
            }
            return 0.0f;
        }

        float GetBodyRatio(RE::StaticFunctionTag*, RE::Actor* actor, std::string metric) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            if (!snap.valid) {
                return 0.0f;
            }
            const std::string key = ToLowerAscii(metric);
            if (key == "bmi" || key == "body_mass_index") {
                return snap.metrics.bodyMassIndex;
            }
            if (key == "shoulder_to_height" || key == "shldr") {
                return snap.metrics.shoulderToHeight;
            }
            if (key == "v_taper" || key == "vtaper") {
                return snap.metrics.vTaperRatio;
            }
            if (key == "whr" || key == "waist_to_hip") {
                return snap.metrics.waistToHipRatio;
            }
            if (key == "bust_to_underbust" || key == "bust_cup" || key == "chest_prominence") {
                return snap.metrics.bustToUnderbust;
            }
            if (key == "hips_to_height" || key == "hips") {
                return snap.metrics.hipsToHeight;
            }
            if (key == "thigh_girth" || key == "thigh") {
                return snap.metrics.upperThighGirth;
            }
            if (key == "calf_girth" || key == "calf") {
                return snap.metrics.calfGirth;
            }
            if (key == "arm_girth" || key == "arm") {
                return snap.metrics.upperArmGirth;
            }
            if (key == "belly_drop" || key == "sagittal_belly_drop") {
                return snap.metrics.sagittalBellyDrop;
            }
            if (key == "flank_flare") {
                return snap.metrics.flankFlareRatio;
            }
            if (key == "geo_tone") {
                return snap.metrics.geoToneSignal;
            }
            if (key == "tex_tone") {
                return snap.texToneScore;
            }
            if (key == "tone" || key == "tone_index") {
                return snap.toneIndex;
            }
            if (key == "pregnancy" || key == "pregnancy_score") {
                return snap.metrics.pregnancyBellyScore;
            }
            return 0.0f;
        }

        float GetPregnancyScore(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.metrics.pregnancyBellyScore : 0.0f;
        }

        float GetToneIndex(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto snap = EvaluateActorSnapshot(actor, false);
            return snap.valid ? snap.toneIndex : 0.0f;
        }

        bool HasGenitals(RE::StaticFunctionTag*, RE::Actor* actor) {
            return GetActorGenitals(actor).hasGenitals;
        }

        std::string GetGenitalSize(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto g = GetActorGenitals(actor);
            return g.hasGenitals ? g.sizeTier : std::string{};
        }

        std::string GetGenitalState(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto g = GetActorGenitals(actor);
            return g.hasGenitals ? g.state : std::string{};
        }

        std::string GetGenitalAddon(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto g = GetActorGenitals(actor);
            return g.hasGenitals ? g.addonKey : std::string{};
        }

        float GetGenitalLengthCm(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto g = GetActorGenitals(actor);
            return g.hasGenitals ? g.lengthCm : 0.0f;
        }

        float GetGenitalGirthCm(RE::StaticFunctionTag*, RE::Actor* actor) {
            const auto g = GetActorGenitals(actor);
            return g.hasGenitals ? g.girthCm : 0.0f;
        }

        std::string GetFullBodyDescription(RE::StaticFunctionTag*, RE::Actor* actor) {
            return GetBodyDescription(actor, "full");
        }

        std::string GetShortBodyDescription(RE::StaticFunctionTag*, RE::Actor* actor) {
            return GetBodyDescription(actor, "short");
        }

        std::string GetBodyDataJson(RE::StaticFunctionTag*, RE::Actor* actor) {
            return GetActorDataJson(actor);
        }

        std::string GetBodyPartsJson(RE::StaticFunctionTag*, RE::Actor* actor) {
            return GetActorBodyPartsJson(actor);
        }

        std::string GetGenitalsJson(RE::StaticFunctionTag*, RE::Actor* actor) {
            return GetActorGenitalsJson(actor);
        }

        void InvalidateActorCachePapyrus(RE::StaticFunctionTag*, RE::Actor* actor) {
            InvalidateActorCache(actor);
        }

    }  // namespace

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
        if (!vm) {
            return false;
        }

        // Version & status
        vm->RegisterFunction("GetApiVersion", kPapyrusScriptName, GetApiVersion);
        vm->RegisterFunction("IsSkyrimNetActive", kPapyrusScriptName, IsSkyrimNetActivePapyrus);

        // Actor state & height
        vm->RegisterFunction("IsNaked", kPapyrusScriptName, IsNaked);
        vm->RegisterFunction("IsExcluded", kPapyrusScriptName, IsExcluded);
        vm->RegisterFunction("GetBeastForm", kPapyrusScriptName, GetBeastForm);
        vm->RegisterFunction("GetHeightScale", kPapyrusScriptName, GetHeightScale);
        vm->RegisterFunction("GetEstimatedHeightCm", kPapyrusScriptName, GetEstimatedHeightCm);

        // Body part size tiers
        vm->RegisterFunction("GetBodyPartTier", kPapyrusScriptName, GetBodyPartTier);
        vm->RegisterFunction("GetBreastSize", kPapyrusScriptName, GetBreastSize);
        vm->RegisterFunction("GetButtSize", kPapyrusScriptName, GetButtSize);
        vm->RegisterFunction("GetThighSize", kPapyrusScriptName, GetThighSize);
        vm->RegisterFunction("GetWaistSize", kPapyrusScriptName, GetWaistSize);
        vm->RegisterFunction("GetBellySize", kPapyrusScriptName, GetBellySize);
        vm->RegisterFunction("GetArmSize", kPapyrusScriptName, GetArmSize);
        vm->RegisterFunction("GetFrameSize", kPapyrusScriptName, GetFrameSize);
        vm->RegisterFunction("GetShoulderBreadth", kPapyrusScriptName, GetShoulderBreadth);
        vm->RegisterFunction("GetChestSize", kPapyrusScriptName, GetChestSize);
        vm->RegisterFunction("GetPhysiqueCondition", kPapyrusScriptName, GetPhysiqueCondition);

        // Real-world measurements (cm) & anthropometric ratios
        vm->RegisterFunction("GetBodyMeasurementCm", kPapyrusScriptName, GetBodyMeasurementCm);
        vm->RegisterFunction("GetBodyRatio", kPapyrusScriptName, GetBodyRatio);
        vm->RegisterFunction("GetPregnancyScore", kPapyrusScriptName, GetPregnancyScore);
        vm->RegisterFunction("GetToneIndex", kPapyrusScriptName, GetToneIndex);

        // Genitals (TNG / SOS)
        vm->RegisterFunction("HasGenitals", kPapyrusScriptName, HasGenitals);
        vm->RegisterFunction("GetGenitalSize", kPapyrusScriptName, GetGenitalSize);
        vm->RegisterFunction("GetGenitalState", kPapyrusScriptName, GetGenitalState);
        vm->RegisterFunction("GetGenitalAddon", kPapyrusScriptName, GetGenitalAddon);
        vm->RegisterFunction("GetGenitalLengthCm", kPapyrusScriptName, GetGenitalLengthCm);
        vm->RegisterFunction("GetGenitalGirthCm", kPapyrusScriptName, GetGenitalGirthCm);

        // Natural-language descriptions & JSON payloads
        vm->RegisterFunction("GetFullBodyDescription", kPapyrusScriptName, GetFullBodyDescription);
        vm->RegisterFunction("GetShortBodyDescription", kPapyrusScriptName, GetShortBodyDescription);
        vm->RegisterFunction("GetBodyDataJson", kPapyrusScriptName, GetBodyDataJson);
        vm->RegisterFunction("GetBodyPartsJson", kPapyrusScriptName, GetBodyPartsJson);
        vm->RegisterFunction("GetGenitalsJson", kPapyrusScriptName, GetGenitalsJson);

        // Cache management
        vm->RegisterFunction("InvalidateActorCache", kPapyrusScriptName, InvalidateActorCachePapyrus);

        spdlog::info("[PapyrusAPI] Registered native functions on script '{}'.", kPapyrusScriptName);
        return true;
    }

}  // namespace BodyLanguage
