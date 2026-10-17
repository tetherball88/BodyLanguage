# Changelog

All notable changes to TT BodyLanguage will be documented in this file.

## v2.0.0

### Real-Time 3D Geometry & Normal-Map Analysis Engine

This release replaces the preset-based BodySlide XML scanner and pre-hashed RSV texture archive system with a **real-time, mod-agnostic 3D geometry and normal-map analysis engine**. Instead of relying on static preset lookup tables or external `.7z` archive scans, `BodyLanguage` now inspects each NPC's actual 3D body mesh (`.nif`), active `.tri` morph sliders via RaceMenu (`skee64.dll`), live XPMSE skeleton bone scales, and object-space normal maps (`_msn.dds`) directly through Skyrim's Virtual File System (supporting both loose files and BSA archives).

### Added

- **Mod-Agnostic 3D Body Geometry Engine (`src/Geometry/`)**
  - **`NifTriParser`** (`NifTriParser.h` / `NifTriParser.cpp`): Streams `_0.nif`, `_1.nif`, and `.tri` files (`PIRT`/`TRIP` formats) directly via `RE::BSResourceNiBinaryStream` and `nifly`, extracting vertex coordinates, UVs, skin bone weights, `BODYTRI` paths, and shader texture sets.
  - **`NifRingIndexer` & `BodyTemplateCache`** (`NifRingIndexer.h` / `NifRingIndexer.cpp` / `BodyTemplateCache.h`): Automatically discovers adult humanoid race skins (`TESRace`), custom follower skins (`TESNPC`), and standalone body armors (`TESObjectARMO` on Slot 32) at `kDataLoaded` (with on-demand fallback for dynamically swapped skins) and slices each mesh into **10 anatomical cross-section rings × 8 compass directions (80 landmark vertices)**.
  - **`ActorBodyEvaluator`** (`ActorBodyEvaluator.h` / `ActorBodyEvaluator.cpp`): Reconstructs an actor's live 80-vertex naked body topology on demand by combining:
    1. Exact continuous actor weight interpolation ($w \in [0, 100]$) between `_0.nif` and `_1.nif`.
    2. All active `SKEE::IBodyMorphInterface` (`skee64.dll`) morph sliders (supporting OBody NG, BodyChange, Fertility Mode, Buffpocalypse, and manual RaceMenu morphs without requiring OBody API coupling).
    3. Live 3D XPMSE bone scales (`NPC L/R Breast`, `NPC Belly`, `NPC L/R Butt`, upper/lower limbs).
  - **Pregnancy vs. Systemic Build Isolation**: Automatically identifies localized belly morphs (`.tri` sliders concentrated in Ring 5 or tagged with pregnancy keywords) and `NPC Belly` node scaling (`pregnancyBellyScore` / $S_{\text{preg}}$), preventing pregnant slender or athletic characters from being misclassified as overweight.
  - **Real-World Anthropometric Logging**: Logs both dimensionless body ratios and approximate real-world measurements (`cm` and `inches`) per evaluated actor in `BodyLanguage.log`.

- **On-Demand Normal Map Muscularity Analyzer (`src/Texture/`)**
  - **`NormalMapAnalyzer`** (`NormalMapAnalyzer.h` / `NormalMapAnalyzer.cpp`): Streams the actor's active object-space normal map (`_msn.dds`) from the 3D scene graph, `BGSTextureSet`, or base NIF via Skyrim's VFS.
  - Decodes the $256 \times 256$ mipmap level via `DirectXTex` (supporting BC1–BC7 block decompression) and runs a 2D high-pass Laplacian filter over the mesh's `abdomenUV` ($75\%$) and `deltoidUV` ($25\%$) regions to compute muscle definition ($S_{\text{tex\_tone}} \in [0, 1]$), cached per texture path.
  - Configurable `SmoothBaseline` and `RippedBaseline` thresholds in `SKSE/Plugins/BodyLanguage.ini`.

