#include "PCH.h"

#include "Geometry/NifRingIndexer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <mutex>
#include <numbers>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

#include "Geometry/NifTriParser.h"

namespace BodyLanguage {

    namespace {

        std::string ToLowerAscii(std::string_view input) {
            std::string out(input);
            std::ranges::transform(out, out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        // Derives canonical key and (_0.nif, _1.nif) pair from a model path
        struct NifWeightPair {
            std::string canonicalKey;
            std::string path0;
            std::string path1;
        };

        NifWeightPair ResolveNifWeightPair(std::string_view modelPath) {
            std::string norm = NormalizeMeshPath(modelPath);
            NifWeightPair pair{};
            pair.canonicalKey = norm;
            pair.path0 = norm;
            pair.path1 = norm;

            if (norm.ends_with("_1.nif")) {
                pair.path0 = norm.substr(0, norm.size() - 6) + "_0.nif";
            } else if (norm.ends_with("_0.nif")) {
                pair.path1 = norm.substr(0, norm.size() - 6) + "_1.nif";
                pair.canonicalKey = pair.path1;
            }
            return pair;
        }

        float GetBoneWeightBySubstring(
            const RawNifVertex& vert,
            const std::vector<std::string>& boneNames,
            std::string_view substringLower) {
            float sum = 0.0f;
            for (const auto& [boneIdx, weight] : vert.boneWeights) {
                if (boneIdx < boneNames.size()) {
                    const std::string bLower = ToLowerAscii(boneNames[boneIdx]);
                    if (bLower.find(substringLower) != std::string::npos) {
                        sum += weight;
                    }
                }
            }
            return sum;
        }

        float GetArmExcludeWeight(
            const RawNifVertex& vert,
            const std::vector<std::string>& boneNames) {
            return GetBoneWeightBySubstring(vert, boneNames, "upperarm") +
                   GetBoneWeightBySubstring(vert, boneNames, "forearm") +
                   GetBoneWeightBySubstring(vert, boneNames, "hand") +
                   GetBoneWeightBySubstring(vert, boneNames, "clavicle");
        }

        // Selects 8 outer boundary vertices around the slice centroid at angles 0, 45, ..., 315 deg.
        // For standard horizontal rings, plane is (X, Y).
        // For the lateral upper arm ring (A-pose ~45 deg downward in XZ), plane perpendicular to the humerus
        // shaft is u = Y, v = 0.70710678f * (Z - X).
        std::array<std::uint16_t, kPointsPerRing> SelectOctagonVertices(
            const RawNifMesh& mesh,
            const std::vector<std::uint16_t>& candidates,
            bool isArmRing = false) {
            std::array<std::uint16_t, kPointsPerRing> result{};
            if (candidates.empty()) {
                return result;
            }

            constexpr float kInvSqrt2 = 0.70710678f;

            float cx = 0.0f;
            float cy = 0.0f;
            for (const std::uint16_t idx : candidates) {
                const auto& p = mesh.vertices[idx].pos;
                cx += isArmRing ? p.y : p.x;
                cy += isArmRing ? ((p.z - p.x) * kInvSqrt2) : p.y;
            }
            const float invN = 1.0f / static_cast<float>(candidates.size());
            cx *= invN;
            cy *= invN;

            constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
            constexpr float kSectorStep = std::numbers::pi_v<float> / 4.0f;      // 45 deg
            constexpr float kSectorHalfWidth = std::numbers::pi_v<float> / 7.0f; // ~25.7 deg

            for (std::size_t dir = 0; dir < kPointsPerRing; ++dir) {
                const float targetAngle = static_cast<float>(dir) * kSectorStep;
                std::uint16_t bestInSector = candidates.front();
                float maxDistSqInSector = -1.0f;

                std::uint16_t bestFallback = candidates.front();
                float bestFallbackScore = -1e9f;

                for (const std::uint16_t idx : candidates) {
                    const auto& p = mesh.vertices[idx].pos;
                    const float u = isArmRing ? p.y : p.x;
                    const float v = isArmRing ? ((p.z - p.x) * kInvSqrt2) : p.y;
                    const float dx = u - cx;
                    const float dy = v - cy;
                    const float distSq = dx * dx + dy * dy;
                    if (distSq < 1e-6f) {
                        continue;
                    }

                    float angle = std::atan2(dy, dx);
                    if (angle < 0.0f) {
                        angle += kTwoPi;
                    }

                    float angleDiff = std::fabs(angle - targetAngle);
                    if (angleDiff > std::numbers::pi_v<float>) {
                        angleDiff = kTwoPi - angleDiff;
                    }

                    if (angleDiff <= kSectorHalfWidth && distSq > maxDistSqInSector) {
                        maxDistSqInSector = distSq;
                        bestInSector = idx;
                    }

                    const float fallbackScore = std::sqrt(distSq) * std::cos(angleDiff);
                    if (fallbackScore > bestFallbackScore) {
                        bestFallbackScore = fallbackScore;
                        bestFallback = idx;
                    }
                }

                result[dir] = (maxDistSqInSector >= 0.0f) ? bestInSector : bestFallback;
            }

            return result;
        }

        // Collects all outer torso skin vertices within [zNormMin, zNormMax], excluding arms/hands/calves
        // and any optional excluded bone substring (such as "breast" for underbust/waist or "thigh" for hips),
        // without discarding anterior/lateral torso vertices weighted to "NPC Belly" or "NPC Pelvis".
        std::vector<std::uint16_t> CollectTorsoSliceCandidates(
            const RawNifMesh& mesh,
            float zNormMin,
            float zNormMax,
            float maxAbsX = 16.5f,
            std::string_view excludedBoneSubstr = {},
            float maxExcludedWeight = 0.20f) {
            const float h0 = std::max(10.0f, mesh.maxZ - mesh.minZ);
            const bool hasBones = !mesh.boneNames.empty();

            std::vector<std::uint16_t> candidates;
            candidates.reserve(256);

            for (std::size_t i = 0; i < mesh.vertices.size() && i < 65535; ++i) {
                const auto& v = mesh.vertices[i];
                const float zNorm = (v.pos.z - mesh.minZ) / h0;
                if (zNorm < zNormMin || zNorm > zNormMax || std::fabs(v.pos.x) > maxAbsX) {
                    continue;
                }

                if (hasBones) {
                    const float limbW = GetBoneWeightBySubstring(v, mesh.boneNames, "upperarm") +
                                        GetBoneWeightBySubstring(v, mesh.boneNames, "forearm") +
                                        GetBoneWeightBySubstring(v, mesh.boneNames, "hand") +
                                        GetBoneWeightBySubstring(v, mesh.boneNames, "calf") +
                                        GetBoneWeightBySubstring(v, mesh.boneNames, "foot");
                    if (limbW > 0.15f) {
                        continue;
                    }

                    if (!excludedBoneSubstr.empty() &&
                        GetBoneWeightBySubstring(v, mesh.boneNames, excludedBoneSubstr) > maxExcludedWeight) {
                        continue;
                    }
                }

                candidates.push_back(static_cast<std::uint16_t>(i));
            }

            if (candidates.size() < 8) {
                candidates.clear();
                for (std::size_t i = 0; i < mesh.vertices.size() && i < 65535; ++i) {
                    const auto& v = mesh.vertices[i];
                    const float zNorm = (v.pos.z - mesh.minZ) / h0;
                    if (zNorm >= zNormMin - 0.01f && zNorm <= zNormMax + 0.01f && std::fabs(v.pos.x) <= maxAbsX) {
                        candidates.push_back(static_cast<std::uint16_t>(i));
                    }
                }
            }

            return candidates;
        }

        // Detects the normalized Z-coordinate of the crotch (bottom of the pelvis where midline vertices |X| < 1.2 appear).
        // In standard Skyrim humanoid meshes (Z in [11.4, 114.0]), the crotch sits at Z ~ 63 (zNorm ~ 0.50).
        float FindCrotchZNorm(const RawNifMesh& mesh) {
            const float h0 = std::max(10.0f, mesh.maxZ - mesh.minZ);
            for (float zCenter = 0.44f; zCenter <= 0.56f; zCenter += 0.01f) {
                std::size_t midlineCount = 0;
                for (const auto& v : mesh.vertices) {
                    const float zNorm = (v.pos.z - mesh.minZ) / h0;
                    if (zNorm >= zCenter - 0.008f && zNorm <= zCenter + 0.008f && std::fabs(v.pos.x) < 1.2f) {
                        ++midlineCount;
                    }
                }
                if (midlineCount >= 2) {
                    return std::clamp(zCenter, 0.46f, 0.53f);
                }
            }
            return 0.50f;
        }

        void PopulateAnatomicalRings(const RawNifMesh& mesh1, bool isFemale, BodyTemplateCache& cache) {
            const float h0 = std::max(10.0f, mesh1.maxZ - mesh1.minZ);
            cache.baseReferenceHeight = h0;
            const float zCrotch = FindCrotchZNorm(mesh1);

            // Ring 0: Neck / Base (zNorm in [0.96, 1.00], |X| <= 8.0)
            {
                auto cands = CollectTorsoSliceCandidates(mesh1, 0.96f, 1.00f, 8.0f);
                cache.rings[0].vIdx = SelectOctagonVertices(mesh1, cands);
                cache.rings[0].primaryBone = "NPC Neck [Neck]";
            }

            // Ring 1: Shoulders / Deltoids (zNorm in [0.88, 0.92], |X| <= 22.0, excluding forearm/hand)
            {
                std::vector<std::uint16_t> cands;
                for (std::size_t i = 0; i < mesh1.vertices.size() && i < 65535; ++i) {
                    const auto& v = mesh1.vertices[i];
                    const float zNorm = (v.pos.z - mesh1.minZ) / h0;
                    if (zNorm < 0.88f || zNorm > 0.92f || std::fabs(v.pos.x) > 22.0f) {
                        continue;
                    }
                    if (!mesh1.boneNames.empty()) {
                        const float lowerArmW = GetBoneWeightBySubstring(v, mesh1.boneNames, "forearm") +
                                                GetBoneWeightBySubstring(v, mesh1.boneNames, "hand");
                        if (lowerArmW > 0.05f) {
                            continue;
                        }
                    }
                    cands.push_back(static_cast<std::uint16_t>(i));
                }
                if (cands.size() < 8) {
                    for (std::size_t i = 0; i < mesh1.vertices.size() && i < 65535; ++i) {
                        const auto& v = mesh1.vertices[i];
                        const float zNorm = (v.pos.z - mesh1.minZ) / h0;
                        if (zNorm >= 0.86f && zNorm <= 0.93f && std::fabs(v.pos.x) <= 22.0f) {
                            cands.push_back(static_cast<std::uint16_t>(i));
                        }
                    }
                }
                cache.rings[1].vIdx = SelectOctagonVertices(mesh1, cands);
                cache.rings[1].primaryBone = "NPC L Clavicle [LClv]";
                cache.rings[1].secondaryBone = "NPC R Clavicle [RClv]";
            }

            // Ring 2: Upper Chest / Bust Peak (scan thin sub-bands in [0.78, 0.85] for max anterior +Y projection)
            {
                float bestBandZ = 0.81f;
                float maxFrontY = std::numeric_limits<float>::lowest();
                for (float zCenter = 0.78f; zCenter <= 0.85f; zCenter += 0.01f) {
                    auto band = CollectTorsoSliceCandidates(mesh1, zCenter - 0.012f, zCenter + 0.012f, 15.5f);
                    if (band.size() < 8) {
                        continue;
                    }
                    for (const std::uint16_t idx : band) {
                        if (mesh1.vertices[idx].pos.y > maxFrontY) {
                            maxFrontY = mesh1.vertices[idx].pos.y;
                            bestBandZ = zCenter;
                        }
                    }
                }
                auto cands = CollectTorsoSliceCandidates(mesh1, bestBandZ - 0.015f, bestBandZ + 0.015f, 15.5f);
                cache.rings[2].vIdx = SelectOctagonVertices(mesh1, cands);
                cache.rings[2].primaryBone = "NPC Spine2 [Spn2]";
                cache.rings[2].secondaryBone = isFemale ? "NPC L Breast" : "NPC Spine2 [Spn2]";
            }

            // Ring 3: Underbust / Ribcage (zNorm in [0.73, 0.76], excluding breast weight > 0.20)
            {
                auto cands = CollectTorsoSliceCandidates(mesh1, 0.73f, 0.76f, 15.0f, "breast", 0.20f);
                cache.rings[3].vIdx = SelectOctagonVertices(mesh1, cands);
                cache.rings[3].primaryBone = "NPC Spine2 [Spn2]";
            }

            // Ring 4: Natural Waist (scan thin sub-bands in [0.65, 0.72] for minimum coronal X-width)
            float bestWaistZ = 0.68f;
            {
                float minSpanX = std::numeric_limits<float>::max();
                for (float zCenter = 0.65f; zCenter <= 0.72f; zCenter += 0.01f) {
                    auto band = CollectTorsoSliceCandidates(
                        mesh1, zCenter - 0.012f, zCenter + 0.012f, 15.0f, "breast", 0.15f);
                    if (band.size() < 8) {
                        continue;
                    }
                    float minX = std::numeric_limits<float>::max();
                    float maxX = std::numeric_limits<float>::lowest();
                    for (const std::uint16_t idx : band) {
                        minX = std::min(minX, mesh1.vertices[idx].pos.x);
                        maxX = std::max(maxX, mesh1.vertices[idx].pos.x);
                    }
                    const float spanX = maxX - minX;
                    if (spanX > 8.0f && spanX < minSpanX) {
                        minSpanX = spanX;
                        bestWaistZ = zCenter;
                    }
                }
                auto cands = CollectTorsoSliceCandidates(
                    mesh1, bestWaistZ - 0.015f, bestWaistZ + 0.015f, 15.0f, "breast", 0.15f);
                cache.rings[4].vIdx = SelectOctagonVertices(mesh1, cands);
                cache.rings[4].primaryBone = "NPC Spine1 [Spn1]";
            }

            // Ring 5: Mid-Belly / Flanks (navel / high iliac level just below natural waist, ~zNorm in [0.60, 0.63])
            {
                const float bellyZCenter = std::clamp(bestWaistZ - 0.055f, zCrotch + 0.09f, 0.64f);
                auto cands = CollectTorsoSliceCandidates(
                    mesh1, bellyZCenter - 0.015f, bellyZCenter + 0.015f, 16.0f, "thigh", 0.20f);
                cache.rings[5].vIdx = SelectOctagonVertices(mesh1, cands);
                cache.rings[5].primaryBone = "NPC Spine [Spn0]";
                cache.rings[5].secondaryBone = "NPC Belly";
            }

            // Ring 6: Hips / Glute Peak (scan thin sub-bands in [zCrotch + 0.02, zCrotch + 0.08] for max posterior -Y glute projection)
            {
                const float hipMinZ = std::clamp(zCrotch + 0.02f, 0.51f, 0.55f);
                const float hipMaxZ = std::clamp(zCrotch + 0.08f, 0.56f, 0.60f);
                float bestHipZ = 0.5f * (hipMinZ + hipMaxZ);
                float minBackY = std::numeric_limits<float>::max();
                for (float zCenter = hipMinZ; zCenter <= hipMaxZ; zCenter += 0.01f) {
                    auto band = CollectTorsoSliceCandidates(mesh1, zCenter - 0.012f, zCenter + 0.012f, 16.5f);
                    if (band.size() < 8) {
                        continue;
                    }
                    for (const std::uint16_t idx : band) {
                        if (mesh1.vertices[idx].pos.y < minBackY) {
                            minBackY = mesh1.vertices[idx].pos.y;
                            bestHipZ = zCenter;
                        }
                    }
                }
                auto cands = CollectTorsoSliceCandidates(mesh1, bestHipZ - 0.015f, bestHipZ + 0.015f, 16.5f);
                cache.rings[6].vIdx = SelectOctagonVertices(mesh1, cands);
                cache.rings[6].primaryBone = "NPC Pelvis [Pelv]";
                cache.rings[6].secondaryBone = "NPC L Butt";
            }

            // Ring 7: Upper Thigh (Left upper femur just below the crotch/gluteal fold, ~zNorm in [zCrotch - 0.07, zCrotch - 0.04])
            {
                const float thighMinZ = std::clamp(zCrotch - 0.07f, 0.41f, 0.45f);
                const float thighMaxZ = std::clamp(zCrotch - 0.04f, 0.44f, 0.48f);
                std::vector<std::uint16_t> cands;
                for (std::size_t i = 0; i < mesh1.vertices.size() && i < 65535; ++i) {
                    const auto& v = mesh1.vertices[i];
                    const float zNorm = (v.pos.z - mesh1.minZ) / h0;
                    if (zNorm < thighMinZ || zNorm > thighMaxZ || v.pos.x > -0.5f) {
                        continue;
                    }
                    if (!mesh1.boneNames.empty()) {
                        const float thighW = GetBoneWeightBySubstring(v, mesh1.boneNames, "l thigh");
                        if (thighW < 0.40f) {
                            continue;
                        }
                    }
                    cands.push_back(static_cast<std::uint16_t>(i));
                }
                if (cands.size() < 8) {
                    for (std::size_t i = 0; i < mesh1.vertices.size() && i < 65535; ++i) {
                        const auto& v = mesh1.vertices[i];
                        const float zNorm = (v.pos.z - mesh1.minZ) / h0;
                        if (zNorm >= thighMinZ - 0.01f && zNorm <= thighMaxZ + 0.01f && v.pos.x < -0.5f) {
                            cands.push_back(static_cast<std::uint16_t>(i));
                        }
                    }
                }
                cache.rings[7].vIdx = SelectOctagonVertices(mesh1, cands);
                cache.rings[7].primaryBone = "NPC L Thigh [LThg]";
            }

            // Ring 8: Calf (Scan thin sub-bands in [0.16, 0.24] on left leg for maximum gastrocnemius coronal width)
            {
                float bestCalfZ = 0.20f;
                float maxCalfSpanX = -1.0f;
                for (float zCenter = 0.16f; zCenter <= 0.24f; zCenter += 0.01f) {
                    float minX = std::numeric_limits<float>::max();
                    float maxX = std::numeric_limits<float>::lowest();
                    std::size_t count = 0;
                    for (std::size_t i = 0; i < mesh1.vertices.size() && i < 65535; ++i) {
                        const auto& v = mesh1.vertices[i];
                        const float zNorm = (v.pos.z - mesh1.minZ) / h0;
                        if (zNorm < zCenter - 0.012f || zNorm > zCenter + 0.012f || v.pos.x > 0.0f) {
                            continue;
                        }
                        if (!mesh1.boneNames.empty() &&
                            GetBoneWeightBySubstring(v, mesh1.boneNames, "l calf") < 0.40f) {
                            continue;
                        }
                        minX = std::min(minX, v.pos.x);
                        maxX = std::max(maxX, v.pos.x);
                        ++count;
                    }
                    if (count >= 8 && (maxX - minX) > maxCalfSpanX) {
                        maxCalfSpanX = maxX - minX;
                        bestCalfZ = zCenter;
                    }
                }

                std::vector<std::uint16_t> cands;
                for (std::size_t i = 0; i < mesh1.vertices.size() && i < 65535; ++i) {
                    const auto& v = mesh1.vertices[i];
                    const float zNorm = (v.pos.z - mesh1.minZ) / h0;
                    if (zNorm < bestCalfZ - 0.015f || zNorm > bestCalfZ + 0.015f || v.pos.x > 0.0f) {
                        continue;
                    }
                    if (!mesh1.boneNames.empty() &&
                        GetBoneWeightBySubstring(v, mesh1.boneNames, "l calf") < 0.40f) {
                        continue;
                    }
                    cands.push_back(static_cast<std::uint16_t>(i));
                }
                if (cands.size() < 8) {
                    for (std::size_t i = 0; i < mesh1.vertices.size() && i < 65535; ++i) {
                        const auto& v = mesh1.vertices[i];
                        const float zNorm = (v.pos.z - mesh1.minZ) / h0;
                        if (zNorm >= bestCalfZ - 0.02f && zNorm <= bestCalfZ + 0.02f && v.pos.x < 0.0f) {
                            cands.push_back(static_cast<std::uint16_t>(i));
                        }
                    }
                }
                cache.rings[8].vIdx = SelectOctagonVertices(mesh1, cands);
                cache.rings[8].primaryBone = "NPC L Calf [LClf]";
            }

            // Ring 9: Upper Arm (Perpendicular cross-sectional slab across the 45-deg A-pose left bicep/tricep midpoint)
            {
                constexpr float kInvSqrt2 = 0.70710678f;
                std::vector<std::uint16_t> armPool;
                float sumShaft = 0.0f;
                for (std::size_t i = 0; i < mesh1.vertices.size() && i < 65535; ++i) {
                    const auto& v = mesh1.vertices[i];
                    if (v.pos.x > -11.5f || v.pos.x < -23.0f) {
                        continue;
                    }
                    if (!mesh1.boneNames.empty()) {
                        const float armW = GetBoneWeightBySubstring(v, mesh1.boneNames, "l upperarm");
                        const float foreW = GetBoneWeightBySubstring(v, mesh1.boneNames, "l forearm");
                        const float torsoW = GetBoneWeightBySubstring(v, mesh1.boneNames, "clavicle") +
                                             GetBoneWeightBySubstring(v, mesh1.boneNames, "spine");
                        if (armW < 0.50f || foreW > 0.15f || torsoW > 0.15f) {
                            continue;
                        }
                    } else {
                        const float zNorm = (v.pos.z - mesh1.minZ) / h0;
                        if (zNorm < 0.74f || zNorm > 0.84f) {
                            continue;
                        }
                    }
                    armPool.push_back(static_cast<std::uint16_t>(i));
                    sumShaft += (v.pos.x + v.pos.z) * kInvSqrt2;
                }

                const float shaftMid = !armPool.empty()
                    ? (sumShaft / static_cast<float>(armPool.size()))
                    : (0.78f * h0 + mesh1.minZ - 16.5f) * kInvSqrt2;

                std::vector<std::uint16_t> cands;
                for (const std::uint16_t idx : armPool) {
                    const auto& p = mesh1.vertices[idx].pos;
                    const float s = (p.x + p.z) * kInvSqrt2;
                    if (std::fabs(s - shaftMid) <= 1.2f) {
                        cands.push_back(idx);
                    }
                }
                if (cands.size() < 8) {
                    cands.clear();
                    for (const std::uint16_t idx : armPool) {
                        const auto& p = mesh1.vertices[idx].pos;
                        const float s = (p.x + p.z) * kInvSqrt2;
                        if (std::fabs(s - shaftMid) <= 2.0f) {
                            cands.push_back(idx);
                        }
                    }
                }
                if (cands.size() < 8) {
                    for (std::size_t i = 0; i < mesh1.vertices.size() && i < 65535; ++i) {
                        const auto& p = mesh1.vertices[i].pos;
                        if (p.x < -11.5f && p.x > -23.0f) {
                            const float s = (p.x + p.z) * kInvSqrt2;
                            if (std::fabs(s - shaftMid) <= 1.8f) {
                                cands.push_back(static_cast<std::uint16_t>(i));
                            }
                        }
                    }
                }
                cache.rings[9].vIdx = SelectOctagonVertices(mesh1, cands, true);
                cache.rings[9].primaryBone = "NPC L UpperArm [LUar]";
            }

            // Extract Abdomen UV Rect strictly around the anterior centerline (CompassDir::kFront of Rings 3, 4, 5)
            // so the patch stays inside the rectus abdominis island and never crosses torso UV seams.
            {
                float uCenterSum = 0.0f;
                std::size_t uCenterCount = 0;
                float vMin = 1.0f;
                float vMax = 0.0f;

                for (std::size_t r = 3; r <= 5; ++r) {
                    const std::uint16_t vIdx = cache.rings[r].vIdx[static_cast<std::size_t>(CompassDir::kFront)];
                    if (vIdx < mesh1.vertices.size()) {
                        const auto& uv = mesh1.vertices[vIdx].uv;
                        if (uv.x > 0.30f && uv.x < 0.70f && uv.y > 0.30f && uv.y < 0.95f) {
                            uCenterSum += uv.x;
                            ++uCenterCount;
                            vMin = std::min(vMin, uv.y);
                            vMax = std::max(vMax, uv.y);
                        }
                    }
                }

                if (uCenterCount > 0) {
                    const float uCenter = uCenterSum / static_cast<float>(uCenterCount);
                    const float vCenter = (vMax > vMin) ? 0.5f * (vMin + vMax) : 0.78f;
                    const float halfV = std::clamp(0.5f * (vMax - vMin), 0.04f, 0.07f);
                    cache.abdomenUV = UVRect{
                        std::clamp(uCenter - 0.055f, 0.38f, 0.52f),
                        std::clamp(uCenter + 0.055f, 0.48f, 0.62f),
                        std::clamp(vCenter - halfV, 0.35f, 0.84f),
                        std::clamp(vCenter + halfV, 0.43f, 0.90f)};
                } else {
                    cache.abdomenUV = UVRect{0.44f, 0.56f, 0.72f, 0.86f};
                }
            }

            // Extract Deltoid/Bicep UV Rect tightly around the outer/anterior midpoint of Ring 9
            // so the patch stays inside a single arm UV island without crossing UV seams.
            {
                const std::uint16_t vIdx = cache.rings[9].vIdx[static_cast<std::size_t>(CompassDir::kRight)];
                const std::uint16_t vIdxAlt = cache.rings[9].vIdx[static_cast<std::size_t>(CompassDir::kFront)];
                RE::NiPoint2 anchorUV{0.72f, 0.52f};
                if (vIdx < mesh1.vertices.size() &&
                    mesh1.vertices[vIdx].uv.x > 0.08f && mesh1.vertices[vIdx].uv.x < 0.92f &&
                    mesh1.vertices[vIdx].uv.y > 0.08f && mesh1.vertices[vIdx].uv.y < 0.92f) {
                    anchorUV = mesh1.vertices[vIdx].uv;
                } else if (vIdxAlt < mesh1.vertices.size() &&
                           mesh1.vertices[vIdxAlt].uv.x > 0.08f && mesh1.vertices[vIdxAlt].uv.x < 0.92f &&
                           mesh1.vertices[vIdxAlt].uv.y > 0.08f && mesh1.vertices[vIdxAlt].uv.y < 0.92f) {
                    anchorUV = mesh1.vertices[vIdxAlt].uv;
                }

                cache.deltoidUV = UVRect{
                    std::clamp(anchorUV.x - 0.04f, 0.06f, 0.88f),
                    std::clamp(anchorUV.x + 0.04f, 0.12f, 0.94f),
                    std::clamp(anchorUV.y - 0.04f, 0.06f, 0.88f),
                    std::clamp(anchorUV.y + 0.04f, 0.12f, 0.94f)};
            }
        }

    }  // namespace

    NifRingIndexer& NifRingIndexer::GetInstance() {
        static NifRingIndexer instance;
        return instance;
    }

    void NifRingIndexer::IndexAllBodyTemplates() {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return;
        }

        std::unordered_set<RE::TESObjectARMO*> skinArmors;

        std::size_t raceSkinCount = 0;
        std::size_t npcSkinCount = 0;
        std::size_t standaloneSkinCount = 0;

        auto isHumanoidAdultRace = [](const RE::TESRace* race) -> bool {
            if (!race || race->IsChildRace()) {
                return false;
            }
            if (!race->HasKeywordString("ActorTypeNPC")) {
                return false;
            }
            // Exclude non-humanoid ActorTypeNPC creatures (e.g. Dremora/Humanoid vs Rieklings/Falmer/Draugr/VampireLord)
            const char* skelMale = race->skeletonModels[RE::SEXES::kMale].model.c_str();
            const char* skelFem = race->skeletonModels[RE::SEXES::kFemale].model.c_str();
            const std::string sm = skelMale ? ToLowerAscii(skelMale) : "";
            const std::string sf = skelFem ? ToLowerAscii(skelFem) : "";
            return sm.find("actors\\character\\") != std::string::npos ||
                   sf.find("actors\\character\\") != std::string::npos;
        };

        auto isHumanoidBodyModelPath = [](std::string_view rawPath) -> bool {
            if (rawPath.empty()) {
                return false;
            }
            const std::string norm = NormalizeMeshPath(rawPath);
            if (norm.find("child") != std::string::npos ||
                norm.find("draugr") != std::string::npos ||
                norm.find("falmer") != std::string::npos ||
                norm.find("riekling") != std::string::npos ||
                norm.find("werewolf") != std::string::npos ||
                norm.find("werebear") != std::string::npos ||
                norm.find("vampirelord") != std::string::npos ||
                norm.find("vampirebrute") != std::string::npos ||
                norm.find("gargoyle") != std::string::npos ||
                norm.find("hagraven") != std::string::npos ||
                norm.find("spriggan") != std::string::npos ||
                norm.find("skeleton") != std::string::npos ||
                norm.find("dragon") != std::string::npos ||
                norm.find("dwarven") != std::string::npos ||
                norm.find("chaurus") != std::string::npos ||
                norm.find("spider") != std::string::npos ||
                norm.find("sabrecat") != std::string::npos ||
                norm.find("mudcrab") != std::string::npos ||
                norm.find("clutter\\") != std::string::npos ||
                norm.find("armor\\") != std::string::npos ||
                norm.find("clothes\\") != std::string::npos) {
                return false;
            }
            return norm.find("actors\\character\\") != std::string::npos ||
                   norm.find("body") != std::string::npos ||
                   norm.find("torso") != std::string::npos;
        };

        std::unordered_set<const RE::TESRace*> humanoidRaces;
        for (auto* race : dataHandler->GetFormArray<RE::TESRace>()) {
            if (isHumanoidAdultRace(race)) {
                humanoidRaces.insert(race);
            }
        }

        // 1. Collect default humanoid race skins
        for (const auto* race : humanoidRaces) {
            if (race->skin) {
                if (skinArmors.insert(race->skin).second) {
                    ++raceSkinCount;
                }
            }
        }

        // 2. Collect custom humanoid NPC skins (standalone followers, unique NPC bodies)
        for (auto* npc : dataHandler->GetFormArray<RE::TESNPC>()) {
            if (npc && npc->skin && isHumanoidAdultRace(npc->race)) {
                if (skinArmors.insert(npc->skin).second) {
                    ++npcSkinCount;
                }
            }
        }

        // 3. Collect standalone humanoid body skin ARMO forms (e.g. Modern Physique Framework / BodyChange / OBody)
        for (auto* armo : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
            if (!armo || !armo->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) {
                continue;
            }
            for (auto* arma : armo->armorAddons) {
                if (!arma || !arma->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) {
                    continue;
                }
                const char* mPath = arma->bipedModels[RE::SEXES::kMale].model.c_str();
                const char* fPath = arma->bipedModels[RE::SEXES::kFemale].model.c_str();
                const std::string mLower = mPath ? ToLowerAscii(mPath) : "";
                const std::string fLower = fPath ? ToLowerAscii(fPath) : "";
                if ((mLower.find("actors\\character\\") != std::string::npos && mLower.find("body") != std::string::npos) ||
                    (fLower.find("actors\\character\\") != std::string::npos && fLower.find("body") != std::string::npos)) {
                    if (isHumanoidBodyModelPath(mLower) || isHumanoidBodyModelPath(fLower)) {
                        if (skinArmors.insert(armo).second) {
                            ++standaloneSkinCount;
                        }
                    }
                }
            }
        }

        spdlog::info(
            "[NifRingIndexer] Scanning {} humanoid skin ARMO form(s) (race={}, customNPC={}, standalone={})...",
            skinArmors.size(),
            raceSkinCount,
            npcSkinCount,
            standaloneSkinCount);

        std::size_t indexedCount = 0;
        for (auto* skin : skinArmors) {
            if (!skin) {
                continue;
            }
            for (auto* arma : skin->armorAddons) {
                if (!arma || !arma->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) {
                    continue;
                }
                const char* maleModel = arma->bipedModels[RE::SEXES::kMale].model.c_str();
                if (maleModel && *maleModel != '\0' && isHumanoidBodyModelPath(maleModel)) {
                    if (GetOrIndexTemplate(maleModel, false)) {
                        ++indexedCount;
                    }
                }
                const char* femaleModel = arma->bipedModels[RE::SEXES::kFemale].model.c_str();
                if (femaleModel && *femaleModel != '\0' && isHumanoidBodyModelPath(femaleModel)) {
                    if (GetOrIndexTemplate(femaleModel, true)) {
                        ++indexedCount;
                    }
                }
            }
        }

        std::shared_lock lock(m_cacheMutex);
        spdlog::info(
            "[NifRingIndexer] Startup indexing complete: {} unique body template(s) cached from {} ARMA model reference(s).",
            m_cache.size(),
            indexedCount);
    }

