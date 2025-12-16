# Changelog

All notable changes to TT BodyLanguage will be documented in this file.

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

- [Body Description System](docs/BodyDescriptionSystem.md) - Technical documentation
- [CBBE 3BA Presets](docs/CBBE_3BA_descriptions.md) - Supported CBBE presets
- [BHUNP Presets](docs/BHUNP_descriptions.md) - Supported BHUNP presets
- [HIMBO Presets](docs/HIMBO_descriptions.md) - Supported HIMBO presets
