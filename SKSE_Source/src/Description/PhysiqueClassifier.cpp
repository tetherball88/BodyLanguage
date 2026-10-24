#include "PCH.h"

#include "Description/PhysiqueClassifier.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <mutex>

#include <spdlog/spdlog.h>

namespace BodyLanguage {

    namespace {

        nlohmann::json LoadJsonFile(const std::filesystem::path& path) {
            std::ifstream file(path);
            if (!file.is_open()) {
                spdlog::warn("[PhysiqueClassifier] Could not open JSON config: '{}'", path.string());
                return nlohmann::json::object();
            }
            try {
                nlohmann::json j;
                file >> j;
                return j;
            } catch (const std::exception& e) {
                spdlog::error("[PhysiqueClassifier] Failed to parse '{}': {}", path.string(), e.what());
                return nlohmann::json::object();
            }
        }

        float GetThreshold(
            const nlohmann::json& sexNode,
            std::string_view group,
            std::string_view key,
            float defaultVal) {
            if (!sexNode.is_object()) {
                return defaultVal;
            }
            const auto groupIt = sexNode.find(group);
            if (groupIt == sexNode.end() || !groupIt->is_object()) {
                return defaultVal;
            }
            const auto keyIt = groupIt->find(key);
            if (keyIt == groupIt->end() || !keyIt->is_number()) {
                return defaultVal;
            }
            return keyIt->get<float>();
        }

        std::string ResolvePartPhrase(
            const nlohmann::json& partsJson,
            std::string_view group,
            std::string_view keyword,
            std::string_view ctx) {
            if (!partsJson.is_object()) {
                return std::string(keyword);
            }
            const auto groupIt = partsJson.find(group);
            if (groupIt == partsJson.end() || !groupIt->is_object()) {
                return std::string(keyword);
            }
            const auto kwIt = groupIt->find(keyword);
            if (kwIt == groupIt->end() || !kwIt->is_object()) {
                return std::string(keyword);
            }
            const auto ctxIt = kwIt->find(ctx);
            if (ctxIt == kwIt->end() || !ctxIt->is_string()) {
                return std::string(keyword);
            }
            return ctxIt->get<std::string>();
        }

