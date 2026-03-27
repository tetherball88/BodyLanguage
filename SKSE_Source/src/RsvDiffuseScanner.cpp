#include "RsvDiffuseScanner.h"
#include "HashUtils.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace BodyLanguage {

// ---------------------------------------------------------------------------
// Internal helpers (mirror the style used in RsvNormalMapScanner.cpp)
// ---------------------------------------------------------------------------

static std::filesystem::path ReadIniPathDiffuse(const std::filesystem::path& iniPath,
    const wchar_t* section, const wchar_t* key)
{
    wchar_t buf[MAX_PATH]{};
    GetPrivateProfileStringW(section, key, L"", buf, MAX_PATH,
        iniPath.wstring().c_str());
    return std::filesystem::path(buf);
}

// Drain the current entry and compute MD5 via libarchive read blocks.
static std::string ComputeEntryMD5(archive* a)
{
    const void* buf  = nullptr;
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

static std::string NormPathDiffuse(std::string_view s)
{
    std::string r(s);
    for (char& c : r)
        if (c == '\\') c = '/';
    return r;
}

static std::string ToLowerDiffuse(std::string_view s)
{
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

// Explicit mapping: "race/variant" (lowercase) → complexion key.
// Derived directly from the known folder structure of the BnP RSV archives.
// Variants prefixed with '-' represent plain/default textures with no
// distinguishing complexion and are intentionally absent from this table.
static const std::unordered_map<std::string, std::string> kDiffuseComplexionMap = {
    // Akaviri (both brown and pink tone share the same complexion key)
    { "akaviri/brown",             "akaviri"       },
    { "akaviri/pink",              "akaviri"       },
    // Altmer
    { "altmer/+stretchmark",       "stretch_marks" },
    // Bosmer
    { "bosmer/+freckles",          "freckles_light" },
    // Dremora
    { "dremora/grey",              "dremora_dark"  },
    { "dremora/red",               "dremora_red"   },
    // Dunmer
    { "dunmer/+veins",             "dunmer_veiny"  },
    // Fantasy Altmer
    { "fantasyaltmer/gold",        "golden_altmer" },
    { "fantasyaltmer/silver",      "silver_altmer" },
    // Fantasy Bosmer
    { "fantasybosmer/autumn",      "autumn_bosmer" },
    { "fantasybosmer/spring",      "spring_bosmer" },
    // Fantasy Dunmer
    { "fantasydunmer/purple",      "purple_dunmer" },
    { "fantasydunmer/silver",      "silver_dunmer" },
    // Fantasy Orc
    { "fantasyorc/green",          "green_orc"     },
    { "fantasyorc/red",            "red_orc"       },
    // Imperial
    { "imperial/+scars",           "scars"         },
    // Monster Vampire
    { "monstervampire/+markings",  "vampire_beast" },
    // Nord
    { "nord/+frostnip",            "frostnip"      },
    // Orc
    { "orc/+dirt",                 "dirty"         },
    // Redguard
    { "redguard/+dark",            "tanlines"      },
};

// Extracts the "race/variant" key from a normalised archive path and looks it
// up in kDiffuseComplexionMap.  Returns "" for non-diffuse or unmapped paths.
//
// Expected layout: ...options/diffuse/<race>/<variant>/textures/...
static std::string MatchComplexion(const std::string& path)
{
    static constexpr std::string_view kMarker = "options/diffuse/";

    const auto markerPos = path.find(kMarker);
    if (markerPos == std::string::npos) return "";

    // Must be a body diffuse DDS: filename must contain "body", end in ".dds",
    // and must not be a normal map (_msn.dds) or specular (_s.dds).
    const std::string pathLow = ToLowerDiffuse(path);
    const auto lastSlash = pathLow.rfind('/');
    const std::string filenameLow = lastSlash != std::string::npos
        ? pathLow.substr(lastSlash + 1) : pathLow;
    if (filenameLow.find("body") == std::string::npos)
        return "";
    if (filenameLow.size() < 4 || filenameLow.substr(filenameLow.size() - 4) != ".dds")
        return "";
    // Exclude normal maps and specular maps that happen to be in the diffuse tree
    if (filenameLow.size() >= 8 && filenameLow.substr(filenameLow.size() - 8) == "_msn.dds")
        return "";
    if (filenameLow.size() >= 6 && filenameLow.substr(filenameLow.size() - 6) == "_s.dds")
        return "";

    // Isolate the two path components immediately after "options/diffuse/"
    // and before "textures/": <race>/<variant>/
    const std::string rel = ToLowerDiffuse(path.substr(markerPos + kMarker.size()));
    // rel = "fantasybosmer/autumn/textures/actors/..."
    const auto texPos = rel.find("textures/");
    if (texPos == std::string::npos) return "";

    // classSegment = "fantasybosmer/autumn/" – strip trailing slash for the lookup key
    std::string classSegment = rel.substr(0, texPos);
    if (!classSegment.empty() && classSegment.back() == '/')
        classSegment.pop_back();
    // classSegment = "fantasybosmer/autumn"

    const auto it = kDiffuseComplexionMap.find(classSegment);
    return it != kDiffuseComplexionMap.end() ? it->second : "";
}

// ---------------------------------------------------------------------------
// Single-archive scan helper
// ---------------------------------------------------------------------------

static void ScanOneArchive(const std::filesystem::path& archivePath,
    nlohmann::json& root)
{
    archive* a = archive_read_new();
    archive_read_support_format_7zip(a);
    archive_read_support_filter_lzma(a);
    archive_read_support_filter_xz(a);

    if (archive_read_open_filename_w(a, archivePath.wstring().c_str(), 65536) != ARCHIVE_OK) {
        spdlog::error("[RSVDiffuse] Failed to open archive '{}': {}",
            archivePath.string(), archive_error_string(a));
        archive_read_free(a);
        return;
    }

    int count = 0;
    archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* rawPath = archive_entry_pathname(entry);
        if (!rawPath) { archive_read_data_skip(a); continue; }

        const std::string normalised = NormPathDiffuse(rawPath);

        const std::string complexion = MatchComplexion(normalised);
        if (complexion.empty()) {
            archive_read_data_skip(a);
            continue;
        }

        const std::string hash = ComputeEntryMD5(a);
        if (hash.empty()) {
            spdlog::warn("[RSVDiffuse] MD5 failed for '{}'", normalised);
            continue;
        }

        nlohmann::json jEntry = nlohmann::json::object();
        jEntry["archive"]    = archivePath.filename().string();
        jEntry["filePath"]   = normalised;
        jEntry["complexion"] = complexion;
        root[hash] = std::move(jEntry);

        spdlog::trace("[RSVDiffuse] {} → complexion={}", hash, complexion);
        ++count;
    }

    archive_read_free(a);
    spdlog::info("[RSVDiffuse] Hashed {} diffuse files from '{}'.", count, archivePath.filename().string());
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void ScanRsvDiffuseMaps(const std::filesystem::path& dataRoot)
{
    // ---- Locate INI --------------------------------------------------------
    const auto iniPath = dataRoot / "SKSE" / "Plugins" / "BodyLanguage.ini";
    if (!std::filesystem::exists(iniPath)) {
        spdlog::warn("[RSVDiffuse] BodyLanguage.ini not found at '{}' – skipping diffuse scan.",
            iniPath.string());
        return;
    }

    // ---- Read archive paths from INI ---------------------------------------
    const auto femaleArchivePath = ReadIniPathDiffuse(iniPath, L"RSV", L"BNP_RSV__Female_ArchivePath");
    const auto maleArchivePath   = ReadIniPathDiffuse(iniPath, L"RSV", L"BNP_RSV__Male_ArchivePath");

    const bool hasFemale = !femaleArchivePath.empty() && std::filesystem::exists(femaleArchivePath);
    const bool hasMale   = !maleArchivePath.empty()   && std::filesystem::exists(maleArchivePath);

    if (!femaleArchivePath.empty() && !hasFemale)
        spdlog::warn("[RSVDiffuse] BNP_RSV__Female_ArchivePath '{}' not found – skipping.",
            femaleArchivePath.string());
    if (!maleArchivePath.empty() && !hasMale)
        spdlog::warn("[RSVDiffuse] BNP_RSV__Male_ArchivePath '{}' not found – skipping.",
            maleArchivePath.string());

    if (!hasFemale && !hasMale) {
        spdlog::info("[RSVDiffuse] No BnP diffuse archives configured – skipping diffuse scan.");
        return;
    }

    // ---- Compute archive MD5s for cache validation -------------------------
    std::string femaleMd5, maleMd5;
    if (hasFemale) {
        femaleMd5 = BodyLanguage::ComputeMD5(femaleArchivePath);
        if (femaleMd5.empty()) {
            spdlog::warn("[RSVDiffuse] Failed to compute MD5 for female archive – skipping female scan.");
        }
    }
    if (hasMale) {
        maleMd5 = BodyLanguage::ComputeMD5(maleArchivePath);
        if (maleMd5.empty()) {
            spdlog::warn("[RSVDiffuse] Failed to compute MD5 for male archive – skipping male scan.");
        }
    }

    const std::string combinedVersion = femaleMd5 + ":" + maleMd5;

    // ---- Output path -------------------------------------------------------
    const auto outDir  = dataRoot / "SKSE" / "Plugins" / "BodyLanguage";
    const auto outPath = outDir / "rsv_diffuse_hashes.json";

    // ---- Check cache -------------------------------------------------------
    std::error_code ec;
    if (std::filesystem::exists(outPath, ec)) {
        std::ifstream f(outPath);
        if (f) {
            try {
                const auto cached = nlohmann::json::parse(f);
                if (cached.value("__version", "") == combinedVersion) {
                    spdlog::info("[RSVDiffuse] Diffuse hash cache is up-to-date – skipping scan.");
                    return;
                }
            } catch (...) {}
        }
    }

    // ---- Scan archives -----------------------------------------------------
    nlohmann::json root = nlohmann::json::object();

    if (hasFemale && !femaleMd5.empty()) {
        spdlog::info("[RSVDiffuse] Scanning female archive '{}'", femaleArchivePath.string());
        ScanOneArchive(femaleArchivePath, root);
    }
    if (hasMale && !maleMd5.empty()) {
        spdlog::info("[RSVDiffuse] Scanning male archive '{}'", maleArchivePath.string());
        ScanOneArchive(maleArchivePath, root);
    }

    root["__version"] = combinedVersion;

    // ---- Write JSON --------------------------------------------------------
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        spdlog::error("[RSVDiffuse] Could not create output directory: {}", ec.message());
        return;
    }

    std::ofstream outFile(outPath);
    if (!outFile) {
        spdlog::error("[RSVDiffuse] Failed to open '{}' for writing.", outPath.string());
        return;
    }
    outFile << root.dump(2);
    spdlog::info("[RSVDiffuse] Written diffuse hash cache to '{}'", outPath.string());
}

}  // namespace BodyLanguage
