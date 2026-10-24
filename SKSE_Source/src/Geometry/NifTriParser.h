#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Geometry/BodyTemplateCache.h"

namespace BodyLanguage {

    struct RawNifVertex {
        RE::NiPoint3 pos{};
        RE::NiPoint2 uv{};
        std::vector<std::pair<std::uint16_t, float>> boneWeights;
    };

    struct RawNifMesh {
        std::string shapeName;
        std::vector<RawNifVertex> vertices;
        std::vector<std::string> boneNames;
        std::string bodyTriPath;
        std::string normalMapPath;
        float minZ{0.0f};
        float maxZ{0.0f};
    };

    // Normalizes a Data-relative mesh path (ensuring lowercase, backslashes, and "meshes\\" prefix).
    std::string NormalizeMeshPath(std::string_view relativePath);

    // Reads a Data-relative NIF path via RE::BSResourceNiBinaryStream and parses it with nifly::NifFile.
    std::optional<RawNifMesh> ParseNifBodyMesh(std::string_view nifRelativePath);

    // Reads a NIF file directly from a filesystem path on disk and parses it with nifly::NifFile.
    std::optional<RawNifMesh> ParseNifBodyMeshFromDisk(const std::filesystem::path& diskPath);

    // Reads a Data-relative BodySlide/RaceMenu .tri path ("TRIP" or zlib-compressed "PIRT" format)
    // via RE::BSResourceNiBinaryStream, extracts morph deltas for the 80 landmark vertices in `outCache`,
    // and computes `isLocalizedBellyMorph` (Section 3.5).
    void ParseTriMorphDeltas(
        std::string_view triRelativePath,
        std::string_view targetShapeName,
        BodyTemplateCache& outCache);

}  // namespace BodyLanguage

