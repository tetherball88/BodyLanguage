#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <unordered_map>
#include <vector>

#include "RE/Skyrim.h"

namespace BodyLanguage {

    // Creation Engine scale: 128 Skyrim units = 6 ft = 182.88 cm (1 unit = 1.42875 cm = 0.5625 in)
    inline constexpr float kSkyrimUnitToCm = 1.42875f;
    inline constexpr float kCmToInches = 1.0f / 2.54f;

    enum class AnatomicalRingId : std::size_t {
        kNeck = 0,          // Ring 0: Neck / Base (Reference Z_top)
        kShoulders = 1,     // Ring 1: Shoulders / Deltoids (Deltoid UV top)
        kChestBust = 2,     // Ring 2: Upper Chest / Bust Peak (Abdomen UV top)
        kUnderbust = 3,     // Ring 3: Underbust / Ribcage (Abdomen UV sternum)
        kNaturalWaist = 4,  // Ring 4: Natural Waist (Abdomen UV left/right)
        kMidBelly = 5,      // Ring 5: Mid-Belly / Flanks (Abdomen UV center)
        kHipsGlutes = 6,    // Ring 6: Hips / Glute Peak (Abdomen UV bottom)
        kUpperThigh = 7,    // Ring 7: Upper Thigh (Left femur upper 1/3)
        kCalf = 8,          // Ring 8: Calf (Left calf max cross-section)
        kUpperArm = 9,      // Ring 9: Upper Arm (Left bicep/tricep midpoint, Deltoid UV bottom)
        kCount = 10
    };

    enum class CompassDir : std::size_t {
        kRight = 0,       //   0 deg (+X)
        kFrontRight = 1,  //  45 deg (+X, +Y)
        kFront = 2,       //  90 deg (+Y Anterior)
        kFrontLeft = 3,   // 135 deg (-X, +Y)
        kLeft = 4,        // 180 deg (-X)
        kBackLeft = 5,    // 225 deg (-X, -Y)
        kBack = 6,        // 270 deg (-Y Posterior)
        kBackRight = 7,   // 315 deg (+X, -Y)
        kPointsPerRing = 8
    };

    inline constexpr std::size_t kRingCount = static_cast<std::size_t>(AnatomicalRingId::kCount);
    inline constexpr std::size_t kPointsPerRing = static_cast<std::size_t>(CompassDir::kPointsPerRing);
    inline constexpr std::size_t kTotalLandmarkVertices = kRingCount * kPointsPerRing;  // 80

    struct OctagonRingIndices {
        std::array<std::uint16_t, kPointsPerRing> vIdx{};
        std::string primaryBone;
        std::string secondaryBone;
    };

    struct UVRect {
        float uMin{0.35f};
        float uMax{0.65f};
        float vMin{0.35f};
        float vMax{0.65f};
    };

    struct MorphDeltaEntry {
        std::array<RE::NiPoint3, kTotalLandmarkVertices> deltas{};
        bool isLocalizedBellyMorph{false};  // Section 3.5: True if Ring 5 +Y delta > 4x Ring 7 & Ring 9 radial delta
    };

    struct RingMeasurements {
        float girth{0.0f};      // Circumference G_r (normalized by H_0)
        float width{0.0f};      // Coronal X-span W_r (normalized by H_0)
        float depth{0.0f};      // Sagittal Y-span D_r (normalized by H_0)
        float frontProj{0.0f};  // Anterior +Y projection Y+_r (normalized by H_0)
        float backProj{0.0f};   // Posterior -Y projection Y-_r (normalized by H_0)
    };

