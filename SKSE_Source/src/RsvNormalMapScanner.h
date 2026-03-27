#pragma once

#include <filesystem>

namespace BodyLanguage {

    // Reads the RSV 7z archive path from [RSV] RSV_ArchivePath= in
    // <dataRoot>/SKSE/Plugins/BodyLanguage.ini, streams through the archive's
    // "Normal Maps" tree, computes an MD5 hash for every body-variant DDS file
    // (hands subtrees are skipped), and writes the result to
    // <dataRoot>/SKSE/Plugins/BodyLanguage/rsv_normal_map_hashes.json.
    //
    // Output JSON shape:
    // {
    //   "cbbe":  { "bigBreasts":   { "boxer": "<md5>", ... },
    //              "smallBreasts": { "boxer": "<md5>", ... } },
    //   "unp":   { "body":         { "boxer": "<md5>", ... } },
    //   "himbo": { "body":         { "boxer": "<md5>", ... } }
    // }
    //
    // If the JSON already exists and is newer than the archive the scan is
    // skipped entirely so subsequent game launches incur no cost.
    void ScanRsvNormalMaps(const std::filesystem::path& dataRoot);

}  // namespace BodyLanguage
