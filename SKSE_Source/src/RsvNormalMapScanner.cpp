#include "RsvNormalMapScanner.h"
#include "HashUtils.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace BodyLanguage {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Read a string value from a Windows INI file.
static std::filesystem::path ReadIniPath(const std::filesystem::path& iniPath,
    const wchar_t* section, const wchar_t* key)
{
    wchar_t buf[MAX_PATH]{};
    GetPrivateProfileStringW(section, key, L"", buf, MAX_PATH,
        iniPath.wstring().c_str());
    return std::filesystem::path(buf);
}

// Stream all data blocks for the current archive entry through the shared MD5
// helper.  Always drains the entry so that libarchive's internal cursor
// advances correctly.
static std::string ComputeMD5(archive* a)
{
    const void* buf = nullptr;
    std::size_t size = 0;
    la_int64_t  off  = 0;

    return BodyLanguage::ComputeMD5([&](HCRYPTHASH hHash) -> bool {
        int r;
        while ((r = archive_read_data_block(a, &buf, &size, &off)) == ARCHIVE_OK) {
            if (size > 0 &&
                !CryptHashData(hHash,
                    static_cast<const BYTE*>(buf),
                    static_cast<DWORD>(size), 0))
                return false;
        }
        return true;  // ARCHIVE_EOF is not an error
    });
}

// Convert every backslash to a forward slash (archive paths vary by platform).
static std::string NormPath(std::string_view s)
{
    std::string r(s);
    for (char& c : r)
        if (c == '\\') c = '/';
    return r;
}