    std::shared_ptr<const BodyTemplateCache> NifRingIndexer::GetOrIndexTemplate(
        std::string_view nifModelPath,
        bool isFemale) {
        if (nifModelPath.empty()) {
            return nullptr;
        }

        const auto pair = ResolveNifWeightPair(nifModelPath);
        const std::string lookupKey = pair.canonicalKey + (isFemale ? "|F" : "|M");

        {
            std::shared_lock readLock(m_cacheMutex);
            auto it = m_cache.find(lookupKey);
            if (it != m_cache.end()) {
                return it->second;
            }
        }

        auto built = BuildTemplateCache(lookupKey, nifModelPath, isFemale);

        std::unique_lock writeLock(m_cacheMutex);
        auto [it, inserted] = m_cache.emplace(lookupKey, built);
        return it->second;
    }

    std::shared_ptr<const BodyTemplateCache> NifRingIndexer::BuildTemplateCache(
        std::string_view canonicalKey,
        std::string_view rawModelPath,
        bool isFemale) {
        const auto pair = ResolveNifWeightPair(rawModelPath);

        auto mesh1Opt = ParseNifBodyMesh(pair.path1);
        std::optional<RawNifMesh> mesh0Opt;
        if (pair.path0 != pair.path1) {
            mesh0Opt = ParseNifBodyMesh(pair.path0);
        } else {
            mesh0Opt = mesh1Opt;
        }

        if (!mesh1Opt && !mesh0Opt) {
            spdlog::debug("[NifRingIndexer] Failed to parse NIF pair for '{}'", pair.canonicalKey);
            return nullptr;
        }

        if (!mesh1Opt) {
            mesh1Opt = mesh0Opt;
        }
        if (!mesh0Opt) {
            mesh0Opt = mesh1Opt;
        }

        const RawNifMesh& mesh1 = *mesh1Opt;
        const RawNifMesh& mesh0 = *mesh0Opt;

        auto cache = std::make_shared<BodyTemplateCache>();
        cache->nifPathKey = std::string(canonicalKey);
        cache->isFemale = isFemale;
        cache->fallbackMsnPath = mesh1.normalMapPath;

        PopulateAnatomicalRings(mesh1, isFemale, *cache);

        // Populate 80 landmark vertex positions from _0.nif and _1.nif
        const bool sameVertCount = (mesh0.vertices.size() == mesh1.vertices.size());
        for (std::size_t r = 0; r < kRingCount; ++r) {
            for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                const std::size_t slot = r * kPointsPerRing + c;
                const std::uint16_t vIdx = cache->rings[r].vIdx[c];

                if (vIdx < mesh1.vertices.size()) {
                    cache->verts1[slot] = mesh1.vertices[vIdx].pos;
                }
                if (sameVertCount && vIdx < mesh0.vertices.size()) {
                    cache->verts0[slot] = mesh0.vertices[vIdx].pos;
                } else {
                    cache->verts0[slot] = cache->verts1[slot];
                }
            }
        }

