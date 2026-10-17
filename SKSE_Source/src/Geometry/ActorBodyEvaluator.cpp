#include "PCH.h"

#include "Geometry/ActorBodyEvaluator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
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

        class ActiveMorphCollector final : public SKEE::IBodyMorphInterface::MorphVisitor {
        public:
            std::vector<std::string> morphNames;

            void Visit(RE::TESObjectREFR*, const char* morphName) override {
                if (morphName && *morphName != '\0') {
                    morphNames.emplace_back(morphName);
                }
            }
        };

        float ReadBoneScale(RE::NiAVObject* root3D, std::string_view boneName) {
            if (!root3D || boneName.empty()) {
                return 1.0f;
            }
            if (auto* node = root3D->GetObjectByName(RE::BSFixedString(boneName.data()))) {
                const float s = node->local.scale;
                if (std::isfinite(s) && s > 0.1f && s < 5.0f) {
                    return s;
                }
            }
            return 1.0f;
        }

        std::string FindSceneGraphBodyMsn(RE::Actor* actor) {
            if (!actor) {
                return {};
            }
            auto* root = actor->Get3D(false);
            if (!root) {
                return {};
            }

            std::string foundPath;
            RE::BSVisit::TraverseScenegraphGeometries(
                root,
                [&](RE::BSGeometry* geo) -> RE::BSVisit::BSVisitControl {
                    if (!geo) {
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }
                    auto* shaderProp = geo->lightingShaderProp_cast();
                    if (!shaderProp) {
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }
                    auto* mat = static_cast<RE::BSLightingShaderMaterialBase*>(
                        shaderProp->GetBaseMaterial());
                    if (!mat) {
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }
                    auto texSet = mat->GetTextureSet();
                    if (!texSet) {
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }
                    const char* path = texSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal);
                    if (!path || *path == '\0') {
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }

                    const std::string fname = ToLowerAscii(
                        std::filesystem::path(path).filename().string());
                    if (fname.find("body") != std::string::npos &&
                        fname.find("msn") != std::string::npos) {
                        foundPath = path;
                        return RE::BSVisit::BSVisitControl::kStop;
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });

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

    bool ActorBodyEvaluator::IsActorNaked(RE::Actor* actor) const {
        if (!actor) {
            return false;
        }
        const auto* bodyArmor = actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
        const auto* chestArmor = actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kModChestPrimary);
        return !bodyArmor && !chestArmor;
    }

    RE::TESObjectARMA* ActorBodyEvaluator::ResolveNakedBodyARMA(RE::Actor* actor) const {
        if (!actor) {
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
                if (!race || arma->IsValidRace(race)) {
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
        if (!actor || actor->IsChild()) {
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
        metrics.isFemale = isFemale;
        metrics.isNaked = IsActorNaked(actor);

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

        metrics.abdomenUV = templateCache->abdomenUV;
        metrics.deltoidUV = templateCache->deltoidUV;

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
        std::string morphSummary;

        if (m_morphInterface && m_morphInterface->HasMorphs(actor)) {
            ActiveMorphCollector collector;
            m_morphInterface->VisitMorphs(actor, collector);

            for (const auto& morphName : collector.morphNames) {
                const float val = m_morphInterface->GetBodyMorphs(actor, morphName.c_str());
                if (std::fabs(val) < 1e-4f) {
                    continue;
                }

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

                if (morphSummary.size() < 240) {
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
                collector.morphNames.size(),
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
            bellyNodeScale = ReadBoneScale(root3D, "NPC Belly");

            for (std::size_t r = 0; r < kRingCount; ++r) {
                const auto& ringInfo = templateCache->rings[r];
                const float primaryScale = ReadBoneScale(root3D, ringInfo.primaryBone);
                const float secondaryScale = ReadBoneScale(root3D, ringInfo.secondaryBone);

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

        // 4. Compute normalized ring measurements & composite ratios
        const float h0 = std::max(10.0f, templateCache->baseReferenceHeight);
        const float invH0 = 1.0f / h0;

        for (std::size_t r = 0; r < kRingCount; ++r) {
            std::array<RE::NiPoint3, kPointsPerRing> ringPts{};
            for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                ringPts[c] = verts[r * kPointsPerRing + c];
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

        // Direct, body-agnostic anthropometric ratios (dimensionless, divided by H_0 or paired ring)
        metrics.shoulderToHeight = rShoulders.width;
        metrics.vTaperRatio = rShoulders.width / std::max(0.05f, rWaist.width);
        metrics.waistToHipRatio = rWaist.girth / std::max(0.10f, rHips.girth);
        metrics.bustToUnderbust = rChest.girth / std::max(0.10f, rUnderbust.girth);
        metrics.hipsToHeight = rHips.girth;
        metrics.upperThighGirth = rThigh.girth;
        metrics.calfGirth = rCalf.girth;
        metrics.upperArmGirth = rArm.girth;
        metrics.sagittalBellyDrop =
            (rUnderbust.frontProj - rBelly.frontProj) - templateCache->baseSagittalBellyDrop;
        metrics.flankFlareRatio = rBelly.width / std::max(0.05f, rWaist.width);

        // Systemic Frame Mass M_body = unweighted mean of the 5 core height-normalized girths
        metrics.bodyMassIndex =
            (rUnderbust.girth + rWaist.girth + rHips.girth + rThigh.girth + rArm.girth) * 0.20f;

        // 5. Section 3.5: Decoupled Pregnancy Belly Score & Systemic Build
        const float domeProtrusion = std::max(0.0f, -metrics.sagittalBellyDrop);
        const float bellyEccentricity = rBelly.depth / std::max(0.05f, rBelly.width);
        const float sysFlankFlare = sysMidBelly.width / std::max(0.05f, rWaist.width);

        const float sPregMorph = std::clamp((pregMorphForwardDisp * invH0) / 0.08f, 0.0f, 1.0f);
        const float sPregNode = std::clamp((bellyNodeScale - 1.05f) / 0.80f, 0.0f, 1.0f);
        const float sPregGeo =
            (domeProtrusion > 0.030f && bellyEccentricity > 0.84f && sysFlankFlare < 1.35f)
                ? std::clamp((domeProtrusion - 0.025f) / 0.10f, 0.0f, 1.0f)
                : 0.0f;

        metrics.pregnancyBellyScore = std::clamp(
            std::max({sPregMorph, sPregNode, sPregGeo}), 0.0f, 1.0f);

        // Systemic Geometric Tone S_geo_tone (pure 3D geometry: flat stomach + V-taper minus waist-to-hip thickness)
        const float sysBellyDrop =
            (rUnderbust.frontProj - sysMidBelly.frontProj) - templateCache->baseSagittalBellyDrop;
        const float neutralWHR = isFemale ? 0.70f : 0.84f;
        const float neutralVTaper = isFemale ? 1.95f : 1.90f;

        const float bellyFlatnessTerm = std::clamp(sysBellyDrop / 0.030f, -1.0f, 1.0f);
        const float waistTaperTerm = std::clamp((neutralWHR - metrics.waistToHipRatio) / 0.12f, -1.0f, 1.0f);
        const float vTaperTerm = std::clamp((metrics.vTaperRatio - neutralVTaper) / 0.30f, -1.0f, 1.0f);

        metrics.geoToneSignal = std::clamp(
            (bellyFlatnessTerm + waistTaperTerm + vTaperTerm) / 3.0f,
            -1.0f,
            1.0f);

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
        metrics.valid = true;

        // Convert normalized measurements to real-world cm and inches (scaled by actor height scale)
        const float rawScale = actor->GetScale();
        const float actorScale = (std::isfinite(rawScale) && rawScale > 0.2f && rawScale < 3.0f) ? rawScale : 1.0f;
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
            "S_geo_tone={:.3f} S_preg={:.3f} (morph={:.2f}, node={:.2f}, geo={:.2f})",
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
            metrics.pregnancyBellyScore,
            sPregMorph,
            sPregNode,
            sPregGeo);

        return metrics;
    }

    ActorGenitalMetrics ActorBodyEvaluator::EvaluateActorGenitals(RE::Actor* actor) const {
        ActorGenitalMetrics result{};
        if (!actor || actor->IsChild() || !IsActorNaked(actor)) {
            return result;
        }

        auto* base = actor->GetActorBase();
        auto* race = actor->GetRace();
        if (!race && base) {
            race = base->race;
        }
        if (!race || race->IsChildRace() || !race->HasKeywordString("ActorTypeNPC")) {
            return result;
        }

        if (actor->HasKeywordString("TNG_Ignored") || actor->HasKeywordString("TNG_Excluded")) {
            return result;
        }

        // Check if pelvis is covered by TNG underwear/covering on Slot 52 (kModPelvisSecondary)
        const auto* slot52Armor = actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kModPelvisSecondary);
        if (slot52Armor && slot52Armor->HasKeywordString("TNG_Covering")) {
            return result;
        }

        auto isGenitalAddonArmor = [](const RE::TESObjectARMO* armo) -> bool {
            if (!armo || armo->HasKeywordString("TNG_Covering")) {
                return false;
            }
            if (armo->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) {
                return false;
            }
            if (armo->HasKeywordString("SOS_Genitals") || armo->HasKeywordString("TNG_Gentlewoman")) {
                return true;
            }

            const char* rawName = armo->GetName();
            const std::string lowerName = rawName ? ToLowerAscii(rawName) : std::string{};
            if (lowerName.starts_with("$tng_") ||
                lowerName.find("schlong") != std::string::npos ||
                lowerName.find("addon") != std::string::npos ||
                lowerName.find("trx") != std::string::npos ||
                lowerName.find("vectorplexus") != std::string::npos ||
                lowerName.find("smurf") != std::string::npos ||
                lowerName.find("erf") != std::string::npos ||
                lowerName.find("leito") != std::string::npos) {
                return true;
            }

            if (const auto* file = armo->GetFile(0); file && file->fileName[0] != '\0') {
                const std::string lowerFile = ToLowerAscii(file->fileName);
                if (lowerFile == "thenewgentleman.esp" ||
                    lowerFile == "schlongs of skyrim.esp" ||
                    lowerFile.find("sos") != std::string::npos ||
                    lowerFile.find("tng") != std::string::npos ||
                    lowerFile.find("schlong") != std::string::npos ||
                    lowerFile.find("trx") != std::string::npos ||
                    lowerFile.find("vectorplexus") != std::string::npos ||
                    lowerFile.find("smurf") != std::string::npos) {
                    return true;
                }
            }

            for (const auto* arma : armo->armorAddons) {
                if (!arma) {
                    continue;
                }
                for (std::uint32_t s = 0; s < RE::SEXES::kTotal; ++s) {
                    const char* rawModel = arma->bipedModels[s].model.c_str();
                    if (!rawModel || *rawModel == '\0') {
                        continue;
                    }
                    const std::string lowerModel = ToLowerAscii(rawModel);
                    if (lowerModel.find("schlong") != std::string::npos ||
                        lowerModel.find("genital") != std::string::npos ||
                        lowerModel.find("penis") != std::string::npos ||
                        lowerModel.find("tng") != std::string::npos ||
                        lowerModel.find("sos") != std::string::npos ||
                        lowerModel.find("trx") != std::string::npos ||
                        lowerModel.find("vectorplexus") != std::string::npos ||
                        lowerModel.find("smurf") != std::string::npos) {
                        return true;
                    }
                }
            }

            return false;
        };

        // 1. Check active Slot 52 (kModPelvisSecondary) worn armor (used by both TNG and SOS)
        if (isGenitalAddonArmor(slot52Armor)) {
            result.hasGenitals = true;
            if (const char* name = slot52Armor->GetName(); name && *name != '\0') {
                result.addonName = name;
            }
            if (const auto* file = slot52Armor->GetFile(0); file && file->fileName[0] != '\0') {
                result.addonSourceFile = file->fileName;
            }
        }

        // 2. Check actor inventory for unequipped SOS/TNG schlong armor items
        if (!result.hasGenitals) {
            const auto inv = actor->GetInventory([](RE::TESBoundObject& obj) {
                return obj.IsArmor();
            });
            for (const auto& [item, invData] : inv) {
                if (invData.first <= 0) {
                    continue;
                }
                const auto* armo = item ? item->As<RE::TESObjectARMO>() : nullptr;
                if (isGenitalAddonArmor(armo)) {
                    result.hasGenitals = true;
                    if (const char* name = armo->GetName(); name && *name != '\0') {
                        result.addonName = name;
                    }
                    if (const auto* file = armo->GetFile(0); file && file->fileName[0] != '\0') {
                        result.addonSourceFile = file->fileName;
                    }
                    break;
                }
            }
        }

        // 3. Inspect SOS factions (SOS_SchlongifiedFaction and SOS addon factions) for rank & addon ESP
        std::int32_t sosFactionRank = -1;
        actor->VisitFactions([&](RE::TESFaction* faction, std::int8_t rank) -> bool {
            if (!faction || rank < 0) {
                return false;
            }
            const auto* file = faction->GetFile(0);
            const std::string lowerFile = (file && file->fileName[0] != '\0')
                ? ToLowerAscii(file->fileName)
                : std::string{};
            const char* rawName = faction->GetName();
            const std::string lowerName = rawName ? ToLowerAscii(rawName) : std::string{};

            const bool isSosCore = (lowerFile == "schlongs of skyrim.esp");
            const bool isSosAddon = !isSosCore && (
                lowerFile.find("sos") != std::string::npos ||
                lowerFile.find("schlong") != std::string::npos ||
                lowerName.find("schlong") != std::string::npos ||
                lowerName.find("sos_") != std::string::npos);

            if (isSosCore || isSosAddon) {
                sosFactionRank = std::max(sosFactionRank, static_cast<std::int32_t>(rank));
                if (isSosAddon && !result.hasGenitals) {
                    result.hasGenitals = true;
                    if (rawName && *rawName != '\0') {
                        result.addonName = rawName;
                    }
                    if (file && file->fileName[0] != '\0') {
                        result.addonSourceFile = file->fileName;
                    }
                }
            }
            return false;
        });

        // 4. Fallback for TNG / SOS when 3D or Slot 52 item hasn't been equipped yet
        const bool isFemale = base && (base->GetSex() == RE::SEX::kFemale);
        const bool eligibleSex = !isFemale || actor->HasKeywordString("TNG_Gentlewoman");
        if (!result.hasGenitals && eligibleSex) {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            const bool tngLoaded = dataHandler && (dataHandler->LookupModByName("TheNewGentleman.esp") != nullptr);
            if (tngLoaded) {
                result.hasGenitals = true;
                result.addonName = "$TNG_TRT";
                result.addonSourceFile = "TheNewGentleman.esp";
            } else if (sosFactionRank >= 0) {
                result.hasGenitals = true;
                result.addonName = "$TNG_TRT";
                result.addonSourceFile = "Schlongs of Skyrim.esp";
            }
        }

        if (!result.hasGenitals) {
            return result;
        }

        // 5. Resolve effective genital size multiplier (3D XPMSE bone scale -> TNG keywords -> SOS faction rank)
        float boneScale = 1.0f;
        if (auto* root3D = actor->Get3D(false)) {
            boneScale = ReadBoneScale(root3D, "NPC GenitalsBase [GenBase]");
            if (std::fabs(boneScale - 1.0f) <= 0.01f) {
                boneScale = ReadBoneScale(root3D, "NPC Genitals01 [Gen01]");
            }
        }

        float keywordMult = -1.0f;
        if (actor->HasKeywordString("TNG_XS")) {
            keywordMult = 0.80f;
        } else if (actor->HasKeywordString("TNG_S")) {
            keywordMult = 0.90f;
        } else if (actor->HasKeywordString("TNG_M")) {
            keywordMult = 1.00f;
        } else if (actor->HasKeywordString("TNG_L")) {
            keywordMult = 1.20f;
        } else if (actor->HasKeywordString("TNG_XL")) {
            keywordMult = 1.40f;
        }

        if (std::fabs(boneScale - 1.0f) > 0.01f) {
            result.sizeMultiplier = boneScale;
        } else if (keywordMult > 0.0f) {
            result.sizeMultiplier = keywordMult;
        } else if (sosFactionRank >= 1) {
            if (sosFactionRank == 1) {
                result.sizeMultiplier = 0.75f;
            } else if (sosFactionRank == 2) {
                result.sizeMultiplier = 0.88f;
            } else if (sosFactionRank <= 5) {
                result.sizeMultiplier = 1.00f;
            } else if (sosFactionRank <= 8) {
                result.sizeMultiplier = 1.25f;
            } else {
                result.sizeMultiplier = 1.45f;
            }
        } else {
            result.sizeMultiplier = 1.00f;
        }

        const float rawScale = actor->GetScale();
        const float actorScale = (std::isfinite(rawScale) && rawScale > 0.2f && rawScale < 3.0f) ? rawScale : 1.0f;

        const float lengthCm = 15.24f * result.sizeMultiplier * actorScale;
        const float girthCm = 12.70f * result.sizeMultiplier * actorScale;
        const float lengthIn = lengthCm * kCmToInches;
        const float girthIn = girthCm * kCmToInches;

        const char* actorName = actor->GetName();
        spdlog::info(
            "[ActorBodyEvaluator] Genitals '{}' ({:08X}): addon='{}' file='{}' "
            "length={:.1f}cm({:.1f}\") girth={:.1f}cm({:.1f}\") "
            "sizeMult={:.2f} (bone={:.2f}, kw={:.2f}, sosRank={})",
            (actorName && *actorName != '\0') ? actorName : "<unnamed>",
            actor->GetFormID(),
            result.addonName,
            result.addonSourceFile,
            lengthCm, lengthIn,
            girthCm, girthIn,
            result.sizeMultiplier,
            boneScale,
            keywordMult,
            sosFactionRank);

        return result;
    }

}  // namespace BodyLanguage