- **Unified Physique Classifier & Expanded Phrase System (`src/Description/`)**
  - **`PhysiqueClassifier`** (`PhysiqueClassifier.h` / `PhysiqueClassifier.cpp`): Combines geometric tone ($45\%$), normal-map muscle definition ($40\%$), and morph slider keywords ($15\%$) into a unified tone index ($I_{\text{tone}} \in [-1, +1]$).
  - Added `SKSE/Plugins/BodyLanguage/geometryThresholds.json` for fully customizable, sex-specific anthropometric ratio cutoffs (`shoulder_breadth`, `chest_prominence`, `waist_to_hip`, `glute_girth`, `thigh_girth`, `arm_girth`, `belly_drop`, `pregnancy_score`, `body_mass_index`, `tone_index`).
  - Overhauled `SKSE/Plugins/BodyLanguage/bodyConfigs/FemaleParts.json` and `MaleParts.json` with expanded regional categories (`belly` including `pregnant_early`/`pregnant_mid`/`pregnant_full`, `arms`, male `shoulder_breadth`, `stature_frame`, `chest`, `midsection`, `glutes`, etc.) and distinct `clothed` vs. `naked` phrasing for both `full` and `short` descriptions.
  - Added thread-safe `ActorDescriptionCache` in `BodyDescription.cpp` with a 15-second TTL and automatic state invalidation when clothing state (`isNaked`), 3D load state (`has3D`), or actor weight changes, plus cache flush on save load / new game.

- **Modular SkyrimNet Subcomponent Prompts & Keyword-Based RSV Skin Features**
  - Split `0401_appearance_body_shape.prompt` into modular subcomponents under `SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/body_language/`:
    - `0001_body.prompt` — Core clothed/naked state and C++ `ttlb_get_full_body_description` / `ttlb_get_short_body_description` integration.
    - `0005_skin_features.prompt` & `SKSE/Plugins/SkyrimNet/jsonData/skinFeatures.json` — Instant SPID keyword-based Racial Skin Variance (RSV) skin complexion descriptions (`RSVnord`, `RSVbreton`, `RSVimperial`, `RSVredguard`, `RSVhighelf`, `RSVdarkelf`, `RSVwoodelf`, `RSVorc`, `RSVfantasymer`, `RSVvampirebeast`, `RSVdremora`, `RSVasian`) with both `clothed` and `naked` variants for male and female NPCs.
    - `0010_height.prompt` — Height tier descriptions driven by `heightRanges.json`.
    - `0020_genitals.prompt` — *The New Gentleman* (TNG) and *Schlongs of Skyrim* (SOS) size and addon anatomy descriptions assembled in the prompt/JSON from `ttlb_get_genitals`.

- **Native C++ TNG & Schlongs of Skyrim (SOS) Support**
  - Added `ActorBodyEvaluator::EvaluateActorGenitals`, `PhysiqueClassifier::BuildGenitalsJson`, and the `ttlb_get_genitals` SkyrimNet JSON decorator (`{"hasGenitals": true, "size": "XS..XL", "addon": "<addonKey>"}`), keeping all descriptive prose in `0020_genitals.prompt` and `tngAddons.json`.
  - Inspects Slot 52 (`kModPelvisSecondary`) worn addons, inventory addons, SOS factions (`SOS_SchlongifiedFaction` and addon factions), TNG keywords (`TNG_XS..XL`, `TNG_Gentlewoman`, `TNG_Ignored`, `TNG_Excluded`, `TNG_Covering`), and live 3D `NPC GenitalsBase [GenBase]` XPMSE bone scaling directly in C++.
  - Added classic SOS addon descriptions (`VectorPlexus Muscular`, `VectorPlexus Regular`, `Smurf Average`, `TRX - Male`, `ERF - HighPoly`, `Leito`) to `tngAddons.json` and configurable `"genital_size"` cutoffs to `geometryThresholds.json`.

- **Documentation**
  - Added [`ARCHITECTURE.md`](ARCHITECTURE.md) detailing the v2.0 component architecture, lifecycle phases, 10-ring slicing math, and runtime caching pipeline.
  - Added [`USER_CUSTOMIZATION.md`](USER_CUSTOMIZATION.md) providing a complete reference for tuning `geometryThresholds.json`, `FemaleParts.json`, `MaleParts.json`, `BodyLanguage.ini`, `heightRanges.json`, and `tngAddons.json`.

### Changed

