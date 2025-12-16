# Body Description System - New Modular Approach

## Overview

The latest update (commit `5e526fc`) introduces a new modular approach to generating body shape descriptions for NPCs. This system replaces the previous decorator-based approach with a simpler, more maintainable JSON-based solution.

## Key Changes

### Removed Components
- **TTBL_Decorators.psc** - Decorator classes removed
- **TTBL_JCDomain.psc** - JContainers domain handling removed
- **TTBL_JData.psc** - JContainers data management removed
- **JCData/Domains/TTBL_JCDomain/** - JContainers storage removed
- **BodyLanguage/tngDescriptions/** - Old TNG descriptions removed

### Simplified Architecture
- **TTBL_MainController.psc** - Streamlined without decorator dependencies
- **TTBL_MCM.psc** - MCM menu simplified (removed 63 lines)

## New File Structure

### Core Description Data

All body description data is now stored in standardized locations:

```
SKSE/Plugins/SkyrimNet/jsonData/
├── femaleBodyPartDescriptions.json  # Modular body part descriptions
├── cbbeMain.json                     # CBBE main presets
├── cbbe/
│   └── cbbeAdditional.json          # Community CBBE presets (1864 lines)
├── bhunpMain.json                    # BHUNP presets
└── himboMain.json                    # HIMBO presets
```

## Modular Description System

### femaleBodyPartDescriptions.json

This file defines the core modular system with descriptions for individual body attributes. Each attribute has separate descriptions for **clothed** and **naked** states.

#### Structure

```json
{
  "combined_description": {
    "clothed": "template string with {placeholders}",
    "naked": "template string with {placeholders}"
  },
  "overall_frame": {
    "slim_narrow": { "clothed": "...", "naked": "..." },
    "regular": { "clothed": "...", "naked": "..." },
    "big": { "clothed": "...", "naked": "..." },
    "large": { "clothed": "...", "naked": "..." },
    "hourglass": { "clothed": "...", "naked": "..." },
    "bulky": { "clothed": "...", "naked": "..." }
  },
  "breasts_size": {
    "flat": { "clothed": "...", "naked": "..." },
    "small": { "clothed": "...", "naked": "..." },
    "medium": { "clothed": "...", "naked": "..." },
    "large": { "clothed": "...", "naked": "..." },
    "huge": { "clothed": "...", "naked": "..." },
    "enormous": { "clothed": "...", "naked": "..." }
  },
  "breasts_shape": {
    "full_round": { "clothed": "...", "naked": "..." },
    "perky": { "clothed": "...", "naked": "..." },
    "saggy": { "clothed": "...", "naked": "..." },
    "teardrop": { "clothed": "...", "naked": "..." },
    "wide_set": { "clothed": "...", "naked": "..." },
    "close_set": { "clothed": "...", "naked": "..." },
    "pressed": { "clothed": "...", "naked": "..." }
  },
  "butt_size": {
    "small": { "clothed": "...", "naked": "..." },
    "medium": { "clothed": "...", "naked": "..." },
    "large": { "clothed": "...", "naked": "..." },
    "huge": { "clothed": "...", "naked": "..." }
  },
  "butt_shape": {
    "flat": { "clothed": "...", "naked": "..." },
    "regular": { "clothed": "...", "naked": "..." },
    "round": { "clothed": "...", "naked": "..." },
    "extremely_round": { "clothed": "...", "naked": "..." },
    "apple_shaped": { "clothed": "...", "naked": "..." },
    "dimpled": { "clothed": "...", "naked": "..." },
    "deep_cleft": { "clothed": "...", "naked": "..." }
  },
  "legs_size": {
    "thin": { "clothed": "...", "naked": "..." },
    "regular": { "clothed": "...", "naked": "..." },
    "big": { "clothed": "...", "naked": "..." },
    "huge": { "clothed": "...", "naked": "..." }
  },
  "legs_shape": {
    "regular": { "clothed": "...", "naked": "..." },
    "toned_athletic": { "clothed": "...", "naked": "..." },
    "soft_plush": { "clothed": "...", "naked": "..." },
    "gap": { "clothed": "...", "naked": "..." },
    "touching": { "clothed": "...", "naked": "..." }
  },
  "body_composition": {
    "lean": { "clothed": "...", "naked": "..." },
    "regular": { "clothed": "...", "naked": "..." },
    "soft": { "clothed": "...", "naked": "..." },
    "chubby": { "clothed": "...", "naked": "..." },
    "athletic": { "clothed": "...", "naked": "..." },
    "muscular": { "clothed": "...", "naked": "..." }
  }
}
```

### Body Preset Format (cbbeAdditional.json)

Each BodySlide preset defines body characteristics at three weight points (0, 50, 100):

```json
{
  "Preset Name": {
    "0": {
      "bodyParts": {
        "overall_frame": "slim_narrow",
        "breasts_size": "medium",
        "breasts_shape": "perky",
        "butt_size": "small",
        "butt_shape": "regular",
        "legs_size": "regular",
        "legs_shape": "toned_athletic",
        "body_composition": "lean"
      }
    },
    "50": {
      "bodyParts": { /* ... */ }
    },
    "100": {
      "bodyParts": { /* ... */ }
    }
  }
}
```

## How It Works

The system generates descriptions through a template-based approach in [0401_appearance_body_shape.prompt](../SKSE/Plugins/SkyrimNet/prompts/submodules/character_bio/0401_appearance_body_shape.prompt):

### 1. Weight Detection
```jinja2
{% set weight = npcData.weight %}
{% if weight <= 33 %}
    {% set weightKey = "0" %}
{% else if weight >= 66 %}
    {% set weightKey = "100" %}
{% else %}
    {% set weightKey = "50" %}
{% endif %}
```

### 2. Clothing State Detection
```jinja2
{% set isClothed = has_key(get_worn_equipment(actorUUID), "torso") %}
{% if isClothed %}
    {% set clothingKey = "clothed" %}
{% else %}
    {% set clothingKey = "naked" %}
{% endif %}
```

### 3. OBody Preset Retrieval
```jinja2
{% set obodyPreset = papyrus_util("GetStringValue", null, "obody_" + to_string(npcData.formId) + "_preset") %}
```

### 4. Body System Detection
```jinja2
{% if isCbbe %}
    {% set femaleBodyDescriptionsPath = "./SkyrimNet/jsonData/cbbeMain.json" %}
{% endif %}
{% if isBhunp %}
    {% set femaleBodyDescriptionsPath = "./SkyrimNet/jsonData/bhunpMain.json" %}
{% endif %}
{% if isHimbo %}
    {% set maleBodyDescriptionsPath = "./SkyrimNet/jsonData/himboMain.json" %}
{% endif %}
```

### 5. Description Assembly

The system attempts to read a pre-composed description first:
```jinja2
{% set bodyDescription = read_json(bodyDescriptionsPath, obodyPreset + "." + weightKey + "." + clothingKey) %}
```

If no pre-composed description exists, it builds one from modular parts:
```jinja2
{% set parts = read_json("./SkyrimNet/jsonData/femaleBodyPartDescriptions.json") %}
{% set femalePresets = read_json("./SkyrimNet/jsonData/cbbe/cbbeAdditional.json") %}
{% set presetData = at(at(at(femalePresets, obodyPreset), weightKey), "bodyParts") %}

