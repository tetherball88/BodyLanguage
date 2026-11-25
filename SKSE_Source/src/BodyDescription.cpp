#include "PCH.h"

#include "BodyDescription.h"

#include <algorithm>
#include <array>
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

        const RE::TESNPC* GetRootFaceNpcSafe(const RE::TESNPC* startNpc) noexcept {
            if (!startNpc) {
                return nullptr;
            }
            constexpr std::size_t kMaxFaceNpcHops = 16;
            std::array<const RE::TESNPC*, kMaxFaceNpcHops> visited{};
            std::size_t visitedCount = 0;

            const RE::TESNPC* current = startNpc;
            while (current->faceNPC && visitedCount < kMaxFaceNpcHops) {
                visited[visitedCount++] = current;
                const RE::TESNPC* next = current->faceNPC;
                for (std::size_t i = 0; i < visitedCount; ++i) {
                    if (visited[i] == next) {
                        return current;
                    }
                }
                current = next;
            }
            return current;
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
                if (!actor || actor->IsDeleted()) {
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

                const auto* rootNpc = GetRootFaceNpcSafe(npc);
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

        constexpr float kBaseStandingHeightCm = 128.0f * kSkyrimUnitToCm;  // 182.88 cm (6 ft)

        struct CachedActorEntry {
            std::chrono::steady_clock::time_point timestamp{};
            BeastFormType beastForm{BeastFormType::kNone};
            bool isNaked{false};
            bool has3D{false};
            float weight{0.0f};
            ActorBodySnapshot snapshot{};
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

            void Erase(RE::FormID formId) {
                std::unique_lock lock(m_mutex);
                m_entries.erase(formId);
            }

            bool TryGet(
                RE::FormID formId,
                BeastFormType beastForm,
                bool isNaked,
                bool has3D,
                float weight,
                std::chrono::steady_clock::time_point now,
                ActorBodySnapshot& outSnapshot) const {
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

                outSnapshot = entry.snapshot;
                return true;
            }

            void Store(
                RE::FormID formId,
                BeastFormType beastForm,
                bool isNaked,
                bool has3D,
                float weight,
                std::chrono::steady_clock::time_point now,
                const ActorBodySnapshot& snapshot) {
                std::unique_lock lock(m_mutex);
                m_entries[formId] = CachedActorEntry{
                    now,
                    beastForm,
                    isNaked,
                    has3D,
                    weight,
                    snapshot};
            }

        private:
            mutable std::shared_mutex m_mutex;
            std::unordered_map<RE::FormID, CachedActorEntry> m_entries;
        };

        // Serializes uncached actor evaluations across worker threads so concurrent
        // requests coalesce on ActorDescriptionCache instead of stampeding the same Actor*.
        std::mutex g_actorEvalMutex;

        void PopulateSnapshotTiers(
            ActorBodySnapshot& snap,
            const std::unordered_map<std::string, std::string>& kw) {
            auto safeAt = [&](const char* key) -> std::string {
                const auto it = kw.find(key);
                return it != kw.end() ? it->second : std::string{};
            };

            if (snap.isFemale) {
                snap.frame = safeAt("frame");
                snap.shoulders = snap.frame;
                snap.breasts = safeAt("breasts_volume");
                snap.chest = snap.breasts;
                snap.waist = safeAt("waist");
                snap.belly = safeAt("belly");
                snap.butt = safeAt("butt_hips");
                snap.thighs = safeAt("legs");
                snap.arms = safeAt("arms");
                snap.condition = safeAt("physique_condition");
            } else {
                snap.frame = safeAt("stature_frame");
                snap.shoulders = safeAt("shoulder_breadth");
                snap.chest = safeAt("chest");
                snap.breasts.clear();
                snap.waist = safeAt("midsection");
                snap.belly = snap.waist;
                snap.butt = safeAt("glutes");
                snap.thighs = safeAt("legs");
                snap.arms = safeAt("arms");
                snap.condition = safeAt("physique_condition");
            }

            snap.descriptions.bodyPartsJson = nlohmann::json{
                {"valid", true},
                {"isFemale", snap.isFemale},
                {"breasts", snap.breasts},
                {"butt", snap.butt},
                {"thighs", snap.thighs}
            }.dump();
        }

        float Round1(float v) noexcept {
            return std::round(v * 10.0f) / 10.0f;
        }

        float Round3(float v) noexcept {
            return std::round(v * 1000.0f) / 1000.0f;
        }

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

    void InvalidateActorCache(RE::Actor* actor) {
        if (!actor) {
            ClearActorDescriptionCache();
            return;
        }
        ActorDescriptionCache::GetInstance().Erase(actor->GetFormID());
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

    ActorBodySnapshot EvaluateActorSnapshot(RE::Actor* actor, bool buildDescriptions) {
        ActorBodySnapshot empty{};
        if (!actor || actor->IsDeleted()) {
            return empty;
        }
        if (IsActorExcluded(actor)) {
            empty.isExcluded = true;
            return empty;
        }

        try {
            const RE::FormID formId = actor->GetFormID();
            const BeastFormType beastForm = ActorBodyEvaluator::GetInstance().GetActorBeastForm(actor);
            const bool isNakedNow = ActorBodyEvaluator::GetInstance().IsActorNaked(actor);
            const bool has3DNow = (actor->Get3D(false) != nullptr);
            const auto* base = actor->GetActorBase();
            const float weightNow = base ? base->GetWeight() : actor->GetWeight();
            auto now = std::chrono::steady_clock::now();

            ActorBodySnapshot cached{};
            if (ActorDescriptionCache::GetInstance().TryGet(
                    formId, beastForm, isNakedNow, has3DNow, weightNow, now, cached)) {
                if (!buildDescriptions || cached.descriptionsBuilt) {
                    return cached;
                }
            }

            std::lock_guard evalLock(g_actorEvalMutex);
            now = std::chrono::steady_clock::now();
            if (ActorDescriptionCache::GetInstance().TryGet(
                    formId, beastForm, isNakedNow, has3DNow, weightNow, now, cached)) {
                if (!buildDescriptions || cached.descriptionsBuilt) {
                    return cached;
                }
                // Upgrade cached structured snapshot with natural-language descriptions without re-evaluating geometry.
                if (cached.beastForm != BeastFormType::kNone) {
                    auto pair = PhysiqueClassifier::GetInstance().BuildBeastBothDescriptions(
                        cached.beastForm, cached.isFemale);
                    pair.genitalsJson = cached.descriptions.genitalsJson;
                    cached.descriptions = std::move(pair);
                } else if (cached.valid) {
                    const auto kw = PhysiqueClassifier::GetInstance().ClassifyRegions(
                        cached.metrics, cached.texToneScore);
                    auto pair = PhysiqueClassifier::GetInstance().BuildBothDescriptionsFromRegions(
                        cached.metrics, cached.toneIndex, kw);
                    pair.genitalsJson = cached.descriptions.genitalsJson;
                    cached.descriptions = std::move(pair);
                }
                cached.descriptionsBuilt = true;
                ActorDescriptionCache::GetInstance().Store(
                    formId, beastForm, isNakedNow, has3DNow, weightNow, now, cached);
                return cached;
            }

            ActorBodySnapshot snap{};
            snap.beastForm = beastForm;
            snap.isNaked = isNakedNow;
            snap.heightScale = ActorBodyEvaluator::GetInstance().GetActorHeightScale(actor);
            snap.estimatedHeightCm = snap.heightScale * kBaseStandingHeightCm;

            // Beast shape takes priority over human body shape regardless of clothed/naked state
            if (beastForm != BeastFormType::kNone) {
                snap.isFemale = base && (base->GetSex() == RE::SEX::kFemale);
                if (buildDescriptions) {
                    snap.descriptions = PhysiqueClassifier::GetInstance().BuildBeastBothDescriptions(
                        beastForm, snap.isFemale);
                    snap.descriptionsBuilt = true;
                }

                if (isNakedNow && !snap.isFemale) {
                    const auto genitalMetrics = ActorBodyEvaluator::GetInstance().EvaluateActorGenitals(actor);
                    snap.genitals = PhysiqueClassifier::GetInstance().ClassifyGenitals(genitalMetrics);
                    snap.descriptions.genitalsJson = PhysiqueClassifier::BuildGenitalsJson(snap.genitals);
                }

                ActorDescriptionCache::GetInstance().Store(
                    formId, beastForm, isNakedNow, has3DNow, weightNow, now, snap);
                return snap;
            }

            const auto metrics = ActorBodyEvaluator::GetInstance().EvaluateActorBody(actor);
            if (!metrics.valid) {
                const char* actorName = actor->GetName();
                spdlog::warn(
                    "[BodyDescription] Could not evaluate 3D body metrics for actor '{}' (FormID {:08X})",
                    actorName ? actorName : "<unnamed>",
                    formId);
                return snap;
            }

            const float texToneScore = NormalMapAnalyzer::GetInstance().EvaluateNormalMapTone(
                metrics.activeMsnPath,
                metrics.abdomenUV,
                metrics.deltoidUV);
            const float toneIndex = PhysiqueClassifier::ComputeToneIndex(
                metrics.geoToneSignal,
                texToneScore);
            const auto kw = PhysiqueClassifier::GetInstance().ClassifyRegions(metrics, texToneScore);

            snap.valid = true;
            snap.isFemale = metrics.isFemale;
            snap.isNaked = metrics.isNaked;
            snap.heightScale = metrics.actorScale;
            snap.estimatedHeightCm = metrics.actorScale * kBaseStandingHeightCm;
            snap.metrics = metrics;
            snap.texToneScore = texToneScore;
            snap.toneIndex = toneIndex;

            const float toCm = metrics.baseReferenceHeight * metrics.actorScale * kSkyrimUnitToCm;
            snap.shoulderWidthCm =
                metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kShoulders)].width * toCm;
            snap.chestCm =
                metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kChestBust)].girth * toCm;
            snap.underbustCm =
                metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kUnderbust)].girth * toCm;
            snap.waistCm =
                metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kNaturalWaist)].girth * toCm;
            snap.bellyCm =
                metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kMidBelly)].girth * toCm;
            snap.hipsCm =
                metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kHipsGlutes)].girth * toCm;
            snap.thighCm =
                metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kUpperThigh)].girth * toCm;
            snap.calfCm =
                metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kCalf)].girth * toCm;
            snap.armCm =
                metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kUpperArm)].girth * toCm;

            PopulateSnapshotTiers(snap, kw);

            if (buildDescriptions) {
                auto pair = PhysiqueClassifier::GetInstance().BuildBothDescriptionsFromRegions(
                    metrics, toneIndex, kw);
                snap.descriptions = std::move(pair);
                snap.descriptionsBuilt = true;
            }

            if (metrics.isNaked) {
                const auto genitalMetrics = ActorBodyEvaluator::GetInstance().EvaluateActorGenitals(actor);
                snap.genitals = PhysiqueClassifier::GetInstance().ClassifyGenitals(genitalMetrics);
                snap.descriptions.genitalsJson = PhysiqueClassifier::BuildGenitalsJson(snap.genitals);
            }

            ActorDescriptionCache::GetInstance().Store(
                formId, beastForm, metrics.isNaked, has3DNow, weightNow, now, snap);

            return snap;
        } catch (const std::exception& e) {
            spdlog::error("[BodyDescription] Exception in EvaluateActorSnapshot: {}", e.what());
            return empty;
        } catch (...) {
            spdlog::error("[BodyDescription] Unknown exception in EvaluateActorSnapshot");
            return empty;
        }
    }

    ClassifiedGenitals GetActorGenitals(RE::Actor* actor) {
        ClassifiedGenitals empty{};
        if (!actor || actor->IsDeleted() || IsActorExcluded(actor)) {
            return empty;
        }

        try {
            const RE::FormID formId = actor->GetFormID();
            const BeastFormType beastForm = ActorBodyEvaluator::GetInstance().GetActorBeastForm(actor);
            const bool isNakedNow = ActorBodyEvaluator::GetInstance().IsActorNaked(actor);
            if (!isNakedNow) {
                return empty;
            }

            const bool has3DNow = (actor->Get3D(false) != nullptr);
            const auto* base = actor->GetActorBase();
            const float weightNow = base ? base->GetWeight() : actor->GetWeight();
            auto now = std::chrono::steady_clock::now();

            ActorBodySnapshot cached{};
            if (ActorDescriptionCache::GetInstance().TryGet(
                    formId, beastForm, isNakedNow, has3DNow, weightNow, now, cached)) {
                return cached.genitals;
            }

            std::lock_guard evalLock(g_actorEvalMutex);
            now = std::chrono::steady_clock::now();
            if (ActorDescriptionCache::GetInstance().TryGet(
                    formId, beastForm, isNakedNow, has3DNow, weightNow, now, cached)) {
                return cached.genitals;
            }

            const auto genitalMetrics = ActorBodyEvaluator::GetInstance().EvaluateActorGenitals(actor);
            return PhysiqueClassifier::GetInstance().ClassifyGenitals(genitalMetrics);
        } catch (const std::exception& e) {
            spdlog::error("[BodyDescription] Exception in GetActorGenitals: {}", e.what());
            return empty;
        } catch (...) {
            spdlog::error("[BodyDescription] Unknown exception in GetActorGenitals");
            return empty;
        }
    }

    std::string GetBodyDescription(RE::Actor* actor, std::string_view mode) {
        const auto snap = EvaluateActorSnapshot(actor, true);
        if (snap.isExcluded || (!snap.valid && snap.beastForm == BeastFormType::kNone)) {
            return {};
        }

        const std::string& sentence = (mode == "short") ? snap.descriptions.shortDesc : snap.descriptions.full;
        if (actor) {
            const char* actorName = actor->GetName();
            if (snap.beastForm != BeastFormType::kNone) {
                spdlog::info(
                    "[BodyDescription] Beast result for '{}' ({:08X}) [form='{}' mode='{}' naked={}]: \"{}\"",
                    actorName ? actorName : "<unnamed>",
                    actor->GetFormID(),
                    ActorBodyEvaluator::BeastFormToKey(snap.beastForm),
                    mode,
                    snap.isNaked,
                    sentence);
            } else {
                spdlog::info(
                    "[BodyDescription] Result for '{}' ({:08X}) [mode='{}' naked={}]: \"{}\"",
                    actorName ? actorName : "<unnamed>",
                    actor->GetFormID(),
                    mode,
                    snap.isNaked,
                    sentence);
            }
        }
        return sentence;
    }

    std::string GetActorDataJson(RE::Actor* actor) {
        const auto snap = EvaluateActorSnapshot(actor, false);
        nlohmann::json out = {
            {"valid", snap.valid},
            {"isFemale", snap.isFemale},
            {"isNaked", snap.isNaked},
            {"isExcluded", snap.isExcluded},
            {"beastForm", std::string(ActorBodyEvaluator::BeastFormToKey(snap.beastForm))},
            {"heightScale", Round3(snap.heightScale)},
            {"estimatedHeightCm", Round1(snap.estimatedHeightCm)}
        };

        if (snap.valid) {
            out["measurements_cm"] = {
                {"shoulders", Round1(snap.shoulderWidthCm)},
                {"chest", Round1(snap.chestCm)},
                {"underbust", Round1(snap.underbustCm)},
                {"waist", Round1(snap.waistCm)},
                {"belly", Round1(snap.bellyCm)},
                {"hips", Round1(snap.hipsCm)},
                {"thigh", Round1(snap.thighCm)},
                {"calf", Round1(snap.calfCm)},
                {"arm", Round1(snap.armCm)}
            };
            out["measurements_in"] = {
                {"shoulders", Round1(snap.shoulderWidthCm * kCmToInches)},
                {"chest", Round1(snap.chestCm * kCmToInches)},
                {"underbust", Round1(snap.underbustCm * kCmToInches)},
                {"waist", Round1(snap.waistCm * kCmToInches)},
                {"belly", Round1(snap.bellyCm * kCmToInches)},
                {"hips", Round1(snap.hipsCm * kCmToInches)},
                {"thigh", Round1(snap.thighCm * kCmToInches)},
                {"calf", Round1(snap.calfCm * kCmToInches)},
                {"arm", Round1(snap.armCm * kCmToInches)}
            };
            out["ratios"] = {
                {"bmi", Round3(snap.metrics.bodyMassIndex)},
                {"shoulderToHeight", Round3(snap.metrics.shoulderToHeight)},
                {"vTaper", Round3(snap.metrics.vTaperRatio)},
                {"whr", Round3(snap.metrics.waistToHipRatio)},
                {"bustToUnderbust", Round3(snap.metrics.bustToUnderbust)},
                {"hipsToHeight", Round3(snap.metrics.hipsToHeight)},
                {"thighGirth", Round3(snap.metrics.upperThighGirth)},
                {"calfGirth", Round3(snap.metrics.calfGirth)},
                {"armGirth", Round3(snap.metrics.upperArmGirth)},
                {"bellyDrop", Round3(snap.metrics.sagittalBellyDrop)},
                {"flankFlare", Round3(snap.metrics.flankFlareRatio)},
                {"geoToneSignal", Round3(snap.metrics.geoToneSignal)},
                {"texToneScore", Round3(snap.texToneScore)},
                {"toneIndex", Round3(snap.toneIndex)},
                {"pregnancyScore", Round3(snap.metrics.pregnancyBellyScore)}
            };
            out["parts"] = {
                {"frame", snap.frame},
                {"shoulders", snap.shoulders},
                {"chest", snap.chest},
                {"breasts", snap.breasts},
                {"waist", snap.waist},
                {"belly", snap.belly},
                {"butt", snap.butt},
                {"thighs", snap.thighs},
                {"arms", snap.arms},
                {"condition", snap.condition}
            };
        }

        if (snap.genitals.hasGenitals) {
            out["genitals"] = {
                {"hasGenitals", true},
                {"state", snap.genitals.state},
                {"size", snap.genitals.sizeTier},
                {"lengthCm", snap.genitals.lengthCm},
                {"girthCm", snap.genitals.girthCm},
                {"addon", snap.genitals.addonKey}
            };
        } else {
            out["genitals"] = {
                {"hasGenitals", false}
            };
        }

        return out.dump();
    }

    std::string GetActorGenitalsJson(RE::Actor* actor) {
        return PhysiqueClassifier::BuildGenitalsJson(GetActorGenitals(actor));
    }

    std::string GetActorBodyPartsJson(RE::Actor* actor) {
        const auto snap = EvaluateActorSnapshot(actor, false);
        if (!snap.valid || snap.isExcluded || snap.beastForm != BeastFormType::kNone) {
            return "{\"valid\":false}";
        }
        return snap.descriptions.bodyPartsJson;
    }

    std::string GetActorHeightJson(RE::Actor* actor) {
        if (!actor || actor->IsDeleted() || IsActorExcluded(actor)) {
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
