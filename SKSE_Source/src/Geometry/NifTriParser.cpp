#include "PCH.h"

#include "Geometry/NifTriParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
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

        bool IsAuxiliaryPartShape(std::string_view shapeName) {
            const std::string lower = ToLowerAscii(shapeName);
            return lower.find("feet") != std::string::npos ||
                   lower.find("foot") != std::string::npos ||
                   lower.find("hand") != std::string::npos ||
                   lower.find("head") != std::string::npos ||
                   lower.find("eyes") != std::string::npos ||
                   lower.find("hair") != std::string::npos ||
                   lower.find("Brow") != std::string::npos ||
                   lower.find("genital") != std::string::npos ||
                   lower.find("schlong") != std::string::npos ||
                   lower.find("anus") != std::string::npos ||
                   lower.find("labia") != std::string::npos ||
                   lower.find("vagina") != std::string::npos;
        }

        // Purely geometric classification of localized anterior belly-dome morphs (zero slider-name assumptions).
        // A morph is classified as a localized belly/pregnancy dome iff:
        // 1. It pushes the anterior mid-belly (Ring 5 Front/FrontLeft/FrontRight) strongly forward (+Y > 2.5 units).
        // 2. Its forward +Y projection exceeds lateral flank widening (X-span delta) by > 2.0x.
        // 3. Its forward +Y projection exceeds posterior back movement by > 2.5x.
        // 4. It is spatially isolated to the belly: non-belly rings (Shoulders, Chest, Hips/Glutes, Thigh, Calf, Arm)
        //    have < 1/3.5 of the belly's displacement.
        void ClassifyLocalizedBellyMorph(MorphDeltaEntry& entry) {
            constexpr std::size_t kRing5Base = static_cast<std::size_t>(AnatomicalRingId::kMidBelly) * kPointsPerRing;

            const float frontY = entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kFront)].y;
            const float frontYSum =
                entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kFrontRight)].y +
                frontY +
                entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kFrontLeft)].y;
            const float deltaY5 = std::max(0.0f, frontYSum / 3.0f);

            const float backYSum =
                std::fabs(entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kBackRight)].y) +
                std::fabs(entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kBack)].y) +
                std::fabs(entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kBackLeft)].y);
            const float backMove5 = backYSum / 3.0f;

            const float rightDeltaX = entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kRight)].x;
            const float leftDeltaX = entry.deltas[kRing5Base + static_cast<std::size_t>(CompassDir::kLeft)].x;
            const float lateralWidening5 = std::fabs(rightDeltaX - leftDeltaX);

            // Measure mean vertex displacement across all non-belly rings (Rings 1, 2, 6, 7, 8, 9)
            constexpr std::array<AnatomicalRingId, 6> kNonBellyRings = {
                AnatomicalRingId::kShoulders,
                AnatomicalRingId::kChestBust,
                AnatomicalRingId::kHipsGlutes,
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
                (frontY > 2.5f) &&
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

        // 2. Select primary body shape
        const auto shapes = nif.GetShapes();
        if (shapes.empty()) {
            return std::nullopt;
        }

        nifly::NiShape* bestShape = nullptr;
        std::size_t bestVertCount = 0;
        bool bestIsPrimaryBody = false;

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

            const std::size_t vCount = shape->GetNumVertices();
            if (vCount < 100) {
                continue;
            }

            const bool isPrimaryCandidate = shape->IsSkinned() && !IsAuxiliaryPartShape(shape->name.get());
            if (!bestShape ||
                (isPrimaryCandidate && !bestIsPrimaryBody) ||
                (isPrimaryCandidate == bestIsPrimaryBody && vCount > bestVertCount)) {
                bestShape = shape;
                bestVertCount = vCount;
                bestIsPrimaryBody = isPrimaryCandidate;
            }
        }

        if (!bestShape) {
            return std::nullopt;
        }

        result.shapeName = bestShape->name.get();

        // 3. Extract vertices & UVs
        std::vector<nifly::Vector3> nifVerts;
        if (!nif.GetVertsForShape(bestShape, nifVerts) || nifVerts.empty()) {
            return std::nullopt;
        }

        std::vector<nifly::Vector2> nifUvs;
        nif.GetUvsForShape(bestShape, nifUvs);

        result.vertices.resize(nifVerts.size());
        result.minZ = std::numeric_limits<float>::max();
        result.maxZ = std::numeric_limits<float>::lowest();

        for (std::size_t i = 0; i < nifVerts.size(); ++i) {
            auto& dst = result.vertices[i];
            dst.pos = RE::NiPoint3(nifVerts[i].x, nifVerts[i].y, nifVerts[i].z);
            if (i < nifUvs.size()) {
                dst.uv = RE::NiPoint2(nifUvs[i].u, nifUvs[i].v);
            }
            result.minZ = std::min(result.minZ, dst.pos.z);
            result.maxZ = std::max(result.maxZ, dst.pos.z);
        }

        // 4. Extract normal map path from texture slot 1
        std::string texSlot1;
        if (nif.GetTextureSlot(bestShape, texSlot1, 1) > 0 && !texSlot1.empty()) {
            result.normalMapPath = texSlot1;
        }

        // 5. Extract bone list and per-vertex bone weights
        nif.GetShapeBoneList(bestShape, result.boneNames);
        for (std::uint32_t boneIdx = 0; boneIdx < result.boneNames.size(); ++boneIdx) {
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

