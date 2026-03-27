#include "BodyDescription.h"
#include "HashUtils.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <RE/T/TESDataHandler.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "OBodyService.h"

namespace BodyLanguage {

namespace {

    // -----------------------------------------------------------------------
    // Data store — loaded once at kDataLoaded
    // -----------------------------------------------------------------------

    struct DescriptionData {
        nlohmann::json femaleParts;
        nlohmann::json maleParts;
        // bodyType key ("3ba" / "bhunp" / "himbo") → full description JSON
        std::unordered_map<std::string, nlohmann::json> descData;
        // RSV normal map hash cache (rsv_normal_map_hashes.json)
        nlohmann::json rsvHashes;
        // RSV diffuse hash cache (rsv_diffuse_hashes.json)
        nlohmann::json rsvDiffuseHashes;
        // Complexion key → description sentence (rsvSkinComplexions.json)
        nlohmann::json complexionSentences;
        std::filesystem::path dataRoot;
    };

    static DescriptionData& GetData()
    {
        static DescriptionData instance;
        return instance;
    }

    static nlohmann::json LoadJson(const std::filesystem::path& path)
    {
        std::ifstream f(path);
        if (!f) return nullptr;
        try {
            nlohmann::json j;
            f >> j;
            return j;
        } catch (const std::exception& e) {
            spdlog::warn("[BodyDescription] Failed to parse '{}': {}", path.string(), e.what());
            return nullptr;
        }
    }

    // -----------------------------------------------------------------------
    // Meta parsing  "waist:tiny(56);belly:flat(-395);..." → {group → keyword}
    // -----------------------------------------------------------------------

    static std::unordered_map<std::string, std::string> ParseMeta(std::string_view meta)
    {
        std::unordered_map<std::string, std::string> result;
        std::string token;
        auto flush = [&]() {
            if (token.empty()) return;
            // token = "group:keyword(value)"
            const auto colon = token.find(':');
            if (colon == std::string::npos) { token.clear(); return; }
            const auto paren = token.find('(', colon);
            const std::string group   = token.substr(0, colon);
            const std::string keyword = token.substr(colon + 1,
                paren == std::string::npos ? std::string::npos : paren - colon - 1);
            result[group] = keyword;
            token.clear();
        };
        for (char c : meta) {
            if (c == ';') flush();
            else          token += c;
        }
        flush();
        return result;
    }

    // -----------------------------------------------------------------------
    // Phrase resolution (mirrors SliderPresetScanner::ResolvePart)
    // -----------------------------------------------------------------------

    static std::string ResolvePart(const nlohmann::json& parts,
                                   const std::string& group,
                                   const std::string& keyword,
                                   const std::string& ctx)
    {
        if (parts.contains(group) &&
            parts[group].contains(keyword) &&
            parts[group][keyword].contains(ctx))
        {
            return parts[group][keyword][ctx].get<std::string>();
        }
        return keyword;
    }

    // -----------------------------------------------------------------------
    // Sentence templates (mirrors SliderPresetScanner)
    // -----------------------------------------------------------------------

    static std::string BuildFemaleFullSentence(
        const std::unordered_map<std::string, std::string>& kw,
        const nlohmann::json& parts,
        const std::string& ctx)
    {
        auto get = [&](const std::string& group) -> std::string {
            const auto it = kw.find(group);
            return it != kw.end() ? ResolvePart(parts, group, it->second, ctx) : "???";
        };

        if (ctx == "clothed") {
            return get("frame") + 
                    ", with " + get("breasts_volume") + 
                    ". Her midsection shows " + get("waist") + 
                    ", her lower body broadening into " + get("butt_hips") + 
                    ". " + get("legs") + 
                    " — the whole silhouette reading as " + get("physique_condition") + ".";
        }
        return get("frame") + 
                ", carrying " + get("breasts_volume") + 
                ". Her waist is " + get("waist") + 
                ", her belly " + get("belly") + 
                ". Below, " + get("butt_hips") + 
                ", and " + get("legs") + 
                " — altogether a " + get("physique_condition") + " figure.";
    }