        std::unordered_map<std::string, std::string> ClassifyFemaleRegions(
            const ActorBodyMetrics& m,
            float toneIndex,
            const nlohmann::json& femThresh) {
            std::unordered_map<std::string, std::string> kw;

            const float massVeryLow = GetThreshold(femThresh, "body_mass_index", "very_low_max", 0.40f);
            const float massMedium = GetThreshold(femThresh, "body_mass_index", "medium_max", 0.48f);
            const float massHigh = GetThreshold(femThresh, "body_mass_index", "high_max", 0.56f);

            const float toneLow = GetThreshold(femThresh, "tone_index", "low_max", -0.15f);
            const float toneNeutral = GetThreshold(femThresh, "tone_index", "neutral_max", 0.25f);
            const float toneRipped = GetThreshold(femThresh, "tone_index", "ripped_min", 0.55f);

            // 1. Frame (Shoulder width-to-height ratio W_1 / H_0)
            const float shldrSmall = GetThreshold(femThresh, "shoulder_breadth", "small_max", 0.29f);
            const float shldrSlim = GetThreshold(femThresh, "shoulder_breadth", "slim_max", 0.31f);
            const float shldrAvg = GetThreshold(femThresh, "shoulder_breadth", "average_max", 0.34f);
            const float shldrBroad = GetThreshold(femThresh, "shoulder_breadth", "broad_max", 0.37f);
            const float gluteAvg = GetThreshold(femThresh, "glute_girth", "average_max", 0.73f);

            if (m.shoulderToHeight < shldrSmall) {
                kw["frame"] = "small";
            } else if (m.shoulderToHeight < shldrSlim) {
                kw["frame"] = (m.hipsToHeight >= gluteAvg || m.bodyMassIndex >= 0.45f) ? "average" : "slim";
            } else if (m.shoulderToHeight < shldrAvg) {
                kw["frame"] = "average";
            } else if (m.shoulderToHeight < shldrBroad) {
                kw["frame"] = "broad";
            } else {
                kw["frame"] = "massive";
            }

            // 2. Breasts volume (Ribcage-invariant anterior breast projection ratio)
            const float bustFlat = GetThreshold(femThresh, "chest_prominence", "flat_max", 1.06f);
            const float bustSmall = GetThreshold(femThresh, "chest_prominence", "small_max", 1.13f);
            const float bustAvg = GetThreshold(femThresh, "chest_prominence", "average_max", 1.20f);
            const float bustLarge = GetThreshold(femThresh, "chest_prominence", "large_max", 1.30f);
            const float bustHuge = GetThreshold(femThresh, "chest_prominence", "huge_max", 1.42f);

            if (m.bustToUnderbust < bustFlat) {
                kw["breasts_volume"] = "flat";
            } else if (m.bustToUnderbust < bustSmall) {
                kw["breasts_volume"] = "small";
            } else if (m.bustToUnderbust < bustAvg) {
                kw["breasts_volume"] = "average";
            } else if (m.bustToUnderbust < bustLarge) {
                kw["breasts_volume"] = "large";
            } else if (m.bustToUnderbust < bustHuge) {
                kw["breasts_volume"] = "huge";
            } else {
                kw["breasts_volume"] = "impossible";
            }

            // 3. Waist (Anatomical Waist-to-Hip Ratio WHR = G_4 / G_6)
            const float whrTiny = GetThreshold(femThresh, "waist_to_hip", "tiny_max", 0.61f);
            const float whrSlim = GetThreshold(femThresh, "waist_to_hip", "slim_max", 0.67f);
            const float whrStd = GetThreshold(femThresh, "waist_to_hip", "standard_max", 0.73f);
            const float whrSoft = GetThreshold(femThresh, "waist_to_hip", "soft_max", 0.79f);

            if (m.waistToHipRatio < whrTiny) {
                kw["waist"] = "tiny";
            } else if (m.waistToHipRatio < whrSlim) {
                kw["waist"] = "slim";
            } else if (m.waistToHipRatio < whrStd) {
                kw["waist"] = "standard";
            } else if (m.waistToHipRatio < whrSoft) {
                kw["waist"] = "soft";
            } else {
                kw["waist"] = "thick";
            }

            // 4. Belly (Section 3.5: Check isolated pregnancyBellyScore first)
            const float pregEarly = GetThreshold(femThresh, "pregnancy_score", "early_min", 0.25f);
            const float pregMid = GetThreshold(femThresh, "pregnancy_score", "mid_min", 0.50f);
            const float pregFull = GetThreshold(femThresh, "pregnancy_score", "full_min", 0.75f);

            if (m.pregnancyBellyScore >= pregFull) {
                kw["belly"] = "pregnant_full";
            } else if (m.pregnancyBellyScore >= pregMid) {
                kw["belly"] = "pregnant_mid";
            } else if (m.pregnancyBellyScore >= pregEarly) {
                kw["belly"] = "pregnant_early";
            } else {
                const float dropFlat = GetThreshold(femThresh, "belly_drop", "flat_min", -0.006f);
                const float dropSoft = GetThreshold(femThresh, "belly_drop", "soft_min", -0.022f);
                const float dropChubby = GetThreshold(femThresh, "belly_drop", "chubby_min", -0.045f);
                const float dropLarge = GetThreshold(femThresh, "belly_drop", "large_min", -0.075f);

                if (m.sagittalBellyDrop >= dropFlat) {
                    if (toneIndex >= toneRipped) {
                        kw["belly"] = "ripped";
                    } else if (toneIndex > toneNeutral) {
                        kw["belly"] = "toned";
                    } else if (toneIndex < toneLow) {
                        kw["belly"] = "soft";
                    } else {
                        kw["belly"] = "flat";
                    }
                } else if (m.sagittalBellyDrop >= dropSoft) {
                    kw["belly"] = (toneIndex > toneNeutral) ? "toned" : "soft";
                } else if (m.sagittalBellyDrop >= dropChubby) {
                    kw["belly"] = "chubby";
                } else if (m.sagittalBellyDrop >= dropLarge) {
                    kw["belly"] = "large";
                } else {
                    kw["belly"] = "huge";
                }
            }

            // 5. Butt & Hips (Hip circumference-to-height ratio G_6 / H_0, tone-aware for high volume)
            const float gluteFlat = GetThreshold(femThresh, "glute_girth", "flat_max", 0.60f);
            const float gluteSmall = GetThreshold(femThresh, "glute_girth", "small_max", 0.66f);
            const float gluteLarge = GetThreshold(femThresh, "glute_girth", "large_max", 0.82f);

            if (m.hipsToHeight < gluteFlat) {
                kw["butt_hips"] = "flat";
            } else if (m.hipsToHeight < gluteSmall) {
                kw["butt_hips"] = "small";
            } else if (m.hipsToHeight < gluteAvg) {
                kw["butt_hips"] = "average";
            } else if (m.hipsToHeight < gluteLarge) {
                kw["butt_hips"] = (toneIndex >= toneRipped) ? "large_muscular" : "large";
            } else {
                kw["butt_hips"] = (toneIndex >= toneRipped) ? "huge_muscular" : "huge";
            }

            // 6. Legs (Upper Thigh circumference-to-height ratio G_7 / H_0, tone-aware for high volume)
            const float thighThin = GetThreshold(femThresh, "thigh_girth", "thin_max", 0.33f);
            const float thighLean = GetThreshold(femThresh, "thigh_girth", "lean_max", 0.37f);
            const float thighAvg = GetThreshold(femThresh, "thigh_girth", "average_max", 0.42f);
            const float thighThick = GetThreshold(femThresh, "thigh_girth", "thick_max", 0.48f);

            if (m.upperThighGirth < thighThin) {
                kw["legs"] = "thin";
            } else if (m.upperThighGirth < thighLean) {
                kw["legs"] = "lean";
            } else if (m.upperThighGirth < thighAvg) {
                kw["legs"] = (toneIndex > toneNeutral) ? "lean" : "average";
            } else if (m.upperThighGirth < thighThick) {
                kw["legs"] = (toneIndex >= toneRipped) ? "muscular" : "thick";
            } else {
                kw["legs"] = (toneIndex >= toneRipped) ? "colossal_muscular" : "colossal";
            }

            // 7. Arms (Upper Arm circumference-to-height ratio G_9 / H_0)
            const float armThin = GetThreshold(femThresh, "arm_girth", "thin_max", 0.18f);
            const float armMed = GetThreshold(femThresh, "arm_girth", "medium_max", 0.22f);
            const float armThick = GetThreshold(femThresh, "arm_girth", "thick_max", 0.25f);

            if (m.upperArmGirth < armThin) {
                kw["arms"] = (toneIndex > toneNeutral) ? "toned" : "thin";
            } else if (m.upperArmGirth < armMed) {
                if (toneIndex >= toneRipped) {
                    kw["arms"] = "muscular";
                } else if (toneIndex > toneNeutral) {
                    kw["arms"] = "toned";
                } else if (toneIndex < toneLow) {
                    kw["arms"] = "soft";
                } else {
                    kw["arms"] = "average";
                }
            } else if (m.upperArmGirth < armThick) {
                kw["arms"] = (toneIndex >= toneNeutral) ? "muscular" : "thick";
            } else {
                kw["arms"] = (toneIndex >= toneNeutral) ? "colossal" : "thick";
            }

            // 8. Overall Physique Condition (Archetype Matrix: Body Mass x Tone Index x WHR)
            const bool isHourglass =
                (m.waistToHipRatio <= whrStd) &&
                (m.hipsToHeight >= gluteAvg || m.bustToUnderbust >= bustAvg);
            const bool isHeavyShouldered =
                (m.shoulderToHeight >= shldrAvg || m.upperArmGirth >= armThick);

            if (m.bodyMassIndex < massVeryLow) {
                if (toneIndex < toneLow) {
                    kw["physique_condition"] = "emaciated";
                } else if (toneIndex <= toneNeutral) {
                    kw["physique_condition"] = "slender";
                } else {
                    kw["physique_condition"] = "wiry";
                }
            } else if (m.bodyMassIndex < massMedium) {
                if (toneIndex < toneLow) {
                    kw["physique_condition"] = isHourglass ? "curvy" : "soft";
                } else if (toneIndex <= toneNeutral) {
                    kw["physique_condition"] = (m.waistToHipRatio < whrSlim && !isHourglass) ? "lean" : (isHourglass ? "curvy" : "average");
                } else if (toneIndex < toneRipped) {
                    kw["physique_condition"] = (isHourglass && m.hipsToHeight >= gluteAvg) ? "curvy" : ((m.upperThighGirth < thighLean) ? "lean" : "athletic");
                } else if (toneIndex >= toneRipped) {
                    kw["physique_condition"] = "shredded";
                } else {
                    kw["physique_condition"] = "athletic";
                }
            } else if (m.bodyMassIndex < massHigh) {
                if (toneIndex < toneLow) {
                    kw["physique_condition"] = isHourglass ? "curvy" : ((m.waistToHipRatio < whrSoft) ? "plump" : "chubby");
                } else if (toneIndex <= toneNeutral) {
                    kw["physique_condition"] = isHourglass ? "curvy" : "sturdy";
                } else if (toneIndex >= toneRipped) {
                    kw["physique_condition"] = "shredded";
                } else {
                    kw["physique_condition"] = "muscular";
                }
            } else {
                if (toneIndex < toneLow) {
                    kw["physique_condition"] = (m.waistToHipRatio < whrSoft) ? "plump" : "obese";
                } else if (toneIndex <= toneNeutral) {
                    kw["physique_condition"] = isHeavyShouldered ? "hulking" : "plump";
                } else {
                    kw["physique_condition"] = isHeavyShouldered ? "amazonian" : "muscular";
                }
            }

            return kw;
        }

