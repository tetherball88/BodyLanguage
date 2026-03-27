#include "SliderPresetScanner.h"

#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <pugixml.hpp>
#include <spdlog/spdlog.h>

namespace BodyLanguage {

    // ---------------------------------------------------------------------------
    // Data structures
    // ---------------------------------------------------------------------------

    struct SliderMapEntry {
        double smallDefault{ 0.0 };
        double bigDefault{ 0.0 };
        double impact{ 0.0 };
    };

    struct GoalRange {
        double lo;
        double hi;
        std::string keyword;
    };

    struct GoalSlider {
        std::string name;
        double multiplier{ 1.0 };
    };

    struct GoalGroup {
        std::vector<GoalSlider> sliders;
        std::vector<GoalRange> ranges;  // ordered as in JSON (lo ascending)
    };

    struct BodyConfig {
        std::unordered_map<std::string, SliderMapEntry> sliderMap;
        std::unordered_map<std::string, GoalGroup> goals;
    };

    struct BodyTypeInfo {
        std::string configFolder;   // sub-folder under bodyConfigs/
        std::string outputFile;     // e.g. "3ba.json"
    };

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    static std::optional<BodyTypeInfo> ResolveBodyType(std::string_view set) {
        // Case-insensitive substring match
        std::string lower(set);
        std::ranges::transform(lower, lower.begin(), ::tolower);

        auto contains = [&](std::string_view sub) {
            return lower.find(sub) != std::string::npos;
        };

        if (contains("himbo"))                                    return BodyTypeInfo{ "himbo", "himbo.json" };
        if (contains("bhunp"))                                    return BodyTypeInfo{ "bhunp", "bhunp.json" };
        if (contains("3ba") || contains("3bbb") || contains("cbbe")) return BodyTypeInfo{ "3BA",   "3ba.json"   };
        return std::nullopt;
    }

    static std::optional<BodyConfig> LoadBodyConfig(const std::filesystem::path& configsRoot,
                                                    const std::string& folder) {
        const auto dir           = configsRoot / folder;
        const auto sliderMapPath  = dir / "SliderMap.json";
        const auto sliderGoalsPath = dir / "SliderGoals.json";

        if (!std::filesystem::exists(sliderMapPath) || !std::filesystem::exists(sliderGoalsPath)) {
            spdlog::warn("[BodyLanguage] Config files not found for body type '{}' in '{}'",
                         folder, dir.string());
            return std::nullopt;
        }

        BodyConfig cfg;

        // --- SliderMap ---
        {
            std::ifstream f(sliderMapPath);
            nlohmann::json j;
            try { f >> j; } catch (const std::exception& e) {
                spdlog::error("[BodyLanguage] Failed to parse SliderMap.json for '{}': {}", folder, e.what());
                return std::nullopt;
            }
            for (auto& [key, val] : j.items()) {
                cfg.sliderMap[key] = {
                    val.value("smallDefault", 0.0),
                    val.value("bigDefault",   0.0),
                    val.value("impact",       0.0)
                };
            }
        }

        // --- SliderGoals ---
        {
            std::ifstream f(sliderGoalsPath);
            nlohmann::json j;
            try { f >> j; } catch (const std::exception& e) {
                spdlog::error("[BodyLanguage] Failed to parse SliderGoals.json for '{}': {}", folder, e.what());
                return std::nullopt;
            }
            for (auto& [groupName, groupVal] : j["goals"].items()) {
                GoalGroup group;

                for (const auto& sliderToken : groupVal["sliders"]) {
                    std::string token = sliderToken.get<std::string>();
                    GoalSlider gs;
                    if (const auto colon = token.find(':'); colon != std::string::npos) {
                        gs.name       = token.substr(0, colon);
                        try { gs.multiplier = std::stod(token.substr(colon + 1)); }
                        catch (...) { gs.multiplier = 1.0; }
                    } else {
                        gs.name       = std::move(token);
                        gs.multiplier = 1.0;
                    }
                    group.sliders.push_back(std::move(gs));
                }

                // nlohmann/json preserves JSON object insertion order
                for (auto& [rangeName, bounds] : groupVal["ranges"].items()) {
                    group.ranges.push_back({ bounds[0].get<double>(), bounds[1].get<double>(), rangeName });
                }

                cfg.goals[groupName] = std::move(group);
            }
        }

        return cfg;
    }