    static std::string BuildFemaleShortSentence(
        const std::unordered_map<std::string, std::string>& kw,
        const nlohmann::json& parts,
        const std::string& ctx)
    {
        auto get = [&](const std::string& group) -> std::string {
            const auto it = kw.find(group);
            return it != kw.end() ? ResolvePart(parts, group, it->second, ctx) : "???";
        };

        if (ctx == "clothed") {
            return get("frame") + 
                    ", with " + get("breasts_volume") + 
                    " and " + get("butt_hips") + 
                    " — an overall " + get("physique_condition") + " figure.";
        }
        return get("frame") + 
                " — the eye drawn first to " + get("breasts_volume") + 
                ", then to " + get("butt_hips") + 
                ". The overall impression: " + get("physique_condition") + ".";
    }

    static std::string BuildMaleFullSentence(
        const std::unordered_map<std::string, std::string>& kw,
        const nlohmann::json& parts,
        const std::string& ctx)
    {
        auto get = [&](const std::string& group) -> std::string {
            const auto it = kw.find(group);
            return it != kw.end() ? ResolvePart(parts, group, it->second, ctx) : "???";
        };

        if (ctx == "clothed") {
            return " " + get("shoulder_breadth") + 
                    " that frames " + get("stature_frame") + 
                    ". Beneath his clothing, " + get("midsection") + 
                    ", with " + get("arms") + 
                    ". His lower body shows " + get("legs") + 
                    " and " + get("glutes") + 
                    ", the whole figure carrying a " + get("physique_condition") + " quality.";
        }
        return " " + get("shoulder_breadth") + 
                " and " + get("stature_frame") + 
                ", defined by " + get("midsection") + 
                ". His silhouette features " + get("arms") + 
                ", " + get("legs") + 
                ", and " + get("glutes") + 
                ", maintaining an overall " + get("physique_condition") + ".";
    }

    static std::string BuildMaleShortSentence(
        const std::unordered_map<std::string, std::string>& kw,
        const nlohmann::json& parts,
        const std::string& ctx)
    {
        auto get = [&](const std::string& group) -> std::string {
            const auto it = kw.find(group);
            return it != kw.end() ? ResolvePart(parts, group, it->second, ctx) : "???";
        };

        if (ctx == "clothed") {
            return " " + get("shoulder_breadth") + 
                    " framing a " + get("physique_condition") + 
                    " build, with " + get("midsection") + ".";
        }
        return " " + get("shoulder_breadth") + 
                " and " + get("stature_frame") + 
                ", with " + get("arms") + 
                " — overall " + get("physique_condition") + ".";
    }

    // -----------------------------------------------------------------------
    // Actor helpers
    // -----------------------------------------------------------------------

    static bool IsPluginLoaded(std::string_view esp)
    {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return false;
        return dh->LookupLoadedModByName(esp) != nullptr ||
               dh->LookupLoadedLightModByName(esp) != nullptr;
    }

    static std::string DetectBodyType(RE::Actor* actor)
    {
        auto* base = actor->GetActorBase();
        const bool isFemale = base && base->GetSex() == RE::SEX::kFemale;
        if (isFemale) {
            if (IsPluginLoaded("CBBE.esp"))      return "3ba";
            if (IsPluginLoaded("BHUNP3BBB.esp")) return "bhunp";
        } else {
            if (IsPluginLoaded("HIMBO.esp"))     return "himbo";
        }
        return "";
    }

    static std::string_view WeightTier(RE::Actor* actor)
    {
        const float w = actor->GetActorBase() ? actor->GetActorBase()->GetWeight() : 50.0f;
        if (w < 34.0f) return "small";
        if (w < 67.0f) return "medium";
        return "big";
    }