        // Compute neutral weight-50 (w = 0.5, zero morphs, bone scale 1.0) baseline ring metrics
        const float h0 = std::max(10.0f, cache->baseReferenceHeight);
        const float invH0 = 1.0f / h0;
        for (std::size_t r = 0; r < kRingCount; ++r) {
            std::array<RE::NiPoint3, kPointsPerRing> midPts{};
            for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                const std::size_t slot = r * kPointsPerRing + c;
                midPts[c] = (cache->verts0[slot] + cache->verts1[slot]) * 0.5f;
            }
            const bool isArmRing = (r == static_cast<std::size_t>(AnatomicalRingId::kUpperArm));
            cache->baseRings[r] = ComputeRingMeasurements(midPts, invH0, isArmRing);
        }

        const auto& bShoulders = cache->baseRings[static_cast<std::size_t>(AnatomicalRingId::kShoulders)];
        const auto& bChest = cache->baseRings[static_cast<std::size_t>(AnatomicalRingId::kChestBust)];
        const auto& bUnderbust = cache->baseRings[static_cast<std::size_t>(AnatomicalRingId::kUnderbust)];
        const auto& bWaist = cache->baseRings[static_cast<std::size_t>(AnatomicalRingId::kNaturalWaist)];
        const auto& bBelly = cache->baseRings[static_cast<std::size_t>(AnatomicalRingId::kMidBelly)];
        const auto& bHips = cache->baseRings[static_cast<std::size_t>(AnatomicalRingId::kHipsGlutes)];
        const auto& bThigh = cache->baseRings[static_cast<std::size_t>(AnatomicalRingId::kUpperThigh)];
        const auto& bArm = cache->baseRings[static_cast<std::size_t>(AnatomicalRingId::kUpperArm)];