        std::unordered_map<std::string, std::string> ClassifyMaleRegions(
            const ActorBodyMetrics& m,
            float toneIndex,
            float texToneScore,
            const nlohmann::json& maleThresh) {
            std::unordered_map<std::string, std::string> kw;

            const float massVeryLow = GetThreshold(maleThresh, "body_mass_index", "very_low_max", 0.43f);
            const float massCompact = GetThreshold(maleThresh, "body_mass_index", "compact_max", 0.47f);
            const float massMedium = GetThreshold(maleThresh, "body_mass_index", "medium_max", 0.52f);
            const float massHigh = GetThreshold(maleThresh, "body_mass_index", "high_max", 0.57f);

            const float toneLow = GetThreshold(maleThresh, "tone_index", "low_max", -0.15f);
            const float toneNeutral = GetThreshold(maleThresh, "tone_index", "neutral_max", 0.25f);
            const float toneRipped = GetThreshold(maleThresh, "tone_index", "ripped_min", 0.55f);

            // 1. Stature Frame (Systemic bodyMassIndex)
            if (m.bodyMassIndex < massVeryLow) {
                kw["stature_frame"] = "slight";
            } else if (m.bodyMassIndex < massCompact) {
                kw["stature_frame"] = "compact";
            } else if (m.bodyMassIndex < massMedium) {
                kw["stature_frame"] = "average";
            } else if (m.bodyMassIndex < massHigh) {
                kw["stature_frame"] = "heavy";
            } else {
                kw["stature_frame"] = "massive";
            }

            // 2. Shoulder Breadth (Shoulder width-to-height ratio W_1 / H_0)
            const float shldrNarrow = GetThreshold(maleThresh, "shoulder_breadth", "narrow_max", 0.34f);
            const float shldrAvg = GetThreshold(maleThresh, "shoulder_breadth", "average_max", 0.37f);
            const float shldrBroad = GetThreshold(maleThresh, "shoulder_breadth", "broad_max", 0.40f);

            if (m.shoulderToHeight < shldrNarrow) {
                kw["shoulder_breadth"] = "narrow";
            } else if (m.shoulderToHeight < shldrAvg) {
                kw["shoulder_breadth"] = "average";
            } else if (m.shoulderToHeight < shldrBroad) {
                kw["shoulder_breadth"] = "broad";
            } else {
                kw["shoulder_breadth"] = "expansive";
            }

            // 3. Chest (Chest-to-Underbust circumference ratio G_2 / G_3 combined with tone)
            const float chestFlat = GetThreshold(maleThresh, "chest_prominence", "flat_max", 1.02f);
            const float chestAvg = GetThreshold(maleThresh, "chest_prominence", "average_max", 1.09f);
            const float chestBroad = GetThreshold(maleThresh, "chest_prominence", "broad_max", 1.15f);

            if (m.bustToUnderbust < chestFlat) {
                kw["chest"] = (m.bodyMassIndex >= massMedium) ? ((toneIndex < toneLow) ? "heavyset" : "average") : "flat";
            } else if (m.bustToUnderbust < chestAvg) {
                kw["chest"] = (toneIndex > toneRipped) ? "sculpted" : "average";
            } else if (m.bustToUnderbust < chestBroad) {
                if (toneIndex > toneNeutral) {
                    kw["chest"] = "sculpted";
                } else if (toneIndex < toneLow) {
                    kw["chest"] = "heavyset";
                } else {
                    kw["chest"] = "broad";
                }
            } else {
                kw["chest"] = (toneIndex < toneLow) ? "heavyset" : "sculpted";
            }

            // 4. Midsection (Anatomical Waist-to-Hip Ratio WHR = G_4 / G_6, sagittalBellyDrop, and waist girth)
            const float distendedMin = GetThreshold(maleThresh, "pregnancy_score", "distended_min", 0.35f);
            const float whrTapered = GetThreshold(maleThresh, "waist_to_hip", "tapered_max", 0.82f);
            const float whrStraight = GetThreshold(maleThresh, "waist_to_hip", "straight_max", 0.90f);
            const float whrThick = GetThreshold(maleThresh, "waist_to_hip", "thick_max", 0.96f);
            const float dropFlat = GetThreshold(maleThresh, "belly_drop", "flat_min", -0.006f);
            const float dropStraight = GetThreshold(maleThresh, "belly_drop", "straight_min", -0.022f);
            const float dropThick = GetThreshold(maleThresh, "belly_drop", "thick_min", -0.045f);
            const float waistGirth = m.rings[static_cast<std::size_t>(AnatomicalRingId::kNaturalWaist)].girth;

            if (m.pregnancyBellyScore >= distendedMin) {
                kw["midsection"] = "distended";
            } else if (m.waistToHipRatio > whrThick || m.sagittalBellyDrop < dropThick ||
                       (m.sagittalBellyDrop < dropStraight && toneIndex <= toneNeutral)) {
                kw["midsection"] = "potbellied";
            } else if (m.waistToHipRatio > whrStraight || m.sagittalBellyDrop < dropStraight || waistGirth >= 0.58f) {
                kw["midsection"] = "thick";
            } else if (toneIndex >= toneRipped && m.sagittalBellyDrop >= dropFlat) {
                kw["midsection"] = "ripped";
            } else if ((m.waistToHipRatio < whrTapered || toneIndex > toneNeutral) && m.sagittalBellyDrop >= dropStraight) {
                kw["midsection"] = "tapered";
            } else {
                kw["midsection"] = "straight";
            }

            // 5. Glutes (Hip circumference-to-height ratio G_6 / H_0, tone-aware for huge tier)
            const float gluteFlat = GetThreshold(maleThresh, "glute_girth", "flat_max", 0.58f);
            const float gluteFirm = GetThreshold(maleThresh, "glute_girth", "firm_max", 0.68f);
            const float gluteLarge = GetThreshold(maleThresh, "glute_girth", "large_max", 0.74f);

            if (m.hipsToHeight < gluteFlat) {
                kw["glutes"] = "flat";
            } else if (m.hipsToHeight < gluteFirm) {
                kw["glutes"] = "firm";
            } else if (m.hipsToHeight < gluteLarge) {
                kw["glutes"] = "large";
            } else {
                kw["glutes"] = (toneIndex < toneLow) ? "huge" : "huge_muscular";
            }

            // 6. Legs (Upper Thigh circumference-to-height ratio G_7 / H_0, tone-aware for immense tier)
            const float thighSpindly = GetThreshold(maleThresh, "thigh_girth", "spindly_max", 0.35f);
            const float thighAthletic = GetThreshold(maleThresh, "thigh_girth", "athletic_max", 0.41f);
            const float thighPillars = GetThreshold(maleThresh, "thigh_girth", "pillars_max", 0.47f);

            if (m.upperThighGirth < thighSpindly) {
                kw["legs"] = "spindly";
            } else if (m.upperThighGirth < thighAthletic) {
                kw["legs"] = "athletic";
            } else if (m.upperThighGirth < thighPillars) {
                kw["legs"] = "pillars";
            } else {
                kw["legs"] = (toneIndex < toneLow) ? "immense_soft" : "immense";
            }

            // 7. Arms (Upper Arm circumference-to-height ratio G_9 / H_0)
            const float armThin = GetThreshold(maleThresh, "arm_girth", "thin_max", 0.20f);
            const float armAthletic = GetThreshold(maleThresh, "arm_girth", "athletic_max", 0.25f);
            const float armBrawny = GetThreshold(maleThresh, "arm_girth", "brawny_max", 0.30f);

            if (m.upperArmGirth < armThin) {
                kw["arms"] = "thin";
            } else if (m.upperArmGirth < armAthletic) {
                kw["arms"] = "athletic";
            } else if (m.upperArmGirth < armBrawny) {
                kw["arms"] = "brawny";
            } else {
                kw["arms"] = "colossal";
            }

            // 8. Overall Physique Condition (Archetype Matrix: Body Mass x Tone Index)
            if (m.bodyMassIndex < massVeryLow) {
                if (toneIndex < toneLow) {
                    kw["physique_condition"] = "emaciated";
                } else if (toneIndex <= toneNeutral) {
                    kw["physique_condition"] = "slim";
                } else {
                    kw["physique_condition"] = "wiry";
                }
            } else if (m.bodyMassIndex < massMedium) {
                if (toneIndex < toneLow) {
                    kw["physique_condition"] = "soft";
                } else if (toneIndex <= toneNeutral) {
                    kw["physique_condition"] = "average";
                } else if (toneIndex >= toneRipped) {
                    kw["physique_condition"] = "shredded";
                } else {
                    kw["physique_condition"] = "athletic";
                }
            } else if (m.bodyMassIndex < massHigh) {
                if (texToneScore >= 0.45f && m.geoToneSignal < 0.0f) {
                    kw["physique_condition"] = "strongman";
                } else if (toneIndex < toneLow) {
                    kw["physique_condition"] = (m.sagittalBellyDrop < -0.025f) ? "portly" : "heavy";
                } else if (toneIndex <= toneNeutral) {
                    kw["physique_condition"] = "solid";
                } else if (toneIndex >= toneRipped) {
                    kw["physique_condition"] = "shredded";
                } else {
                    kw["physique_condition"] = "muscular";
                }
            } else {
                if (texToneScore >= 0.45f && m.geoToneSignal < 0.0f) {
                    kw["physique_condition"] = "strongman";
                } else if (toneIndex < toneLow) {
                    kw["physique_condition"] = "obese";
                } else if (toneIndex <= toneNeutral) {
                    kw["physique_condition"] = "hulking";
                } else {
                    kw["physique_condition"] = "herculean";
                }
            }

            return kw;
        }

