# TT Body Language — Integration Guide

This document covers all six runtime integrations that TT Body Language provides: **OBody NG**, **RSV Normal Maps**, **RSV Skin Complexion (Diffuse)**, **Height**, **T.N.G**, and **SkyrimNet decorators**. Each section explains how the system works and how to configure or extend it.

---

## 1. OBody NG Integration

OBody NG is the source of truth for every body shape description. Without it the mod produces no output.

### How it works

On startup the SKSE plugin registers itself with OBody NG via the OBody API (`SKSEMessages::RequestPluginInterface`). OBody signals back when its data is ready (`OBodyIsReady`) and when it suspends during saves/loads (`OBodyIsNoLongerReady`). While the API is live the plugin can call two methods on every actor:

- `GetPresetAssignedToActor` — returns the name of the BodySlide preset currently applied to the actor (e.g. `"BHUNP - 7B Original"`).
- `ActorIsNaked` — returns whether the actor currently has no body-covering equipment.

These two values drive every subsequent stage of description generation.

---

### Slider scanning flow

At `kDataLoaded` (after all plugins and data are fully loaded) the plugin scans every `.xml` file inside:

```
Data/CalienteTools/BodySlide/SliderPresets/
```

For each `<Preset>` node the body type is detected from the preset's `set` attribute using substring matching:

| Set attribute contains | Resolved body type |
|---|---|
| `himbo` | HIMBO |
| `bhunp` | BHUNP |
| `3ba`, `3bbb`, or `cbbe` | 3BA / CBBE |

The plugin then loads the configuration for the matched body type from `Data/SKSE/Plugins/BodyLanguage/bodyConfigs/<BodyType>/`:

- **`SliderMap.json`** — one entry per BodySlide slider name. Defines the slider's `impact` (a weighting constant measured in the same units as the score axis), plus `smallDefault` and `bigDefault` fallback values used when a preset does not set that slider explicitly.
- **`SliderGoals.json`** — defines *goal groups* (e.g. `breasts_volume`, `waist`, `frame`). Each group lists which sliders contribute to it (with optional per-slider multipliers) and the score range thresholds that map a numeric score to a keyword (e.g. `"large"`, `"slim"`).

For each preset, slider values are read from the XML at both the `small` (weight = 0) and `big` (weight = 100) morph targets (normalised from 0–100 to 0–1). A `medium` value is synthesised as the average. For every goal group and every tier (small/medium/big) the weighted score is computed as:

```
score = Σ ( slider_value × impact × per_slider_multiplier )
```

The score is matched against the range table to produce the keyword for that tier. All keywords are concatenated into a semicolon-separated **meta string**:

```
breasts_volume:large(13443);physique_condition:soft(0);butt_hips:average(4997);...
```

The number in parentheses is the raw score, kept for transparency/debugging.

After scanning finishes the collected entries are written to:

```
Data/SKSE/Plugins/SkyrimNet/jsonData/descriptions/<bodytype>.json
```

These files are the lookup table used at runtime to resolve preset names into descriptive keywords. They are **fully regenerated on every game load**; you do not need to manually refresh them.

---

### bodyConfigs files in detail

All files live under `Data/SKSE/Plugins/BodyLanguage/bodyConfigs/`.

#### `<BodyType>/SliderMap.json`

Maps every BodySlide slider name used by this body type to its metadata:

```json
{
    "Breasts": {
        "group": "Breasts",
        "displayName": "Size",
        "referenceName": "3BA RefBreasts",
        "smallDefault": 0.0,
        "bigDefault": 0.0,
        "sliderName": "3BA RefBreasts",
        "impact": 9626.393
    }
}
```

| Field | Purpose |
|---|---|
| `impact` | How much one unit of this slider (0–1) shifts the goal score. Higher = more influential. |
| `smallDefault` / `bigDefault` | Percentage value to assume (0–100) when a preset omits this slider. |
| `group`, `displayName`, `referenceName`, `sliderName` | Informational; not used at runtime. |

The key (e.g. `"Breasts"`) must match the `name` attribute of the corresponding `<SetSlider>` node in the BodySlide XML.

#### `<BodyType>/SliderGoals.json`

Defines which sliders feed each goal group and how their score maps to a keyword:

```json
{
    "goals": {
        "breasts_volume": {
            "sliders": ["VanillaSSELo:-0.28", "Breasts", "BreastsSmall:-1"],
            "ranges": {
                "flat":    [-999999, -4750],
                "small":   [-4750,   -1000],
                "average": [-1000,    5775],
                "large":   [5775,    15000],
                "huge":    [15000,   25000],
                "impossible": [25000, 99999999999]
            }
        }
    }
}
```

