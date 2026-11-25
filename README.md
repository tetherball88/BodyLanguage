# TT Body Language 🎭

✨ A standalone Skyrim SKSE plugin and modder framework (with optional **SkyrimNet** integration) that evaluates every NPC's live physical dimensions and generates context-aware body descriptions using **real-time 3D geometry and normal-map analysis**.

Instead of relying on static BodySlide preset databases or external archive scans, **BodyLanguage v2.0** inspects each NPC's actual 3D body mesh (`.nif`), active `.tri` morph sliders via RaceMenu (`skee64.dll`), live XPMSE skeleton bone scales, object-space normal maps (`_msn.dds`), SPID skin keywords, height, and *The New Gentleman* (TNG) anatomy directly through Skyrim's Virtual File System.

> 💡 **Standalone or SkyrimNet:** When **SkyrimNet** is installed, BodyLanguage automatically registers its Jinja decorators. When SkyrimNet is *not* installed, all SkyrimNet integration is skipped with zero overhead, and BodyLanguage serves as a standalone C++ & Papyrus body-analysis library for any Skyrim mod.

---

## Requirements 📋

- [SKSE64](https://skse.silverlock.org/)
- [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) *(optional — v0.25.0 or later; required only for LLM prompt decorators)*
- [RaceMenu](https://www.nexusmods.com/skyrimspecialedition/mods/19080) *(optional, required only if you use runtime `.tri` body morphs)*
- [XP32 Maximum Skeleton Special Extended (XPMSE)](https://www.nexusmods.com/skyrimspecialedition/mods/1988) *(optional, required only if you use bone-scale morphs on Breast/Belly/Butt nodes)*
- [Racial Skin Variance - SPID (RSV)](https://www.nexusmods.com/skyrimspecialedition/mods/81668) & [BnP - RSV Patch](https://www.nexusmods.com/skyrimspecialedition/mods/102667) *(optional — normal-map muscularity and SPID skin complexion features are detected automatically)*
- [The New Gentleman (TNG)](https://www.nexusmods.com/skyrimspecialedition/mods/104215) or **Schlongs of Skyrim (SOS)** *(optional — adds unclothed size and addon anatomy descriptions natively via C++)*

---

## Key Features ✨

- 📐 **Universal 3D Mesh & Morph Analysis** — Slices any humanoid `_0.nif` and `_1.nif` into **10 anatomical cross-section rings (80 landmark vertices)** and reconstructs each actor's live body shape at runtime from their weight, active `SKEE` `.tri` morph sliders, and XPMSE bone scales.
- 👗 **Body-Replacer Agnostic** — Works seamlessly with **CBBE 3BA**, **BHUNP**, **UNP**, **TBD**, **UBE**, **HIMBO**, **SAM**, **COtR**, custom standalone follower bodies, and vanilla meshes.
- 🤰 **Pregnancy vs. Systemic Build Isolation** — Distinguishes localized anterior belly distension (pregnancy morphs or `NPC Belly` node scaling) from systemic body mass so pregnant slender or athletic NPCs are accurately described as pregnant rather than overweight.
- 💪 **On-Demand Normal Map Muscularity (`_msn.dds`)** — Streams each NPC's active object-space normal map from the VFS, decodes the $256 \times 256$ mipmap via `DirectXTex`, and runs a 2D Laplacian filter over the abdomen and deltoid UV regions to measure muscle definition.
- 🎨 **RSV Skin Complexion Features** — Detects Racial Skin Variance SPID keywords (`RSVnord`, `RSVbreton`, `RSVimperial`, `RSVredguard`, `RSVhighelf`, `RSVdarkelf`, `RSVwoodelf`, `RSVorc`, `RSVfantasymer`, `RSVvampirebeast`, `RSVdremora`, `RSVasian`) and injects clothed or naked complexion details (frostnip, freckles, scars, veins, tanlines, fantasy patterns, etc.).
- 🎯 **Context-Aware Prose** — Generates distinct descriptions for **clothed** vs. **naked** states and **full** (character bio / dialogue target) vs. **short** (inline reference) render modes.
- 📏 **Height Tiers** — Maps each actor's effective height multiplier to configurable height descriptors (`towering`, `tall`, `above_average`, `average`, `short`, `diminutive`).
- 🔞 **Native C++ TNG & SOS Integration** — Inspects equipped Slot 52 addons, XPMSE `NPC GenitalsBase [GenBase]` bone scales, TNG keywords, and SOS factions directly in C++ (no `.esp` or `StorageUtil` scripts needed).

---

## Modder Public API (Standalone C++ & Papyrus) 🧩

BodyLanguage exposes its full 3D body measurement and classification engine to other Skyrim mods — with **no SkyrimNet dependency**. All queries share a thread-safe 15-second per-actor cache and skip prose generation unless a text description is explicitly requested.

### C++ SKSE Plugin API (`BodyLanguageAPI.h`)
Drop [`SKSE_Source/include/BodyLanguageAPI.h`](./SKSE_Source/include/BodyLanguageAPI.h) into your SKSE plugin project. All exports use pure C ABI POD structs and caller-owned buffers (safe across `/MT` and `/MD` and different MSVC toolsets):

```cpp
#include "BodyLanguageAPI.h"

// Call once in kPostPostLoad or kDataLoaded:
if (BodyLanguage::API::Load()) {
    // Query full structured data (measurements in cm, ratios, part tiers, genitals, state):
    if (auto data = BodyLanguage::API::GetActorData(actor); data && data->valid) {
        float chestCm = data->measurements.chestCm;
        std::string_view breasts = data->parts.breasts;  // e.g. "large"
        std::string_view butt = data->parts.butt;        // e.g. "average"
        float whr = data->ratios.waistToHipRatio;
    }
    // Or fetch complete JSON / descriptions:
    std::string json = BodyLanguage::API::GetActorDataJson(actor);
}
```

### Papyrus Native API (`BodyLanguage.psc`)
Script [`Scripts/Source/BodyLanguage.psc`](./Scripts/Source/BodyLanguage.psc) provides global native functions callable from any Papyrus script:

```papyrus
If BodyLanguage.GetApiVersion() > 0
    String breasts = BodyLanguage.GetBreastSize(akActor)            ; "flat".."impossible"
    String butt    = BodyLanguage.GetButtSize(akActor)              ; "flat".."huge_muscular"
    String thighs  = BodyLanguage.GetThighSize(akActor)             ; "thin".."colossal_muscular"
    Float  waistCm = BodyLanguage.GetBodyMeasurementCm(akActor, "waist")
    Float  whr     = BodyLanguage.GetBodyRatio(akActor, "whr")
    Float  preg    = BodyLanguage.GetPregnancyScore(akActor)        ; 0.0..1.0
    String genital = BodyLanguage.GetGenitalSize(akActor)           ; "XS".."XXXL" (when naked)
    String fullJson = BodyLanguage.GetBodyDataJson(akActor)
EndIf
```

---

## How Descriptions Reach SkyrimNet 🤖

The plugin registers eight native C++ decorator functions with SkyrimNet's public API:

| Decorator | Returns |
| :--- | :--- |
| `ttlb_get_full_body_description(actor)` | Full multi-sentence body description (`clothed`, `naked`, or static beast shape) |
| `ttlb_get_short_body_description(actor)` | Concise single-phrase summary (`clothed`, `naked`, or static beast shape) |
| `ttlb_is_naked(actor)` | `"true"` or `"false"` based on live equipment slot inspection |
| `ttlb_get_beast_form(actor)` | `"werewolf"`, `"vampire_lord"`, `"werebear"`, or `""` if not in beast form |
| `ttlb_get_genitals(actor)` | JSON object (`{"hasGenitals": true, "size": "XS..XXXL", "state": "flaccid|hard", "addon": "<addonKey>"}`) for TNG or SOS |
| `ttlb_get_body_parts(actor)` | JSON object (`{"valid": bool, "isFemale": bool, "breasts": "...", "butt": "...", "thighs": "..."}`) with body-part classification tiers |
| `ttlb_is_excluded(actor)` | `"true"` or `"false"` based on `[Exclusions]` in `BodyLanguage.ini` |
| `ttlb_get_height(actor)` | JSON object (`{"height": float}`) with the actor's effective height scale |

SkyrimNet's character bio prompt [`0401_appearance_body_shape.prompt`](./SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/character_bio/0401_appearance_body_shape.prompt) renders the modular `body_language` subcomponents:
1. **[`0001_body.prompt`](./SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/body_language/0001_body.prompt)** — Clothed/naked status and 3D physique description.
2. **[`0005_skin_features.prompt`](./SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/body_language/0005_skin_features.prompt)** — RSV skin complexion details from [`skinFeatures.json`](./SKSE/Plugins/SkyrimNet/jsonData/skinFeatures.json).
3. **[`0010_height.prompt`](./SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/body_language/0010_height.prompt)** — Height tier phrase from [`heightRanges.json`](./SKSE/Plugins/SkyrimNet/jsonData/heightRanges.json).
4. **[`0020_genitals.prompt`](./SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/body_language/0020_genitals.prompt)** — Unclothed TNG / SOS anatomy description assembled from `0020_genitals.prompt` and [`tngAddons.json`](./SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json).

---

## Customization & Tuning 🛠️

`BodyLanguage` is 100% data-driven. All anthropometric ratio cutoffs, descriptive text snippets, normal-map roughness baselines, skin features, height ranges, and TNG addon descriptions live in plain-text JSON/INI files under `Data/SKSE/Plugins/`:

| Configuration File | Purpose |
| :--- | :--- |
| [`SKSE/Plugins/BodyLanguage/geometryThresholds.json`](./SKSE/Plugins/BodyLanguage/geometryThresholds.json) | Sex-specific 3D ratio thresholds (`shoulder_breadth`, `chest_prominence`, `waist_to_hip`, `glute_girth`, `thigh_girth`, `arm_girth`, `belly_drop`, `pregnancy_score`, `body_mass_index`, `tone_index`). |
| [`SKSE/Plugins/BodyLanguage/bodyConfigs/FemaleParts.json`](./SKSE/Plugins/BodyLanguage/bodyConfigs/FemaleParts.json) | Clothed and naked text phrases for every female body region and tier. |
| [`SKSE/Plugins/BodyLanguage/bodyConfigs/MaleParts.json`](./SKSE/Plugins/BodyLanguage/bodyConfigs/MaleParts.json) | Clothed and naked text phrases for every male body region and tier. |
| [`SKSE/Plugins/BodyLanguage/bodyConfigs/BeastParts.json`](./SKSE/Plugins/BodyLanguage/bodyConfigs/BeastParts.json) | Static full and short descriptions for Werewolf, Vampire Lord, and Werebear forms (`female` and `male`). |
| [`SKSE/Plugins/BodyLanguage.ini`](./SKSE/Plugins/BodyLanguage.ini) | `SmoothBaseline` and `RippedBaseline` calibration for normal-map (`_msn.dds`) muscularity detection. |
| [`SKSE/Plugins/SkyrimNet/jsonData/skinFeatures.json`](./SKSE/Plugins/SkyrimNet/jsonData/skinFeatures.json) | Clothed and naked complexion sentences for RSV racial/special skin keywords. |
| [`SKSE/Plugins/SkyrimNet/jsonData/heightRanges.json`](./SKSE/Plugins/SkyrimNet/jsonData/heightRanges.json) | Height multiplier cutoffs (`towering` down to `diminutive`). |
| [`SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json`](./SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json) | Anatomy descriptions for default and custom *The New Gentleman* addons. |

### Inspecting Actor Measurements in `BodyLanguage.log`
Whenever SkyrimNet requests a description for an NPC, `BodyLanguage` logs their exact real-world measurements (`cm` / `inches`), dimensionless ratios, normal-map roughness score, and resulting tier classifications to `Documents\My Games\Skyrim Special Edition\SKSE\BodyLanguage.log`:

```text
[ActorBodyEvaluator] Measurements 'Serana' (02002B74) [scale=1.03]: shoulderWidth=49.5cm(19.5") chest=93.9cm(36.9") underbust=79.2cm(31.2") waist=78.7cm(31.0") belly=101.4cm(39.9") hips=110.9cm(43.7") thigh=60.8cm(23.9") calf=42.3cm(16.6") arm=30.3cm(11.9")
[ActorBodyEvaluator] Evaluated 'Serana' (02002B74) [sex=F w=75 naked=false ...]: ratios=[mass=0.48 shldr=0.33 vtaper=1.89 whr=0.71 bustCup=1.18 hips=0.73 thigh=0.40 arm=0.20 bellyDrop=-0.002] S_geo_tone=-0.035 S_preg=0.085
[PhysiqueClassifier] Female classification (I_tone=0.069, ctx='clothed'): frame='average' breasts='large' waist='standard' belly='flat' butt_hips='large' legs='thick' arms='average' condition='curvy'
```

---

## Documentation 📖

- **[User & Modder Customization Guide (`USER_CUSTOMIZATION.md`)](./USER_CUSTOMIZATION.md)** — Complete step-by-step guide to tuning ratio thresholds, editing body part phrases, calibrating normal maps, and adding custom TNG addons.
- **[Architecture & Lifecycle Documentation (`ARCHITECTURE.md`)](./ARCHITECTURE.md)** — Technical deep-dive into the 10-ring 80-vertex slicer, `.tri` morph evaluation, Laplacian normal-map filter, and thread-safe caching pipeline.
- **[Changelog (`CHANGELOG.md`)](./CHANGELOG.md)** — Full version history and release notes.

---

## License & Permissions ⚖️

See [`LICENSE`](./LICENSE) for full details:

- **Mod Package, Prose Databases & SkyrimNet Prompts (`SKSE/`):** Copyright © 2026 `tetherball88`. **All Rights Reserved.**
  - **Forking & Contributions:** You are welcome to fork this repository on GitHub, submit pull requests, and publish compatibility/customization patches that require the original mod.
  - **No "As-Is" Redistribution:** Re-uploading, mirroring, or redistributing the complete mod package or its prose/prompt files "as-is" on Nexus Mods, third-party websites, or paid modlists without explicit permission is prohibited.
- **C++ SKSE Plugin Source Code (`SKSE_Source/`):** Licensed under the **GNU General Public License v3.0 (`GPL-3.0`)** with **Section 7 Additional Terms** (requiring modified versions to be clearly marked as unofficial and prohibiting redistribution under the name *"TT Body Language"* or *"tetherball88"* without permission).

