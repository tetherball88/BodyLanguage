#include "PCH.h"

#include "Geometry/ActorBodyEvaluator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "Geometry/NifRingIndexer.h"

namespace BodyLanguage {

    namespace {

        std::string ToLowerAscii(std::string_view input) {
            std::string out(input);
            std::ranges::transform(out, out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        BeastFormType ClassifySingleRaceBeastForm(const RE::TESRace* race, bool checkBehaviorGraph) {
            if (!race) {
                return BeastFormType::kNone;
            }

            const std::uint32_t formId = race->GetFormID();
            const std::uint32_t localId = formId & 0x00FFFFFFu;
            const auto* file = race->GetFile(0);
            const std::string lowerFile = (file && file->fileName[0] != '\0')
                ? ToLowerAscii(file->fileName)
                : std::string{};

            if (lowerFile == "dragonborn.esm" && localId == 0x01E17Bu) {
                return BeastFormType::kWerebear;
            }
            if (lowerFile == "dawnguard.esm" && localId == 0x00283Au) {
                return BeastFormType::kVampireLord;
            }
            if (formId == 0x000CDD84u) {
                return BeastFormType::kWerewolf;
            }

            const char* rawEdid = race->GetFormEditorID();
            const std::string lowerEdid = (rawEdid && *rawEdid != '\0')
                ? ToLowerAscii(rawEdid)
                : std::string{};
            const char* rawName = race->GetName();
            const std::string lowerName = (rawName && *rawName != '\0')
                ? ToLowerAscii(rawName)
                : std::string{};

            // Check Werebear before Werewolf so modded werebear races using werewolf behavior graphs match werebear first
            if (lowerEdid.find("werebear") != std::string::npos ||
                lowerName.find("werebear") != std::string::npos) {
                return BeastFormType::kWerebear;
            }

            if (lowerEdid.find("vampirebeast") != std::string::npos ||
                lowerEdid.find("vampirelord") != std::string::npos ||
                lowerEdid.find("vampire_lord") != std::string::npos ||
                lowerName.find("vampire lord") != std::string::npos ||
                lowerName.find("vampire beast") != std::string::npos) {
                return BeastFormType::kVampireLord;
            }

            if (lowerEdid.find("werewolf") != std::string::npos ||
                lowerName.find("werewolf") != std::string::npos) {
                return BeastFormType::kWerewolf;
            }

            if (checkBehaviorGraph) {
                for (std::uint32_t s = 0; s < RE::SEXES::kTotal; ++s) {
                    const char* rawGraph = race->behaviorGraphProjectNames[s].c_str();
                    const std::string lowerGraph = (rawGraph && *rawGraph != '\0')
                        ? ToLowerAscii(rawGraph)
                        : std::string{};
                    if (lowerGraph.find("vampirelord") != std::string::npos) {
                        return BeastFormType::kVampireLord;
                    }
                    if (lowerGraph.find("werewolfbeast") != std::string::npos) {
                        return BeastFormType::kWerewolf;
                    }
                }
            }

            return BeastFormType::kNone;
        }

        bool IsNakedSkinOrGenitalArmor(const RE::Actor* actor, const RE::TESObjectARMO* armo) {
            if (!armo) {
                return false;
            }
            if (actor) {
                if (armo == actor->GetSkin()) {
                    return true;
                }
                if (const auto* base = actor->GetActorBase(); base && armo == base->skin) {
                    return true;
                }
                if (const auto* race = actor->GetRace(); race && armo == race->skin) {
                    return true;
                }
            }
            if (!armo->GetPlayable() && armo->armorRating == 0) {
                return true;
            }
            for (const auto* arma : armo->armorAddons) {
                if (!arma) {
                    continue;
                }
                for (std::uint32_t s = 0; s < RE::SEXES::kTotal; ++s) {
                    const char* rawModel = arma->bipedModels[s].model.c_str();
                    if (rawModel && *rawModel != '\0' && GetOrIndexGenitalNif(rawModel) != nullptr) {
                        return true;
                    }
                }
            }
            return false;
        }

        constexpr std::size_t kMaxSceneGraphDepth = 24;
        constexpr std::uint16_t kMaxChildArrayCapacity = 4096;
        constexpr std::size_t kMaxInventoryEntries = 2048;
        constexpr std::uint32_t kMaxContainerObjects = 2048u;
        constexpr std::size_t kMaxOutfitDepth = 4;

        std::int32_t GetBaseContainerCount(
            const RE::Actor* actor,
            const RE::TESBoundObject* item) noexcept {
            if (!actor || !item) {
                return 0;
            }
            const auto* container = actor->GetContainer();
            if (!container || !container->containerObjects) {
                return 0;
            }
            const std::uint32_t count = std::min(container->numContainerObjects, kMaxContainerObjects);
            std::int32_t total = 0;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (const auto* co = container->containerObjects[i]; co && co->obj == item) {
                    total += co->count;
                }
            }
            return total;
        }

        std::int32_t GetContainerDeltaInChanges(
            const RE::InventoryChanges* invChanges,
            const RE::TESBoundObject* item,
            bool& outFoundEntry) noexcept {
            outFoundEntry = false;
            if (!invChanges || !invChanges->entryList || !item) {
                return 0;
            }
            std::size_t inspected = 0;
            for (const auto* entry : *invChanges->entryList) {
                if (++inspected > kMaxInventoryEntries) {
                    break;
                }
                if (entry && entry->object == item) {
                    outFoundEntry = true;
                    return entry->countDelta;
                }
            }
            return 0;
        }

        const RE::TESObjectARMO* FindArmorInOutfitForm(
            const RE::TESForm* form,
            RE::BGSBipedObjectForm::BipedObjectSlot slot,
            std::size_t depth = 0) noexcept {
            if (!form || depth > kMaxOutfitDepth) {
                return nullptr;
            }
            if (const auto* armo = form->As<RE::TESObjectARMO>()) {
                return armo->HasPartOf(slot) ? armo : nullptr;
            }
            if (const auto* levItem = form->As<RE::TESLevItem>()) {
                if (levItem->chanceNone >= 100 || levItem->entries.empty()) {
                    return nullptr;
                }
                const std::size_t count = std::min(
                    levItem->entries.size(),
                    static_cast<std::size_t>(levItem->numEntries));
                for (std::size_t i = 0; i < count; ++i) {
                    if (const auto* found =
                            FindArmorInOutfitForm(levItem->entries[i].form, slot, depth + 1)) {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        // Read-only worn armor query that never calls InitInventoryIfRequired / ForceInitInventoryChanges
        // and never allocates temporary InventoryEntryData copies on Skyrim's heap.
        const RE::TESObjectARMO* GetWornArmorReadOnly(
            RE::Actor* actor,
            RE::BGSBipedObjectForm::BipedObjectSlot slot) {
            if (!actor || actor->IsDeleted()) {
                return nullptr;
            }

            if (const auto* invChanges = actor->GetInventoryChanges(true)) {
                if (invChanges->entryList) {
                    std::size_t inspected = 0;
                    for (const auto* entry : *invChanges->entryList) {
                        if (++inspected > kMaxInventoryEntries) {
                            break;
                        }
                        if (!entry || !entry->object || !entry->object->IsArmor()) {
                            continue;
                        }
                        const auto* armo = entry->object->As<RE::TESObjectARMO>();
                        if (!armo || !armo->HasPartOf(slot)) {
                            continue;
                        }
                        const std::int32_t netCount =
                            entry->countDelta + GetBaseContainerCount(actor, entry->object);
                        if (netCount > 0 && entry->IsWorn()) {
                            return armo;
                        }
                    }
                }
                return nullptr;
            }

            if (const auto* base = actor->GetActorBase(); base && base->defaultOutfit) {
                for (const auto* item : base->defaultOutfit->outfitItems) {
                    if (const auto* armo = FindArmorInOutfitForm(item, slot)) {
                        return armo;
                    }
                }
            }
            return nullptr;
        }

        class ActiveMorphCollector final : public SKEE::IBodyMorphInterface::MorphVisitor {
        public:
            std::vector<std::string> morphNames;

            void Visit(RE::TESObjectREFR*, const char* morphName) override {
                if (morphName && *morphName != '\0') {
                    morphNames.emplace_back(morphName);
                }
            }
        };

        // Purely read-only scene-graph node lookup comparing existing BSFixedString storage against
        // std::string_view without constructing/releasing temporary BSFixedStrings in BSStringPool.
        RE::NiAVObject* FindNodeByNameReadOnly(
            RE::NiAVObject* root,
            std::string_view targetName,
            std::size_t depth = 0) noexcept {
            if (!root || targetName.empty() || depth > kMaxSceneGraphDepth) {
                return nullptr;
            }
            if (root->name == targetName) {
                return root;
            }
            auto* node = root->AsNode();
            if (!node) {
                return nullptr;
            }
            auto& children = node->GetChildren();
            const auto* data = children.begin();
            const std::uint16_t cap = children.capacity();
            if (!data || cap > kMaxChildArrayCapacity) {
                return nullptr;
            }
            for (std::uint16_t i = 0; i < cap; ++i) {
                if (auto* child = data[i].get()) {
                    if (auto* found = FindNodeByNameReadOnly(child, targetName, depth + 1)) {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        float ReadBoneScale(
            RE::NiAVObject* root3D,
            RE::NiAVObject* searchRoot,
            std::string_view boneName) noexcept {
            if (!root3D || !searchRoot || boneName.empty()) {
                return 1.0f;
            }
            if (auto* node = FindNodeByNameReadOnly(searchRoot, boneName)) {
                float s = node->local.scale;
                if (auto* parent = node->parent; parent && !parent->name.empty()) {
                    const std::string_view parentName(parent->name);
                    if (parentName.starts_with("CME ") || parentName.starts_with("cme ")) {
                        s *= parent->local.scale;
                    }
                }
                if (root3D->world.scale > 0.1f && node->world.scale > 0.1f) {
                    const float worldRel = node->world.scale / root3D->world.scale;
                    if (std::isfinite(worldRel) && worldRel > s && worldRel < 5.0f) {
                        s = worldRel;
                    }
                }
                if (std::isfinite(s) && s > 0.1f && s < 5.0f) {
                    return s;
                }
            }
            return 1.0f;
        }

        bool FindSceneGraphBodyMsnRecursive(
            RE::NiAVObject* obj,
            std::string& outPath,
            std::size_t depth = 0) {
            if (!obj || depth > kMaxSceneGraphDepth) {
                return false;
            }

            if (auto* geo = obj->AsGeometry()) {
                if (auto* shaderProp = geo->lightingShaderProp_cast()) {
                    if (auto* baseMat = shaderProp->GetBaseMaterial();
                        baseMat && baseMat->GetType() == RE::BSShaderMaterial::Type::kLighting) {
                        const auto* mat =
                            static_cast<const RE::BSLightingShaderMaterialBase*>(baseMat);
                        if (auto* texSet = mat->textureSet.get()) {
                            const char* path =
                                texSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal);
                            if (path && *path != '\0') {
                                const std::string lowerPath = ToLowerAscii(path);
                                const auto slashPos = lowerPath.find_last_of("\\/");
                                const std::string_view fname = (slashPos == std::string::npos)
                                    ? std::string_view(lowerPath)
                                    : std::string_view(lowerPath).substr(slashPos + 1);
                                if (fname.find("body") != std::string_view::npos &&
                                    fname.find("msn") != std::string_view::npos) {
                                    outPath = path;
                                    return true;
                                }
                            }
                        }
                    }
                }
                return false;
            }

            if (auto* node = obj->AsNode()) {
                auto& children = node->GetChildren();
                const auto* data = children.begin();
                const std::uint16_t cap = children.capacity();
                if (data && cap <= kMaxChildArrayCapacity) {
                    for (std::uint16_t i = 0; i < cap; ++i) {
                        if (auto* child = data[i].get()) {
                            if (FindSceneGraphBodyMsnRecursive(child, outPath, depth + 1)) {
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        }

        std::string FindSceneGraphBodyMsn(RE::Actor* actor) {
            if (!actor || actor->IsDeleted()) {
                return {};
            }
            auto* root = actor->Get3D(false);
            if (!root) {
                return {};
            }

            std::string foundPath;
            FindSceneGraphBodyMsnRecursive(root, foundPath);
            return foundPath;
        }

    }  // namespace

    ActorBodyEvaluator& ActorBodyEvaluator::GetInstance() {
        static ActorBodyEvaluator instance;
        return instance;
    }

    void ActorBodyEvaluator::InitSKEE() {
        const auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            spdlog::warn("[ActorBodyEvaluator] SKSE messaging interface unavailable.");
            return;
        }

        SKEE::InterfaceExchangeMessage msg{};
        messaging->Dispatch(
            SKEE::InterfaceExchangeMessage::kMessage_ExchangeInterface,
            &msg,
            sizeof(msg),
            "skee");

        if (!msg.interfaceMap) {
            messaging->Dispatch(
                SKEE::InterfaceExchangeMessage::kMessage_ExchangeInterface,
                &msg,
                sizeof(msg),
                "SKEE");
        }

        if (msg.interfaceMap) {
            if (auto* raw = msg.interfaceMap->QueryInterface("BodyMorph")) {
                m_morphInterface = static_cast<SKEE::IBodyMorphInterface*>(raw);
                spdlog::info(
                    "[ActorBodyEvaluator] Acquired SKEE IBodyMorphInterface (v{}).",
                    m_morphInterface->GetVersion());
            } else {
                spdlog::warn("[ActorBodyEvaluator] SKEE interfaceMap found, but 'BodyMorph' unavailable.");
            }
        } else {
            spdlog::info("[ActorBodyEvaluator] SKEE (RaceMenu) not detected; proceeding with base NIF + bone evaluation.");
        }
    }

    BeastFormType ActorBodyEvaluator::GetActorBeastForm(RE::Actor* actor) const {
        if (!actor || actor->IsDeleted()) {
            return BeastFormType::kNone;
        }
        const auto* race = actor->GetRace();
        if (!race) {
            return BeastFormType::kNone;
        }

        BeastFormType detected = ClassifySingleRaceBeastForm(race, false);
        if (detected != BeastFormType::kNone) {
            return detected;
        }
        if (race->morphRace) {
            detected = ClassifySingleRaceBeastForm(race->morphRace, false);
            if (detected != BeastFormType::kNone) {
                return detected;
            }
        }
        if (race->armorParentRace) {
            detected = ClassifySingleRaceBeastForm(race->armorParentRace, false);
            if (detected != BeastFormType::kNone) {
                return detected;
            }
        }
        return ClassifySingleRaceBeastForm(race, true);
    }

    std::string_view ActorBodyEvaluator::BeastFormToKey(BeastFormType beastForm) {
        switch (beastForm) {
            case BeastFormType::kWerewolf:
                return "werewolf";
            case BeastFormType::kVampireLord:
                return "vampire_lord";
            case BeastFormType::kWerebear:
                return "werebear";
            case BeastFormType::kNone:
            default:
                return {};
        }
    }

    float ActorBodyEvaluator::GetActorHeightScale(RE::Actor* actor) const {
        if (!actor || actor->IsDeleted()) {
            return 1.0f;
        }

        float baseScale = actor->GetScale();
        if (!std::isfinite(baseScale) || baseScale <= 0.2f || baseScale >= 3.0f) {
            baseScale = actor->GetBaseHeight();
            if (!std::isfinite(baseScale) || baseScale <= 0.2f || baseScale >= 3.0f) {
                baseScale = 1.0f;
            }
        }

        float rootScale = baseScale;
        float skeletonScale = 1.0f;
        if (auto* root3D = actor->Get3D(false)) {
            if (std::isfinite(root3D->world.scale) &&
                root3D->world.scale > 0.2f && root3D->world.scale < 3.0f &&
                std::fabs(root3D->world.scale - 1.0f) > 1e-4f) {
                rootScale = root3D->world.scale;
            }
            if (auto* npcRoot = FindNodeByNameReadOnly(root3D, "NPC Root [Root]")) {
                if (std::isfinite(root3D->world.scale) && root3D->world.scale > 0.2f &&
                    std::isfinite(npcRoot->world.scale) && npcRoot->world.scale > 0.2f) {
                    const float rel = npcRoot->world.scale / root3D->world.scale;
                    if (std::isfinite(rel) && rel > 0.2f && rel < 3.0f) {
                        skeletonScale = rel;
                    }
                } else if (std::isfinite(npcRoot->local.scale) &&
                           npcRoot->local.scale > 0.2f && npcRoot->local.scale < 3.0f) {
                    skeletonScale = npcRoot->local.scale;
                }
            }
        }

        const float effectiveScale = rootScale * skeletonScale;
        return (std::isfinite(effectiveScale) && effectiveScale > 0.2f && effectiveScale < 3.0f)
            ? effectiveScale
            : baseScale;
    }

    bool ActorBodyEvaluator::IsActorNaked(RE::Actor* actor) const {
        if (!actor || actor->IsDeleted()) {
            return false;
        }
        const auto* bodyArmor = GetWornArmorReadOnly(
            actor,
            RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
        if (IsNakedSkinOrGenitalArmor(actor, bodyArmor)) {
            bodyArmor = nullptr;
        }
        const auto* chestArmor = GetWornArmorReadOnly(
            actor,
            RE::BGSBipedObjectForm::BipedObjectSlot::kModChestPrimary);
        if (IsNakedSkinOrGenitalArmor(actor, chestArmor)) {
            chestArmor = nullptr;
        }
        return !bodyArmor && !chestArmor;
    }

    RE::TESObjectARMA* ActorBodyEvaluator::ResolveNakedBodyARMA(RE::Actor* actor) const {
        if (!actor || actor->IsDeleted()) {
            return nullptr;
        }

        auto* npc = actor->GetActorBase();
        auto* race = actor->GetRace();
        if (!race && npc) {
            race = npc->race;
        }

        const std::array<RE::TESObjectARMO*, 3> skinCandidates = {
            actor->GetSkin(),
            npc ? npc->skin : nullptr,
            race ? race->skin : nullptr
        };

        for (RE::TESObjectARMO* skinArMo : skinCandidates) {
            if (!skinArMo) {
                continue;
            }
            RE::TESObjectARMA* fallbackBodyArma = nullptr;
            for (auto* arma : skinArMo->armorAddons) {
                if (!arma || !arma->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) {
                    continue;
                }
                if (!fallbackBodyArma) {
                    fallbackBodyArma = arma;
                }
                if (!race || IsArmaValidForRace(arma, race)) {
                    return arma;
                }
            }
            if (fallbackBodyArma) {
                return fallbackBodyArma;
            }
        }

        return nullptr;
    }

    ActorBodyMetrics ActorBodyEvaluator::EvaluateActorBody(RE::Actor* actor) const {
        ActorBodyMetrics metrics{};
        if (!actor || actor->IsDeleted() || actor->IsChild()) {
            return metrics;
        }

        auto* base = actor->GetActorBase();
        auto* race = actor->GetRace();
        if (!race && base) {
            race = base->race;
        }
        if (!race || race->IsChildRace() || !race->HasKeywordString("ActorTypeNPC")) {
            return metrics;
        }

        const bool isFemale = base && (base->GetSex() == RE::SEX::kFemale);
        const bool isNaked = IsActorNaked(actor);
        metrics.isFemale = isFemale;
        metrics.isNaked = isNaked;

        RE::TESObjectARMA* nakedArma = ResolveNakedBodyARMA(actor);
        std::string modelPath;
        if (nakedArma) {
            const auto sexIdx = isFemale ? RE::SEXES::kFemale : RE::SEXES::kMale;
            const char* rawModel = nakedArma->bipedModels[sexIdx].model.c_str();
            if (rawModel && *rawModel != '\0') {
                modelPath = rawModel;
            }
        }

        if (modelPath.empty()) {
            modelPath = isFemale
                ? "actors\\character\\character assets\\femalebody_1.nif"
                : "actors\\character\\character assets\\malebody_1.nif";
        }

        auto templateCache = NifRingIndexer::GetInstance().GetOrIndexTemplate(modelPath, isFemale);
        if (!templateCache) {
            spdlog::warn("[ActorBodyEvaluator] No body template cache available for '{}'", modelPath);
            return metrics;
        }

        // 1. Base weight interpolation
        const float rawWeight = base ? base->GetWeight() : actor->GetWeight();
        const float w = std::clamp(rawWeight / 100.0f, 0.0f, 1.0f);

        std::array<RE::NiPoint3, kTotalLandmarkVertices> verts{};
        for (std::size_t i = 0; i < kTotalLandmarkVertices; ++i) {
            verts[i] = templateCache->verts0[i] * (1.0f - w) + templateCache->verts1[i] * w;
        }

        constexpr std::size_t kRing5Offset =
            static_cast<std::size_t>(AnatomicalRingId::kMidBelly) * kPointsPerRing;
        std::array<RE::NiPoint3, kPointsPerRing> systemicRing5{};
        for (std::size_t c = 0; c < kPointsPerRing; ++c) {
            systemicRing5[c] = verts[kRing5Offset + c];
        }

        const char* actorName = actor->GetName();
        const std::string_view safeName = (actorName && *actorName != '\0') ? actorName : "<unnamed>";

        // 2. Apply active SKEE body morphs & Section 3.5 geometric pregnancy morph isolation
        float pregMorphForwardDisp = 0.0f;
        std::size_t appliedMorphCount = 0;
        std::size_t reportedMorphCount = 0;
        bool hasActiveMorphs = false;
        std::vector<std::pair<std::string, float>> activeMorphs;
        std::string morphSummary;

        if (m_morphInterface) {
            std::lock_guard skeeLock(m_skeeMutex);
            if (m_morphInterface->HasMorphs(actor)) {
                hasActiveMorphs = true;
                ActiveMorphCollector collector;
                m_morphInterface->VisitMorphs(actor, collector);
                reportedMorphCount = collector.morphNames.size();
                activeMorphs.reserve(reportedMorphCount);
                for (auto& morphName : collector.morphNames) {
                    const float val = m_morphInterface->GetBodyMorphs(actor, morphName.c_str());
                    if (std::fabs(val) >= 1e-4f) {
                        activeMorphs.emplace_back(std::move(morphName), val);
                    }
                }
            }
        }

        if (hasActiveMorphs) {
            for (const auto& [morphName, val] : activeMorphs) {
                auto it = templateCache->triDeltas.find(morphName);
                const bool matchedTri = (it != templateCache->triDeltas.end());
                const bool isPregMorph = matchedTri && it->second.isLocalizedBellyMorph;

                if (matchedTri) {
                    ++appliedMorphCount;
                    const auto& entry = it->second;
                    for (std::size_t i = 0; i < kTotalLandmarkVertices; ++i) {
                        verts[i] += entry.deltas[i] * val;
                    }

                    if (entry.isLocalizedBellyMorph && val > 0.0f) {
                        const float frontDeltaY =
                            entry.deltas[kRing5Offset + static_cast<std::size_t>(CompassDir::kFront)].y;
                        pregMorphForwardDisp += std::max(0.0f, val * frontDeltaY);
                    } else {
                        for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                            systemicRing5[c] += entry.deltas[kRing5Offset + c] * val;
                        }
                    }
                }

                if (morphSummary.size() < 4096 || isPregMorph) {
                    if (!morphSummary.empty()) {
                        morphSummary += ", ";
                    }
                    morphSummary += morphName + "=" + std::to_string(val).substr(0, 5);
                    if (isPregMorph) {
                        morphSummary += "[PREG]";
                    } else if (!matchedTri) {
                        morphSummary += "[no-tri]";
                    }
                }
            }

            spdlog::info(
                "[ActorBodyEvaluator] Actor '{}' ({:08X}): SKEE reported {} morph(s), {} matched TRI deltas ({})",
                safeName,
                actor->GetFormID(),
                reportedMorphCount,
                appliedMorphCount,
                morphSummary.empty() ? "none non-zero" : morphSummary);
        } else {
            spdlog::info(
                "[ActorBodyEvaluator] Actor '{}' ({:08X}): no active SKEE morphs (weight={:.1f})",
                safeName,
                actor->GetFormID(),
                rawWeight);
        }

        // 3. Apply live XPMSE / NiOverride skeleton bone scales
        float bellyNodeScale = 1.0f;
        if (auto* root3D = actor->Get3D(false)) {
            RE::NiAVObject* searchRoot = FindNodeByNameReadOnly(root3D, "NPC Root [Root]");
            if (!searchRoot) {
                searchRoot = root3D;
            }
            bellyNodeScale = ReadBoneScale(root3D, searchRoot, "NPC Belly");

            for (std::size_t r = 0; r < kRingCount; ++r) {
                const auto& ringInfo = templateCache->rings[r];
                const float primaryScale = ReadBoneScale(root3D, searchRoot, ringInfo.primaryBone);
                const float secondaryScale = ReadBoneScale(root3D, searchRoot, ringInfo.secondaryBone);

                const std::size_t baseIdx = r * kPointsPerRing;
                RE::NiPoint3 centroid{};
                for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                    centroid += verts[baseIdx + c];
                }
                centroid /= static_cast<float>(kPointsPerRing);

                if (std::fabs(primaryScale - 1.0f) > 1e-3f) {
                    for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                        auto& p = verts[baseIdx + c];
                        p.x = centroid.x + (p.x - centroid.x) * primaryScale;
                        p.y = centroid.y + (p.y - centroid.y) * primaryScale;
                        if (r == static_cast<std::size_t>(AnatomicalRingId::kUpperArm)) {
                            p.z = centroid.z + (p.z - centroid.z) * primaryScale;
                        }
                    }
                    if (r == static_cast<std::size_t>(AnatomicalRingId::kMidBelly)) {
                        for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                            auto& sp = systemicRing5[c];
                            sp.x = centroid.x + (sp.x - centroid.x) * primaryScale;
                            sp.y = centroid.y + (sp.y - centroid.y) * primaryScale;
                        }
                    }
                }

                // Directional secondary bone scales (Breast +Y, Belly +Y, Butt -Y)
                if (std::fabs(secondaryScale - 1.0f) > 1e-3f) {
                    if (r == static_cast<std::size_t>(AnatomicalRingId::kChestBust) && isFemale) {
                        for (const CompassDir dir : {CompassDir::kFrontRight, CompassDir::kFront, CompassDir::kFrontLeft}) {
                            auto& p = verts[baseIdx + static_cast<std::size_t>(dir)];
                            if (p.y > centroid.y) {
                                p.y = centroid.y + (p.y - centroid.y) * secondaryScale;
                            }
                        }
                    } else if (r == static_cast<std::size_t>(AnatomicalRingId::kMidBelly)) {
                        for (const CompassDir dir : {CompassDir::kFrontRight, CompassDir::kFront, CompassDir::kFrontLeft}) {
                            auto& p = verts[baseIdx + static_cast<std::size_t>(dir)];
                            if (p.y > centroid.y) {
                                p.y = centroid.y + (p.y - centroid.y) * secondaryScale;
                            }
                        }
                    } else if (r == static_cast<std::size_t>(AnatomicalRingId::kHipsGlutes)) {
                        for (const CompassDir dir : {CompassDir::kBackLeft, CompassDir::kBack, CompassDir::kBackRight}) {
                            auto& p = verts[baseIdx + static_cast<std::size_t>(dir)];
                            if (p.y < centroid.y) {
                                p.y = centroid.y + (p.y - centroid.y) * secondaryScale;
                            }
                        }
                    }
                }
            }
        }

        // 4. Compute normalized ring measurements & composite ratios via shared core method
        const float h0 = std::max(10.0f, templateCache->baseReferenceHeight);
        metrics = ComputeMetricsFromVertices(
            verts,
            systemicRing5,
            h0,
            isFemale,
            isNaked,
            rawWeight,
            pregMorphForwardDisp,
            bellyNodeScale);
        metrics.abdomenUV = templateCache->abdomenUV;
        metrics.deltoidUV = templateCache->deltoidUV;

        // 6. Resolve active normal map path
        std::string msnPath = FindSceneGraphBodyMsn(actor);
        std::string_view msnSource = "SceneGraph3D";
        if (msnPath.empty() && nakedArma) {
            const auto sexIdx = isFemale ? RE::SEXES::kFemale : RE::SEXES::kMale;
            if (auto* skinTexSet = nakedArma->skinTextures[sexIdx]) {
                const char* texPath = skinTexSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal);
                if (texPath && *texPath != '\0') {
                    msnPath = texPath;
                    msnSource = "NakedARMA";
                }
            }
        }
        if (msnPath.empty()) {
            msnPath = templateCache->fallbackMsnPath;
            msnSource = "TemplateNIF";
        }
        metrics.activeMsnPath = std::move(msnPath);

        const auto& rShoulders = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kShoulders)];
        const auto& rChest = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kChestBust)];
        const auto& rUnderbust = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kUnderbust)];
        const auto& rWaist = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kNaturalWaist)];
        const auto& rBelly = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kMidBelly)];
        const auto& rHips = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kHipsGlutes)];
        const auto& rThigh = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kUpperThigh)];
        const auto& rCalf = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kCalf)];
        const auto& rArm = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kUpperArm)];

        // Convert normalized measurements to real-world cm and inches (scaled by actor height scale)
        const float actorScale = GetActorHeightScale(actor);
        const float toCm = h0 * actorScale * kSkyrimUnitToCm;
        const float toIn = toCm * kCmToInches;

        spdlog::info(
            "[ActorBodyEvaluator] Measurements '{}' ({:08X}) [scale={:.2f}]: "
            "shoulderWidth={:.1f}cm({:.1f}\") chest={:.1f}cm({:.1f}\") underbust={:.1f}cm({:.1f}\") "
            "waist={:.1f}cm({:.1f}\") belly={:.1f}cm({:.1f}\") hips={:.1f}cm({:.1f}\") "
            "thigh={:.1f}cm({:.1f}\") calf={:.1f}cm({:.1f}\") arm={:.1f}cm({:.1f}\")",
            safeName,
            actor->GetFormID(),
            actorScale,
            rShoulders.width * toCm, rShoulders.width * toIn,
            rChest.girth * toCm, rChest.girth * toIn,
            rUnderbust.girth * toCm, rUnderbust.girth * toIn,
            rWaist.girth * toCm, rWaist.girth * toIn,
            rBelly.girth * toCm, rBelly.girth * toIn,
            rHips.girth * toCm, rHips.girth * toIn,
            rThigh.girth * toCm, rThigh.girth * toIn,
            rCalf.girth * toCm, rCalf.girth * toIn,
            rArm.girth * toCm, rArm.girth * toIn);

        spdlog::info(
            "[ActorBodyEvaluator] Evaluated '{}' ({:08X}) [sex={} w={:.0f} naked={} tpl='{}' msn='{}' ({})]: "
            "ratios=[mass={:.2f} shldr={:.2f} vtaper={:.2f} whr={:.2f} bustCup={:.2f} hips={:.2f} thigh={:.2f} arm={:.2f} bellyDrop={:+.3f}] "
            "S_geo_tone={:.3f} S_preg={:.3f}",
            safeName,
            actor->GetFormID(),
            isFemale ? "F" : "M",
            rawWeight,
            metrics.isNaked,
            templateCache->nifPathKey,
            metrics.activeMsnPath.empty() ? "<none>" : metrics.activeMsnPath,
            msnSource,
            metrics.bodyMassIndex,
            metrics.shoulderToHeight,
            metrics.vTaperRatio,
            metrics.waistToHipRatio,
            metrics.bustToUnderbust,
            metrics.hipsToHeight,
            metrics.upperThighGirth,
            metrics.upperArmGirth,
            metrics.sagittalBellyDrop,
            metrics.geoToneSignal,
            metrics.pregnancyBellyScore);

        return metrics;
    }

    ActorBodyMetrics ActorBodyEvaluator::ComputeMetricsFromVertices(
        const std::array<RE::NiPoint3, kTotalLandmarkVertices>& verts,
        const std::array<RE::NiPoint3, kPointsPerRing>& systemicRing5,
        float baseReferenceHeight,
        bool isFemale,
        bool isNaked,
        [[maybe_unused]] float weight,
        float pregMorphForwardDisp,
        float bellyNodeScale) {
        ActorBodyMetrics metrics{};
        metrics.isFemale = isFemale;
        metrics.isNaked = isNaked;
        metrics.valid = true;

        const float h0 = std::max(10.0f, baseReferenceHeight);
        const float invH0 = 1.0f / h0;

        // Clamp anterior (+Y) vertices of Ring 6 (Hips/Glutes) to the natural pelvic wall
        // (0.82 * underbust front Y) so a lower-belly or pregnancy dome overhang at hip height
        // never inflates Hips circumference or tilts the Belly Drop reference chord forward.
        constexpr std::size_t kUnderBase =
            static_cast<std::size_t>(AnatomicalRingId::kUnderbust) * kPointsPerRing;
        const float underbustFrontY =
            verts[kUnderBase + static_cast<std::size_t>(CompassDir::kFront)].y;
        const float pelvicWallY = 0.82f * underbustFrontY;

        for (std::size_t r = 0; r < kRingCount; ++r) {
            std::array<RE::NiPoint3, kPointsPerRing> ringPts{};
            for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                ringPts[c] = verts[r * kPointsPerRing + c];
                if (r == static_cast<std::size_t>(AnatomicalRingId::kHipsGlutes)) {
                    ringPts[c].y = std::min(ringPts[c].y, pelvicWallY);
                }
            }
            const bool isArmRing = (r == static_cast<std::size_t>(AnatomicalRingId::kUpperArm));
            metrics.rings[r] = ComputeRingMeasurements(ringPts, invH0, isArmRing);
        }

        const RingMeasurements sysMidBelly = ComputeRingMeasurements(systemicRing5, invH0, false);

        const auto& rShoulders = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kShoulders)];
        const auto& rChest = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kChestBust)];
        const auto& rUnderbust = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kUnderbust)];
        const auto& rWaist = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kNaturalWaist)];
        const auto& rBelly = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kMidBelly)];
        const auto& rHips = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kHipsGlutes)];
        const auto& rThigh = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kUpperThigh)];
        const auto& rCalf = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kCalf)];
        const auto& rArm = metrics.rings[static_cast<std::size_t>(AnatomicalRingId::kUpperArm)];

        metrics.shoulderToHeight = rShoulders.width;
        metrics.vTaperRatio = rShoulders.width / std::max(0.05f, rWaist.width);
        metrics.waistToHipRatio = rWaist.girth / std::max(0.10f, rHips.girth);
        if (isFemale) {
            constexpr std::size_t kChestBase =
                static_cast<std::size_t>(AnatomicalRingId::kChestBust) * kPointsPerRing;
            const float apexY = 0.5f *
                (verts[kChestBase + static_cast<std::size_t>(CompassDir::kFrontRight)].y +
                 verts[kChestBase + static_cast<std::size_t>(CompassDir::kFrontLeft)].y) * invH0;
            const float sternumY =
                verts[kChestBase + static_cast<std::size_t>(CompassDir::kFront)].y * invH0;
            const float subMammaryY = 0.5f *
                (verts[kUnderBase + static_cast<std::size_t>(CompassDir::kFrontRight)].y +
                 verts[kUnderBase + static_cast<std::size_t>(CompassDir::kFrontLeft)].y) * invH0;
            const float ribWallY = 0.65f * sternumY + 0.35f * subMammaryY;
            metrics.bustToUnderbust = 1.0f + 6.0f * (apexY - ribWallY);
        } else {
            metrics.bustToUnderbust = rChest.girth / std::max(0.10f, rUnderbust.girth);
        }
        metrics.hipsToHeight = rHips.girth;
        metrics.upperThighGirth = rThigh.girth;
        metrics.calfGirth = rCalf.girth;
        metrics.upperArmGirth = rArm.girth;

        const auto computeBellyDrop = [&](const RingMeasurements& bellyRing) {
            return (0.60f * rUnderbust.frontProj + 0.40f * rHips.frontProj) - bellyRing.frontProj;
        };
        metrics.sagittalBellyDrop = computeBellyDrop(rBelly);
        metrics.flankFlareRatio = rBelly.width / std::max(0.05f, rWaist.width);
        metrics.bodyMassIndex =
            (rUnderbust.girth + rWaist.girth + rHips.girth + rThigh.girth + rArm.girth) * 0.20f;

        const float domeProtrusion = std::max(0.0f, -metrics.sagittalBellyDrop);
        const float bellyEccentricity = rBelly.depth / std::max(0.05f, rBelly.width);
        const float bellyToWaistGirth = rBelly.girth / std::max(0.05f, rWaist.girth);

        const float sPregMorph = std::clamp((pregMorphForwardDisp * invH0) / 0.08f, 0.0f, 1.0f);
        const float sPregNode = std::clamp((bellyNodeScale - 1.05f) / 0.80f, 0.0f, 1.0f);
        const bool isRoundGravidDome =
            isFemale &&
            (domeProtrusion > 0.025f) &&
            (bellyEccentricity > 0.72f || (bellyToWaistGirth > 1.22f && metrics.waistToHipRatio < 0.75f));
        const float sPregGeo = isRoundGravidDome
            ? std::clamp((domeProtrusion - 0.016f) / 0.055f, 0.0f, 1.0f)
            : 0.0f;

        metrics.pregnancyBellyScore = isFemale
            ? std::clamp(std::max({sPregMorph, sPregNode, sPregGeo}), 0.0f, 1.0f)
            : 0.0f;

        const float sysBellyDrop = computeBellyDrop(sysMidBelly);
        const float neutralWHR = isFemale ? 0.70f : 0.84f;
        const float neutralVTaper = isFemale ? 1.95f : 1.90f;

        // Isolate localized pregnancy dome from whole-body tone penalty so gravid bellies
        // do not drag toneIndex down and misclassify average limbs as soft/flabby.
        const float bellyFlatnessTerm =
            (metrics.pregnancyBellyScore >= 0.25f)
                ? 0.0f
                : std::clamp((sysBellyDrop + 0.012f) / 0.020f, -1.0f, 1.0f);
        const float waistTaperTerm = std::clamp((neutralWHR - metrics.waistToHipRatio) / 0.12f, -1.0f, 1.0f);
        const float vTaperTerm = std::clamp((metrics.vTaperRatio - neutralVTaper) / 0.30f, -1.0f, 1.0f);

        metrics.geoToneSignal = std::clamp(
            isFemale
                ? (0.5f * (bellyFlatnessTerm + vTaperTerm) + 0.5f * std::min(0.0f, waistTaperTerm))
                : ((bellyFlatnessTerm + waistTaperTerm + vTaperTerm) / 3.0f),
            -1.0f,
            1.0f);

        return metrics;
    }

    ActorGenitalMetrics ActorBodyEvaluator::EvaluateActorGenitals(RE::Actor* actor) const {
        ActorGenitalMetrics result{};
        if (!actor || actor->IsDeleted() || actor->IsChild() || !IsActorNaked(actor)) {
            return result;
        }

        auto* base = actor->GetActorBase();
        auto* race = actor->GetRace();
        if (!race && base) {
            race = base->race;
        }
        const BeastFormType beastForm = GetActorBeastForm(actor);
        if (!race || race->IsChildRace()) {
            return result;
        }

        const bool isFemale = base && (base->GetSex() == RE::SEX::kFemale);
        if (isFemale && beastForm != BeastFormType::kNone) {
            return result;
        }

        const auto sexIdx = isFemale ? RE::SEXES::kFemale : RE::SEXES::kMale;

        auto getArmaModelPath = [sexIdx](const RE::TESObjectARMA* arma) -> const char* {
            if (!arma) {
                return nullptr;
            }
            const char* rawModel = arma->bipedModels[sexIdx].model.c_str();
            return (rawModel && *rawModel != '\0') ? rawModel : nullptr;
        };

        struct MatchedGenitalMesh {
            const RE::TESObjectARMA* arma = nullptr;
            std::string nifPath;
            std::shared_ptr<const GenitalNifTemplate> tmpl;
        };

        auto findGenitalMeshInArmor = [&](const RE::TESObjectARMO* armo,
                                          bool requireSlot52) -> MatchedGenitalMesh {
            MatchedGenitalMesh matched{};
            if (!armo) {
                return matched;
            }
            for (const auto* arma : armo->armorAddons) {
                if (!arma || !IsArmaValidForRace(arma, race)) {
                    continue;
                }
                if (requireSlot52) {
                    const bool isSlot52Addon =
                        arma->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kModPelvisSecondary) &&
                        !arma->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
                    if (!isSlot52Addon) {
                        continue;
                    }
                }
                const char* rawModel = getArmaModelPath(arma);
                if (!rawModel) {
                    continue;
                }
                auto tmpl = GetOrIndexGenitalNif(rawModel);
                if (!tmpl) {
                    continue;
                }
                matched.arma = arma;
                matched.nifPath = rawModel;
                matched.tmpl = std::move(tmpl);
                break;
            }
            return matched;
        };

        auto findParentAddonArmor = [](const RE::TESObjectARMA* targetArma,
                                       std::string_view modelPath,
                                       const RE::TESObjectARMO* excludeSkin) -> const RE::TESObjectARMO* {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler || (!targetArma && modelPath.empty())) {
                return nullptr;
            }

            const RE::TESObjectARMO* bestMatch = nullptr;
            int bestPriority = -1;
            const std::string lowerTargetModel = ToLowerAscii(modelPath);

            for (const auto* armo : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
                if (!armo || armo == excludeSkin ||
                    armo->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) {
                    continue;
                }

                bool pointerMatch = false;
                bool modelMatch = false;
                for (const auto* arma : armo->armorAddons) {
                    if (!arma) {
                        continue;
                    }
                    if (arma == targetArma) {
                        pointerMatch = true;
                        break;
                    }
                    if (!lowerTargetModel.empty()) {
                        for (std::uint32_t s = 0; s < RE::SEXES::kTotal; ++s) {
                            const char* m = arma->bipedModels[s].model.c_str();
                            if (m && *m != '\0' && ToLowerAscii(m) == lowerTargetModel) {
                                modelMatch = true;
                            }
                        }
                    }
                }

                if (!pointerMatch && !modelMatch) {
                    continue;
                }

                const char* rawName = armo->GetName();
                const bool hasName = (rawName && *rawName != '\0');
                const int priority = (pointerMatch ? 10 : 4) + (hasName ? 5 : 0);
                if (priority > bestPriority) {
                    bestPriority = priority;
                    bestMatch = armo;
                }
            }
            return bestMatch;
        };

        MatchedGenitalMesh activeMesh{};
        const RE::TESObjectARMO* activeGenitalArmor = nullptr;

        // 1. Check Slot 52 (kModPelvisSecondary) worn item:
        //    If a Slot 52 item is worn, check if its NIF contains a 3D genital mesh; if not, it is pelvic covering/underwear.
        if (const auto* slot52Armor = GetWornArmorReadOnly(
                actor,
                RE::BGSBipedObjectForm::BipedObjectSlot::kModPelvisSecondary)) {
            activeMesh = findGenitalMeshInArmor(slot52Armor, false);
            if (!activeMesh.tmpl) {
                return result;
            }
            activeGenitalArmor = slot52Armor;
        }

        // 2. Check actor/NPC/race skin and worn body skin (Slot 52 addon ARMAs first, then full-body creature/genital ARMAs)
        if (!activeMesh.tmpl) {
            const std::array<const RE::TESObjectARMO*, 4> skinCandidates = {
                actor->GetSkin(),
                GetWornArmorReadOnly(actor, RE::BGSBipedObjectForm::BipedObjectSlot::kBody),
                base ? base->skin : nullptr,
                race ? race->skin : nullptr
            };

            for (const auto* skinArmo : skinCandidates) {
                if (!skinArmo) {
                    continue;
                }
                activeMesh = findGenitalMeshInArmor(skinArmo, true);
                if (!activeMesh.tmpl) {
                    activeMesh = findGenitalMeshInArmor(skinArmo, false);
                }
                if (activeMesh.tmpl) {
                    activeGenitalArmor = findParentAddonArmor(activeMesh.arma, activeMesh.nifPath, skinArmo);
                    if (!activeGenitalArmor) {
                        activeGenitalArmor = skinArmo;
                    }
                    break;
                }
            }
        }

        // 3. Check actor inventory for unequipped Slot 52 genital addon items (males only, read-only noInit)
        if (!activeMesh.tmpl && !isFemale) {
            auto trySlot52Armor = [&](const RE::TESObjectARMO* armo) -> bool {
                if (!armo || armo->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody) ||
                    !armo->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kModPelvisSecondary)) {
                    return false;
                }
                activeMesh = findGenitalMeshInArmor(armo, false);
                if (activeMesh.tmpl) {
                    activeGenitalArmor = armo;
                    return true;
                }
                return false;
            };

            const auto* invChanges = actor->GetInventoryChanges(true);
            if (invChanges && invChanges->entryList) {
                std::size_t inspected = 0;
                for (const auto* entry : *invChanges->entryList) {
                    if (++inspected > kMaxInventoryEntries) {
                        break;
                    }
                    if (!entry || !entry->object || !entry->object->IsArmor()) {
                        continue;
                    }
                    const std::int32_t netCount =
                        entry->countDelta + GetBaseContainerCount(actor, entry->object);
                    if (netCount > 0 && trySlot52Armor(entry->object->As<RE::TESObjectARMO>())) {
                        break;
                    }
                }
            }

            if (!activeMesh.tmpl) {
                if (const auto* container = actor->GetContainer();
                    container && container->containerObjects) {
                    const std::uint32_t count =
                        std::min(container->numContainerObjects, kMaxContainerObjects);
                    for (std::uint32_t i = 0; i < count; ++i) {
                        const auto* co = container->containerObjects[i];
                        if (!co || co->count <= 0 || !co->obj || !co->obj->IsArmor()) {
                            continue;
                        }
                        bool foundInChanges = false;
                        const std::int32_t delta =
                            GetContainerDeltaInChanges(invChanges, co->obj, foundInChanges);
                        if (foundInChanges || (co->count + delta) <= 0) {
                            continue;
                        }
                        if (trySlot52Armor(co->obj->As<RE::TESObjectARMO>())) {
                            break;
                        }
                    }
                }
            }
        }

        if (!activeMesh.tmpl) {
            return result;
        }

        result.hasGenitals = true;
        if (activeGenitalArmor) {
            if (const char* name = activeGenitalArmor->GetName(); name && *name != '\0') {
                result.addonName = name;
            }
            if (const auto* file = activeGenitalArmor->GetFile(0); file && file->fileName[0] != '\0') {
                result.addonSourceFile = file->fileName;
            }
        }
        if (result.addonSourceFile.empty() && activeMesh.arma) {
            if (const auto* file = activeMesh.arma->GetFile(0); file && file->fileName[0] != '\0') {
                result.addonSourceFile = file->fileName;
            }
        }
        if (result.addonName.empty()) {
            if (beastForm == BeastFormType::kWerewolf) {
                result.addonName = "Werewolf";
            } else if (beastForm == BeastFormType::kVampireLord) {
                result.addonName = "Vampire Lord";
            } else if (beastForm == BeastFormType::kWerebear) {
                result.addonName = "Werebear";
            }
        }

        const std::string& genitalNifPath = activeMesh.nifPath;
        const auto& genitalTemplate = activeMesh.tmpl;

        // 4. Interpolate _0.nif and _1.nif base dimensions by actor weight if a weight-slider pair exists
        float baseLengthUnits = genitalTemplate->baseLengthUnits;
        float baseGirthUnits = genitalTemplate->baseGirthUnits;

        if (genitalNifPath.size() > 6) {
            const std::string lowerPath = ToLowerAscii(genitalNifPath);
            if (lowerPath.ends_with("_1.nif") || lowerPath.ends_with("_0.nif")) {
                std::string pairPath = lowerPath;
                pairPath[pairPath.size() - 5] = lowerPath.ends_with("_1.nif") ? '0' : '1';
                if (auto pairTemplate = GetOrIndexGenitalNif(pairPath)) {
                    const float rawWeight = base ? base->GetWeight() : actor->GetWeight();
                    const float w = std::clamp(rawWeight / 100.0f, 0.0f, 1.0f);
                    const auto& t0 = lowerPath.ends_with("_0.nif") ? genitalTemplate : pairTemplate;
                    const auto& t1 = lowerPath.ends_with("_1.nif") ? genitalTemplate : pairTemplate;
                    baseLengthUnits = t0->baseLengthUnits * (1.0f - w) + t1->baseLengthUnits * w;
                    baseGirthUnits = t0->baseGirthUnits * (1.0f - w) + t1->baseGirthUnits * w;
                }
            }
        }

        // 5. Real-time 3D skeleton bone-chain measurement & flaccid/erect state detection
        auto* root3D = actor->Get3D(false);
        const float actorScale = GetActorHeightScale(actor);
        result.actorScale = actorScale;

        std::vector<RE::NiAVObject*> liveChainNodes;
        if (root3D && !genitalTemplate->shaftBoneChain.empty()) {
            RE::NiAVObject* searchRoot = FindNodeByNameReadOnly(root3D, "NPC Root [Root]");
            if (!searchRoot) {
                searchRoot = root3D;
            }
            for (const auto& bName : genitalTemplate->shaftBoneChain) {
                if (auto* node = FindNodeByNameReadOnly(searchRoot, bName)) {
                    liveChainNodes.push_back(node);
                }
            }
        }

        float lengthStretch = 1.0f;
        float girthStretch = 1.0f;
        float pitchDeg = 0.0f;
        bool isErect = false;

        if (!liveChainNodes.empty()) {
            float scaleSum = 0.0f;
            std::size_t scaleCount = 0;

            for (const auto* node : liveChainNodes) {
                const float relScale = node->world.scale / actorScale;
                if (std::isfinite(relScale) && relScale > 0.2f && relScale < 4.0f) {
                    scaleSum += relScale;
                    ++scaleCount;
                }
            }

            if (scaleCount > 0) {
                girthStretch = scaleSum / static_cast<float>(scaleCount);
            }
            if (std::fabs(girthStretch - 1.0f) < 0.01f) {
                if (const auto* parentBase = liveChainNodes.front()->parent) {
                    if (std::isfinite(parentBase->local.scale) &&
                        parentBase->local.scale > 0.2f && parentBase->local.scale < 4.0f &&
                        std::fabs(parentBase->local.scale - 1.0f) >= 0.01f) {
                        girthStretch = parentBase->local.scale;
                    }
                }
            }
            lengthStretch = girthStretch;

            if (liveChainNodes.size() >= 2) {
                // Detect flaccid vs. hard state from sagittal pitch angle relative to parent/pelvis local space
                const RE::NiPoint3 vWorld =
                    liveChainNodes.back()->world.translate - liveChainNodes.front()->world.translate;
                const RE::NiAVObject* refNode = liveChainNodes.front()->parent
                    ? liveChainNodes.front()->parent
                    : root3D;
                if (refNode && refNode->parent && refNode != root3D) {
                    refNode = refNode->parent;
                }
                const RE::NiMatrix3& refRot = refNode ? refNode->world.rotate : root3D->world.rotate;
                const RE::NiPoint3 vLocal = refRot.Transpose() * vWorld;
                const float vNorm = std::sqrt(vLocal.x * vLocal.x + vLocal.y * vLocal.y + vLocal.z * vLocal.z);
                if (vNorm > 0.5f) {
                    pitchDeg = std::atan2(vLocal.z, vLocal.y) * (180.0f / std::numbers::pi_v<float>);
                    isErect = (pitchDeg >= -25.0f);
                }
            }
        }

        result.isErect = isErect;
        result.lengthCm = baseLengthUnits * lengthStretch * kSkyrimUnitToCm;
        result.girthCm = baseGirthUnits * girthStretch * kSkyrimUnitToCm;

        const float worldLengthCm = result.lengthCm * actorScale;
        const float worldGirthCm = result.girthCm * actorScale;

        const char* actorName = actor->GetName();
        spdlog::info(
            "[ActorBodyEvaluator] Genitals '{}' ({:08X}): addon='{}' nif='{}' state='{}' (pitch={:.1f}deg) "
            "base={:.1f}cm stretch={:.2f}x actorScale={:.2f} -> length={:.1f}cm({:.1f}\") girth={:.1f}cm({:.1f}\")",
            (actorName && *actorName != '\0') ? actorName : "<unnamed>",
            actor->GetFormID(),
            result.addonName,
            genitalNifPath,
            result.isErect ? "hard" : "flaccid",
            pitchDeg,
            baseLengthUnits * kSkyrimUnitToCm,
            lengthStretch,
            actorScale,
            worldLengthCm, worldLengthCm * kCmToInches,
            worldGirthCm, worldGirthCm * kCmToInches);

        return result;
    }

}  // namespace BodyLanguage
