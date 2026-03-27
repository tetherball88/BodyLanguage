#pragma once

#include <filesystem>

namespace BodyLanguage {

    // Reads BNP_RSV__Female_ArchivePath and BNP_RSV__Male_ArchivePath from
    // [RSV] in <dataRoot>/SKSE/Plugins/BodyLanguage.ini.  For each archive,
    // streams all body .dds files found under the "options/diffuse/" subtree,
    // matches each file to a complexion key defined in kDiffuseComplexionMap,
    // computes an MD5 hash, and writes the result to
    // <dataRoot>/SKSE/Plugins/BodyLanguage/rsv_diffuse_hashes.json.
    void ScanRsvDiffuseMaps(const std::filesystem::path& dataRoot);

}  // namespace BodyLanguage