        std::string BuildFemaleSentence(
            const std::unordered_map<std::string, std::string>& kw,
            const nlohmann::json& parts,
            std::string_view ctx,
            std::string_view mode) {
            auto get = [&](std::string_view group) -> std::string {
                const auto it = kw.find(std::string(group));
                return it != kw.end() ? ResolvePartPhrase(parts, group, it->second, ctx) : "";
            };

            const auto bellyIt = kw.find("belly");
            const std::string bellyKey = (bellyIt != kw.end()) ? bellyIt->second : "flat";
            const bool isPregnant = bellyKey.starts_with("pregnant_");
            const bool hasNotableBelly = isPregnant || (bellyKey != "flat");

            if (mode == "short") {
                if (isPregnant) {
                    return get("frame") + ", with " + get("breasts_volume") + " and " +
                           ((ctx == "naked") ? ("her belly " + get("belly")) : get("belly")) + " — overall " + get("physique_condition");
                }
                return get("frame") + ", with " + get("breasts_volume") + " and " +
                       get("butt_hips") + " — overall " + get("physique_condition");
            }

            if (ctx == "clothed") {
                const std::string midsectionClause = hasNotableBelly
                    ? (get("waist") + " and " + get("belly"))
                    : get("waist");
                return get("frame") + ", with " + get("breasts_volume") + " and " + get("arms") +
                       ". Her midsection shows " + midsectionClause +
                       ", her lower body broadening into " + get("butt_hips") + ". " +
                       get("legs") + " complete a silhouette that reads as " +
                       get("physique_condition");
            }

            return get("frame") + ", carrying " + get("breasts_volume") + " and " + get("arms") +
                   ". Her waist is " + get("waist") + ", her belly " + get("belly") +
                   ". Below, she has " + get("butt_hips") + " and " + get("legs") +
                   ", altogether " + get("physique_condition");
        }

