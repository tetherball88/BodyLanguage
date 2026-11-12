#include "PCH.h"

#include "Geometry/NifTriParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <NifFile.hpp>
#include <spdlog/spdlog.h>

namespace BodyLanguage {

    namespace {

        std::string ToLowerAscii(std::string_view input) {
            std::string out(input);
            std::ranges::transform(out, out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        bool ContainsSubstringCI(std::string_view haystack, std::string_view needleLower) {
            const std::string lower = ToLowerAscii(haystack);
            return lower.find(needleLower) != std::string::npos;
        }

        bool ReadVfsFileToBytes(std::string_view normalizedPath, std::vector<std::uint8_t>& outBytes) {
            RE::BSResourceNiBinaryStream stream(std::string{normalizedPath});
            if (!stream.good()) {
                return false;
            }

            RE::NiBinaryStream::BufferInfo bufInfo{};
            stream.get_info(bufInfo);
            if (bufInfo.totalSize > 0) {
                outBytes.resize(bufInfo.totalSize);
                if (stream.read(reinterpret_cast<char*>(outBytes.data()), bufInfo.totalSize)) {
                    return true;
                }
                outBytes.clear();
            }

            if (!stream.stream) {
                return false;
            }

            constexpr std::size_t kChunkSize = 65536;
            std::array<std::uint8_t, kChunkSize> chunk{};
            while (true) {
                std::uint64_t bytesRead = 0;
                const auto err = stream.stream->DoRead(chunk.data(), kChunkSize, bytesRead);
                if (bytesRead > 0) {
                    outBytes.insert(
                        outBytes.end(),
                        chunk.begin(),
                        chunk.begin() + static_cast<std::ptrdiff_t>(bytesRead));
                }
                if (err != RE::BSResource::ErrorCode::kNone || bytesRead < kChunkSize) {
                    break;
                }
            }

            return !outBytes.empty();
        }

        // Purely geometric classification of localized anterior belly-dome morphs (zero slider-name assumptions).
        // A morph is classified as a localized belly/pregnancy dome iff:
        // 1. It pushes the anterior midline navel (Ring 5 Front) strongly forward (+Y > 2.5 units).
        // 2. Its forward +Y projection exceeds lateral flank widening (X-span delta) by > 2.0x.
        // 3. Its forward +Y projection exceeds posterior back movement by > 2.5x.
        // 4. It is spatially isolated to the abdomen: non-abdominal rings (Shoulders, Chest, Thigh, Calf, Arm)
        //    have < 1/3.5 of the belly's forward displacement.
        void ClassifyLocalizedBellyMorph(MorphDeltaEntry& entry) {
            constexpr std::size_t kRing5Base = static_cast<std::size_t>(AnatomicalRingId::kMidBelly) * kPointsPerRing;

            const float frontY = entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kFront)].y;
            const float deltaY5 = std::max(0.0f, frontY);

            const float backYSum =
                std::fabs(entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kBackRight)].y) +
                std::fabs(entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kBack)].y) +
                std::fabs(entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kBackLeft)].y);
            const float backMove5 = backYSum / 3.0f;

            const float rightDeltaX = entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kRight)].x;
            const float leftDeltaX = entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kLeft)].x;
            const float lateralWidening5 = std::fabs(rightDeltaX - leftDeltaX);

            // Measure mean vertex displacement across non-abdominal rings (Rings 1, 2, 7, 8, 9).
            // Ring 6 (kHipsGlutes) is excluded because its anterior vertices sit on the lower abdomen
            // and move forward with the lower pregnancy dome.
            constexpr std::array<AnatomicalRingId, 5> kNonBellyRings = {
                AnatomicalRingId::kShoulders,
                AnatomicalRingId::kChestBust,
                AnatomicalRingId::kUpperThigh,
                AnatomicalRingId::kCalf,
                AnatomicalRingId::kUpperArm
            };

            float maxOtherRingDisp = 0.05f;
            for (const AnatomicalRingId ringId : kNonBellyRings) {
                const std::size_t baseIdx = static_cast<std::size_t>(ringId) * kPointsPerRing;
                float ringSum = 0.0f;
                for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                    const auto& d = entry.deltas[baseIdx + c];
                    ringSum += std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                }
                const float ringMean = ringSum / static_cast<float>(kPointsPerRing);
                maxOtherRingDisp = std::max(maxOtherRingDisp, ringMean);
            }

            const bool isAnteriorDome =
                (deltaY5 > 2.5f) &&
                (deltaY5 > 2.0f * lateralWidening5) &&
                (deltaY5 > 2.5f * backMove5);

            entry.isLocalizedBellyMorph = isAnteriorDome && (deltaY5 > 3.5f * maxOtherRingDisp);
        }

    }  // namespace

    std::string NormalizeMeshPath(std::string_view relativePath) {
        std::string path = ToLowerAscii(relativePath);
        std::ranges::replace(path, '/', '\\');
        while (!path.empty() && path.front() == '\\') {
            path.erase(path.begin());
        }
        if (!path.starts_with("meshes\\")) {
            path = "meshes\\" + path;
        }
        return path;
    }

    std::optional<RawNifMesh> ParseNifBodyMeshFromStream(std::istream& iss, std::string_view normalizedPath);

    std::optional<RawNifMesh> ParseNifBodyMesh(std::string_view nifRelativePath) {
        if (nifRelativePath.empty()) {
            return std::nullopt;
        }

        const std::string normalizedPath = NormalizeMeshPath(nifRelativePath);
        std::vector<std::uint8_t> rawBytes;
        if (!ReadVfsFileToBytes(normalizedPath, rawBytes)) {
            spdlog::debug("[NifTriParser] Could not open NIF stream '{}'", normalizedPath);
            return std::nullopt;
        }

        std::string byteStr(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size());
        std::istringstream iss(byteStr, std::ios::in | std::ios::binary);
        return ParseNifBodyMeshFromStream(iss, normalizedPath);
    }

    std::optional<RawNifMesh> ParseNifBodyMeshFromDisk(const std::filesystem::path& diskPath) {
        std::ifstream file(diskPath, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            return std::nullopt;
        }
        return ParseNifBodyMeshFromStream(file, diskPath.string());
    }

    std::optional<RawNifMesh> ParseNifBodyMeshFromStream(std::istream& iss, std::string_view normalizedPath) {

        nifly::NifFile nif;
        if (nif.Load(iss) != 0 || !nif.IsValid()) {
            spdlog::warn("[NifTriParser] nifly failed to load '{}'", normalizedPath);
            return std::nullopt;
        }

        RawNifMesh result{};

        // 1. Extract "BODYTRI" extra data from root node or shapes
        if (auto* root = nif.GetRootNode()) {
            auto extraList = nif.GetChildren<nifly::NiStringExtraData>(root, true);
            for (auto* extra : extraList) {
                if (extra && ContainsSubstringCI(extra->name.get(), "bodytri")) {
                    result.bodyTriPath = extra->stringData.get();
                    break;
                }
            }
        }

        // 2. Select primary full-height humanoid body shape (validated by 3D bounding box)
        const auto shapes = nif.GetShapes();
        if (shapes.empty()) {
            return std::nullopt;
        }

        nifly::NiShape* bestShape = nullptr;
        std::vector<nifly::Vector3> bestVerts;
        float bestMinZ = std::numeric_limits<float>::max();
        float bestMaxZ = std::numeric_limits<float>::lowest();

        for (auto* shape : shapes) {
            if (!shape) {
                continue;
            }

            if (result.bodyTriPath.empty()) {
                for (auto& extraRef : shape->extraDataRefs) {
                    if (auto* extra = nif.GetHeader().GetBlock<nifly::NiStringExtraData>(extraRef)) {
                        if (ContainsSubstringCI(extra->name.get(), "bodytri")) {
                            result.bodyTriPath = extra->stringData.get();
                            break;
                        }
                    }
                }
            }

            if (!shape->IsSkinned() || shape->GetNumVertices() < 400) {
                continue;
            }

            std::vector<nifly::Vector3> candVerts;
            if (!nif.GetVertsForShape(shape, candVerts) || candVerts.size() < 400) {
                continue;
            }

            float cMinZ = std::numeric_limits<float>::max();
            float cMaxZ = std::numeric_limits<float>::lowest();
            for (const auto& v : candVerts) {
                cMinZ = std::min(cMinZ, v.z);
                cMaxZ = std::max(cMaxZ, v.z);
            }
            const float zSpan = cMaxZ - cMinZ;

            // A full humanoid body mesh (ankle to neck) spans ~95..110 Skyrim units in Z (minZ ~11, maxZ ~114).
            // Reject partial attachments (e.g. breast motors, jewelry, hands/feet) or non-humanoid props.
            if (zSpan < 75.0f || zSpan > 135.0f || cMinZ < -5.0f || cMaxZ < 85.0f) {
                continue;
            }

            if (!bestShape || candVerts.size() > bestVerts.size()) {
                bestShape = shape;
                bestVerts = std::move(candVerts);
                bestMinZ = cMinZ;
                bestMaxZ = cMaxZ;
            }
        }

        if (!bestShape || bestVerts.empty()) {
            return std::nullopt;
        }

        result.shapeName = bestShape->name.get();
        result.minZ = bestMinZ;
        result.maxZ = bestMaxZ;

        // 3. Extract vertices & UVs
        std::vector<nifly::Vector2> nifUvs;
        nif.GetUvsForShape(bestShape, nifUvs);

        result.vertices.resize(bestVerts.size());
        for (std::size_t i = 0; i < bestVerts.size(); ++i) {
            auto& dst = result.vertices[i];
            dst.pos = RE::NiPoint3(bestVerts[i].x, bestVerts[i].y, bestVerts[i].z);
            if (i < nifUvs.size()) {
                dst.uv = RE::NiPoint2(nifUvs[i].u, nifUvs[i].v);
            }
        }

        // 4. Extract normal map path from texture slot 1
        std::string texSlot1;
        if (nif.GetTextureSlot(bestShape, texSlot1, 1) > 0 && !texSlot1.empty()) {
            result.normalMapPath = texSlot1;
        }

        // 5. Extract bone list and per-vertex bone weights
        nif.GetShapeBoneList(bestShape, result.boneNames);
        bool extractedFromSkinData = false;
        if (auto* skinInst = nif.GetHeader().GetBlock<nifly::NiSkinInstance>(bestShape->SkinInstanceRef())) {
            if (auto* skinData = nif.GetHeader().GetBlock<nifly::NiSkinData>(skinInst->dataRef)) {
                if (skinData->hasVertWeights && skinData->bones.size() == result.boneNames.size()) {
                    for (std::uint32_t boneIdx = 0; boneIdx < skinData->bones.size(); ++boneIdx) {
                        for (const auto& sw : skinData->bones[boneIdx].vertexWeights) {
                            if (sw.index < result.vertices.size() && sw.weight > 0.001f) {
                                result.vertices[sw.index].boneWeights.emplace_back(
                                    static_cast<std::uint16_t>(boneIdx), sw.weight);
                                extractedFromSkinData = true;
                            }
                        }
                    }
                }
            }
        }
        for (std::uint32_t boneIdx = 0; !extractedFromSkinData && boneIdx < result.boneNames.size(); ++boneIdx) {
            std::unordered_map<std::uint16_t, float> boneWeights;
            nif.GetShapeBoneWeights(bestShape, boneIdx, boneWeights);
            for (const auto& [vIdx, weight] : boneWeights) {
                if (vIdx < result.vertices.size() && weight > 0.001f) {
                    result.vertices[vIdx].boneWeights.emplace_back(
                        static_cast<std::uint16_t>(boneIdx), weight);
                }
            }
        }

        spdlog::info(
            "[NifTriParser] Parsed NIF '{}' -> shape='{}' verts={} bones={} zRange=[{:.1f}, {:.1f}] bodyTri='{}' msn='{}'",
            normalizedPath,
            result.shapeName,
            result.vertices.size(),
            result.boneNames.size(),
            result.minZ,
            result.maxZ,
            result.bodyTriPath.empty() ? "<none>" : result.bodyTriPath,
            result.normalMapPath.empty() ? "<none>" : result.normalMapPath);

        return result;
    }

    void ParseTriMorphDeltas(
        std::string_view triRelativePath,
        std::string_view targetShapeName,
        BodyTemplateCache& outCache) {
        if (triRelativePath.empty()) {
            return;
        }

        const std::string normalizedPath = NormalizeMeshPath(triRelativePath);
        std::vector<std::uint8_t> rawBytes;
        if (!ReadVfsFileToBytes(normalizedPath, rawBytes) || rawBytes.size() < 8) {
            spdlog::warn("[NifTriParser] Could not open TRI file '{}'", normalizedPath);
            return;
        }

        // Note: BodySlide and RaceMenu write the 4-byte magic as uint32_t 'TRIP' (0x54524950),
        // which is stored on little-endian x86 disk as the 4 bytes "PIRT" (0x50, 0x49, 0x52, 0x54).
        // Some tools write the literal char[4] "TRIP". Both are uncompressed packed TRI files
        // where byte 4 is immediately uint16_t shapeCount.
        const bool isMagicPIRT = (std::memcmp(rawBytes.data(), "PIRT", 4) == 0);
        const bool isMagicTRIP = (std::memcmp(rawBytes.data(), "TRIP", 4) == 0);
        if (!isMagicPIRT && !isMagicTRIP) {
            spdlog::warn("[NifTriParser] Unsupported TRI header magic in '{}'", normalizedPath);
            return;
        }

        // Map mesh vertex ID -> list of landmark slots [0..79]
        std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> vertToLandmarkSlots;
        vertToLandmarkSlots.reserve(kTotalLandmarkVertices);
        for (std::size_t r = 0; r < kRingCount; ++r) {
            for (std::size_t c = 0; c < kPointsPerRing; ++c) {
                const std::uint16_t vId = outCache.rings[r].vIdx[c];
                const auto slot = static_cast<std::uint8_t>(r * kPointsPerRing + c);
                vertToLandmarkSlots[vId].push_back(slot);
            }
        }

        std::size_t cursor = 4;
        auto readBytes = [&](void* dst, std::size_t count) -> bool {
            if (cursor + count > rawBytes.size()) {
                return false;
            }
            std::memcpy(dst, rawBytes.data() + cursor, count);
            cursor += count;
            return true;
        };

        std::uint16_t shapeCount = 0;
        if (!readBytes(&shapeCount, sizeof(std::uint16_t))) {
            return;
        }

        const std::string targetLower = ToLowerAscii(targetShapeName);

        for (std::uint16_t i = 0; i < shapeCount; ++i) {
            std::uint8_t shapeLen = 0;
            if (!readBytes(&shapeLen, sizeof(std::uint8_t))) {
                break;
            }

            std::string shapeName(shapeLen, '\0');
            if (shapeLen > 0 && !readBytes(shapeName.data(), shapeLen)) {
                break;
            }

            std::uint16_t morphCount = 0;
            if (!readBytes(&morphCount, sizeof(std::uint16_t))) {
                break;
            }

            const bool matchesTargetShape =
                (shapeCount == 1) ||
                targetLower.empty() ||
                (ToLowerAscii(shapeName) == targetLower) ||
                outCache.triDeltas.empty();

            for (std::uint16_t j = 0; j < morphCount; ++j) {
                std::uint8_t morphLen = 0;
                if (!readBytes(&morphLen, sizeof(std::uint8_t))) {
                    return;
                }

                std::string morphName(morphLen, '\0');
                if (morphLen > 0 && !readBytes(morphName.data(), morphLen)) {
                    return;
                }

                float mult = 0.0f;
                std::uint16_t morphVertCount = 0;
                if (!readBytes(&mult, sizeof(float)) ||
                    !readBytes(&morphVertCount, sizeof(std::uint16_t))) {
                    return;
                }

                if (!matchesTargetShape) {
                    const std::size_t skipBytes = static_cast<std::size_t>(morphVertCount) * 8;
                    if (cursor + skipBytes > rawBytes.size()) {
                        return;
                    }
                    cursor += skipBytes;
                    continue;
                }

                MorphDeltaEntry entry{};
                bool hasAnyLandmarkDelta = false;

                for (std::uint16_t k = 0; k < morphVertCount; ++k) {
                    struct PackedVertDelta {
                        std::uint16_t id;
                        std::int16_t x;
                        std::int16_t y;
                        std::int16_t z;
                    };
                    static_assert(sizeof(PackedVertDelta) == 8);

                    PackedVertDelta pv{};
                    if (!readBytes(&pv, sizeof(PackedVertDelta))) {
                        return;
                    }

                    auto it = vertToLandmarkSlots.find(pv.id);
                    if (it != vertToLandmarkSlots.end()) {
                        const RE::NiPoint3 delta(
                            static_cast<float>(pv.x) * mult,
                            static_cast<float>(pv.y) * mult,
                            static_cast<float>(pv.z) * mult);
                        for (const std::uint8_t slot : it->second) {
                            entry.deltas[slot] = delta;
                        }
                        hasAnyLandmarkDelta = true;
                    }
                }

                if (hasAnyLandmarkDelta) {
                    ClassifyLocalizedBellyMorph(entry);
                    outCache.triDeltas[morphName] = entry;
                }
            }
        }

        std::size_t localizedBellyCount = 0;
        for (const auto& [mName, mEntry] : outCache.triDeltas) {
            if (mEntry.isLocalizedBellyMorph) {
                ++localizedBellyCount;
            }
        }

        spdlog::info(
            "[NifTriParser] Indexed {} morphs ({} localized belly/pregnancy morphs) from '{}' (shapes={}) for '{}'",
            outCache.triDeltas.size(),
            localizedBellyCount,
            normalizedPath,
            shapeCount,
            outCache.nifPathKey);
    }

    namespace {

        bool IsPelvicNonShaftBoneName(std::string_view lowerBoneName) {
            constexpr std::array<std::string_view, 36> kNonShaftSubstrings = {
                "pelv",     "thigh",    "thg",      "spine",    "spn",      "belly",
                "groin",    "butt",     "glute",    " hip",     "scrot",    "ball",
                "wwlb",     "wwrb",     "anus",     "vagina",   "clit",     "labia",
                "mouth",    "neck",     "head",     "tail",     "clav",     "shldr",
                "shoulder", "arm",      "hand",     "calf",     "foot",     "feet",
                "toe",      "ear",      "jaw",      "chest",    "rib",      "saddle"
            };
            for (const auto kw : kNonShaftSubstrings) {
                if (lowerBoneName.find(kw) != std::string_view::npos) {
                    return true;
                }
            }
            return false;
        }

        std::shared_ptr<const GenitalNifTemplate> ParseNifGenitalMesh(std::string_view normalizedPath) {
            std::vector<std::uint8_t> rawBytes;
            if (!ReadVfsFileToBytes(normalizedPath, rawBytes)) {
                return nullptr;
            }

            std::string byteStr(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size());
            std::istringstream iss(byteStr, std::ios::in | std::ios::binary);

            nifly::NifFile nif;
            if (nif.Load(iss) != 0 || !nif.IsValid()) {
                return nullptr;
            }

            const auto shapes = nif.GetShapes();
            if (shapes.empty()) {
                return nullptr;
            }

            struct BoneNodePos {
                std::string name;
                RE::NiPoint3 pos{};
            };

            std::size_t bestScore = 0;
            std::vector<RE::NiPoint3> bestShaftVerts;
            std::vector<BoneNodePos> bestChain;

            for (auto* shape : shapes) {
                if (!shape || !shape->IsSkinned() || shape->GetNumVertices() < 50) {
                    continue;
                }

                const std::string lowerShapeName = ToLowerAscii(shape->name.get());
                if (lowerShapeName.find("collision") != std::string::npos ||
                    lowerShapeName.find("tongue") != std::string::npos ||
                    lowerShapeName.find("ball") != std::string::npos ||
                    lowerShapeName.find("scrot") != std::string::npos ||
                    lowerShapeName.find("hands") != std::string::npos ||
                    lowerShapeName.find("feet") != std::string::npos ||
                    lowerShapeName.find("head") != std::string::npos) {
                    continue;
                }

                std::vector<nifly::Vector3> verts;
                if (!nif.GetVertsForShape(shape, verts) || verts.size() < 50) {
                    continue;
                }

                std::vector<std::string> boneNames;
                nif.GetShapeBoneList(shape, boneNames);
                if (boneNames.empty()) {
                    continue;
                }

                std::vector<std::string> lowerBones(boneNames.size());
                for (std::size_t b = 0; b < boneNames.size(); ++b) {
                    lowerBones[b] = ToLowerAscii(boneNames[b]);
                }

                std::vector<std::vector<std::pair<std::uint32_t, float>>> vertWeights(verts.size());
                std::vector<float> boneWeightSum(boneNames.size(), 0.0f);
                std::vector<RE::NiPoint3> bonePosSum(boneNames.size(), RE::NiPoint3(0.0f, 0.0f, 0.0f));

                auto accumulateWeight = [&](std::uint32_t boneIdx, std::uint16_t vIdx, float w) {
                    if (vIdx >= verts.size() || boneIdx >= boneNames.size() || w <= 0.001f) {
                        return;
                    }
                    vertWeights[vIdx].emplace_back(boneIdx, w);
                    boneWeightSum[boneIdx] += w;
                    bonePosSum[boneIdx].x += w * verts[vIdx].x;
                    bonePosSum[boneIdx].y += w * verts[vIdx].y;
                    bonePosSum[boneIdx].z += w * verts[vIdx].z;
                };

                bool extractedFromSkinData = false;
                if (auto* skinInst = nif.GetHeader().GetBlock<nifly::NiSkinInstance>(shape->SkinInstanceRef())) {
                    if (auto* skinData = nif.GetHeader().GetBlock<nifly::NiSkinData>(skinInst->dataRef)) {
                        if (skinData->hasVertWeights && skinData->bones.size() == boneNames.size()) {
                            for (std::uint32_t bIdx = 0; bIdx < skinData->bones.size(); ++bIdx) {
                                for (const auto& sw : skinData->bones[bIdx].vertexWeights) {
                                    accumulateWeight(bIdx, sw.index, sw.weight);
                                    extractedFromSkinData = true;
                                }
                            }
                        }
                    }
                }

                for (std::uint32_t bIdx = 0; !extractedFromSkinData && bIdx < boneNames.size(); ++bIdx) {
                    std::unordered_map<std::uint16_t, float> bWeights;
                    nif.GetShapeBoneWeights(shape, bIdx, bWeights);
                    for (const auto& [vIdx, w] : bWeights) {
                        accumulateWeight(bIdx, vIdx, w);
                    }
                }

                // Geometrically classify each bone using its NiNode transform and vertex-weighted 3D centroid
                std::vector<bool> isShaftBone(boneNames.size(), false);
                std::vector<bool> isAxialMidlineBone(boneNames.size(), false);
                std::vector<RE::NiPoint3> bonePos3D(boneNames.size(), RE::NiPoint3(0.0f, 0.0f, 0.0f));

                for (std::size_t b = 0; b < boneNames.size(); ++b) {
                    if (boneWeightSum[b] <= 0.01f) {
                        continue;
                    }
                    const RE::NiPoint3 centroid = bonePosSum[b] / boneWeightSum[b];
                    RE::NiPoint3 bPos = centroid;

                    nifly::MatTransform xform{};
                    if (nif.GetNodeTransformToGlobal(boneNames[b], xform) ||
                        nif.GetNodeTransformToParent(boneNames[b], xform)) {
                        const float absSum = std::fabs(xform.translation.x) +
                                             std::fabs(xform.translation.y) +
                                             std::fabs(xform.translation.z);
                        if (absSum > 0.1f) {
                            bPos = RE::NiPoint3(xform.translation.x, xform.translation.y, xform.translation.z);
                        }
                    }
                    bonePos3D[b] = bPos;

                    const bool inAnteriorPelvicZone =
                        (std::fabs(centroid.x) < 4.5f) &&
                        (centroid.y > 0.2f) &&
                        (centroid.z >= 40.0f && centroid.z <= 90.0f);

                    if (inAnteriorPelvicZone && !IsPelvicNonShaftBoneName(lowerBones[b])) {
                        isShaftBone[b] = true;
                        if (std::fabs(bPos.x) < 0.6f && std::fabs(centroid.x) < 0.8f) {
                            isAxialMidlineBone[b] = true;
                        }
                    }
                }

                std::vector<RE::NiPoint3> candVerts;
                candVerts.reserve(verts.size());
                std::unordered_set<std::size_t> usedAxialBoneIndices;

                for (std::size_t v = 0; v < verts.size(); ++v) {
                    float axialShaftW = 0.0f;
                    float nonShaftW = 0.0f;
                    for (const auto& [bIdx, w] : vertWeights[v]) {
                        if (isShaftBone[bIdx]) {
                            if (isAxialMidlineBone[bIdx]) {
                                axialShaftW += w;
                                if (w > 0.15f) {
                                    usedAxialBoneIndices.insert(bIdx);
                                }
                            }
                        } else {
                            nonShaftW += w;
                        }
                    }
                    if (axialShaftW > 0.35f && axialShaftW > nonShaftW) {
                        candVerts.emplace_back(verts[v].x, verts[v].y, verts[v].z);
                    }
                }

                if (candVerts.size() < 20) {
                    continue;
                }

                float minCandX = std::numeric_limits<float>::max();
                float maxCandX = std::numeric_limits<float>::lowest();
                float minCandY = std::numeric_limits<float>::max();
                float maxCandY = std::numeric_limits<float>::lowest();
                float minCandZ = std::numeric_limits<float>::max();
                float maxCandZ = std::numeric_limits<float>::lowest();
                for (const auto& cv : candVerts) {
                    minCandX = std::min(minCandX, cv.x);
                    maxCandX = std::max(maxCandX, cv.x);
                    minCandY = std::min(minCandY, cv.y);
                    maxCandY = std::max(maxCandY, cv.y);
                    minCandZ = std::min(minCandZ, cv.z);
                    maxCandZ = std::max(maxCandZ, cv.z);
                }
                // Reject full-body quadruped/creature torsos whose midline vertices span a wide ribcage or body length
                if ((maxCandX - minCandX) > 10.0f ||
                    (maxCandY - minCandY) > 35.0f ||
                    (maxCandZ - minCandZ) > 35.0f) {
                    continue;
                }

                float minShapeZ = std::numeric_limits<float>::max();
                float maxShapeZ = std::numeric_limits<float>::lowest();
                for (const auto& v : verts) {
                    minShapeZ = std::min(minShapeZ, v.z);
                    maxShapeZ = std::max(maxShapeZ, v.z);
                }
                const bool isDedicatedGenitalShape = (maxShapeZ - minShapeZ) < 45.0f;
                const std::size_t score = candVerts.size() + (isDedicatedGenitalShape ? 100000u : 0u);

                if (score > bestScore) {
                    bestScore = score;
                    bestShaftVerts = std::move(candVerts);

                    std::vector<BoneNodePos> rawChain;
                    for (const std::size_t bIdx : usedAxialBoneIndices) {
                        rawChain.push_back(BoneNodePos{boneNames[bIdx], bonePos3D[bIdx]});
                    }
                    std::ranges::sort(rawChain, [](const BoneNodePos& a, const BoneNodePos& b) {
                        return a.pos.y < b.pos.y;
                    });

                    bestChain.clear();
                    if (!rawChain.empty()) {
                        std::vector<float> zs;
                        zs.reserve(rawChain.size());
                        for (const auto& item : rawChain) {
                            zs.push_back(item.pos.z);
                        }
                        std::ranges::sort(zs);
                        const float medianZ = zs[zs.size() / 2];
                        for (const auto& item : rawChain) {
                            if (std::fabs(item.pos.z - medianZ) < 3.5f) {
                                bestChain.push_back(item);
                            }
                        }
                    }
                }
            }

            if (bestShaftVerts.size() < 20 || bestChain.size() < 2) {
                return nullptr;
            }

            const auto& midlineBones = bestChain;

            auto tmpl = std::make_shared<GenitalNifTemplate>();
            tmpl->nifPathKey = std::string(normalizedPath);

            float chainArc = 0.0f;
            for (std::size_t i = 0; i < midlineBones.size(); ++i) {
                tmpl->shaftBoneChain.push_back(midlineBones[i].name);
                if (i > 0) {
                    const RE::NiPoint3 d = midlineBones[i].pos - midlineBones[i - 1].pos;
                    chainArc += std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                }
            }
            tmpl->bindChainArcUnits = std::max(1.0f, chainArc);

            float axisY = 1.0f;
            float axisZ = 0.0f;
            if (midlineBones.size() >= 2) {
                const float dy = midlineBones.back().pos.y - midlineBones.front().pos.y;
                const float dz = midlineBones.back().pos.z - midlineBones.front().pos.z;
                const float norm = std::hypot(dy, dz);
                if (norm > 1e-4f) {
                    axisY = dy / norm;
                    axisZ = dz / norm;
                }
            }
            const float perpY = -axisZ;
            const float perpZ = axisY;

            std::vector<float> projAlong;
            projAlong.reserve(bestShaftVerts.size());
            for (const auto& v : bestShaftVerts) {
                projAlong.push_back(v.y * axisY + v.z * axisZ);
            }
            std::ranges::sort(projAlong);

            float sMin = projAlong[static_cast<std::size_t>(0.02f * static_cast<float>(projAlong.size()))];
            const float sMax = projAlong[static_cast<std::size_t>(0.99f * static_cast<float>(projAlong.size()))];
            if (midlineBones.size() >= 2) {
                const float rootY = 0.35f * midlineBones[0].pos.y + 0.65f * midlineBones[1].pos.y;
                const float rootZ = 0.35f * midlineBones[0].pos.z + 0.65f * midlineBones[1].pos.z;
                const float rootS = rootY * axisY + rootZ * axisZ;
                sMin = std::max(sMin, rootS);
            }

            if ((sMax - sMin) < 3.5f || (sMax - sMin) > 30.0f) {
                return nullptr;
            }

            const float lengthU = sMax - sMin;
            tmpl->baseLengthUnits = lengthU;

            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minP = std::numeric_limits<float>::max();
            float maxP = std::numeric_limits<float>::lowest();
            std::size_t midCount = 0;

            for (const auto& v : bestShaftVerts) {
                const float s = v.y * axisY + v.z * axisZ;
                if (s >= (sMin + 0.25f * lengthU) && s <= (sMin + 0.65f * lengthU)) {
                    const float p = v.y * perpY + v.z * perpZ;
                    minX = std::min(minX, v.x);
                    maxX = std::max(maxX, v.x);
                    minP = std::min(minP, p);
                    maxP = std::max(maxP, p);
                    ++midCount;
                }
            }

            if (midCount >= 8 && maxX > minX && maxP > minP) {
                const float a = 0.5f * (maxX - minX);
                const float b = 0.5f * (maxP - minP);
                tmpl->baseGirthUnits = std::numbers::pi_v<float> *
                    (3.0f * (a + b) - std::sqrt(std::max(0.0f, (3.0f * a + b) * (a + 3.0f * b))));
            } else {
                tmpl->baseGirthUnits = lengthU * 0.90f;
            }

            if (tmpl->baseGirthUnits < 3.0f ||
                tmpl->baseGirthUnits > 28.0f ||
                tmpl->baseGirthUnits > lengthU * 1.65f) {
                return nullptr;
            }

            spdlog::info(
                "[NifTriParser] Indexed Genital NIF '{}': baseLen={:.2f}u ({:.1f}cm) baseGirth={:.2f}u ({:.1f}cm) "
                "chainBones={} bindArc={:.2f}u",
                normalizedPath,
                tmpl->baseLengthUnits,
                tmpl->baseLengthUnits * kSkyrimUnitToCm,
                tmpl->baseGirthUnits,
                tmpl->baseGirthUnits * kSkyrimUnitToCm,
                tmpl->shaftBoneChain.size(),
                tmpl->bindChainArcUnits);

            return tmpl;
        }

        std::shared_mutex g_genitalNifMutex;
        std::unordered_map<std::string, std::shared_ptr<const GenitalNifTemplate>> g_genitalNifCache;

    }  // namespace

    std::shared_ptr<const GenitalNifTemplate> GetOrIndexGenitalNif(std::string_view nifRelativePath) {
        if (nifRelativePath.empty()) {
            return nullptr;
        }

        const std::string normalizedPath = NormalizeMeshPath(nifRelativePath);
        {
            std::shared_lock readLock(g_genitalNifMutex);
            auto it = g_genitalNifCache.find(normalizedPath);
            if (it != g_genitalNifCache.end()) {
                return it->second;
            }
        }

        auto parsed = ParseNifGenitalMesh(normalizedPath);
        {
            std::unique_lock writeLock(g_genitalNifMutex);
            g_genitalNifCache[normalizedPath] = parsed;
        }
        return parsed;
    }

}  // namespace BodyLanguage

