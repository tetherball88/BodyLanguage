#include "PCH.h"

#include "Geometry/NifTriParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <limits>
#include <sstream>
#include <unordered_map>

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

}  // namespace BodyLanguage