        std::string BuildMaleSentence(
            const std::unordered_map<std::string, std::string>& kw,
            const nlohmann::json& parts,
            std::string_view ctx,
            std::string_view mode) {
            auto get = [&](std::string_view group) -> std::string {
                const auto it = kw.find(std::string(group));
                return it != kw.end() ? ResolvePartPhrase(parts, group, it->second, ctx) : "";
            };

            if (mode == "short") {
                if (ctx == "clothed") {
                    return get("shoulder_breadth") + " framing " + get("stature_frame") +
                           ", with " + get("midsection") + " — overall " +
                           get("physique_condition");
                }
                return get("shoulder_breadth") + " and " + get("stature_frame") +
                       ", with " + get("arms") + " — overall " + get("physique_condition");
            }

            if (ctx == "clothed") {
                return get("shoulder_breadth") + " framing " + get("stature_frame") +
                       " and " + get("chest") + ". Beneath his clothing, he has " +
                       get("midsection") + " and " + get("arms") +
                       ". His lower body shows " + get("legs") + " and " + get("glutes") +
                       ", the whole figure reading as " + get("physique_condition");
            }

            return get("shoulder_breadth") + " and " + get("stature_frame") +
                   ", fronted by " + get("chest") + " and defined by " + get("midsection") +
                   ". His build features " + get("arms") + ", " + get("legs") +
                   ", and " + get("glutes") + ", altogether " +
                   get("physique_condition");
        }