    // -----------------------------------------------------------------------
    // Looks up a (bodyType, fileStem, md5) triplet in the RSV hash cache.
    // Returns the matched subKey (e.g. "bigBreasts", "smallBreasts", "body")
    struct RsvMatch {
        std::string breastSize;   // "big", "small", or "NA" (himbo/no split)
        std::string muscularity;  // e.g. "toned50", "boxer"
    };

    // Direct O(1) lookup: new JSON is keyed by MD5.
    static std::optional<RsvMatch> LookupRsvVariant(const nlohmann::json& rsvHashes,
        const std::string& md5)
    {
        if (rsvHashes.is_null() || !rsvHashes.contains(md5)) return std::nullopt;

        const auto& entry = rsvHashes[md5];
        RsvMatch m{};
        m.muscularity = entry.value("muscularity", "");
        if (entry.contains("isBigBreasts"))
            m.breastSize = entry["isBigBreasts"].get<std::string>();
        else
            m.breastSize = "NA";
        return m;
    }

    // -----------------------------------------------------------------------
    // RSV bump tables and helpers
    // -----------------------------------------------------------------------

    // Female: single combined axis — lower index = more defined/less fat
    static const std::vector<std::string> kFemalePhysiqueOrder = {
        "muscular", "athletic", "lean", "soft", "plump", "chubby", "obese"
    };
    // Male: single combined axis — lower index = more defined/less fat
    static const std::vector<std::string> kMalePhysiqueOrder = {
        "shredded", "muscular", "athletic", "solid", "soft", "heavy", "obese"
    };


    static void BumpKeyword(std::unordered_map<std::string, std::string>& kw,
                            const std::string& group,
                            const std::vector<std::string>& order,
                            int delta)
    {
        auto it = kw.find(group);
        if (it == kw.end()) return;
        const auto pos = std::find(order.begin(), order.end(), it->second);
        if (pos == order.end()) return;
        const int idx    = static_cast<int>(std::distance(order.begin(), pos));
        const int newIdx = std::clamp(idx + delta, 0, static_cast<int>(order.size()) - 1);
        it->second = order[static_cast<std::size_t>(newIdx)];
    }

    // Applies RSV-derived muscle/fat bumps to the parsed meta keyword map.
    // adjusts "physique_condition" on its combined axis
    //   (muscle++ = toward shredded; fat++ = toward obese).
    static void ApplyRsvBumps(std::unordered_map<std::string, std::string>& kw,
                              const RsvMatch& rsv,
                              bool isMale)
    {
        int muscleBump = 0;

        const auto& m = rsv.muscularity;
        if (m == "vigorexia")
            muscleBump = 2;
        else if (m == "boxer" || m == "vigorexia50" || m == "dovaklini")
            muscleBump = 1;

        if (m == "chubby")
            muscleBump = -1;

        if (muscleBump == 0) return;

        if (isMale) {
            // On the male axis, muscle++ lowers the index; fat++ raises it
            BumpKeyword(kw, "physique_condition", kMalePhysiqueOrder, -muscleBump);
        } else {
            // On the female axis, muscle++ lowers the index; fat++ raises it
            BumpKeyword(kw, "physique_condition", kFemalePhysiqueOrder, -muscleBump);
        }
    }