    // Computes smooth anatomical tape-measure circumference (combining Ramanujan ellipse perimeter
    // and 8-spoke radial circumference) so sector zig-zag never inflates girth, and multiplying by
    // (H_0 * kSkyrimUnitToCm) yields exact real-world cm / inches.
    // For Ring 9 (isArmRing = true), projects onto the plane perpendicular to Skyrim's 45-degree A-pose
    // left humerus shaft: u = Y (anterior-posterior), v = (Z - X) / sqrt(2) (superior-inferior).
    inline RingMeasurements ComputeRingMeasurements(
        const std::array<RE::NiPoint3, kPointsPerRing>& pts,
        float invH0,
        bool isArmRing = false) {
        RingMeasurements m{};
        constexpr float kInvSqrt2 = 0.70710678f;

        auto projU = [isArmRing](const RE::NiPoint3& p) -> float {
            return isArmRing ? p.y : p.x;
        };
        auto projV = [isArmRing](const RE::NiPoint3& p) -> float {
            return isArmRing ? ((p.z - p.x) * kInvSqrt2) : p.y;
        };

        float minU = std::numeric_limits<float>::max();
        float maxU = std::numeric_limits<float>::lowest();
        float minV = std::numeric_limits<float>::max();
        float maxV = std::numeric_limits<float>::lowest();
        float sumU = 0.0f;
        float sumV = 0.0f;

        for (std::size_t k = 0; k < kPointsPerRing; ++k) {
            const float u = projU(pts[k]);
            const float v = projV(pts[k]);
            minU = std::min(minU, u);
            maxU = std::max(maxU, u);
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
            sumU += u;
            sumV += v;
        }

        const float centerU = sumU / static_cast<float>(kPointsPerRing);
        const float centerV = sumV / static_cast<float>(kPointsPerRing);

        float radialSum = 0.0f;
        for (std::size_t k = 0; k < kPointsPerRing; ++k) {
            const float du = projU(pts[k]) - centerU;
            const float dv = projV(pts[k]) - centerV;
            radialSum += std::sqrt(du * du + dv * dv);
        }

        const auto& rightPt = pts[static_cast<std::size_t>(CompassDir::kRight)];
        const auto& leftPt = pts[static_cast<std::size_t>(CompassDir::kLeft)];
        const auto& frontPt = pts[static_cast<std::size_t>(CompassDir::kFront)];
        const auto& backPt = pts[static_cast<std::size_t>(CompassDir::kBack)];

        const float spanU = std::max(maxU - minU, std::fabs(projU(rightPt) - projU(leftPt)));
        const float spanV = std::max(maxV - minV, std::fabs(projV(frontPt) - projV(backPt)));

        const float ellipseCirc =
            0.5f * std::numbers::pi_v<float> *
            (1.5f * (spanU + spanV) - std::sqrt(std::max(0.0f, spanU * spanV)));
        const float radialCirc =
            (2.0f * std::numbers::pi_v<float> / static_cast<float>(kPointsPerRing)) * radialSum;

        const float smoothCirc = 0.5f * ellipseCirc + 0.5f * radialCirc;

        m.girth = smoothCirc * invH0;
        m.width = spanU * invH0;
        m.depth = spanV * invH0;

        if (isArmRing) {
            m.frontProj = rightPt.y * invH0;
            m.backProj = -leftPt.y * invH0;
        } else {
            m.frontProj = frontPt.y * invH0;
            m.backProj = -backPt.y * invH0;
        }

        return m;
    }

    struct BodyTemplateCache {
        std::string nifPathKey;
        std::string triPath;
        std::string fallbackMsnPath;
        bool isFemale{true};
        float baseReferenceHeight{120.0f};  // Z_neck - Z_ankle in _1.nif
        std::array<OctagonRingIndices, kRingCount> rings{};
        std::array<RE::NiPoint3, kTotalLandmarkVertices> verts0{};  // _0.nif
        std::array<RE::NiPoint3, kTotalLandmarkVertices> verts1{};  // _1.nif

        // Neutral weight-50 (w = 0.5, zero morphs, bone scale 1.0) baseline ring metrics
        std::array<RingMeasurements, kRingCount> baseRings{};
        float baseSagittalBellyDrop{0.0f};

        UVRect abdomenUV{};
        UVRect deltoidUV{};
        std::unordered_map<std::string, MorphDeltaEntry> triDeltas;
    };

    struct ActorBodyMetrics {
        bool valid{false};
        bool isFemale{true};
        bool isNaked{false};

        // Height-normalized ring measurements (divided by baseReferenceHeight H_0)
        std::array<RingMeasurements, kRingCount> rings{};

        // Direct, body-agnostic anthropometric ratios (dimensionless, independent of template mesh):
        float bodyMassIndex{0.0f};       // M_body = mean of height-normalized torso & limb girths
        float shoulderToHeight{0.0f};    // W_shoulders / H_0 (bideltoid breadth index)
        float vTaperRatio{1.25f};        // W_shoulders / W_waist (shoulder-to-waist ratio)
        float waistToHipRatio{0.75f};    // G_waist / G_hips (WHR)
        float bustToUnderbust{1.0f};     // G_chest / G_underbust (cup / chest expansion ratio)
        float hipsToHeight{0.0f};        // G_hips / H_0 (hip girth-to-height ratio)
        float upperThighGirth{0.0f};     // G_thigh / H_0 (thigh girth-to-height ratio)
        float calfGirth{0.0f};           // G_calf / H_0 (calf girth-to-height ratio)
        float upperArmGirth{0.0f};       // G_arm / H_0 (upper arm girth-to-height ratio)
        float sagittalBellyDrop{0.0f};   // (Y+_underbust - Y+_belly) / H_0 (positive = tucked, negative = protruding)
        float flankFlareRatio{1.0f};     // W_belly / W_waist

        // Tone & Pregnancy signals (100% geometry & texture driven, zero slider-name assumptions)
        float geoToneSignal{0.0f};        // S_geo_tone in [-1.0, +1.0] (systemic, decoupled from pregnancy)
        float pregnancyBellyScore{0.0f};  // S_preg in [0.0, 1.0] (Section 3.5 localized anterior belly dome)

        // Texture analysis inputs
        std::string activeMsnPath;
        UVRect abdomenUV{};
        UVRect deltoidUV{};
    };

    struct ActorGenitalMetrics {
        bool hasGenitals{false};
        float sizeMultiplier{1.0f};
        std::string addonName;
        std::string addonSourceFile;
    };

}  // namespace BodyLanguage

