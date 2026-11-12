#include "PCH.h"

#include "BodyDescription.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "Description/PhysiqueClassifier.h"
#include "Geometry/ActorBodyEvaluator.h"
#include "Texture/NormalMapAnalyzer.h"

namespace BodyLanguage {

    namespace {

        constexpr auto kActorCacheTtl = std::chrono::seconds(15);
        constexpr RE::FormID kPlayerBaseFormId = 0x00000007;
        constexpr RE::FormID kPlayerRefFormId = 0x00000014;

        std::string ToLowerAscii(std::string_view input) {
            std::string out(input);
            std::ranges::transform(out, out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        std::string TrimWhitespace(std::string_view sv) {
            const auto first = sv.find_first_not_of(" \t\r\n\"'");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = sv.find_last_not_of(" \t\r\n\"'");
            return std::string(sv.substr(first, last - first + 1));
        }

        class ActorExclusionFilter {
        public:
            static ActorExclusionFilter& GetInstance() {
                static ActorExclusionFilter instance;
                return instance;
            }

            void LoadConfig(const std::filesystem::path& dataRoot) {
                std::unique_lock lock(m_mutex);
                m_excludePlayer = false;
                m_excludedFormIds.clear();
                m_excludedEditorIds.clear();

                const auto iniPath = dataRoot / "SKSE" / "Plugins" / "BodyLanguage.ini";
                std::ifstream file(iniPath);
                if (!file.is_open()) {
                    return;
                }

                auto* dataHandler = RE::TESDataHandler::GetSingleton();
                std::string currentSection;
                std::string line;

                while (std::getline(file, line)) {
                    const auto commentPos = line.find_first_of(";#");
                    if (commentPos != std::string::npos) {
                        line.erase(commentPos);
                    }

                    const std::string trimmed = TrimWhitespace(line);
                    if (trimmed.empty()) {
                        continue;
                    }

                    if (trimmed.front() == '[' && trimmed.back() == ']') {
                        currentSection = ToLowerAscii(
                            TrimWhitespace(std::string_view(trimmed).substr(1, trimmed.size() - 2)));
                        continue;
                    }

                    std::string listValue;
                    const auto eqPos = trimmed.find('=');
                    if (eqPos != std::string::npos) {
                        const std::string key = ToLowerAscii(
                            TrimWhitespace(std::string_view(trimmed).substr(0, eqPos)));
                        if (key == "excludednpcs" || key == "excludenpcs" ||
                            key == "excludednpc" || key == "excludenpc") {
                            listValue = TrimWhitespace(std::string_view(trimmed).substr(eqPos + 1));
                        }
                    } else if (currentSection == "exclusions") {
                        listValue = trimmed;
                    }

                    if (listValue.empty()) {
                        continue;
                    }

                    std::string_view remaining(listValue);
                    while (!remaining.empty()) {
                        const auto commaPos = remaining.find(',');
                        const std::string_view rawToken = (commaPos == std::string_view::npos)
                            ? remaining
                            : remaining.substr(0, commaPos);
                        remaining = (commaPos == std::string_view::npos)
                            ? std::string_view{}
                            : remaining.substr(commaPos + 1);

                        const std::string token = TrimWhitespace(rawToken);
                        if (!token.empty()) {
                            AddEntry(token, dataHandler);
                        }
                    }
                }

                if (!m_excludedEditorIds.empty() && dataHandler) {
                    for (auto* npc : dataHandler->GetFormArray<RE::TESNPC>()) {
                        if (!npc) {
                            continue;
                        }
                        const char* edid = npc->GetFormEditorID();
                        if (edid && *edid != '\0' && m_excludedEditorIds.contains(ToLowerAscii(edid))) {
                            m_excludedFormIds.insert(npc->GetFormID());
                        }
                    }
                }

                if (m_excludePlayer || !m_excludedFormIds.empty() || !m_excludedEditorIds.empty()) {
                    spdlog::info(
                        "[BodyDescription] Loaded NPC exclusions from BodyLanguage.ini: "
                        "excludePlayer={}, formIds={}, editorIds={}",
                        m_excludePlayer,
                        m_excludedFormIds.size(),
                        m_excludedEditorIds.size());
                }
            }

            bool IsNpcExcluded(const RE::TESNPC* npc) const {
                if (!npc) {
                    return false;
                }
                if (npc->race && npc->race->IsChildRace()) {
                    return true;
                }

                std::shared_lock lock(m_mutex);
                if (!m_excludePlayer && m_excludedFormIds.empty() && m_excludedEditorIds.empty()) {
                    return false;
                }

                return IsNpcExcludedUnlocked(npc);
            }

            bool IsActorExcluded(RE::Actor* actor) const {
                if (!actor) {
                    return false;
                }
                if (actor->IsChild()) {
                    return true;
                }

                const auto* base = actor->GetActorBase();
                const auto* race = actor->GetRace();
                if (!race && base) {
                    race = base->race;
                }
                if (race && race->IsChildRace()) {
                    return true;
                }

                std::shared_lock lock(m_mutex);
                if (!m_excludePlayer && m_excludedFormIds.empty() && m_excludedEditorIds.empty()) {
                    return false;
                }

                const RE::FormID actorFormId = actor->GetFormID();
                if (m_excludePlayer && (actor->IsPlayerRef() || actorFormId == kPlayerRefFormId)) {
                    return true;
                }

                if (m_excludedFormIds.contains(actorFormId)) {
                    return true;
                }

                if (!m_excludedEditorIds.empty()) {
                    const char* actorEdid = actor->GetFormEditorID();
                    if (actorEdid && *actorEdid != '\0' &&
                        m_excludedEditorIds.contains(ToLowerAscii(actorEdid))) {
                        return true;
                    }
                    for (const auto& edid : m_excludedEditorIds) {
                        if (auto* form = RE::TESForm::LookupByEditorID(edid)) {
                            if (form->GetFormID() == actorFormId) {
                                return true;
                            }
                        }
                    }
                }

                if (base && IsNpcExcludedUnlocked(base)) {
                    return true;
                }

                return false;
            }

        private:
            void AddEntry(std::string_view token, RE::TESDataHandler* dataHandler) {
                const std::string lowerToken = ToLowerAscii(token);
                if (lowerToken == "player" || lowerToken == "playerref") {
                    m_excludePlayer = true;
                    m_excludedFormIds.insert(kPlayerBaseFormId);
                    m_excludedFormIds.insert(kPlayerRefFormId);
                    spdlog::info("[BodyDescription] Excluded player character via INI.");
                    return;
                }

                const auto colonPos = token.find(':');
                if (colonPos != std::string_view::npos) {
                    const std::string pluginName = TrimWhitespace(token.substr(0, colonPos));
                    const std::string formIdStr = TrimWhitespace(token.substr(colonPos + 1));
                    if (pluginName.empty() || formIdStr.empty()) {
                        spdlog::warn("[BodyDescription] Invalid plugin:shortformid exclusion entry '{}'", token);
                        return;
                    }

                    try {
                        const auto localFormId = static_cast<RE::FormID>(std::stoul(formIdStr, nullptr, 16));
                        if (!dataHandler) {
                            return;
                        }
                        const RE::FormID resolvedId = dataHandler->LookupFormID(localFormId, pluginName);
                        if (resolvedId == 0) {
                            spdlog::info(
                                "[BodyDescription] Exclusion entry '{}' not resolved (plugin not active or FormID invalid).",
                                token);
                            return;
                        }

                        RegisterResolvedFormId(resolvedId);
                        spdlog::info(
                            "[BodyDescription] Excluded NPC '{}' -> FormID {:08X}",
                            token,
                            resolvedId);
                    } catch (const std::exception&) {
                        spdlog::warn("[BodyDescription] Could not parse hex FormID in exclusion entry '{}'", token);
                    }
                    return;
                }

                m_excludedEditorIds.insert(lowerToken);
                if (auto* form = RE::TESForm::LookupByEditorID(token)) {
                    RegisterResolvedFormId(form->GetFormID());
                    spdlog::info(
                        "[BodyDescription] Excluded NPC by EditorID '{}' -> FormID {:08X}",
                        token,
                        form->GetFormID());
                } else {
                    spdlog::info("[BodyDescription] Registered NPC exclusion EditorID '{}'", token);
                }
            }

            void RegisterResolvedFormId(RE::FormID formId) {
                if (formId == 0) {
                    return;
                }
                if (formId == kPlayerBaseFormId || formId == kPlayerRefFormId) {
                    m_excludePlayer = true;
                }
                m_excludedFormIds.insert(formId);

                if (auto* form = RE::TESForm::LookupByID(formId)) {
                    if (auto* actor = form->As<RE::Actor>()) {
                        if (const auto* base = actor->GetActorBase()) {
                            m_excludedFormIds.insert(base->GetFormID());
                        }
                    }
                }
            }

            bool IsNpcExcludedUnlocked(const RE::TESNPC* npc) const {
                const RE::FormID npcFormId = npc->GetFormID();
                if (m_excludePlayer && npcFormId == kPlayerBaseFormId) {
                    return true;
                }
                if (m_excludedFormIds.contains(npcFormId)) {
                    return true;
                }

                const auto* rootNpc = npc->GetRootFaceNPC();
                if (rootNpc && rootNpc != npc && m_excludedFormIds.contains(rootNpc->GetFormID())) {
                    return true;
                }

                if (!m_excludedEditorIds.empty()) {
                    const char* baseEdid = npc->GetFormEditorID();
                    if (baseEdid && *baseEdid != '\0' &&
                        m_excludedEditorIds.contains(ToLowerAscii(baseEdid))) {
                        return true;
                    }
                    if (rootNpc && rootNpc != npc) {
                        const char* rootEdid = rootNpc->GetFormEditorID();
                        if (rootEdid && *rootEdid != '\0' &&
                            m_excludedEditorIds.contains(ToLowerAscii(rootEdid))) {
                            return true;
                        }
                    }
                    for (const auto& edid : m_excludedEditorIds) {
                        if (auto* form = RE::TESForm::LookupByEditorID(edid)) {
                            const RE::FormID lookedUpId = form->GetFormID();
                            if (lookedUpId == npcFormId ||
                                (rootNpc && lookedUpId == rootNpc->GetFormID())) {
                                return true;
                            }
                        }
                    }
                }

                return false;
            }

            mutable std::shared_mutex m_mutex;
            bool m_excludePlayer{false};
            std::unordered_set<RE::FormID> m_excludedFormIds;
            std::unordered_set<std::string> m_excludedEditorIds;
        };

        struct CachedActorDescription {
            std::chrono::steady_clock::time_point timestamp{};
            BeastFormType beastForm{BeastFormType::kNone};
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
                BeastFormType beastForm,
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
                if (entry.beastForm != beastForm) {
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
                BeastFormType beastForm,
                bool isNaked,
                bool has3D,
                float weight,
                std::chrono::steady_clock::time_point now,
                const ActorDescriptionPair& pair) {
                std::unique_lock lock(m_mutex);
                m_entries[formId] = CachedActorDescription{
                    now,
                    beastForm,
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
            ActorExclusionFilter::GetInstance().LoadConfig(dataRoot);
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

    bool IsActorExcluded(RE::Actor* actor) {
        try {
            return ActorExclusionFilter::GetInstance().IsActorExcluded(actor);
        } catch (const std::exception& e) {
            spdlog::error("[BodyDescription] Exception in IsActorExcluded: {}", e.what());
            return false;
        } catch (...) {
            spdlog::error("[BodyDescription] Unknown exception in IsActorExcluded");
            return false;
        }
    }

    bool IsNpcExcluded(const RE::TESNPC* npc) {
        try {
            return ActorExclusionFilter::GetInstance().IsNpcExcluded(npc);
        } catch (const std::exception& e) {
            spdlog::error("[BodyDescription] Exception in IsNpcExcluded: {}", e.what());
            return false;
        } catch (...) {
            spdlog::error("[BodyDescription] Unknown exception in IsNpcExcluded");
            return false;
        }
    }

    std::string GetBodyDescription(RE::Actor* actor, std::string_view mode) {
        if (!actor || IsActorExcluded(actor)) {
            return {};
        }

        try {
            const RE::FormID formId = actor->GetFormID();
            const BeastFormType beastForm = ActorBodyEvaluator::GetInstance().GetActorBeastForm(actor);
            const bool isNakedNow = ActorBodyEvaluator::GetInstance().IsActorNaked(actor);
            const bool has3DNow = (actor->Get3D(false) != nullptr);
            const auto* base = actor->GetActorBase();
            const float weightNow = base ? base->GetWeight() : actor->GetWeight();
            const auto now = std::chrono::steady_clock::now();

            ActorDescriptionPair cachedPair{};
            if (ActorDescriptionCache::GetInstance().TryGet(
                    formId, beastForm, isNakedNow, has3DNow, weightNow, now, cachedPair)) {
                const std::string& cachedResult = (mode == "short") ? cachedPair.shortDesc : cachedPair.full;
                return cachedResult;
            }

            // Beast shape takes priority over human body shape regardless of clothed/naked state
            if (beastForm != BeastFormType::kNone) {
                const bool isFemale = base && (base->GetSex() == RE::SEX::kFemale);
                auto pair = PhysiqueClassifier::GetInstance().BuildBeastBothDescriptions(
                    beastForm,
                    isFemale);

                if (isNakedNow) {
                    const auto genitalMetrics = ActorBodyEvaluator::GetInstance().EvaluateActorGenitals(actor);
                    pair.genitalsJson = PhysiqueClassifier::GetInstance().BuildGenitalsJson(genitalMetrics);
                }

                ActorDescriptionCache::GetInstance().Store(
                    formId,
                    beastForm,
                    isNakedNow,
                    has3DNow,
                    weightNow,
                    now,
                    pair);

                const std::string& sentence = (mode == "short") ? pair.shortDesc : pair.full;
                const char* actorName = actor->GetName();
                spdlog::info(
                    "[BodyDescription] Beast result for '{}' ({:08X}) [form='{}' mode='{}' naked={}]: \"{}\"",
                    actorName ? actorName : "<unnamed>",
                    formId,
                    ActorBodyEvaluator::BeastFormToKey(beastForm),
                    mode,
                    isNakedNow,
                    sentence);

                return sentence;
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
                beastForm,
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
        if (!actor || IsActorExcluded(actor)) {
            return "{\"hasGenitals\":false}";
        }

        try {
            const RE::FormID formId = actor->GetFormID();
            const BeastFormType beastForm = ActorBodyEvaluator::GetInstance().GetActorBeastForm(actor);
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
                    formId, beastForm, isNakedNow, has3DNow, weightNow, now, cachedPair)) {
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

    std::string GetActorHeightJson(RE::Actor* actor) {
        if (!actor || IsActorExcluded(actor)) {
            return "{\"height\":1.0}";
        }

        try {
            const float heightScale = ActorBodyEvaluator::GetInstance().GetActorHeightScale(actor);
            const float rounded = std::round(heightScale * 1000.0f) / 1000.0f;
            const nlohmann::json out = {
                {"height", rounded}
            };
            return out.dump();
        } catch (const std::exception& e) {
            spdlog::error("[BodyDescription] Exception in GetActorHeightJson: {}", e.what());
            return "{\"height\":1.0}";
        } catch (...) {
            spdlog::error("[BodyDescription] Unknown exception in GetActorHeightJson");
            return "{\"height\":1.0}";
        }
    }

}  // namespace BodyLanguage
