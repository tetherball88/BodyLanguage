# BodyLanguage v2.0 — User & Modder Customization Guide

`BodyLanguage` is fully data-driven. All body ratio cutoffs, descriptive text phrases, height tiers, normal-map muscularity baselines, and *The New Gentleman* (TNG) addon descriptions live in plain-text JSON and INI files that you can edit without recompiling the SKSE plugin.

---

## 1. File Locations Overview

All configuration files are located inside your Skyrim `Data/` directory (or inside the mod's folder in Mod Organizer 2 / Vortex):

| File Path (relative to `Data/`) | Purpose |
| :--- | :--- |
| [`SKSE/Plugins/BodyLanguage/geometryThresholds.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage/geometryThresholds.json) | Numeric anthropometric ratio thresholds that classify 3D body measurements into tiers (`slim`, `average`, `large`, `muscular`, etc.). |
| [`SKSE/Plugins/BodyLanguage/bodyConfigs/FemaleParts.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage/bodyConfigs/FemaleParts.json) | Descriptive text phrases for each female body region and tier (`clothed` and `naked`). |
| [`SKSE/Plugins/BodyLanguage/bodyConfigs/MaleParts.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage/bodyConfigs/MaleParts.json) | Descriptive text phrases for each male body region and tier (`clothed` and `naked`). |
| [`SKSE/Plugins/BodyLanguage.ini`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage.ini) | Normal-map (`_msn.dds`) roughness baselines for smooth vs. muscular/ripped skin textures. |
| [`SKSE/Plugins/SkyrimNet/jsonData/heightRanges.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/SkyrimNet/jsonData/heightRanges.json) | Actor height scale thresholds (`towering`, `tall`, `above_average`, `average`, `short`, `diminutive`). |
| [`SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json) | Visual descriptions for *The New Gentleman* (TNG) racial/custom genital addons when an actor is naked. |
| [`SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/character_bio/0401_appearance_body_shape.prompt`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/character_bio/0401_appearance_body_shape.prompt) | SkyrimNet Jinja/prompt template that combines height, clothing state, body description, and TNG description. |

---

## 2. How to Tune Thresholds Using `BodyLanguage.log`

Whenever SkyrimNet requests a description for an NPC, `BodyLanguage` writes the exact real-world measurements (`cm` and `inches`) and dimensionless ratios for that NPC into:
`Documents\My Games\Skyrim Special Edition\SKSE\BodyLanguage.log`

Example log output:
```text
[ActorBodyEvaluator] Measurements 'Serana' (02002B74) [scale=1.03]: shoulderWidth=49.5cm(19.5") chest=93.9cm(36.9") underbust=79.2cm(31.2") waist=78.7cm(31.0") belly=101.4cm(39.9") hips=110.9cm(43.7") thigh=60.8cm(23.9") calf=42.3cm(16.6") arm=30.3cm(11.9")
[ActorBodyEvaluator] Evaluated 'Serana' (02002B74) [sex=F w=75 naked=false ...]: ratios=[mass=0.48 shldr=0.33 vtaper=1.89 whr=0.71 bustCup=1.18 hips=0.73 thigh=0.40 arm=0.20 bellyDrop=-0.002] S_geo_tone=-0.035 S_preg=0.085
[PhysiqueClassifier] Female classification (I_tone=0.069, ctx='clothed'): frame='average' breasts='large' waist='standard' belly='flat' butt_hips='large' legs='thick' arms='average' condition='curvy'
```

To customize how any character is classified:
1. Check their `ratios=[...]` line in `BodyLanguage.log`.
2. Compare those numbers against [`geometryThresholds.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage/geometryThresholds.json).
3. Lower a `*_max` threshold if you want characters to reach the next larger tier sooner, or raise a `*_max` threshold if you want more characters to stay in the smaller tier.

---

## 3. Body Ratio Thresholds (`geometryThresholds.json`)

[`SKSE/Plugins/BodyLanguage/geometryThresholds.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage/geometryThresholds.json) is split into `"female"` and `"male"` sections. Every ratio is **body-type agnostic** (computed directly from the 3D mesh relative to neck-to-ankle height $H_0$).

### 3.1 Female Thresholds (`"female"`)

| JSON Group | Log Metric (`ratios=[...]`) | Formula | Keys & Default Cutoffs | Resulting Tier in `FemaleParts.json` |
| :--- | :--- | :--- | :--- | :--- |
| **`shoulder_breadth`** | `shldr` | $\frac{\text{Shoulder Width}}{\text{Height } H_0}$ | `small_max`: `0.29`<br>`slim_max`: `0.31`<br>`average_max`: `0.34`<br>`broad_max`: `0.37` | `< 0.29` $\rightarrow$ `small`<br>`< 0.31` $\rightarrow$ `slim`<br>`< 0.34` $\rightarrow$ `average`<br>`< 0.37` $\rightarrow$ `broad`<br>`>= 0.37` $\rightarrow$ `massive` |
| **`chest_prominence`** | `bustCup` | $\frac{\text{Chest Girth}}{\text{Underbust Girth}}$ | `flat_max`: `1.06`<br>`small_max`: `1.11`<br>`average_max`: `1.17`<br>`large_max`: `1.24`<br>`huge_max`: `1.34` | `< 1.06` $\rightarrow$ `flat`<br>`< 1.11` $\rightarrow$ `small`<br>`< 1.17` $\rightarrow$ `average`<br>`< 1.24` $\rightarrow$ `large`<br>`< 1.34` $\rightarrow$ `huge`<br>`>= 1.34` $\rightarrow$ `impossible` |
| **`waist_to_hip`** | `whr` | $\frac{\text{Waist Girth}}{\text{Hip Girth}}$ | `tiny_max`: `0.61`<br>`slim_max`: `0.67`<br>`standard_max`: `0.73`<br>`soft_max`: `0.79` | `< 0.61` $\rightarrow$ `tiny`<br>`< 0.67` $\rightarrow$ `slim`<br>`< 0.73` $\rightarrow$ `standard`<br>`< 0.79` $\rightarrow$ `soft`<br>`>= 0.79` $\rightarrow$ `thick` |
| **`glute_girth`** | `hips` | $\frac{\text{Hip Girth}}{\text{Height } H_0}$ | `flat_max`: `0.58`<br>`small_max`: `0.63`<br>`average_max`: `0.69`<br>`large_max`: `0.76` | `< 0.58` $\rightarrow$ `flat`<br>`< 0.63` $\rightarrow$ `small`<br>`< 0.69` $\rightarrow$ `average`<br>`< 0.76` $\rightarrow$ `large` (or `large_muscular` if toned)<br>`>= 0.76` $\rightarrow$ `huge` (or `huge_muscular` if toned) |
| **`thigh_girth`** | `thigh` | $\frac{\text{Thigh Girth}}{\text{Height } H_0}$ | `thin_max`: `0.34`<br>`lean_max`: `0.37`<br>`average_max`: `0.40`<br>`thick_max`: `0.45` | `< 0.34` $\rightarrow$ `thin`<br>`< 0.37` $\rightarrow$ `lean`<br>`< 0.40` $\rightarrow$ `average` (or `lean` if toned)<br>`< 0.45` $\rightarrow$ `thick` (or `muscular` if toned)<br>`>= 0.45` $\rightarrow$ `colossal` (or `colossal_muscular` if toned) |
| **`arm_girth`** | `arm` | $\frac{\text{Arm Girth}}{\text{Height } H_0}$ | `thin_max`: `0.18`<br>`medium_max`: `0.21`<br>`thick_max`: `0.24` | `< 0.18` $\rightarrow$ `thin` / `toned`<br>`< 0.21` $\rightarrow$ `average` / `soft` / `toned`<br>`< 0.24` $\rightarrow$ `thick` / `muscular`<br>`>= 0.24` $\rightarrow$ `thick` / `colossal` |
| **`belly_drop`** | `bellyDrop` | $\frac{Y_{\text{underbust}} - Y_{\text{belly}}}{H_0}$ | `flat_min`: `-0.006`<br>`soft_min`: `-0.022`<br>`chubby_min`: `-0.045`<br>`large_min`: `-0.075` | *(More negative = more forward belly protrusion)*<br>`>= -0.006` $\rightarrow$ `flat` / `toned` / `ripped`<br>`>= -0.022` $\rightarrow$ `soft`<br>`>= -0.045` $\rightarrow$ `chubby`<br>`>= -0.075` $\rightarrow$ `large`<br>`< -0.075` $\rightarrow$ `huge` |
| **`pregnancy_score`** | `S_preg` | Isolated anterior belly dome $[0, 1]$ | `early_min`: `0.25`<br>`mid_min`: `0.50`<br>`full_min`: `0.75` | Overrides `belly` when active:<br>`>= 0.75` $\rightarrow$ `pregnant_full`<br>`>= 0.50` $\rightarrow$ `pregnant_mid`<br>`>= 0.25` $\rightarrow$ `pregnant_early` |
| **`body_mass_index`** & **`tone_index`** | `mass` & `I_tone` | Mean normalized girth & combined geometry/texture tone $[-1, +1]$ | `very_low_max`: `0.43`<br>`medium_max`: `0.49`<br>`high_max`: `0.55`<br>Tone: `-0.15`, `0.25`, `0.55` | Combines with `whr` and `shldr` to select `physique_condition` (`emaciated`, `slender`, `wiry`, `soft`, `curvy`, `average`, `lean`, `athletic`, `plump`, `chubby`, `sturdy`, `muscular`, `obese`, `hulking`, `amazonian`). |

---

### 3.2 Male Thresholds (`"male"`)

| JSON Group | Log Metric (`ratios=[...]`) | Formula | Keys & Default Cutoffs | Resulting Tier in `MaleParts.json` |
| :--- | :--- | :--- | :--- | :--- |
| **`body_mass_index`** | `mass` | Mean of 5 normalized girths | `very_low_max`: `0.45`<br>`compact_max`: `0.49`<br>`medium_max`: `0.54`<br>`high_max`: `0.59` | Selects `stature_frame`:<br>`< 0.45` $\rightarrow$ `slight`<br>`< 0.49` $\rightarrow$ `compact`<br>`< 0.54` $\rightarrow$ `average`<br>`< 0.59` $\rightarrow$ `heavy`<br>`>= 0.59` $\rightarrow$ `massive` |
| **`shoulder_breadth`** | `shldr` | $\frac{\text{Shoulder Width}}{\text{Height } H_0}$ | `narrow_max`: `0.34`<br>`average_max`: `0.38`<br>`broad_max`: `0.41` | `< 0.34` $\rightarrow$ `narrow`<br>`< 0.38` $\rightarrow$ `average`<br>`< 0.41` $\rightarrow$ `broad`<br>`>= 0.41` $\rightarrow$ `expansive` |
| **`chest_prominence`** | `bustCup` | $\frac{\text{Chest Girth}}{\text{Underbust Girth}}$ | `flat_max`: `1.04`<br>`average_max`: `1.09`<br>`broad_max`: `1.14` | `< 1.04` $\rightarrow$ `flat`<br>`< 1.09` $\rightarrow$ `average` (or `sculpted` if ripped)<br>`< 1.14` $\rightarrow$ `broad` / `sculpted` / `heavyset`<br>`>= 1.14` $\rightarrow$ `sculpted` / `heavyset` |
| **`waist_to_hip`** & **`belly_drop`** | `whr` & `bellyDrop` | $\frac{\text{Waist Girth}}{\text{Hip Girth}}$ | `tapered_max`: `0.81`<br>`straight_max`: `0.87`<br>`thick_max`: `0.94`<br>`thick_min` (drop): `-0.045` | Selects `midsection`:<br>`S_preg >= 0.35` $\rightarrow$ `distended`<br>`whr > 0.94` or `bellyDrop < -0.045` $\rightarrow$ `potbellied`<br>`whr > 0.87` $\rightarrow$ `thick`<br>`I_tone >= 0.55` $\rightarrow$ `ripped`<br>`whr < 0.81` or `I_tone > 0.25` $\rightarrow$ `tapered`<br>Otherwise $\rightarrow$ `straight` |
| **`glute_girth`** | `hips` | $\frac{\text{Hip Girth}}{\text{Height } H_0}$ | `flat_max`: `0.58`<br>`firm_max`: `0.65`<br>`large_max`: `0.72` | `< 0.58` $\rightarrow$ `flat`<br>`< 0.65` $\rightarrow$ `firm`<br>`< 0.72` $\rightarrow$ `large`<br>`>= 0.72` $\rightarrow$ `huge_muscular` (or `huge` if low tone) |
| **`thigh_girth`** | `thigh` | $\frac{\text{Thigh Girth}}{\text{Height } H_0}$ | `spindly_max`: `0.35`<br>`athletic_max`: `0.40`<br>`pillars_max`: `0.46` | `< 0.35` $\rightarrow$ `spindly`<br>`< 0.40` $\rightarrow$ `athletic`<br>`< 0.46` $\rightarrow$ `pillars`<br>`>= 0.46` $\rightarrow$ `immense` (or `immense_soft` if low tone) |
| **`arm_girth`** | `arm` | $\frac{\text{Arm Girth}}{\text{Height } H_0}$ | `thin_max`: `0.20`<br>`athletic_max`: `0.24`<br>`brawny_max`: `0.28` | `< 0.20` $\rightarrow$ `thin`<br>`< 0.24` $\rightarrow$ `athletic`<br>`< 0.28` $\rightarrow$ `brawny`<br>`>= 0.28` $\rightarrow$ `colossal` |

---

## 4. Customizing Body Part Phrases (`FemaleParts.json` & `MaleParts.json`)

[`FemaleParts.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage/bodyConfigs/FemaleParts.json) and [`MaleParts.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage/bodyConfigs/MaleParts.json) store the exact text snippets injected into the final description for each tier. Every tier has two context keys:
- **`"clothed"`:** Used when the NPC is wearing armor or clothing in slot 32 (`kBody`). Focuses on silhouette, proportions, and how garments fit.
- **`"naked"`:** Used when the NPC is unclothed. Focuses on skin, muscle definition, and anatomy.

### How Sentences Are Assembled
Keeping your custom phrases grammatically aligned with these templates ensures smooth sentences:

- **Female `short` (clothed & naked):**
  > `{frame}, with {breasts_volume} and {butt_hips} — overall {physique_condition}`
  *(If pregnant: `{butt_hips}` is replaced by `{belly}`)*
- **Female `full` (clothed):**
  > `{frame}, with {breasts_volume} and {arms}. Her midsection shows {waist} [and {belly}], her lower body broadening into {butt_hips}. {legs} complete a silhouette that reads as {physique_condition}`
  *(Note: Clothed `{legs}` starts a new sentence, so capitalize its first letter in `FemaleParts.json`!)*
- **Female `full` (naked):**
  > `{frame}, carrying {breasts_volume} and {arms}. Her waist is {waist}, her belly {belly}. Below, she has {butt_hips} and {legs}, altogether {physique_condition}`
- **Male `short` (clothed):**
  > `{shoulder_breadth} framing {stature_frame}, with {midsection} — overall {physique_condition}`
- **Male `short` (naked):**
  > `{shoulder_breadth} and {stature_frame}, with {arms} — overall {physique_condition}`
- **Male `full` (clothed):**
  > `{shoulder_breadth} framing {stature_frame} and {chest}. Beneath his clothing, he has {midsection} and {arms}. His lower body shows {legs} and {glutes}, the whole figure reading as {physique_condition}`
- **Male `full` (naked):**
  > `{shoulder_breadth} and {stature_frame}, fronted by {chest} and defined by {midsection}. His build features {arms}, {legs}, and {glutes}, altogether {physique_condition}`

---

## 5. Normal Map Muscularity Baselines (`BodyLanguage.ini`)

[`SKSE/Plugins/BodyLanguage.ini`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage.ini) calibrates how normal maps (`_msn.dds`, including Racial Skin Variance / RSV textures) contribute to muscle tone (`S_tex_tone` in $[0.0, 1.0]$):

```ini
[NormalMapAnalysis]
SmoothBaseline=0.018
RippedBaseline=0.045
```

- Check `[NormalMapAnalyzer]` in `BodyLanguage.log` to see the `combined=` roughness energy of any NPC's normal map:
  ```text
  [NormalMapAnalyzer] Decoded 'textures\actors\character\rsv\nordfemale\femalebody_9_msn.dds' ... combined=0.02740 -> S_tex_tone=0.348
  ```
- **`SmoothBaseline` (default `0.018`):** Any normal map with `combined` energy at or below this value gets `S_tex_tone = 0.0` (completely smooth skin).
- **`RippedBaseline` (default `0.045`):** Any normal map with `combined` energy at or above this value gets `S_tex_tone = 1.0` (deeply carved abs/deltoids).
- If your skin texture overhaul has softer normal maps and athletic NPCs aren't registering as toned/ripped, **lower `RippedBaseline`** (e.g., from `0.045` to `0.035`).

---

## 6. Height Thresholds (`heightRanges.json`)

[`SKSE/Plugins/SkyrimNet/jsonData/heightRanges.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/SkyrimNet/jsonData/heightRanges.json) controls how an NPC's effective height (`npcData.height`, where `1.00` is standard human height) maps to height descriptions in the SkyrimNet prompt:

```json
{
    "towering": 1.12,
    "tall": 1.08,
    "above_average": 1.04,
    "average": 0.99,
    "short": 0.95,
    "diminutive": 0.90
}
```

| Condition | Default Threshold | Approximate Real Height | Prompt Phrase |
| :--- | :--- | :--- | :--- |
| `npcHeight >= towering` | `>= 1.12` | $\ge 6'5"$ ($196\text{ cm}$) | *"Standing at a striking, towering height, "* |
| `npcHeight >= tall` | `>= 1.08` | $6'3" - 6'5"$ ($190 - 195\text{ cm}$) | *"Standing notably tall, "* |
| `npcHeight >= above_average` | `>= 1.04` | $6'0" - 6'2"$ ($183 - 189\text{ cm}$) | *"Carrying a slightly above-average height, "* |
| `npcHeight >= average` | `>= 0.99` | $5'9" - 6'0"$ ($175 - 182\text{ cm}$) | *"Standing at an average height, "* |
| `npcHeight >= short` | `>= 0.95` | $5'6" - 5'8"$ ($168 - 174\text{ cm}$) | *"Standing on the shorter side, "* |
| `npcHeight < short` | `< 0.95` | $< 5'6"$ ($< 168\text{ cm}$) | *"Notably petite in stature, "* |

---

## 7. *The New Gentleman* (TNG) & *Schlongs of Skyrim* (SOS) Customization (`tngAddons.json`, `0020_genitals.prompt`, & `geometryThresholds.json`)

When an NPC with a TNG or SOS addon is **naked** (`ttlb_is_naked == "true"`) and rendered in **full** mode, [`0020_genitals.prompt`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/body_language/0020_genitals.prompt) calls `ttlb_get_genitals(actorUUID)` (which returns `{"hasGenitals": true, "size": "XS..XL", "addon": "<addonKey>"}`) and assembles the description directly in the prompt and JSON (zero prose is hardcoded in C++):

### 7.1 TNG & SOS Addon Appearance (`tngAddons.json`)
[`SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json) maps each TNG or SOS addon name (or plugin filename) to a visual description sentence:

```json
{
    "$TNG_TCT": "is smooth and taut with a clean, fully exposed head and sharply defined coronal ridge.",
    "$TNG_TMT": "is heavily veined and densely muscled, with firm sinew visible beneath the skin and a broad, fully exposed head.",
    "$TNG_TRT": "is well-balanced with moderate veining and a naturally tapered, proportionate head.",
    "VectorPlexus Muscular": "is heavily veined and densely muscled, with firm sinew visible beneath the skin and a broad, fully exposed head."
}
```
- **Adding Custom TNG or SOS Addons:** Check `[ActorBodyEvaluator] Genitals` in `BodyLanguage.log` to see the exact `addon='...'` display name or `file='...'` `.esp` filename detected on the actor, then add a matching key in `tngAddons.json`.

### 7.2 Genital Size Tiers & Prose (`geometryThresholds.json` & `0020_genitals.prompt`)
The C++ evaluator reads the live 3D scale of the **`NPC GenitalsBase [GenBase]`** bone (which reflects TNG racial multipliers + `TNG_XS..XL` keywords or SOS scaling), falling back to `TNG_XS..XL` keywords or `SOS_SchlongifiedFaction` rank when 3D is unloaded.
- **Numeric Thresholds:** Configured under `"male" -> "genital_size"` in [`geometryThresholds.json`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/BodyLanguage/geometryThresholds.json).
- **Size Prose (`sizePart`):** Directly editable in [`0020_genitals.prompt`](file:///e:/Skyrim/development/mods/BodyLanguage%20Dev/SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/prompts/submodules/body_language/0020_genitals.prompt) without rebuilding the DLL:

| Size Tier | Default `sizeMultiplier` Cutoff | Threshold Key in `geometryThresholds.json` |
| :--- | :--- | :--- |
| `XS` | `< 0.79` | `xs_max` |
| `S` | `< 0.99` | `s_max` |
| `M` | `< 1.19` | `m_max` |
| `L` | `< 1.39` | `l_max` |
| `XL` | `>= 1.39` | `>= l_max` |