    // Validate that a preset entry has the required small/medium/big tier structure.
    // Each tier must have a 'meta' string.
    static bool IsValidPresetEntry(const nlohmann::json& entry, std::string& outError) {
        if (!entry.is_object()) { outError = "root must be an object"; return false; }

        for (const auto& tier : { "small", "medium", "big" }) {
            if (!entry.contains(tier) || !entry[tier].is_object()) {
                outError = std::string("missing or invalid tier '") + tier + "'";
                return false;
            }
            const auto& t = entry[tier];
            if (!t.contains("meta") || !t["meta"].is_string()) {
                outError = std::string("tier '") + tier + "': 'meta' must be a string";
                return false;
            }
        }
        return true;
    }

    static const std::string& FindRangeKeyword(const std::vector<GoalRange>& ranges, double score) {
        for (const auto& r : ranges) {
            if (score >= r.lo && score < r.hi) return r.keyword;
        }
        // out-of-range: clamp to nearest end
        return score < ranges.front().lo ? ranges.front().keyword : ranges.back().keyword;
    }

    // Compute goal group results for one preset given its small/big slider values (0-1 normalised).
    static nlohmann::json ComputePresetGoals(
        const std::unordered_map<std::string, double>& sliderSmall,
        const std::unordered_map<std::string, double>& sliderBig,
        const BodyConfig& cfg)
    {
        nlohmann::json result = nlohmann::json::object();

        // Per-tier keyword maps for sentence building, and meta string accumulators
        std::unordered_map<std::string, std::string> kwSmall, kwMedium, kwBig;
        std::string metaSmall, metaMedium, metaBig;

        for (const auto& [groupName, group] : cfg.goals) {
            double scoreSmall = 0.0, scoreMedium = 0.0, scoreBig = 0.0;

            for (const auto& gs : group.sliders) {
                const auto mapIt = cfg.sliderMap.find(gs.name);
                if (mapIt == cfg.sliderMap.end()) continue;

                const double impact = mapIt->second.impact;

                const double valSm = sliderSmall.contains(gs.name)
                    ? sliderSmall.at(gs.name)
                    : mapIt->second.smallDefault / 100.0;
                const double valBg = sliderBig.contains(gs.name)
                    ? sliderBig.at(gs.name)
                    : mapIt->second.bigDefault / 100.0;
                const double valMd = (valSm + valBg) / 2.0;

                scoreSmall  += valSm * impact * gs.multiplier;
                scoreMedium += valMd * impact * gs.multiplier;
                scoreBig    += valBg * impact * gs.multiplier;
            }

            const std::string& ksm = FindRangeKeyword(group.ranges, scoreSmall);
            const std::string& kmd = FindRangeKeyword(group.ranges, scoreMedium);
            const std::string& kbg = FindRangeKeyword(group.ranges, scoreBig);

            kwSmall[groupName]  = ksm;
            kwMedium[groupName] = kmd;
            kwBig[groupName]    = kbg;

            auto appendMeta = [&](std::string& meta, const std::string& kw, double score) {
                if (!meta.empty()) meta += ';';
                meta += groupName + ':' + kw + '(' + std::to_string(static_cast<int>(std::round(score))) + ')';
            };
            appendMeta(metaSmall,  ksm, scoreSmall);
            appendMeta(metaMedium, kmd, scoreMedium);
            appendMeta(metaBig,    kbg, scoreBig);
        }

        result["small"]["meta"]   = metaSmall;
        result["medium"]["meta"]  = metaMedium;
        result["big"]["meta"]     = metaBig;

        return result;
    }

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    void ScanAndGeneratePresetDescriptions(const std::filesystem::path& dataRoot) {
        const auto presetsDir   = dataRoot / "CalienteTools" / "BodySlide" / "SliderPresets";
        const auto outputDir    = dataRoot / "SKSE" / "Plugins" / "SkyrimNet" / "jsonData" / "descriptions";
        const auto configsRoot  = dataRoot / "SKSE" / "Plugins" / "BodyLanguage" / "bodyConfigs";
        
        if (!std::filesystem::exists(presetsDir)) {
            spdlog::warn("[BodyLanguage] SliderPresets directory not found: {}", presetsDir.string());
            return;
        }

        // Cache configs by folder name so we load each at most once
        std::unordered_map<std::string, BodyConfig> configCache;

        // outputFile -> { presetName -> goalJson }
        std::map<std::string, std::map<std::string, nlohmann::json>> collected;

        for (const auto& entry : std::filesystem::directory_iterator(presetsDir)) {
            if (entry.path().extension() != ".xml") continue;

            pugi::xml_document doc;
            const pugi::xml_parse_result parseResult = doc.load_file(entry.path().c_str());
            if (!parseResult) {
                spdlog::warn("[BodyLanguage] Failed to parse '{}': {}",
                             entry.path().filename().string(), parseResult.description());
                continue;
            }

            for (const pugi::xml_node presetNode : doc.child("SliderPresets").children("Preset")) {
                const std::string name = presetNode.attribute("name").as_string();
                const std::string set  = presetNode.attribute("set").as_string();
                if (name.empty()) continue;

                const auto bodyTypeOpt = ResolveBodyType(set);
                if (!bodyTypeOpt) {
                    spdlog::warn("[BodyLanguage] Unrecognised preset set '{}' in '{}' (preset: '{}')",
                                 set, entry.path().filename().string(), name);
                    continue;
                }

                const auto& bodyType = *bodyTypeOpt;

                // Load config on first encounter of this body type
                if (!configCache.contains(bodyType.configFolder)) {
                    auto cfgOpt = LoadBodyConfig(configsRoot, bodyType.configFolder);
                    if (!cfgOpt) continue;
                    configCache[bodyType.configFolder] = std::move(*cfgOpt);
                }
                const auto& cfg = configCache.at(bodyType.configFolder);

                // Skip duplicates (first file/preset wins)
                auto& bucket = collected[bodyType.outputFile];
                if (bucket.contains(name)) continue;

                // Collect SetSlider values, normalised from 0-100 to 0-1
                std::unordered_map<std::string, double> sliderSmall, sliderBig;
                for (const pugi::xml_node ss : presetNode.children("SetSlider")) {
                    const std::string slName = ss.attribute("name").as_string();
                    const std::string size   = ss.attribute("size").as_string();
                    const double value       = ss.attribute("value").as_double() / 100.0;
                    if (size == "small")     sliderSmall[slName] = value;
                    else if (size == "big")  sliderBig[slName]   = value;
                }

                bucket[name] = ComputePresetGoals(sliderSmall, sliderBig, cfg);
            }
        }

        // Ensure output directory exists
        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);
        if (ec) {
            spdlog::error("[BodyLanguage] Failed to create descriptions directory: {}", ec.message());
            return;
        }

