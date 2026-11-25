#include "PCH.h"

#define BODYLANGUAGE_BUILD_DLL
#include "BodyLanguageAPI.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <excpt.h>
#include <exception>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "BodyDescription.h"
#include "Geometry/ActorBodyEvaluator.h"
#include "SkyrimNetIntegration.h"

namespace BodyLanguage {

    namespace {

        bool ExecuteSehProtected(
            void (*fn)(void*) noexcept,
            void* ctx,
            unsigned long& outExceptionCode) noexcept {
            __try {
                fn(ctx);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                outExceptionCode = GetExceptionCode();
                return false;
            }
        }

        template <typename Ret, typename Callable>
        Ret RunApiSafe(
            const char* apiName,
            const void* actorPtr,
            Ret fallbackValue,
            Callable&& callable) noexcept {
            struct CallContext {
                Callable* fn;
                const char* name;
                Ret result;
            } ctx{&callable, apiName, fallbackValue};

            unsigned long sehCode = 0;
            const bool ok = ExecuteSehProtected(
                [](void* rawCtx) noexcept {
                    auto* c = static_cast<CallContext*>(rawCtx);
                    try {
                        c->result = (*c->fn)();
                    } catch (const std::exception& e) {
                        spdlog::error("[PublicAPI] Exception in {}: {}", c->name, e.what());
                    } catch (...) {
                        spdlog::error("[PublicAPI] Unknown exception in {}", c->name);
                    }
                },
                &ctx,
                sehCode);

            if (!ok) {
                spdlog::error(
                    "[PublicAPI] SEH hardware exception 0x{:08X} in {} (actor={:p})",
                    sehCode,
                    apiName,
                    actorPtr);
                return fallbackValue;
            }
            return ctx.result;
        }

        template <std::size_t N>
        void CopyToFixedString(char (&dest)[N], std::string_view src) noexcept {
            static_assert(N > 0);
            const std::size_t count = std::min(src.size(), N - 1);
            if (count > 0) {
                std::memcpy(dest, src.data(), count);
            }
            dest[count] = '\0';
        }

        std::size_t CopyStringToBuffer(const std::string& str, char* outBuf, std::size_t bufSize) noexcept {
            if (!outBuf || bufSize == 0 || str.size() >= bufSize) {
                return 0;
            }
            std::memcpy(outBuf, str.data(), str.size() + 1);
            return str.size();
        }

        void FillGenitalsStruct(const ClassifiedGenitals& src, BodyLanguage_Genitals& dst) noexcept {
            dst = BodyLanguage_Genitals{};
            dst.hasGenitals = src.hasGenitals;
            dst.isErect = src.isErect;
            dst.lengthCm = src.lengthCm;
            dst.girthCm = src.girthCm;
            CopyToFixedString(dst.sizeTier, src.sizeTier);
            CopyToFixedString(dst.state, src.state);
            CopyToFixedString(dst.addon, src.addonKey);
        }

    }  // namespace

}  // namespace BodyLanguage