    // Walk the actor's third-person skeleton looking for the body geometry's
    // MSN (normal map) texture path.  Returns empty string if not found.
    //
    // Strategy: traverse every BSGeometry on the scene graph; for each one
    // grab the BSLightingShaderProperty → BSShaderMaterial → BSTextureSet and
    // read slot kNormal (index 1).  Accept the first path whose lower-case
    // form contains both "body" and "msn" (the standard naming convention used
    // by CBBE/3BA/BHUNP/HIMBO body textures).
    // -----------------------------------------------------------------------
    static std::string GetBodyNormalMapPath(RE::Actor* actor)
    {
        auto* root = actor->Get3D(false);  // third-person node
        if (!root) return {};

        std::string result;

        RE::BSVisit::TraverseScenegraphGeometries(root,
            [&](RE::BSGeometry* geo) -> RE::BSVisit::BSVisitControl
            {
                auto* shaderProp = geo->lightingShaderProp_cast();
                if (!shaderProp) return RE::BSVisit::BSVisitControl::kContinue;

                auto* mat = static_cast<RE::BSLightingShaderMaterialBase*>(
                    shaderProp->GetBaseMaterial());
                if (!mat) return RE::BSVisit::BSVisitControl::kContinue;

                auto texSet = mat->GetTextureSet();
                if (!texSet) return RE::BSVisit::BSVisitControl::kContinue;

                const char* path = texSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal);
                if (!path || *path == '\0') return RE::BSVisit::BSVisitControl::kContinue;

                // Check only the filename, not directory components
                std::string fname = std::filesystem::path(path).filename().string();
                std::transform(fname.begin(), fname.end(), fname.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (fname.find("body") != std::string::npos &&
                    fname.find("msn")  != std::string::npos)
                {
                    result = path;
                    return RE::BSVisit::BSVisitControl::kStop;
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });

        return result;
    }

    // Walk the actor's third-person skeleton looking for the body geometry's
    // diffuse (albedo) texture path.  Returns empty string if not found.
    // Reads slot kDiffuse (index 0); accepts the first entry whose *filename*
    // contains "body" but NOT "msn" (checks filename only, not directory).
    static std::string GetBodyDiffusePath(RE::Actor* actor)
    {
        auto* root = actor->Get3D(false);
        if (!root) return {};

        std::string result;

        RE::BSVisit::TraverseScenegraphGeometries(root,
            [&](RE::BSGeometry* geo) -> RE::BSVisit::BSVisitControl
            {
                auto* shaderProp = geo->lightingShaderProp_cast();
                if (!shaderProp) return RE::BSVisit::BSVisitControl::kContinue;

                auto* mat = static_cast<RE::BSLightingShaderMaterialBase*>(
                    shaderProp->GetBaseMaterial());
                if (!mat) return RE::BSVisit::BSVisitControl::kContinue;

                auto texSet = mat->GetTextureSet();
                if (!texSet) return RE::BSVisit::BSVisitControl::kContinue;

                const char* path = texSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
                if (!path || *path == '\0') return RE::BSVisit::BSVisitControl::kContinue;

                // Check only the filename, not directory components
                std::string fname = std::filesystem::path(path).filename().string();
                std::transform(fname.begin(), fname.end(), fname.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (fname.find("body") != std::string::npos &&
                    fname.find("msn")  == std::string::npos)
                {
                    result = path;
                    return RE::BSVisit::BSVisitControl::kStop;
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });

        return result;
    }

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void InitBodyDescriptions(const std::filesystem::path& dataRoot)
{
    const auto descDir     = dataRoot / "SKSE" / "Plugins" / "SkyrimNet" / "jsonData" / "descriptions";
    const auto configsRoot = dataRoot / "SKSE" / "Plugins" / "BodyLanguage" / "bodyConfigs";

    auto& data = GetData();

    data.femaleParts = LoadJson(configsRoot / "FemaleParts.json");
    data.maleParts   = LoadJson(configsRoot / "MaleParts.json");

    for (const auto& [key, file] : std::initializer_list<std::pair<const char*, const char*>>{
            {"3ba",   "3ba.json"},
            {"bhunp", "bhunp.json"},
            {"himbo", "himbo.json"}})
    {
        auto j = LoadJson(descDir / file);
        if (!j.is_null())
            data.descData[key] = std::move(j);
    }

    spdlog::info("[BodyDescription] Loaded description data for {} body type(s).", data.descData.size());

    data.dataRoot = dataRoot;
    data.rsvHashes = LoadJson(
        dataRoot / "SKSE" / "Plugins" / "BodyLanguage" / "rsv_normal_map_hashes.json");
    if (data.rsvHashes.is_null())
        spdlog::warn("[BodyDescription] RSV hash cache not found - RSV variant detection disabled.");
    else
        spdlog::info("[BodyDescription] Loaded RSV normal map hash cache.");

    data.rsvDiffuseHashes = LoadJson(
        dataRoot / "SKSE" / "Plugins" / "BodyLanguage" / "rsv_diffuse_hashes.json");
    if (data.rsvDiffuseHashes.is_null())
        spdlog::warn("[BodyDescription] RSV diffuse hash cache not found - complexion detection disabled.");
    else
        spdlog::info("[BodyDescription] Loaded RSV diffuse hash cache.");

    data.complexionSentences = LoadJson(
        dataRoot / "SKSE" / "Plugins" / "BodyLanguage" / "rsvSkinComplexions.json");
    if (data.complexionSentences.is_null())
        spdlog::warn("[BodyDescription] rsvSkinComplexions.json not found - complexion sentences disabled.");
    else
        spdlog::info("[BodyDescription] Loaded complexion sentences.");
}

void ReloadRsvHashes(const std::filesystem::path& dataRoot)
{
    auto& data = GetData();
    data.rsvHashes = LoadJson(
        dataRoot / "SKSE" / "Plugins" / "BodyLanguage" / "rsv_normal_map_hashes.json");
    if (data.rsvHashes.is_null())
        spdlog::warn("[BodyDescription] RSV hash cache reload: file not found.");
    else
        spdlog::info("[BodyDescription] RSV hash cache reloaded successfully.");
}

void ReloadRsvDiffuseHashes(const std::filesystem::path& dataRoot)
{
    auto& data = GetData();
    data.rsvDiffuseHashes = LoadJson(
        dataRoot / "SKSE" / "Plugins" / "BodyLanguage" / "rsv_diffuse_hashes.json");
    if (data.rsvDiffuseHashes.is_null())
        spdlog::warn("[BodyDescription] RSV diffuse hash cache reload: file not found.");
    else
        spdlog::info("[BodyDescription] RSV diffuse hash cache reloaded successfully.");
}

std::string GetBodyDescription(RE::Actor* actor, std::string_view mode)
{
    if (!actor) return "";

    auto& obody    = OBodyService::GetInstance();
    auto& data     = GetData();

    if (!obody.IsAvailable()) {
        spdlog::warn("[BodyDescription] OBody API not available (not ready or no interface).");
        return "";
    }

    const std::string preset   = obody.GetPresetName(actor);
    const std::string bodyType = DetectBodyType(actor);
    const std::string tier     = std::string(WeightTier(actor));
    const std::string ctx      = obody.IsNaked(actor) ? "naked" : "clothed";
    const bool        isMale   = bodyType == "himbo";
    const auto& parts          = isMale ? data.maleParts : data.femaleParts;

    spdlog::info("[BodyDescription] mode={} preset='{}' bodyType='{}' tier='{}' ctx='{}'",
        mode, preset, bodyType, tier, ctx);

    const std::string msnPath = GetBodyNormalMapPath(actor);
    spdlog::info("[BodyDescription] body MSN path: '{}'", msnPath.empty() ? "<not found>" : msnPath);

    // Find body diffuse texture path for complexion lookup
    const std::string diffusePath = GetBodyDiffusePath(actor);
    spdlog::info("[BodyDescription] body diffuse path: '{}'", diffusePath.empty() ? "<not found>" : diffusePath);

    std::optional<RsvMatch> rsvMatch;

    if (!msnPath.empty() && !data.rsvHashes.is_null()) {
        // Derive the file stem (lowercase, no extension) for JSON lookup
        std::filesystem::path msnFsPath(msnPath);
        std::string msnStem = msnFsPath.stem().string();
        std::transform(msnStem.begin(), msnStem.end(), msnStem.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // Texture paths can be stored either relative to the textures directory
        // (e.g. "actors\...") or from the data root (e.g. "textures\actors\...").
        // Normalise by prepending "textures\" when not already present.
        std::string msnRelative = msnPath;
        {
            std::string low = msnRelative;
            std::transform(low.begin(), low.end(), low.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (low.rfind("textures", 0) != 0 &&
                low.rfind("textures/", 0) != 0 &&
                low.rfind("textures\\", 0) != 0)
            {
                msnRelative = "textures\\" + msnRelative;
            }
        }
        const auto fullPath = data.dataRoot / msnRelative;
        const std::string md5 = ComputeMD5(fullPath);
        if (md5.empty()) {
            spdlog::warn("[BodyDescription] RSV: could not read file '{}' (BSA-packed or missing)",
                fullPath.string());
        } else {
            rsvMatch = LookupRsvVariant(data.rsvHashes, md5);
            if (!rsvMatch)
                spdlog::info("[BodyDescription] RSV: no variant match for stem='{}' md5='{}'",
                    msnStem, md5);
            else
                spdlog::info("[BodyDescription] RSV: matched muscularity='{}' breastSize='{}'",
                    rsvMatch->muscularity, rsvMatch->breastSize);
        }
    }

    // Try to look up meta and build a data-driven sentence
    std::string sentence;
    if (!bodyType.empty() && !preset.empty()) {
        const auto descIt = data.descData.find(bodyType);
        if (descIt != data.descData.end()) {
            const auto& desc = descIt->second;
            if (desc.contains(preset) && desc[preset].contains(tier) &&
                desc[preset][tier].contains("meta"))
            {
                const std::string meta = desc[preset][tier]["meta"].get<std::string>();
                auto kw = ParseMeta(meta);

                if (rsvMatch)
                    ApplyRsvBumps(kw, *rsvMatch, isMale);

                if (mode == "short") {
                    sentence = isMale
                        ? BuildMaleShortSentence(kw, parts, ctx)
                        : BuildFemaleShortSentence(kw, parts, ctx);
                } else {
                    sentence = isMale
                        ? BuildMaleFullSentence(kw, parts, ctx)
                        : BuildFemaleFullSentence(kw, parts, ctx);
                }
            }
        }
    }

    // For full naked descriptions, prepend the complexion sentence if detected
    if (mode == "full" && ctx == "naked" && !diffusePath.empty()
        && !data.rsvDiffuseHashes.is_null() && !data.complexionSentences.is_null())
    {
        std::string diffuseRelative = diffusePath;
        {
            std::string low = diffuseRelative;
            std::transform(low.begin(), low.end(), low.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (low.rfind("textures", 0) != 0 &&
                low.rfind("textures/", 0) != 0 &&
                low.rfind("textures\\", 0) != 0)
            {
                diffuseRelative = "textures\\" + diffuseRelative;
            }
        }
        const auto diffuseFullPath = data.dataRoot / diffuseRelative;
        const std::string diffuseMd5 = ComputeMD5(diffuseFullPath);
        if (!diffuseMd5.empty() && data.rsvDiffuseHashes.contains(diffuseMd5)) {
            const std::string complexionKey =
                data.rsvDiffuseHashes[diffuseMd5].value("complexion", "");
            spdlog::info("[BodyDescription] Complexion matched: key='{}'", complexionKey);
            if (!complexionKey.empty() && data.complexionSentences.contains(complexionKey)) {
                const std::string pronoun = isMale ? "He " : "She ";
                const std::string complexionDesc =
                    pronoun + data.complexionSentences[complexionKey].get<std::string>();
                if (!sentence.empty())
                    sentence = sentence + " " + complexionDesc;
                else
                    sentence = complexionDesc;
            }
        } else {
            spdlog::info("[BodyDescription] Complexion: no match for diffuse md5='{}'",
                diffuseMd5.empty() ? "<unreadable>" : diffuseMd5);
        }
    }

    return sentence;
}

}  // namespace BodyLanguage