- Each entry in `sliders` is either a plain slider name (`"Breasts"`) or a name with a multiplier (`"BreastsSmall:-1"`). A multiplier of `-1` means this slider subtracts from the score (it makes breasts smaller, so adding it *reduces* the volume score).
- `ranges` must be ordered from the most-negative threshold to the most-positive. The score is matched against the first range `[lo, hi)` it falls into. Out-of-range scores clamp to the nearest end.

#### `FemaleParts.json`

Maps each goal group keyword to the English phrase used in the generated sentence, for both `clothed` and `naked` contexts:

```json
{
    "breasts_volume": {
        "large": {
            "clothed": "a large, prominent bust",
            "naked":   "large, heavy breasts"
        }
    }
}
```

Groups available for females: `breasts_volume`, `waist`, `frame`, `butt_hips`, `legs`, `physique_condition`.

#### `MaleParts.json`

Same structure as `FemaleParts.json` but for male actors (HIMBO body type).

Groups available for males: `stature_frame`, `arms`, `shoulder_breadth`, `midsection`, `glutes`, `legs`, `physique_condition`.

---

### Manual overrides

The scanner applies a **sidecar override file** immediately after computing all auto-generated entries. Create a file named:

```
Data/SKSE/Plugins/SkyrimNet/jsonData/descriptions/<bodytype>_manual.json
```

For example, for 3BA presets: `3ba_manual.json`. An example file (`Example_3ba_manual.json`) is included to show the expected format:

```json
{
    "(GO-THICC GIRL)": {
        "big": {
            "meta": "breasts_volume:impossible;physique_condition:chubby;butt_hips:huge;frame:average;legs:thick;waist:soft"
        },
        "medium": {
            "meta": "breasts_volume:impossible;physique_condition:chubby;butt_hips:huge;frame:average;legs:standard;waist:standard"
        },
        "small": {
            "meta": "breasts_volume:huge;physique_condition:soft;butt_hips:huge;frame:slim;legs:average;waist:standard"
        }
    }
}
```

Rules:

- The top-level key is the **exact preset name** as it appears in OBody NG (case-sensitive).
- All three tiers (`small`, `medium`, `big`) must be present.
- Each tier must contain a `meta` string in `group:keyword` format. The optional `(score)` annotation is allowed but not required.
- Keywords must be valid entries in the corresponding `FemaleParts.json` or `MaleParts.json`. Invalid keywords will fall back to the raw keyword string.
- Manual entries always win over auto-computed ones. The auto-computed entry for that preset is fully replaced.
- The file must be valid JSON. Malformed files are skipped with a warning in `BodyLanguage.log`.

Use manual overrides when the auto-computed result is wrong for a preset (e.g. extreme or unusual shapes that land outside normal slider patterns).

---

### descriptions/ files

These are **the runtime lookup table** — generated automatically from slider scanning and your manual overrides. You rarely need to edit them directly, but understanding their structure helps with debugging.

Location: `Data/SKSE/Plugins/SkyrimNet/jsonData/descriptions/`

| File | Body type |
|---|---|
| `3ba.json` | CBBE 3BA (female) |
| `bhunp.json` | BHUNP (female) |
| `himbo.json` | HIMBO (male) |

Each file is a JSON object keyed by preset name. Each preset has three tiers (`small`, `medium`, `big`) each with a `meta` string:

```json
"BHUNP - 7B Original": {
    "big":    { "meta": "breasts_volume:large(14543);..." },
    "medium": { "meta": "breasts_volume:large(13552);..." },
    "small":  { "meta": "breasts_volume:average(12561);..." }
}
```

At runtime the plugin:
1. Reads the actor's preset name from OBody NG.
2. Selects the tier based on the actor's weight slider: below 34 = `small`, 34–67 = `medium`, above 67 = `big`.
3. Parses the `meta` string into a keyword map.
4. Resolves each keyword to an English phrase via `FemaleParts.json` / `MaleParts.json`.
5. Assembles the phrase map into a sentence using the clothed/naked context.

Two sentence forms are registered with SkyrimNet as decorators:
- `get_full_body_description` — full multi-segment sentence covering all goal groups.
- `get_short_body_description` — abbreviated sentence covering only the most prominent features.

---

## 2. RSV Normal Map Integration

RSV is an optional mod that assigns race-specific skin normal maps to actors. These maps encode muscularity information (e.g. `boxer`, `toned50`, `vigorexia`) and, for female bodies with CBBE/BnP patches, whether the actor has a large or small breast variant assigned by RSV.