extern "C" {

__declspec(dllexport) std::uint32_t BodyLanguage_GetApiVersion() noexcept {
    return kBodyLanguageApiVersion;
}

__declspec(dllexport) bool BodyLanguage_IsSkyrimNetActive() noexcept {
    return BodyLanguage::IsSkyrimNetActive();
}

__declspec(dllexport) bool BodyLanguage_GetActorData(
    RE::Actor* actor,
    BodyLanguage_ActorData* outData) noexcept {
    if (!actor || !outData) {
        return false;
    }
    if (outData->structSize != 0 && outData->structSize < sizeof(BodyLanguage_ActorData)) {
        return false;
    }

    return BodyLanguage::RunApiSafe<bool>(
        "BodyLanguage_GetActorData",
        actor,
        false,
        [actor, outData]() -> bool {
            const auto snap = BodyLanguage::EvaluateActorSnapshot(actor, false);

            BodyLanguage_ActorData data{};
            data.structSize = sizeof(BodyLanguage_ActorData);
            data.valid = snap.valid;
            data.isFemale = snap.isFemale;
            data.isNaked = snap.isNaked;
            data.isExcluded = snap.isExcluded;
            data.beastForm = static_cast<std::uint8_t>(snap.beastForm);

            data.measurements.shoulderWidthCm = snap.shoulderWidthCm;
            data.measurements.chestCm = snap.chestCm;
            data.measurements.underbustCm = snap.underbustCm;
            data.measurements.waistCm = snap.waistCm;
            data.measurements.bellyCm = snap.bellyCm;
            data.measurements.hipsCm = snap.hipsCm;
            data.measurements.thighCm = snap.thighCm;
            data.measurements.calfCm = snap.calfCm;
            data.measurements.armCm = snap.armCm;
            data.measurements.heightScale = snap.heightScale;
            data.measurements.estimatedHeightCm = snap.estimatedHeightCm;

            if (snap.valid) {
                data.ratios.bodyMassIndex = snap.metrics.bodyMassIndex;
                data.ratios.shoulderToHeight = snap.metrics.shoulderToHeight;
                data.ratios.vTaperRatio = snap.metrics.vTaperRatio;
                data.ratios.waistToHipRatio = snap.metrics.waistToHipRatio;
                data.ratios.bustToUnderbust = snap.metrics.bustToUnderbust;
                data.ratios.hipsToHeight = snap.metrics.hipsToHeight;
                data.ratios.upperThighGirth = snap.metrics.upperThighGirth;
                data.ratios.calfGirth = snap.metrics.calfGirth;
                data.ratios.upperArmGirth = snap.metrics.upperArmGirth;
                data.ratios.sagittalBellyDrop = snap.metrics.sagittalBellyDrop;
                data.ratios.flankFlareRatio = snap.metrics.flankFlareRatio;
                data.ratios.geoToneSignal = snap.metrics.geoToneSignal;
                data.ratios.texToneScore = snap.texToneScore;
                data.ratios.toneIndex = snap.toneIndex;
                data.ratios.pregnancyBellyScore = snap.metrics.pregnancyBellyScore;

                BodyLanguage::CopyToFixedString(data.parts.frame, snap.frame);
                BodyLanguage::CopyToFixedString(data.parts.shoulders, snap.shoulders);
                BodyLanguage::CopyToFixedString(data.parts.chest, snap.chest);
                BodyLanguage::CopyToFixedString(data.parts.breasts, snap.breasts);
                BodyLanguage::CopyToFixedString(data.parts.waist, snap.waist);
                BodyLanguage::CopyToFixedString(data.parts.belly, snap.belly);
                BodyLanguage::CopyToFixedString(data.parts.butt, snap.butt);
                BodyLanguage::CopyToFixedString(data.parts.thighs, snap.thighs);
                BodyLanguage::CopyToFixedString(data.parts.arms, snap.arms);
                BodyLanguage::CopyToFixedString(data.parts.condition, snap.condition);
            }

            BodyLanguage::FillGenitalsStruct(snap.genitals, data.genitals);
            *outData = data;
            return snap.valid || snap.beastForm != BodyLanguage::BeastFormType::kNone;
        });
}

__declspec(dllexport) bool BodyLanguage_GetActorGenitals(
    RE::Actor* actor,
    BodyLanguage_Genitals* outGenitals) noexcept {
    if (!actor || !outGenitals) {
        return false;
    }

    return BodyLanguage::RunApiSafe<bool>(
        "BodyLanguage_GetActorGenitals",
        actor,
        false,
        [actor, outGenitals]() -> bool {
            const auto genitals = BodyLanguage::GetActorGenitals(actor);
            BodyLanguage::FillGenitalsStruct(genitals, *outGenitals);
            return genitals.hasGenitals;
        });
}

__declspec(dllexport) bool BodyLanguage_IsActorNaked(RE::Actor* actor) noexcept {
    if (!actor) {
        return false;
    }
    return BodyLanguage::RunApiSafe<bool>(
        "BodyLanguage_IsActorNaked",
        actor,
        false,
        [actor]() { return BodyLanguage::ActorBodyEvaluator::GetInstance().IsActorNaked(actor); });
}

__declspec(dllexport) bool BodyLanguage_IsActorExcluded(RE::Actor* actor) noexcept {
    if (!actor) {
        return false;
    }
    return BodyLanguage::RunApiSafe<bool>(
        "BodyLanguage_IsActorExcluded",
        actor,
        false,
        [actor]() { return BodyLanguage::IsActorExcluded(actor); });
}

__declspec(dllexport) std::uint8_t BodyLanguage_GetActorBeastForm(RE::Actor* actor) noexcept {
    if (!actor) {
        return kBodyLanguageBeastNone;
    }
    return BodyLanguage::RunApiSafe<std::uint8_t>(
        "BodyLanguage_GetActorBeastForm",
        actor,
        kBodyLanguageBeastNone,
        [actor]() {
            return static_cast<std::uint8_t>(
                BodyLanguage::ActorBodyEvaluator::GetInstance().GetActorBeastForm(actor));
        });
}

__declspec(dllexport) float BodyLanguage_GetActorHeightScale(RE::Actor* actor) noexcept {
    if (!actor) {
        return 1.0f;
    }
    return BodyLanguage::RunApiSafe<float>(
        "BodyLanguage_GetActorHeightScale",
        actor,
        1.0f,
        [actor]() { return BodyLanguage::ActorBodyEvaluator::GetInstance().GetActorHeightScale(actor); });
}

__declspec(dllexport) std::size_t BodyLanguage_GetActorDataJson(
    RE::Actor* actor,
    char* outBuf,
    std::size_t bufSize) noexcept {
    if (!actor || !outBuf || bufSize == 0) {
        return 0;
    }
    return BodyLanguage::RunApiSafe<std::size_t>(
        "BodyLanguage_GetActorDataJson",
        actor,
        0,
        [actor, outBuf, bufSize]() {
            const std::string json = BodyLanguage::GetActorDataJson(actor);
            return BodyLanguage::CopyStringToBuffer(json, outBuf, bufSize);
        });
}

__declspec(dllexport) std::size_t BodyLanguage_GetActorGenitalsJson(
    RE::Actor* actor,
    char* outBuf,
    std::size_t bufSize) noexcept {
    if (!actor || !outBuf || bufSize == 0) {
        return 0;
    }
    return BodyLanguage::RunApiSafe<std::size_t>(
        "BodyLanguage_GetActorGenitalsJson",
        actor,
        0,
        [actor, outBuf, bufSize]() {
            const std::string json = BodyLanguage::GetActorGenitalsJson(actor);
            return BodyLanguage::CopyStringToBuffer(json, outBuf, bufSize);
        });
}

__declspec(dllexport) std::size_t BodyLanguage_GetActorBodyPartsJson(
    RE::Actor* actor,
    char* outBuf,
    std::size_t bufSize) noexcept {
    if (!actor || !outBuf || bufSize == 0) {
        return 0;
    }
    return BodyLanguage::RunApiSafe<std::size_t>(
        "BodyLanguage_GetActorBodyPartsJson",
        actor,
        0,
        [actor, outBuf, bufSize]() {
            const std::string json = BodyLanguage::GetActorBodyPartsJson(actor);
            return BodyLanguage::CopyStringToBuffer(json, outBuf, bufSize);
        });
}

__declspec(dllexport) std::size_t BodyLanguage_GetBodyDescription(
    RE::Actor* actor,
    bool shortMode,
    char* outBuf,
    std::size_t bufSize) noexcept {
    if (!actor || !outBuf || bufSize == 0) {
        return 0;
    }
    return BodyLanguage::RunApiSafe<std::size_t>(
        "BodyLanguage_GetBodyDescription",
        actor,
        0,
        [actor, shortMode, outBuf, bufSize]() {
            const std::string desc = BodyLanguage::GetBodyDescription(actor, shortMode ? "short" : "full");
            return BodyLanguage::CopyStringToBuffer(desc, outBuf, bufSize);
        });
}

__declspec(dllexport) void BodyLanguage_InvalidateActorCache(RE::Actor* actor) noexcept {
    BodyLanguage::RunApiSafe<int>(
        "BodyLanguage_InvalidateActorCache",
        actor,
        0,
        [actor]() {
            BodyLanguage::InvalidateActorCache(actor);
            return 0;
        });
}

}  // extern "C"