static std::string ToLower(std::string_view s)
{
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

// Parse a normalised archive path into (bodyKey, subKey, stem).
// Returns false for hands subtrees, directory entries, or unrecognised paths.
static bool ParseEntry(const std::string& path,
    std::string& bodyKey,
    std::string& subKey,
    std::string& stem)
{
    static constexpr std::string_view kMarker = "Normal Maps/";
    const auto pos = path.find(kMarker);
    if (pos == std::string::npos) return false;

    const std::string rel = path.substr(pos + kMarker.size());

    // Split on '/'
    std::vector<std::string> parts;
    std::string token;
    for (char c : rel) {
        if (c == '/') {
            if (!token.empty()) { parts.push_back(std::move(token)); token.clear(); }
        } else {
            token += c;
        }
    }
    if (!token.empty()) parts.push_back(std::move(token));

    // Need: bodyType / subDir / filename.dds
    if (parts.size() < 3) return false;

    const std::string bodyLow = ToLower(parts[0]);
    const std::string subLow  = ToLower(parts[1]);
    const std::string& fname  = parts.back();

    // Skip hands and any non-DDS entries
    if (subLow == "hands") return false;
    if (ToLower(fname.substr(fname.size() > 4 ? fname.size() - 4 : 0)) != ".dds")
        return false;

    // Body type → JSON key
    if      (bodyLow == "cbbe")  bodyKey = "cbbe";
    else if (bodyLow == "unp")   bodyKey = "unp";
    else if (bodyLow == "himbo") bodyKey = "himbo";
    else                         bodyKey = bodyLow;

    // Sub-directory → JSON key
    // UNP ships a single "body" folder with no breast-size split; map it to
    // the sentinel "__body__" so the scan loop can fan it out into both
    // bigBreasts and smallBreasts to keep the JSON structure uniform with CBBE.
    if      (subLow == "big breasts")                      subKey = "bigBreasts";
    else if (subLow == "small breasts")                    subKey = "smallBreasts";
    else if (subLow == "body" && bodyKey == "unp")         subKey = "__body__";
    else if (subLow == "body")                             subKey = "body";
    else                                                   subKey = subLow;

    // File stem, lowercase and without extension
    const auto dot = fname.rfind('.');
    stem = ToLower(dot != std::string::npos ? fname.substr(0, dot) : fname);

    return true;
}

// Parse a BnP (BnP RSV patch) archive path into (isBigBreasts, muscularity).
// Only the options sub-tree is recognised.  Two layouts are handled:
//   CBBE: options/body normals/<big|small boobs>/<muscularity>/textures/.../_msn.dds
//   UNP:  options/body normals/normals body/<muscularity>/textures/.../_msn.dds
// Returns false for non-MSN entries, base texture paths, or unrecognised paths.
static bool ParseBnpEntry(const std::string& path,
    std::string& isBigBreasts,
    std::string& muscularity)
{
    static constexpr std::string_view kOptionsMarker = "options/body normals/";
    const auto optPos = path.find(kOptionsMarker);
    if (optPos == std::string::npos) return false;

    // Must also be an _msn.dds file
    const std::string pathLow = ToLower(path);
    if (pathLow.size() < 8 || pathLow.substr(pathLow.size() - 8) != "_msn.dds")
        return false;

    const std::string optRel = path.substr(optPos + kOptionsMarker.size());
    std::vector<std::string> optParts;
    {
        std::string tok;
        for (char c : optRel) {
            if (c == '/') { if (!tok.empty()) { optParts.push_back(std::move(tok)); tok.clear(); } }
            else          { tok += c; }
        }
    }
    if (optParts.size() < 2) return false;

    const std::string firstLow = ToLower(optParts[0]);
    if (firstLow.find("big") != std::string::npos || firstLow.find("small") != std::string::npos) {
        // CBBE layout: options/body normals/<big|small boobs>/<muscularity>/...
        isBigBreasts = (firstLow.find("big") != std::string::npos) ? "big" : "small";
        muscularity  = ToLower(optParts[1]);
    } else if (firstLow == "normals body") {
        // UNP layout: options/body normals/normals body/<muscularity>/...
        isBigBreasts = "NA";
        muscularity  = ToLower(optParts[1]);
    } else {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void ScanRsvNormalMaps(const std::filesystem::path& dataRoot)
{
    // ---- Locate INI --------------------------------------------------------
    const auto iniPath = dataRoot / "SKSE" / "Plugins" / "BodyLanguage.ini";
    if (!std::filesystem::exists(iniPath)) {
        spdlog::warn("[RSV] BodyLanguage.ini not found at '{}' – skipping RSV scan.",
            iniPath.string());
        return;
    }

    // ---- Read [RSV] ArchivePath= -------------------------------------------
    const auto archivePath = ReadIniPath(iniPath, L"RSV", L"RSV_Main_ArchivePath");
    if (archivePath.empty()) {
        spdlog::warn("[RSV] RSV_Main_ArchivePath not set under [RSV] in '{}' – skipping.",
            iniPath.string());
        return;
    }
    if (!std::filesystem::exists(archivePath)) {
        spdlog::warn("[RSV] RSV_Main_ArchivePath '{}' not found – skipping RSV scan.",
            archivePath.string());
        return;
    }

    // ---- Read optional BnP female patch archive path -----------------------
    const auto bnpArchivePath = ReadIniPath(iniPath, L"RSV", L"BNP_RSV__Female_ArchivePath");
    const bool hasBnp = !bnpArchivePath.empty() && std::filesystem::exists(bnpArchivePath);
    if (!bnpArchivePath.empty() && !hasBnp)
        spdlog::warn("[RSV] BNP_RSV__Female_ArchivePath '{}' not found – skipping BnP scan.",
            bnpArchivePath.string());

    // ---- Check if cached JSON is already up-to-date via archive MD5 --------
    const auto outDir  = dataRoot / "SKSE" / "Plugins" / "BodyLanguage";
    const auto outPath = outDir / "rsv_normal_map_hashes.json";

    const std::string archiveMd5 = BodyLanguage::ComputeMD5(archivePath);
    if (archiveMd5.empty()) {
        spdlog::error("[RSV] Failed to compute MD5 of archive '{}' – skipping RSV scan.",
            archivePath.string());
        return;
    }

    std::string bnpMd5;
    if (hasBnp) {
        bnpMd5 = BodyLanguage::ComputeMD5(bnpArchivePath);
        if (bnpMd5.empty()) {
            spdlog::warn("[RSV] Failed to compute MD5 of BnP archive '{}' – skipping BnP scan.",
                bnpArchivePath.string());
        }
    }

    // Combined version string includes both archive fingerprints so that
    // replacing either archive invalidates the cache.
    const std::string combinedVersion = bnpMd5.empty()
        ? archiveMd5
        : archiveMd5 + ":" + bnpMd5;

    std::error_code ec;
    if (std::filesystem::exists(outPath, ec)) {
        std::ifstream f(outPath);
        if (f) {
            try {
                const auto cached = nlohmann::json::parse(f);
                if (cached.value("__version", "") == combinedVersion) {
                    spdlog::info("[RSV] Hash cache is up-to-date (archive MD5 matches), skipping scan.");
                    return;
                }
            } catch (...) {}
        }
    }

    spdlog::info("[RSV] Scanning archive '{}'", archivePath.string());

    // ---- Open archive with libarchive --------------------------------------
    archive* a = archive_read_new();
    archive_read_support_format_7zip(a);
    archive_read_support_filter_lzma(a);
    archive_read_support_filter_xz(a);

    if (archive_read_open_filename_w(a, archivePath.wstring().c_str(), 65536)
            != ARCHIVE_OK) {
        spdlog::error("[RSV] Failed to open archive: {}", archive_error_string(a));
        archive_read_free(a);
        return;
    }

    // ---- Stream through every entry ----------------------------------------
    nlohmann::json root = nlohmann::json::object();
    int fileCount = 0;

    archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* rawPath = archive_entry_pathname(entry);
        if (!rawPath) { archive_read_data_skip(a); continue; }

        const std::string normalised = NormPath(rawPath);

        std::string bodyKey, subKey, stem;
        if (!ParseEntry(normalised, bodyKey, subKey, stem)) {
            archive_read_data_skip(a);
            continue;
        }

        const std::string hash = ComputeMD5(a);
        if (hash.empty()) {
            spdlog::warn("[RSV] MD5 failed for '{}'", normalised);
            continue;
        }

        // Structure: md5 → { isBigBreasts, muscularity, archive, filePath }
        auto writeEntry = [&](const std::string& hash,
                              const std::string& resolvedSubKey,
                              const std::string& skinStem)
        {
            nlohmann::json entry = nlohmann::json::object();
            if (resolvedSubKey == "bigBreasts")
                entry["isBigBreasts"] = std::string("big");
            else if (resolvedSubKey == "smallBreasts")
                entry["isBigBreasts"] = std::string("small");
            else
                entry["isBigBreasts"] = std::string("NA");
            entry["muscularity"] = skinStem;
            entry["archive"]     = archivePath.filename().string();
            entry["filePath"]    = normalised;
            root[hash] = std::move(entry);
        };

        writeEntry(hash, subKey, stem);
        spdlog::trace("[RSV] {} → {} / {}", hash, subKey, stem);
        ++fileCount;
    }

    archive_read_free(a);
    spdlog::info("[RSV] Hashed {} body-variant Normal Maps files.", fileCount);

    // ---- Scan BnP female patch archive if available ------------------------
    if (hasBnp && !bnpMd5.empty()) {
        spdlog::info("[RSV] Scanning BnP archive '{}'", bnpArchivePath.string());

        archive* ab = archive_read_new();
        archive_read_support_format_7zip(ab);
        archive_read_support_filter_lzma(ab);
        archive_read_support_filter_xz(ab);

        if (archive_read_open_filename_w(ab, bnpArchivePath.wstring().c_str(), 65536)
                != ARCHIVE_OK) {
            spdlog::error("[RSV] Failed to open BnP archive: {}", archive_error_string(ab));
            archive_read_free(ab);
        } else {
            int bnpCount = 0;
            archive_entry* bnpEntry = nullptr;
            while (archive_read_next_header(ab, &bnpEntry) == ARCHIVE_OK) {
                const char* rawPath = archive_entry_pathname(bnpEntry);
                if (!rawPath) { archive_read_data_skip(ab); continue; }

                const std::string normalised = NormPath(rawPath);

                std::string isBigBreasts, muscularity;
                if (!ParseBnpEntry(normalised, isBigBreasts, muscularity)) {
                    archive_read_data_skip(ab);
                    continue;
                }

                const std::string hash = ComputeMD5(ab);
                if (hash.empty()) {
                    spdlog::warn("[RSV] BnP MD5 failed for '{}'", normalised);
                    continue;
                }

                nlohmann::json jEntry = nlohmann::json::object();
                jEntry["isBigBreasts"] = isBigBreasts;
                jEntry["muscularity"]  = muscularity;
                jEntry["archive"]      = bnpArchivePath.filename().string();
                jEntry["filePath"]     = normalised;
                root[hash] = std::move(jEntry);

                spdlog::trace("[RSV] BnP {} → {} / {}", hash, isBigBreasts, muscularity);
                ++bnpCount;
            }

            archive_read_free(ab);
            spdlog::info("[RSV] Hashed {} BnP Normal Maps files.", bnpCount);
        }
    }

    root["__version"] = combinedVersion;

    // ---- Write JSON --------------------------------------------------------
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        spdlog::error("[RSV] Could not create output directory: {}", ec.message());
        return;
    }

    std::ofstream outFile(outPath);
    if (!outFile) {
        spdlog::error("[RSV] Failed to open '{}' for writing.", outPath.string());
        return;
    }
    outFile << root.dump(2);
    spdlog::info("[RSV] Written RSV Normal Maps hash cache to '{}'", outPath.string());
}

}  // namespace BodyLanguage
