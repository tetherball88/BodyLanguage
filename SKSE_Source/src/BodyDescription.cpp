#include "PCH.h"

#include "BodyDescription.h"

#include <chrono>
#include <cmath>
#include <exception>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "Description/PhysiqueClassifier.h"
#include "Geometry/ActorBodyEvaluator.h"
#include "Texture/NormalMapAnalyzer.h"

namespace BodyLanguage {

    namespace {

        constexpr auto kActorCacheTtl = std::chrono::seconds(15);

        struct CachedActorDescription {
            std::chrono::steady_clock::time_point timestamp{};
            bool isNaked{false};
            bool has3D{false};
            float weight{0.0f};
            ActorDescriptionPair descriptions{};
        };

        class ActorDescriptionCache {
        public:
            static ActorDescriptionCache& GetInstance() {
                static ActorDescriptionCache instance;
                return instance;
            }

            void Clear() {
                std::unique_lock lock(m_mutex);
                m_entries.clear();
            }

            bool TryGet(
                RE::FormID formId,
                bool isNaked,
                bool has3D,
                float weight,
                std::chrono::steady_clock::time_point now,
                ActorDescriptionPair& outPair) const {
                std::shared_lock lock(m_mutex);
                const auto it = m_entries.find(formId);
                if (it == m_entries.end()) {
                    return false;
                }

                const auto& entry = it->second;
                if ((now - entry.timestamp) > kActorCacheTtl) {
                    return false;
                }
                if (entry.isNaked != isNaked) {
                    return false;
                }
                // If 3D scene graph became available since last evaluation, re-evaluate to capture live bone scales/MSN
                if (!entry.has3D && has3D) {
                    return false;
                }
                if (std::fabs(entry.weight - weight) > 0.5f) {
                    return false;
                }

                outPair = entry.descriptions;
                return true;
            }

            void Store(
                RE::FormID formId,
                bool isNaked,
                bool has3D,
                float weight,
                std::chrono::steady_clock::time_point now,
                const ActorDescriptionPair& pair) {
                std::unique_lock lock(m_mutex);
                m_entries[formId] = CachedActorDescription{
                    now,
                    isNaked,
                    has3D,
                    weight,
                    pair};
            }

        private:
            mutable std::shared_mutex m_mutex;
            std::unordered_map<RE::FormID, CachedActorDescription> m_entries;
        };

    }  // namespace

    void InitBodyDescriptions(const std::filesystem::path& dataRoot) {
        try {
            NormalMapAnalyzer::GetInstance().LoadConfig(dataRoot);
            PhysiqueClassifier::GetInstance().LoadConfigs(dataRoot);
            spdlog::info("[BodyDescription] Initialized BodyLanguage v2.0 configuration from '{}'", dataRoot.string());
        } catch (const std::exception& e) {
            spdlog::error("[BodyDescription] Exception in InitBodyDescriptions: {}", e.what());
        } catch (...) {
            spdlog::error("[BodyDescription] Unknown exception in InitBodyDescriptions");
        }
    }

    void ClearActorDescriptionCache() {
        ActorDescriptionCache::GetInstance().Clear();
        spdlog::info("[BodyDescription] Cleared per-actor description cache.");
    }

    std::string GetBodyDescription(RE::Actor* actor, std::string_view mode) {
        if (!actor) {
            return {};
        }

        try {
            const RE::FormID formId = actor->GetFormID();
            const bool isNakedNow = ActorBodyEvaluator::GetInstance().IsActorNaked(actor);
            const bool has3DNow = (actor->Get3D(false) != nullptr);
            const auto* base = actor->GetActorBase();
            const float weightNow = base ? base->GetWeight() : actor->GetWeight();
            const auto now = std::chrono::steady_clock::now();

            ActorDescriptionPair cachedPair{};
            if (ActorDescriptionCache::GetInstance().TryGet(
                    formId, isNakedNow, has3DNow, weightNow, now, cachedPair)) {
                const std::string& cachedResult = (mode == "short") ? cachedPair.shortDesc : cachedPair.full;
                // const char* actorName = actor->GetName();
                // spdlog::info(
                //     "[BodyDescription] Cache hit for '{}' ({:08X}) [mode='{}' naked={}]: \"{}\"",
                //     actorName ? actorName : "<unnamed>",
                //     formId,
                //     mode,
                //     isNakedNow,
                //     cachedResult);
                return cachedResult;
            }

            const auto metrics = ActorBodyEvaluator::GetInstance().EvaluateActorBody(actor);
            if (!metrics.valid) {
                const char* actorName = actor->GetName();
                spdlog::warn(
                    "[BodyDescription] Could not evaluate 3D body metrics for actor '{}' (FormID {:08X})",
                    actorName ? actorName : "<unnamed>",
                    formId);
                return {};
            }

            const float texToneScore = NormalMapAnalyzer::GetInstance().EvaluateNormalMapTone(
                metrics.activeMsnPath,
                metrics.abdomenUV,
                metrics.deltoidUV);

            auto pair = PhysiqueClassifier::GetInstance().BuildBothDescriptions(
                metrics,
                texToneScore);

            if (metrics.isNaked) {
                const auto genitalMetrics = ActorBodyEvaluator::GetInstance().EvaluateActorGenitals(actor);
                pair.genitalsJson = PhysiqueClassifier::GetInstance().BuildGenitalsJson(genitalMetrics);
            }

            ActorDescriptionCache::GetInstance().Store(
                formId,
                metrics.isNaked,
                has3DNow,
                weightNow,
                now,
                pair);

            const std::string& sentence = (mode == "short") ? pair.shortDesc : pair.full;
            const char* actorName = actor->GetName();
            spdlog::info(
                "[BodyDescription] Result for '{}' ({:08X}) [mode='{}' naked={}]: \"{}\"",
                actorName ? actorName : "<unnamed>",
                formId,
                mode,
                metrics.isNaked,
                sentence);

            return sentence;
        } catch (const std::exception& e) {
            spdlog::error("[BodyDescription] Exception in GetBodyDescription: {}", e.what());
            return {};
        } catch (...) {
            spdlog::error("[BodyDescription] Unknown exception in GetBodyDescription");
            return {};
        }
    }

    std::string GetActorGenitalsJson(RE::Actor* actor) {
        if (!actor) {
            return "{\"hasGenitals\":false}";
        }

        try {
            const RE::FormID formId = actor->GetFormID();
            const bool isNakedNow = ActorBodyEvaluator::GetInstance().IsActorNaked(actor);
            if (!isNakedNow) {
                return "{\"hasGenitals\":false}";
            }

            const bool has3DNow = (actor->Get3D(false) != nullptr);
            const auto* base = actor->GetActorBase();
            const float weightNow = base ? base->GetWeight() : actor->GetWeight();
            const auto now = std::chrono::steady_clock::now();

            ActorDescriptionPair cachedPair{};
            if (ActorDescriptionCache::GetInstance().TryGet(
                    formId, isNakedNow, has3DNow, weightNow, now, cachedPair)) {
                return cachedPair.genitalsJson;
            }

            const auto genitalMetrics = ActorBodyEvaluator::GetInstance().EvaluateActorGenitals(actor);
            return PhysiqueClassifier::GetInstance().BuildGenitalsJson(genitalMetrics);
        } catch (const std::exception& e) {
            spdlog::error("[BodyDescription] Exception in GetActorGenitalsJson: {}", e.what());
            return "{\"hasGenitals\":false}";
        } catch (...) {
            spdlog::error("[BodyDescription] Unknown exception in GetActorGenitalsJson");
            return "{\"hasGenitals\":false}";
        }
    }

}  // namespace BodyLanguage