        // Write each output file — computed entries always override previous output.
        // A sidecar *_manual.json file (same structure) is applied on top and always wins.
        for (auto& [filename, newEntries] : collected) {
            const auto outputFile = outputDir / filename;

            // All computed entries replace whatever was there before
            nlohmann::json result = nlohmann::json::object();
            for (auto& [presetName, goalData] : newEntries) {
                result[presetName] = std::move(goalData);
            }

            // Apply _manual sidecar on top — manual entries always win over computed ones
            const auto stem       = std::filesystem::path(filename).stem().string();
            const auto ext        = std::filesystem::path(filename).extension().string();
            const auto manualFile = outputDir / (stem + "_manual" + ext);
            if (std::filesystem::exists(manualFile)) {
                std::ifstream manualStream(manualFile);
                nlohmann::json manual = nlohmann::json::object();
                try {
                    manualStream >> manual;
                    int applied = 0;
                    for (auto& [presetName, manualData] : manual.items()) {
                        std::string err;
                        if (!IsValidPresetEntry(manualData, err)) {
                            spdlog::warn("[BodyLanguage] Manual override '{}' in '{}' skipped — invalid structure: {}",
                                         presetName, manualFile.filename().string(), err);
                            continue;
                        }
                    result[presetName] = manualData;
                        ++applied;
                    }
                    spdlog::info("[BodyLanguage] Applied {} manual override(s) from {}", applied, manualFile.filename().string());
                } catch (const std::exception& e) {
                    spdlog::warn("[BodyLanguage] Could not parse manual override file '{}': {}", manualFile.filename().string(), e.what());
                }
            }

            std::ofstream outStream(outputFile);
            if (!outStream) {
                spdlog::error("[BodyLanguage] Failed to open {} for writing", outputFile.string());
                continue;
            }

            outStream << result.dump(4) << '\n';
            spdlog::info("[BodyLanguage] Wrote {} preset(s) to {}", result.size(), outputFile.string());
        }
    }

}  // namespace BodyLanguage
