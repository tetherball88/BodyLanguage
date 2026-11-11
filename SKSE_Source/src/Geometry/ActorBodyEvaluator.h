#pragma once

#include "Geometry/BodyTemplateCache.h"
#include "external/SKEEInterface.h"

namespace BodyLanguage {

    class ActorBodyEvaluator {
    public:
        static ActorBodyEvaluator& GetInstance();

        // Call at kPostPostLoad to acquire SKEE::IBodyMorphInterface from skee64.dll.
        void InitSKEE();

        // Returns the active beast form (Werewolf, Vampire Lord, Werebear) if the actor is transformed, or kNone.
        BeastFormType GetActorBeastForm(RE::Actor* actor) const;

        // Returns the config key ("werewolf", "vampire_lord", "werebear", or "") for a BeastFormType.
        static std::string_view BeastFormToKey(BeastFormType beastForm);

        // Returns the actor's effective height scale combining reference scale, TESNPC height,
        // TESRace sex height, and live 3D skeleton root scale (RaceMenu / XPMSE height sliders).
        float GetActorHeightScale(RE::Actor* actor) const;

        // Returns true if the actor is not wearing armor in slot 32 (kBody) or slot 49 (kModChestPrimary).
        bool IsActorNaked(RE::Actor* actor) const;

        // Resolves the actor's underlying naked body TESObjectARMA for slot 32 (kBody).
        RE::TESObjectARMA* ResolveNakedBodyARMA(RE::Actor* actor) const;

        // Evaluates the actor's 80 naked landmark vertices, SKEE morphs, XPMSE bone scales,
        // and computes normalized geometric ratios + decoupled pregnancy belly score.
        ActorBodyMetrics EvaluateActorBody(RE::Actor* actor) const;

        // Pure geometric evaluation of 80 landmark vertices into normalized ActorBodyMetrics.
        static ActorBodyMetrics ComputeMetricsFromVertices(
            const std::array<RE::NiPoint3, kTotalLandmarkVertices>& verts,
            const std::array<RE::NiPoint3, kPointsPerRing>& systemicRing5,
            float baseReferenceHeight,
            bool isFemale,
            bool isNaked,
            float weight,
            float pregMorphForwardDisp = 0.0f,
            float bellyNodeScale = 1.0f);

        // Evaluates the actor's exposed genital addon and size multiplier (supporting both TNG and SOS).
        ActorGenitalMetrics EvaluateActorGenitals(RE::Actor* actor) const;

    private:
        ActorBodyEvaluator() = default;
        SKEE::IBodyMorphInterface* m_morphInterface{nullptr};
    };

}  // namespace BodyLanguage