- **Zero ESP / Papyrus / External Archive Setup Required**: The mod is now a pure SKSE DLL + SkyrimNet bundle. Users no longer need `TT_BodyLanguage.esp`, `PapyrusUtil`/`StorageUtil`, or `.7z` archive paths in `BodyLanguage.ini`.
- **Native Equipment Nakedness Detection**: `ttlb_is_naked` now evaluates actor equipment slots directly via `ActorBodyEvaluator::IsActorNaked` (checking Slot 32 `kBody` and Slot 46 `kModChestPrimary`), removing the hard runtime dependency on `OBodyAPI.h`.
- **Dependencies (`vcpkg.json` & `CMakeLists.txt`)**: Added `nifly` and `directxtex` for NIF/TRI parsing and DDS mipmap decoding; removed `bit7z` and `xxhash`.

### Removed

- **Legacy ESP & Papyrus Scripts**
  - Removed `TT_BodyLanguage.esp`, `Scripts/Source/TTBL_MainController.psc`, `Scripts/Source/TTBL_PlayerRef.psc`, compiled `.pex` scripts, and `skyrimse.ppj`
- **Legacy Preset Scanner & Archive Hash Scanners**
  - Removed `SliderPresetScanner.h/.cpp`, `OBodyService.h/.cpp`, `extern/OBody-API/OBodyAPI.h`
  - Removed `RsvNormalMapScanner.h/.cpp`, `RsvDiffuseScanner.h/.cpp`, `HashUtils.h/.cpp`
- **Obsolete Slider Maps & Pre-Generated Preset Databases**
  - Removed `SKSE/Plugins/BodyLanguage/bodyConfigs/{3BA,bhunp,himbo}/{SliderGoals,SliderMap}.json`
  - Removed `SKSE/Plugins/SkyrimNet/jsonData/descriptions/{3ba,bhunp,himbo,Example_3ba_manual}.json`
  - Removed `SKSE/Plugins/BodyLanguage/{rsv_normal_map_hashes,rsv_diffuse_hashes,rsvSkinComplexions}.json`

## v1.0.3

### SkyrimNet Beta 25+ External Plugin Layout Migration

- **Migrated to External Plugin Bundle:**
  - Packaged prompts into external plugin layout at `SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/`
  - Added plugin `manifest.json` targeting SkyrimNet `min_skyrimnet_version: "0.25.0"`
  - Relocated `0401_appearance_body_shape.prompt` to `external/tetherball88.bodylanguage/prompts/submodules/character_bio/`
  - Relocated `0501_exposure.prompt` to `external/tetherball88.bodylanguage/prompts/submodules/user_final_instructions/`
  - Cleaned up legacy loose `prompts/` directory while preserving `jsonData/` lookup tables and config files

## v0.1.0

### Major Architectural Overhaul

This release represents a significant refactoring of the body description system, moving from a Papyrus decorator pattern to a lightweight JSON-based modular architecture.

### Added

- **New Modular Body Description System**
  - `SKSE/Plugins/SkyrimNet/jsonData/femaleBodyPartDescriptions.json` - Core modular body part description system
  - 8 body part categories with clothed/naked state variations:
    - `overall_frame` (6 variants: slim_narrow, regular, big, large, hourglass, bulky)
    - `breasts_size` (6 variants: flat, small, medium, large, huge, enormous)
    - `breasts_shape` (7 variants: full_round, perky, saggy, teardrop, wide_set, close_set, pressed)
    - `butt_size` (4 variants: small, medium, large, huge)
    - `butt_shape` (7 variants: flat, regular, round, extremely_round, apple_shaped, dimpled, deep_cleft)
    - `legs_size` (4 variants: thin, regular, big, huge)
    - `legs_shape` (5 variants: regular, toned_athletic, soft_plush, gap, touching)
    - `body_composition` (6 variants: lean, regular, soft, chubby, athletic, muscular)

- **Expanded Body Preset Support**
  - `SKSE/Plugins/SkyrimNet/jsonData/cbbe/cbbeAdditional.json` - 70+ community CBBE/3BA body presets
  - Popular presets including:
    - CBBE Pygmalion
    - Busty Babe
    - My Sensual/Majestic Body series
    - Omikami Hips
    - Tea Body 2024
    - Stellar Blade variants
    - Viper's preset collections (Sweet Embrace, Wildflower, Vintage Muse, etc.)
    - And many more...