        cache->baseSagittalBellyDrop = bUnderbust.frontProj - bBelly.frontProj;

        // Resolve .tri path from "BODYTRI" extra data
        std::string triPath = !mesh1.bodyTriPath.empty() ? mesh1.bodyTriPath : mesh0.bodyTriPath;
        cache->triPath = triPath;

        if (!triPath.empty()) {
            ParseTriMorphDeltas(triPath, mesh1.shapeName, *cache);
        }

        const float toCm = h0 * kSkyrimUnitToCm;
        const float toIn = toCm * kCmToInches;
        const float baseWHR = bWaist.girth / std::max(0.10f, bHips.girth);
        const float baseVTaper = bShoulders.width / std::max(0.05f, bWaist.width);

        spdlog::info(
            "[NifRingIndexer] Cached template '{}' (sex={}, verts0={}, verts1={}, refHeight={:.1f}, morphs={}, "
            "baseW50=[shoulderWidth={:.1f}cm({:.1f}\") chest={:.1f}cm({:.1f}\") waist={:.1f}cm({:.1f}\") hips={:.1f}cm({:.1f}\") thigh={:.1f}cm({:.1f}\") arm={:.1f}cm({:.1f}\") whr={:.2f} vtaper={:.2f}], "
            "abdomenUV=[{:.2f}..{:.2f}, {:.2f}..{:.2f}], deltoidUV=[{:.2f}..{:.2f}, {:.2f}..{:.2f}], tri='{}')",
            cache->nifPathKey,
            isFemale ? "F" : "M",
            mesh0.vertices.size(),
            mesh1.vertices.size(),
            cache->baseReferenceHeight,
            cache->triDeltas.size(),
            bShoulders.width * toCm, bShoulders.width * toIn,
            bChest.girth * toCm, bChest.girth * toIn,
            bWaist.girth * toCm, bWaist.girth * toIn,
            bHips.girth * toCm, bHips.girth * toIn,
            bThigh.girth * toCm, bThigh.girth * toIn,
            bArm.girth * toCm, bArm.girth * toIn,
            baseWHR,
            baseVTaper,
            cache->abdomenUV.uMin,
            cache->abdomenUV.uMax,
            cache->abdomenUV.vMin,
            cache->abdomenUV.vMax,
            cache->deltoidUV.uMin,
            cache->deltoidUV.uMax,
            cache->deltoidUV.vMin,
            cache->deltoidUV.vMax,
            cache->triPath.empty() ? "<none>" : cache->triPath);

        return cache;
    }

}  // namespace BodyLanguage

