# TT Body Language 🎭

✨ A Skyrim SKSE plugin that integrates with SkyrimNet to generate rich, context-aware body descriptions for every NPC. It reads live body data from OBody NG, enriches it with muscularity hints and skin complexion details from Racial Skin Variance, and adds height and T.N.G (The New Gentleman) details — all surfaced to SkyrimNet's prompt templates automatically.

> 💡 **Note:** The mod ships with pre-generated descriptions for popular BodySlide presets. Unknown presets are detected and their descriptions computed automatically on each game load using your installed BodySlide XML files.

## Requirements 📋

- [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin)
- [OBody NG](https://www.nexusmods.com/skyrimspecialedition/mods/77016) (tested on 4.3.7)
- [The New Gentleman](https://www.nexusmods.com/skyrimspecialedition/mods/104215) *(optional, tested on 4.2.3)*
- [Racial Skin Variance - SPID](https://www.nexusmods.com/skyrimspecialedition/mods/81668) *(optional)*
  - [BnP - RSV patch](https://www.nexusmods.com/skyrimspecialedition/mods/102667)
  - **ATTENTION**: You need to configure `SKSE\Plugins\BodyLanguage.ini` to provide pathes to RSV and BnP original archives you downloaded from Nexus Mods!!!

## Features ✨

- 🔄 **Automatic preset scanning** — on every game load the plugin reads your BodySlide XML preset files and computes body keyword descriptions for every installed preset. No manual steps required.
- 🎯 **Context-aware descriptions** — different wording is used depending on whether an actor is clothed or naked, and whether the description is full (for bios) or short (for inline dialogue).
- 👗 **Supported body types:** BHUNP 💃, CBBE 3BA 👒, HIMBO 💪
- 💪 **RSV muscularity detection** — if Racial Skin Variance is installed, the active body normal map is identified and used to adjust the physique keyword (e.g. a `boxer` normal map shifts the description toward muscular).
- 🎨 **RSV skin complexion detection** — when the BnP RSV diffuse patch is installed, the active body diffuse texture is identified and a complexion sentence is prepended to the naked full-body description (e.g. frost-nip, freckles, scars, veins, tanlines).
- 📏 **Height descriptions** — each actor's height multiplier is mapped to a tier (towering / tall / above average / average / short / diminutive) with configurable thresholds.
- 🔞 **TNG genital descriptions** — when The New Gentleman is active and the actor is unclothed, a size + anatomy descriptor is appended to the prompt. Racial size multipliers are applied automatically.

## How Descriptions Reach SkyrimNet 🤖

The plugin registers two decorator functions with SkyrimNet's public API:

| Decorator | Used when |
|---|---|
| `get_full_body_description` | NPC bio, full dialogue context |
| `get_short_body_description` | Short inline references |

Descriptions appear in these contexts:
- 📝 Whenever an NPC's bio is requested
- 💬 During dialogues for the speaking NPC and the dialogue target

## Integrations 🔌

For full technical detail on every integration see **[docs/Integration_Guide.md](./docs/Integration_Guide.md)**.

### OBody NG

The SKSE plugin connects to OBody NG at startup via its public API to read the preset name and naked/clothed state of any actor on demand. At `kDataLoaded` it also scans all BodySlide XML preset files and computes weighted goal scores (breast volume, waist, physique condition, etc.) using the slider impact tables in `SKSE/Plugins/BodyLanguage/bodyConfigs/`. The results are written to `SKSE/Plugins/SkyrimNet/jsonData/descriptions/` and used as the lookup table at runtime.

### Racial Skin Variance (RSV) 💪🎨

Two separate background scans run on startup from the same `.7z` archives configured in `BodyLanguage.ini`:

**Normal map scan** — builds `rsv_normal_map_hashes.json`. At description time, the actor's active body normal map is hashed and the match used to adjust the `physique_condition` keyword (e.g. `vigorexia` → two steps toward muscular).

**Diffuse scan** — builds `rsv_diffuse_hashes.json`. At naked full-body description time, the actor's active body diffuse texture is hashed and the match looked up in `rsvSkinComplexions.json` to produce a complexion sentence such as *"She has faint patches of frostnip..."* that is prepended to the description. Only BnP RSV variant diffuse textures (freckles, scars, veins, tanlines, etc.) produce a match — the default unmarked textures are intentionally excluded.

Configure the archive paths in `Data/SKSE/Plugins/BodyLanguage.ini`:

```ini
[RSV]
RSV_Main_ArchivePath=C:\path\to\Racial Skin Variance - SPID 4K Version.7z
BNP_RSV__Female_ArchivePath=C:\path\to\BnP female RSV (CBBE) 4k.7z
BNP_RSV__Male_ArchivePath=C:\path\to\BnP male RSV (SOS full) 4k.7z
```

Leave blank or remove any key to skip that archive. Complexion sentences can be customised by editing `SKSE/Plugins/BodyLanguage/rsvSkinComplexions.json`.

### Height 📏

Height is handled in the SkyrimNet prompt template. The actor's height multiplier is compared against the thresholds in `SKSE/Plugins/SkyrimNet/jsonData/heightRanges.json`:

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

Edit these values to adjust what counts as tall or short for your load order. Changes take effect on the next game load.

### T.N.G (The New Gentleman) 🔞

A Papyrus quest script reads TNG's racial group data and addon lists on every load and caches them via `StorageUtil`. The prompt template then checks TNG size keywords (`TNG_XS` through `TNG_XL`), applies the racial size multiplier, and looks up the addon's anatomy description in `SKSE/Plugins/SkyrimNet/jsonData/tngAddons.json`. Descriptions only appear when the actor is unclothed.

To add descriptions for additional TNG addons, open `tngAddons.json` and add an entry whose key matches the addon name exactly as shown in TNG's MCM:

```json
{
    "My Custom Addon": "smooth, slender shaft with subtle veining and a well-defined tapered head."
}
```

The description completes the sentence: *"It has a [description]"*

## Manual Overrides ✍️

If the auto-computed description for a preset is inaccurate, you can override it without touching any generated file. Create a sidecar file:

```
SKSE/Plugins/SkyrimNet/jsonData/descriptions/<bodytype>_manual.json
```

Example (`3ba_manual.json`):

```json
{
    "My Custom Preset": {
        "big":    { "meta": "breasts_volume:huge;physique_condition:soft;butt_hips:large;frame:average;legs:thick;waist:standard" },
        "medium": { "meta": "breasts_volume:large;physique_condition:lean;butt_hips:average;frame:slim;legs:average;waist:standard" },
        "small":  { "meta": "breasts_volume:average;physique_condition:lean;butt_hips:average;frame:slim;legs:lean;waist:slim" }
    }
}
```

Manual entries always win over auto-computed ones and survive regeneration on reload. 🏆

See `SKSE/Plugins/SkyrimNet/jsonData/descriptions/Example_3ba_manual.json` for a working example.

## Documentation 📖

- **[Integration Guide](./docs/Integration_Guide.md)** — full technical reference for all six integrations, config files, and manual overrides
- [BHUNP Body Presets](./docs/BHUNP_descriptions.md)
- [CBBE 3BA Body Presets](./docs/CBBE_3BA_descriptions.md)
- [HIMBO Body Presets](./docs/HIMBO_descriptions.md)