- **TNG (The New Gentleman) Support**
  - `SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json` - Male body addon descriptions
  - Added support for [LDD - TNG - Racial Penis Variances - BNP 'n TRX](https://www.nexusmods.com/skyrimspecialedition/mods/143062) addons
  - smart collection of current multipliers plus detecting TNG override keywords for sizes and addons

- **Documentation**
  - `docs/BodyDescriptionSystem.md` - Comprehensive technical documentation for the new system
  - Updated `docs/CBBE_3BA_descriptions.md` - Complete list of supported CBBE presets with file references

### Changed

- **Reorganized File Structure**
  - Moved body descriptions from `SKSE/Plugins/BodyLanguage/bodyDescriptions/` to `SKSE/Plugins/SkyrimNet/jsonData/`
  - Centralized all body-related JSON data in SkyrimNet directory structure

- **Simplified Script Architecture**
  - `Scripts/Source/TTBL_MainController.psc` - Streamlined main controller (removed decorator dependencies)
  - `Scripts/Source/TTBL_MCM.psc` - Simplified MCM menu (63 lines removed)

- **Enhanced Prompt System**
  - `SKSE/Plugins/SkyrimNet/prompts/submodules/character_bio/0401_appearance_body_shape.prompt` - Updated to use new modular description system
  - Dynamic description assembly from body part components
  - Automatic fallback from pre-composed to modular descriptions
  - Improved weight-based description selection (0/50/100 weight points)
  - Enhanced clothed/naked state detection

### Removed

- **Deprecated Papyrus Scripts** (~500 lines of code removed)
  - `Scripts/Source/TTBL_Decorators.psc` - Decorator pattern implementation
  - `Scripts/TTBL_Decorators.pex` - Compiled decorator script
  - `Scripts/Source/TTBL_JCDomain.psc` - JContainers domain management (269 lines)
  - `Scripts/TTBL_JCDomain.pex` - Compiled JCDomain script
  - `Scripts/Source/TTBL_JData.psc` - JContainers data utilities (119 lines)
  - `Scripts/TTBL_JData.pex` - Compiled JData script

- **Deprecated Storage Systems**
  - `SKSE/Plugins/JCData/Domains/TTBL_JCDomain/.placeholder` - JContainers domain storage no longer needed
  - `SKSE/Plugins/BodyLanguage/tngDescriptions/default.json` - Old form-based TNG descriptions

### Technical Improvements

- **No More JContainers Dependency** for body descriptions
- **Zero Papyrus Code Required** to add new body presets
- **Community-Friendly** - Anyone can add presets by editing JSON files
- **Consistent Formatting** - All descriptions follow standardized structure
- **Dynamic Clothing Adaptation** - Descriptions automatically change based on equipped torso armor
- **Reduced Maintenance Burden** - Single source of truth for body part descriptions
- **Better Performance** - Removed unnecessary script overhead

### Migration Notes

For users upgrading from previous versions:
- JContainers domain data is no longer used for body descriptions
- Old decorator scripts are removed
- Existing OBody preset assignments continue to work
- No manual intervention required

### Developer Notes

The new system works as follows:
1. Detects NPC weight (0/50/100 thresholds at 33% and 66%)
2. Checks clothing state (equipped torso armor = clothed)
3. Retrieves OBody preset name via PapyrusUtil
4. Attempts to load pre-composed description from preset JSON
5. Falls back to modular assembly if no pre-composed description exists
6. Combines 8 body part descriptions into coherent sentence

See [docs/BodyDescriptionSystem.md](docs/BodyDescriptionSystem.md) for complete technical documentation.

---

## Previous Versions

### v0.0.4
- body descriptions appear only for speaking npc

### v0.0.3
- Updated for SkyrimNet version Beta 7, rc3

### v0.0.2
- Body descriptions now appear only for speaking NPCs

### v0.0.1
- Added additional validation check

---

## Related Documentation

- [Architecture & Lifecycle Documentation](ARCHITECTURE.md) - Technical architecture and runtime pipeline for v2.0
- [User & Modder Customization Guide](USER_CUSTOMIZATION.md) - Guide for tuning thresholds, phrases, normal-map baselines, and TNG addons
- [Body Description System](docs/BodyDescriptionSystem.md) - Legacy v1.x technical documentation
- [CBBE 3BA Presets](docs/CBBE_3BA_descriptions.md) - Supported CBBE presets
- [BHUNP Presets](docs/BHUNP_descriptions.md) - Supported BHUNP presets
- [HIMBO Presets](docs/HIMBO_descriptions.md) - Supported HIMBO presets