{% set partKey = at(presetData, "overall_frame") %}
{% set overall_frame = at(at(at(parts, "overall_frame"), partKey), clothingKey) %}

{% set partKey = at(presetData, "breasts_size") %}
{% set breasts_size = at(at(at(parts, "breasts_size"), partKey), clothingKey) %}

// ... repeat for all body parts ...

{% set bodyDescription = overall_frame + " " + breasts_size + " " + breasts_shape + " " + butt_size + " " + butt_shape + " " + legs_size + " " + legs_shape + " " + body_composition %}
```

## Adding New Body Presets

### Option 1: Pre-composed Descriptions
Add complete descriptions to `cbbeMain.json`, `bhunpMain.json`, or `himboMain.json`:

```json
{
  "Your Preset Name": {
    "0": {
      "clothed": "Complete description for weight 0 clothed...",
      "naked": "Complete description for weight 0 naked..."
    },
    "50": {
      "clothed": "Complete description for weight 50 clothed...",
      "naked": "Complete description for weight 50 naked..."
    },
    "100": {
      "clothed": "Complete description for weight 100 clothed...",
      "naked": "Complete description for weight 100 naked..."
    }
  }
}
```

### Option 2: Modular Descriptions (Recommended)
Add body part definitions to `cbbe/cbbeAdditional.json`:

```json
{
  "Your Preset Name": {
    "0": {
      "bodyParts": {
        "overall_frame": "slim_narrow",
        "breasts_size": "medium",
        "breasts_shape": "perky",
        "butt_size": "small",
        "butt_shape": "regular",
        "legs_size": "regular",
        "legs_shape": "toned_athletic",
        "body_composition": "lean"
      }
    },
    "50": { /* ... */ },
    "100": { /* ... */ }
  }
}
```

The system will automatically compose descriptions from `femaleBodyPartDescriptions.json`.

## Advantages of New System

1. **No Papyrus Code Required** - All descriptions defined in JSON
2. **Easier to Maintain** - Single source of truth for body part descriptions
3. **Consistent Formatting** - All descriptions follow same structure
4. **Community-Friendly** - Anyone can add presets by editing JSON files
5. **Dynamic Clothing States** - Automatically adapts descriptions based on equipment
6. **Extensible** - Easy to add new body attributes or description variants
7. **Reduced File Size** - Removed ~500 lines of Papyrus code and binary files

## Supported Body Presets

The system now includes 70+ community body presets in `cbbeAdditional.json`, including popular presets from:
- CBBE Pygmalion
- Busty Babe
- My Sensual Body
- Omikami Hips
- Tea Body
- Stellar Blade variants
- Viper's preset collections
- And many more...

See [CBBE_3BA_descriptions.md](CBBE_3BA_descriptions.md) for the complete list.

## TNG (The New Gentleman) Support

The system also includes support for male body descriptions via [tngAddons.json](../SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json), replacing the old form-based TNG description system.

## Migration Notes

For users upgrading from the old system:
- JContainers domain data is no longer used
- Old decorator scripts are removed
- Existing OBody preset assignments still work
- Body descriptions will be regenerated using the new system automatically