### How it works

On startup a background thread calls `ScanRsvNormalMaps`, which reads the RSV `.7z` archive(s) configured in `BodyLanguage.ini` and computes an MD5 hash for every `.dds` file in the `Normal Maps/` sub-tree. The results are saved to:

```
Data/SKSE/Plugins/BodyLanguage/rsv_normal_map_hashes.json
```

At description time, the plugin walks the actor's third-person skeleton looking for the body geometry's normal-map texture slot (any `BSGeometry` whose `BSTextureSet` slot `kNormal` contains both `"body"` and `"msn"` in the path). The texture file on disk is hashed with MD5 and looked up in the cache.

A cache hit returns two fields:

- `muscularity` — the RSV variant name (e.g. `"boxer"`, `"vigorexia"`, `"toned50"`).
- `isBigBreasts` — `"big"`, `"small"`, or `"NA"` (HIMBO / neutral normals).

### Impact on body descriptions

The RSV match is used to **bump** the `physique_condition` keyword up or down along the physique axis before the description sentence is assembled:

| Muscularity | Adjustment |
|---|---|
| `vigorexia` | +2 steps toward muscular |
| `boxer`, `vigorexia50`, `dovaklini` | +1 step toward muscular |
| `chubby` | +1 step toward fat |
| all others | no change |

Female physique axis (most muscular → most fat):
`muscular → athletic → lean → soft → plump → chubby → obese`

Male physique axis:
`shredded → muscular → athletic → solid → soft → heavy → obese`

For example, an actor whose OBody preset maps to `physique_condition:soft` but whose RSV normal map is the `boxer` variant will appear as `physical_condition:lean` in the final description.

If no RSV match is found (RSV not installed, texture is BSA-packed, or the hash is unknown) the OBody-derived keyword is used unchanged.

---

### rsv_normal_map_hashes.json

This file is a **generated cache** — do not edit it by hand. It is rebuilt on every game load from the `.7z` archives listed in `BodyLanguage.ini`.

Each entry uses the file's MD5 hash as the key:

```json
"05700fd9950f40158cbb2908783da0ee": {
    "archive":      "BnP female RSV (CBBE) 4k-...",
    "filePath":     "BnP RSV patch (CBBE) 4k/options/body normals/big boobs/boxer/.../femalebody_1_msn.dds",
    "isBigBreasts": "big",
    "muscularity":  "boxer"
}
```

### Configuring RSV paths (BodyLanguage.ini)

Open `Data/SKSE/Plugins/BodyLanguage.ini` and set the paths to your RSV archives:

```ini
[RSV]
; Main RSV archive (required for RSV support)
RSV_Main_ArchivePath=C:\path\to\Racial Skin Variance - SPID 4K Version.7z

; Optional BnP female RSV patch (CBBE)
BNP_RSV__Female_ArchivePath=C:\path\to\BnP female RSV (CBBE) 4k.7z

; Optional BnP male RSV patch
BNP_RSV__Male_ArchivePath=C:\path\to\BnP male RSV (SOS full) 4k.7z
```

Leave the keys blank or remove them entirely to skip the RSV scan.

> **MO2 users:** Use the archive path in your downloads folder, not the virtual filesystem. The scanner reads directly from the `.7z` file.

---

## 3. RSV Skin Complexion (Diffuse) Integration

BnP's RSV patch archives ship a set of optional **diffuse (albedo) texture variants** under `options/diffuse/` that give actors race-specific skin markings — frost-nip, freckles, scars, veins, tattoos, and more. BodyLanguage can detect which diffuse variant is active on an actor and prepend a complexion sentence to the full naked description.

### How it works

On startup a background thread calls `ScanRsvDiffuseMaps`, which reads the same BnP female and male `.7z` archives configured in `[RSV]` of `BodyLanguage.ini`. It streams every `.dds` whose **filename** (not directory path) contains `"body"` and does not end in `_msn.dds` or `_s.dds` (only diffuse / albedo files). For each matching file the path segment between `options/diffuse/` and `textures/` is extracted as a `race/variant` key (e.g. `nord/+frostnip`). This key is looked up in a hardcoded map inside the scanner to produce a **complexion key**. The file is then MD5-hashed and the result written to:

```
Data/SKSE/Plugins/BodyLanguage/rsv_diffuse_hashes.json
```

At description time, when the actor is **naked** and `mode == "full"`, the plugin:

1. Walks the actor's third-person skeleton looking for the body geometry's diffuse texture slot (`kDiffuse`, slot 0). The first geometry whose **filename** contains `"body"` is used.
2. Normalises the path (prepends `textures\` if absent) and hashes the file with MD5.
3. Looks up the MD5 in `rsv_diffuse_hashes.json` to get the complexion key.
4. Looks up the complexion key in `rsvSkinComplexions.json` to get the sentence fragment.
5. Prepends `"She <sentence>"` or `"He <sentence>"` to the body description sentence.

If no match is found (diffuse not from an RSV BnP patch, texture BSA-packed, or hash unknown) the body description is returned unchanged.

---

### Supported complexion variants

| `race/variant` path | Complexion key | Notes |
|---|---|---|
| `akaviri/brown` | `akaviri` | |
| `akaviri/pink` | `akaviri` | |
| `altmer/+stretchmark` | `stretch_marks` | |
| `bosmer/+freckles` | `freckles_light` | |
| `dremora/grey` | `dremora_dark` | |
| `dremora/red` | `dremora_red` | |
| `dunmer/+veins` | `dunmer_veiny` | |
| `fantasyaltmer/gold` | `golden_altmer` | |
| `fantasyaltmer/silver` | `silver_altmer` | |
| `fantasybosmer/autumn` | `autumn_bosmer` | |
| `fantasybosmer/spring` | `spring_bosmer` | |
| `fantasydunmer/purple` | `purple_dunmer` | |
| `fantasydunmer/silver` | `silver_dunmer` | |
| `fantasyorc/green` | `green_orc` | |
| `fantasyorc/red` | `red_orc` | |
| `imperial/+scars` | `scars` | |
| `monstervampire/+markings` | `vampire_beast` | |
| `nord/+frostnip` | `frostnip` | |
| `orc/+dirt` | `dirty` | |
| `redguard/+dark` | `tanlines` | |

The default (unmarked) diffuse textures — `nord/default`, `imperial/default`, etc. — are deliberately excluded. They carry no special complexion marking and produce no match, which is the correct behaviour (no sentence is prepended).

---

### rsv_diffuse_hashes.json

This file is a **generated cache** — do not edit it by hand. It is rebuilt on every game load from the `.7z` archives listed in `BodyLanguage.ini`.

Each entry uses the file's MD5 hash as the key:

```json
"a3f8c21d0e5b4791fbedc8e227349a10": {
    "archive":    "BnP female RSV (CBBE) 4k-...",
    "filePath":   "options/diffuse/nord/+frostnip/textures/.../femalebody_1.dds",
    "complexion": "frostnip"
}
```

---

### rsvSkinComplexions.json

Location: `Data/SKSE/Plugins/BodyLanguage/rsvSkinComplexions.json`

Maps each complexion key to the sentence fragment appended to the description. The fragment should be phrased as a continuation after `"She "` or `"He "`:

```json
{
    "frostnip":      "has faint reddish patches of frostnip dotting her exposed skin, a common mark among those who brave the northern cold.",
    "freckles_light":"has a light dusting of freckles across her cheeks and shoulders.",
    "scars":         "bears a network of old battle scars across her torso and arms.",
    "tanlines":      "shows distinct tanlines — pale stripes where armour and clothing have shielded her skin from the sun."
}
```

**To add or change a complexion sentence**, edit this file directly. Changes take effect on the next game load. Do not add keys that are not present in the scanner's map — they will never be matched.

---

## 5. Height Integration

### How it works

Height is handled entirely in SkyrimNet's Jinja2 prompt template (`0401_appearance_body_shape.prompt`). No C++ code is involved. The template reads the actor's `height` value (a float, where `1.0` is the race base height) and compares it against the thresholds in `heightRanges.json` to select a canned sentence.

The six tiers, from tallest to shortest:

| Tier | Description inserted into the prompt |
|---|---|
| `towering` | *"stands with a towering, statuesque height that forces others to crane their necks"* |
| `tall` | *"possesses a commanding and rangy stature, standing noticeably taller than the average citizen of Skyrim"* |
| `above_average` | *"has a sturdy and substantial presence, carrying enough height to easily overlook a crowded room"* |
| `average` | *"stands at a perfectly unremarkable height, blending effortlessly into any crowd"* |
| `short` | *"possesses a modest and compact frame, standing just a few inches below the eye level of most peers"* |
| `diminutive` | *"has a strikingly small and agile stature, appearing quite slight even compared to the shorter races"* |

A value below the `short` threshold and also below the `diminutive` threshold triggers the diminutive description. Anything between `short` and `average` gets the short description. Both comparisons use `>=`.

### heightRanges.json

Location: `Data/SKSE/Plugins/SkyrimNet/jsonData/heightRanges.json`

```json
{
    "towering":      1.12,
    "tall":          1.08,
    "above_average": 1.04,
    "average":       0.99,
    "short":         0.95,
    "diminutive":    0.90
}
```

Each value is the **minimum height multiplier** required to be classified at that tier. For reference, vanilla Skyrim height multipliers by race typically range from about 0.95 (Bosmer) to 1.04 (Nord male). Heights set by mods like NPCS are distributed or auto-generated heights can push values outside this range.

**To adjust the thresholds**, edit the numbers in this file. Changes take effect on the next game load (the template reads the file fresh each time). The template evaluates thresholds from highest to lowest, so you only need to ensure `towering > tall > above_average > average > short > diminutive`.

Example — make `tall` harder to reach and extend the `average` band:

```json
{
    "towering":      1.15,
    "tall":          1.10,
    "above_average": 1.05,
    "average":       0.97,
    "short":         0.93,
    "diminutive":    0.88
}
```

---

## 6. T.N.G (The New Gentleman) Integration

T.N.G assigns male genital addons to actors and exposes size keywords and racial group data through its Papyrus API.

### How it works

A Papyrus quest script (`TTBL_MainController`) runs on game start and on every load. It queries T.N.G's API and caches all relevant data using `StorageUtil`:

1. **Racial group addons and multipliers** — for every racial group defined in T.N.G (`TNG_PapyrusUtil.GetRgNames()`), the script stores:
   - The default addon key for that race: `TTLB_TNG_RacialGroup_<RaceName>_Addon`
   - The racial size multiplier: `TTLB_TNG_RacialGroup_<RaceName>_Multiplier`

2. **Full addon lists** — all possible male and female addon names are stored in:
   - `StorageUtil` string list `TTLB_TNG_MaleAddons`
   - `StorageUtil` string list `TTLB_TNG_FemaleAddons`

At prompt-generation time, the template:

1. Checks whether TNG applies to the actor (skipped for females unless the `TNG_Gentlewoman` keyword is present; always skipped if the `TNG_Ignored` or `TNG_Excluded` keywords are on the actor).
2. Reads the racial base multiplier for the actor's race.
3. Reads the base T.N.G size keyword assigned to the actor (`TNG_XS`, `TNG_S`, `TNG_M`, `TNG_L`, `TNG_XL`) and multiplies it by the racial multiplier to get a numeric size factor. This is then re-quantised back to an XS–XL label.
4. Iterates the full addon list and checks which `TNG_ActorAddnUser:NN` keyword the actor has, selecting the matching addon name.
5. Looks up the description text for that addon key in `tngAddons.json`.

The size label and the addon description are combined into a sentence inserted into the SkyrimNet prompt only when the actor is **not clothed** (checked via `has_key(get_worn_equipment(actorUUID), "torso")`).

### tngAddons.json

Location: `Data/SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json`

This file maps a T.N.G addon key (usually the addon's display name as returned by `TNG_PapyrusUtil.GetAllPossibleAddons()`) to a short description fragment. The description should complete the sentence:

> *"[NPC name]'s exposed penis is [size description]. It has a **[tngAddon]**"*

```json
{
    "$TNG_TRT": "well-balanced shaft of typical proportions with moderate veining and a naturally tapered head.",
    "$TNG_TCT": "smooth, taut shaft with a fully exposed head and well-defined coronal ridge.",
    "[LDD] TRX - Nord Schlong": "thick, robust shaft marked by prominent veins and vigorous coloring. The head is broad and well-defined with a pronounced ridge.",
    "[LDD] TRX - Khajiit Schlong": "distinctly feline anatomy with rough, textured surface. The sharply tapered head differs in coloration from the shaft, with small barb-like ridges along the glans."
}
```

Keys beginning with `$TNG_` are the default T.N.G internal addon identifiers. Keys in `[Mod Name] Addon Name` format are addons from third-party T.N.G addon packs.

**To add descriptions for additional addons:**

1. Open `tngAddons.json`.
2. Add an entry whose key exactly matches the addon name as it appears in T.N.G's MCM or is returned by `TNG_PapyrusUtil.GetAllPossibleAddons()`. The addon name is case-sensitive.
3. Write a description fragment in the second person that fits the sentence template above. End with a full stop.

If an addon key is not found in the file the template falls back to a generic description: *"well-balanced shaft of typical proportions with moderate veining and a naturally tapered head."*