        std::string ToLowerAscii(std::string_view input) {
            std::string out(input);
            std::ranges::transform(out, out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        std::string ResolveGenitalAddonKey(
            const nlohmann::json& addonsJson,
            std::string_view addonName,
            std::string_view addonSourceFile) {
            constexpr std::string_view kDefaultKey = "$TNG_TRT";

            if (!addonsJson.is_object() || addonsJson.empty()) {
                return addonName.empty() ? std::string(kDefaultKey) : std::string(addonName);
            }

            // 1. Exact match on addonName
            if (!addonName.empty() && addonsJson.contains(addonName)) {
                return std::string(addonName);
            }

            const std::string lowerName = ToLowerAscii(addonName);
            const std::string lowerFile = ToLowerAscii(addonSourceFile);
            const std::string lowerStem = addonSourceFile.empty()
                ? std::string{}
                : ToLowerAscii(std::filesystem::path(addonSourceFile).stem().string());

            // 2. Case-insensitive exact match on addonName, plugin filename, or plugin stem
            for (const auto& [key, val] : addonsJson.items()) {
                if (!val.is_string()) {
                    continue;
                }
                const std::string lowerKey = ToLowerAscii(key);
                if ((!lowerName.empty() && lowerKey == lowerName) ||
                    (!lowerFile.empty() && lowerKey == lowerFile) ||
                    (!lowerStem.empty() && lowerKey == lowerStem)) {
                    return key;
                }
            }

            // 3. Substring match on addonName or plugin stem (skipping short internal $TNG_* keys)
            for (const auto& [key, val] : addonsJson.items()) {
                if (!val.is_string() || key.starts_with("$")) {
                    continue;
                }
                const std::string lowerKey = ToLowerAscii(key);
                if (lowerKey.size() < 4) {
                    continue;
                }
                if ((!lowerName.empty() && (lowerName.find(lowerKey) != std::string::npos ||
                                            lowerKey.find(lowerName) != std::string::npos)) ||
                    (!lowerStem.empty() && (lowerStem.find(lowerKey) != std::string::npos ||
                                            lowerKey.find(lowerStem) != std::string::npos))) {
                    return key;
                }
            }

            return std::string(kDefaultKey);
        }

    }  // namespace

    PhysiqueClassifier& PhysiqueClassifier::GetInstance() {
        static PhysiqueClassifier instance;
        return instance;
    }

    void PhysiqueClassifier::LoadConfigs(const std::filesystem::path& dataRoot) {
        const auto baseDir = dataRoot / "SKSE" / "Plugins" / "BodyLanguage";
        const auto configsDir = baseDir / "bodyConfigs";
        const auto skyrimNetJsonDir = dataRoot / "SKSE" / "Plugins" / "SkyrimNet" / "jsonData";

        auto thresholds = LoadJsonFile(baseDir / "geometryThresholds.json");
        auto femaleParts = LoadJsonFile(configsDir / "FemaleParts.json");
        auto maleParts = LoadJsonFile(configsDir / "MaleParts.json");
        auto genitalAddons = LoadJsonFile(skyrimNetJsonDir / "tngAddons.json");

        std::unique_lock lock(m_configMutex);
        m_thresholds = std::move(thresholds);
        m_femaleParts = std::move(femaleParts);
        m_maleParts = std::move(maleParts);
        m_genitalAddons = std::move(genitalAddons);

        spdlog::info(
            "[PhysiqueClassifier] Loaded geometryThresholds.json (empty={}), FemaleParts.json (empty={}), MaleParts.json (empty={}), tngAddons.json (empty={})",
            m_thresholds.empty(),
            m_femaleParts.empty(),
            m_maleParts.empty(),
            m_genitalAddons.empty());
    }

    float PhysiqueClassifier::ComputeToneIndex(
        float geoToneSignal,
        float texToneScore) {
        // Normal maps (_msn.dds) carry the primary visual muscle definition in Skyrim (abs, obliques, deltoids),
        // so texture roughness contributes 65% and 3D silhouette taper/flatness contributes 35%.
        const float combined =
            0.35f * geoToneSignal +
            0.65f * (2.0f * texToneScore - 0.5f);
        return std::clamp(combined, -1.0f, 1.0f);
    }

    std::unordered_map<std::string, std::string> PhysiqueClassifier::ClassifyRegions(
        const ActorBodyMetrics& metrics,
        float texToneScore) const {
        const float toneIndex = ComputeToneIndex(metrics.geoToneSignal, texToneScore);
        std::shared_lock lock(m_configMutex);
        const nlohmann::json emptyObj = nlohmann::json::object();
        if (metrics.isFemale) {
            const auto& femThresh =
                (m_thresholds.is_object() && m_thresholds.contains("female"))
                    ? m_thresholds["female"]
                    : emptyObj;
            return ClassifyFemaleRegions(metrics, toneIndex, femThresh);
        }
        const auto& maleThresh =
            (m_thresholds.is_object() && m_thresholds.contains("male"))
                ? m_thresholds["male"]
                : emptyObj;
        return ClassifyMaleRegions(metrics, toneIndex, texToneScore, maleThresh);
    }

    ActorDescriptionPair PhysiqueClassifier::BuildBothDescriptions(
        const ActorBodyMetrics& metrics,
        float texToneScore) const {
        ActorDescriptionPair pair{};
        if (!metrics.valid) {
            return pair;
        }

        const float toneIndex = ComputeToneIndex(
            metrics.geoToneSignal,
            texToneScore);
        const std::string_view ctx = metrics.isNaked ? "naked" : "clothed";

        std::shared_lock lock(m_configMutex);
        if (metrics.isFemale) {
            const nlohmann::json emptyObj = nlohmann::json::object();
            const auto& femThresh =
                (m_thresholds.is_object() && m_thresholds.contains("female"))
                    ? m_thresholds["female"]
                    : emptyObj;
            const auto kw = ClassifyFemaleRegions(metrics, toneIndex, femThresh);
            spdlog::info(
                "[PhysiqueClassifier] Female classification (I_tone={:.3f}, ctx='{}'): "
                "frame='{}' breasts='{}' waist='{}' belly='{}' butt_hips='{}' legs='{}' arms='{}' condition='{}'",
                toneIndex,
                ctx,
                kw.at("frame"),
                kw.at("breasts_volume"),
                kw.at("waist"),
                kw.at("belly"),
                kw.at("butt_hips"),
                kw.at("legs"),
                kw.at("arms"),
                kw.at("physique_condition"));
            pair.full = BuildFemaleSentence(kw, m_femaleParts, ctx, "full");
            pair.shortDesc = BuildFemaleSentence(kw, m_femaleParts, ctx, "short");
        } else {
            const nlohmann::json emptyObj = nlohmann::json::object();
            const auto& maleThresh =
                (m_thresholds.is_object() && m_thresholds.contains("male"))
                    ? m_thresholds["male"]
                    : emptyObj;
            const auto kw = ClassifyMaleRegions(metrics, toneIndex, texToneScore, maleThresh);
            spdlog::info(
                "[PhysiqueClassifier] Male classification (I_tone={:.3f}, ctx='{}'): "
                "stature='{}' shoulders='{}' chest='{}' midsection='{}' glutes='{}' legs='{}' arms='{}' condition='{}'",
                toneIndex,
                ctx,
                kw.at("stature_frame"),
                kw.at("shoulder_breadth"),
                kw.at("chest"),
                kw.at("midsection"),
                kw.at("glutes"),
                kw.at("legs"),
                kw.at("arms"),
                kw.at("physique_condition"));
            pair.full = BuildMaleSentence(kw, m_maleParts, ctx, "full");
            pair.shortDesc = BuildMaleSentence(kw, m_maleParts, ctx, "short");
        }

        return pair;
    }

    std::string PhysiqueClassifier::BuildDescription(
        const ActorBodyMetrics& metrics,
        float texToneScore,
        std::string_view mode) const {
        const auto pair = BuildBothDescriptions(metrics, texToneScore);
        return (mode == "short") ? pair.shortDesc : pair.full;
    }

    std::string PhysiqueClassifier::BuildGenitalsJson(
        const ActorGenitalMetrics& genitalMetrics) const {
        if (!genitalMetrics.hasGenitals) {
            return "{\"hasGenitals\":false}";
        }

        std::shared_lock lock(m_configMutex);
        const nlohmann::json emptyObj = nlohmann::json::object();
        const auto& maleThresh =
            (m_thresholds.is_object() && m_thresholds.contains("male"))
                ? m_thresholds["male"]
                : emptyObj;

        const float xsMax = GetThreshold(maleThresh, "genital_size", "xs_max", 0.79f);
        const float sMax = GetThreshold(maleThresh, "genital_size", "s_max", 0.99f);
        const float mMax = GetThreshold(maleThresh, "genital_size", "m_max", 1.19f);
        const float lMax = GetThreshold(maleThresh, "genital_size", "l_max", 1.39f);

        std::string_view sizeTier = "M";
        if (genitalMetrics.sizeMultiplier < xsMax) {
            sizeTier = "XS";
        } else if (genitalMetrics.sizeMultiplier < sMax) {
            sizeTier = "S";
        } else if (genitalMetrics.sizeMultiplier < mMax) {
            sizeTier = "M";
        } else if (genitalMetrics.sizeMultiplier < lMax) {
            sizeTier = "L";
        } else {
            sizeTier = "XL";
        }

        const std::string addonKey = ResolveGenitalAddonKey(
            m_genitalAddons,
            genitalMetrics.addonName,
            genitalMetrics.addonSourceFile);

        spdlog::info(
            "[PhysiqueClassifier] Genital classification: size='{}' (mult={:.2f}) addon='{}' (raw='{}')",
            sizeTier,
            genitalMetrics.sizeMultiplier,
            addonKey,
            genitalMetrics.addonName);

        const nlohmann::json out = {
            {"hasGenitals", true},
            {"size", sizeTier},
            {"addon", addonKey}
        };
        return out.dump();
    }

}  // namespace BodyLanguage

