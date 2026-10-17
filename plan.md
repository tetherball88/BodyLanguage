# BodyLanguage v2.0 — Step-by-Step Implementation Plan (Flash Execution Guide)

> **MANDATORY AGENT RULES (READ BEFORE EVERY STEP):**
> 1. **DO NOT TRY TO BUILD C++:** Never run `cmake`, `msbuild`, `ninja`, `cl.exe`, `vcpkg`, or any build/compile command. The user compiles C++ externally.
> 2. **DO NOT LEAVE OUTDATED OR DEPRECATED CODE:** Delete all v1 files listed in Step 1 completely. Do not leave commented-out v1 code, `#if 0` blocks, unused headers, or legacy fallback paths.
> 3. **C++23 & `.clang-format` CONVENTIONS:**
>    - Every `.cpp` file MUST include `"PCH.h"` first.
>    - Use 4 spaces for indentation (no tabs), 120-character line limit, `AccessModifierOffset: -4`, and `namespace BodyLanguage`.
>    - Read all game assets (`.nif`, `.tri`, `.dds`) via `RE::BSResourceNiBinaryStream` using Data-relative paths (`"meshes\\..."`, `"textures\\..."`).
>    - Protect shared caches with `std::shared_mutex`.

---

## Step 1: Remove All Deprecated v1 Files & Clean Up Build Configs (COMPLETED)

- Deleted obsolete v1 C++ sources (`OBodyService.*`, `SliderPresetScanner.*`, `RsvNormalMapScanner.*`, `RsvDiffuseScanner.*`, `HashUtils.*`, `extern/OBody-API/`) and JSON caches (`rsv_*.json`, `rsvSkinComplexions.json`, `bodyConfigs/{3BA,bhunp,himbo}`, `descriptions/`).
- Updated `vcpkg.json` and `CMakeLists.txt` to v2.0.0.

---

## Step 2: Ready-to-Use External Packages & SKEE Header (COMPLETED)

Instead of writing custom NIF binary parsers or BC1–BC7 texture decompressors from scratch, the project uses these ready-to-use packages:
1. **`nifly` (`SKSE_Source/extern/nifly`):** Official C++ NIF library by ousnius (`#include <NifFile.hpp>`), linked as `nifly` in `CMakeLists.txt`.
2. **`DirectXTex` (`vcpkg.json` -> `"directxtex"`):** Official Microsoft DDS parser and BC1–BC7 decompressor (`#include <DirectXTex.h>`), linked as `Microsoft::DirectXTex`.
3. **`zlib` (`vcpkg.json` -> `"zlib"`):** Decompresses `"PIRT"` (zlib-compressed RaceMenu `.tri` files) into `"TRIP"` buffers (`#include <zlib.h>`), linked as `ZLIB::ZLIB`.
4. **`SKEEInterface.h` (`SKSE_Source/include/external/SKEEInterface.h`):** Official RaceMenu `skee64.dll` `IBodyMorphInterface` vtable header.

---

## Step 3: Create Core Geometry Data Structures (`SKSE_Source/src/Geometry/BodyTemplateCache.h`)

Create `SKSE_Source/src/Geometry/BodyTemplateCache.h` defining all shared data structures for Phases 1–3:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "RE/Skyrim.h"

namespace BodyLanguage {

    enum class AnatomicalRingId : std::size_t {
        kNeck = 0,          // Ring 0: Neck / Base (Reference Z_top)
        kShoulders = 1,     // Ring 1: Shoulders / Deltoids (Deltoid UV top)
        kChestBust = 2,     // Ring 2: Upper Chest / Bust Peak (Abdomen UV top)
        kUnderbust = 3,     // Ring 3: Underbust / Ribcage (Abdomen UV sternum)
        kNaturalWaist = 4,  // Ring 4: Natural Waist (Abdomen UV left/right)
        kMidBelly = 5,      // Ring 5: Mid-Belly / Flanks (Abdomen UV center)
        kHipsGlutes = 6,    // Ring 6: Hips / Glute Peak (Abdomen UV bottom)
        kUpperThigh = 7,    // Ring 7: Upper Thigh (Left femur upper 1/3)
        kCalf = 8,          // Ring 8: Calf (Left calf max cross-section)
        kUpperArm = 9,      // Ring 9: Upper Arm (Left bicep/tricep midpoint, Deltoid UV bottom)
        kCount = 10
    };

    enum class CompassDir : std::size_t {
        kRight = 0,       //   0 deg (+X)
        kFrontRight = 1,  //  45 deg (+X, +Y)
        kFront = 2,       //  90 deg (+Y Anterior)
        kFrontLeft = 3,   // 135 deg (-X, +Y)
        kLeft = 4,        // 180 deg (-X)
        kBackLeft = 5,    // 225 deg (-X, -Y)
        kBack = 6,        // 270 deg (-Y Posterior)
        kBackRight = 7,   // 315 deg (+X, -Y)
        kPointsPerRing = 8
    };

    inline constexpr std::size_t kRingCount = static_cast<std::size_t>(AnatomicalRingId::kCount);
    inline constexpr std::size_t kPointsPerRing = static_cast<std::size_t>(CompassDir::kPointsPerRing);
    inline constexpr std::size_t kTotalLandmarkVertices = kRingCount * kPointsPerRing;  // 80

    struct OctagonRingIndices {
        std::array<std::uint16_t, kPointsPerRing> vIdx{};
        std::string primaryBone;
        std::string secondaryBone;
    };

    struct UVRect {
        float uMin{0.35f};
        float uMax{0.65f};
        float vMin{0.35f};
        float vMax{0.65f};
    };

    struct MorphDeltaEntry {
        std::array<RE::NiPoint3, kTotalLandmarkVertices> deltas{};
        bool isLocalizedBellyMorph{false};  // Section 3.5: True if Ring 5 +Y delta > 4x Ring 7 & Ring 9 radial delta
    };

    struct BodyTemplateCache {
        std::string nifPathKey;
        std::string triPath;
        std::string fallbackMsnPath;
        bool isFemale{true};
        float baseReferenceHeight{120.0f};  // Z_neck - Z_ankle in _1.nif
        std::array<OctagonRingIndices, kRingCount> rings{};
        std::array<RE::NiPoint3, kTotalLandmarkVertices> verts0{};  // _0.nif
        std::array<RE::NiPoint3, kTotalLandmarkVertices> verts1{};  // _1.nif
        UVRect abdomenUV{};
        UVRect deltoidUV{};
        std::unordered_map<std::string, MorphDeltaEntry> triDeltas;
    };

    struct RingMeasurements {
        float girth{0.0f};       // Circumference G_r
        float width{0.0f};       // Coronal X-span W_r
        float depth{0.0f};       // Sagittal Y-span D_r
        float frontProj{0.0f};   // Anterior +Y projection Y+_r
        float backProj{0.0f};    // Posterior -Y projection Y-_r
    };

    struct ActorBodyMetrics {
        bool valid{false};
        bool isFemale{true};
        bool isNaked{false};

        // Normalized linear measurements (divided by baseReferenceHeight H_0)
        std::array<RingMeasurements, kRingCount> rings{};

        // Core composite ratios & signals
        float bodyMassIndex{0.0f};         // M_body (systemic mass, decoupled from pregnancy dome)
        float vTaperRatio{1.25f};          // R_vtaper = W_1 / W_4
        float waistToHipRatio{0.75f};      // R_whr = G_4 / G_6
        float chestProminence{0.0f};       // P_chest = (G_2 - G_3) / G_3
        float chestShelfDeltaY{0.0f};      // (Y+_2 - Y+_3) / H_0
        float gluteProjection{0.0f};       // (Y-_6 - Y-_4) / H_0
        float sagittalBellyDrop{0.0f};     // (Y+_3 - Y+_5) / H_0
        float flankFlareRatio{1.0f};       // R_flank = W_5 / W_4
        float upperThighGirth{0.0f};       // G_7 / H_0
        float calfGirth{0.0f};             // G_8 / H_0
        float upperArmGirth{0.0f};         // G_9 / H_0

        // Tone & Pregnancy signals
        float geoToneSignal{0.0f};         // S_geo_tone in [-1.0, +1.0] (systemic, decoupled from pregnancy)
        float morphKeywordSignal{0.0f};    // S_morph_keywords in [-1.0, +1.0]
        float pregnancyBellyScore{0.0f};   // S_preg in [0.0, 1.0] (Section 3.5 localized anterior belly dome)

        // Texture analysis inputs
        std::string activeMsnPath;
        UVRect abdomenUV{};
        UVRect deltoidUV{};
    };

}  // namespace BodyLanguage
```

---

## Step 4: Implement `.nif` & `.tri` Reader Using `nifly` (`SKSE_Source/src/Geometry/NifTriParser.h` & `.cpp`)

Create `SKSE_Source/src/Geometry/NifTriParser.h` and `SKSE_Source/src/Geometry/NifTriParser.cpp` using `nifly::NifFile` (`#include <NifFile.hpp>`) and `zlib` (`#include <zlib.h>`).

### 4.1 Data Structures in `NifTriParser.h`
```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Geometry/BodyTemplateCache.h"

namespace BodyLanguage {

    struct RawNifVertex {
        RE::NiPoint3 pos{};
        RE::NiPoint2 uv{};
        // Map of boneIndex -> weight for this vertex
        std::vector<std::pair<std::uint16_t, float>> boneWeights;
    };

    struct RawNifMesh {
        std::string shapeName;
        std::vector<RawNifVertex> vertices;
        std::vector<std::string> boneNames;
        std::string bodyTriPath;
        std::string normalMapPath;
        float minZ{0.0f};
        float maxZ{0.0f};
    };

    // Reads a Data-relative NIF path via RE::BSResourceNiBinaryStream into memory and parses it with nifly::NifFile.
    std::optional<RawNifMesh> ParseNifBodyMesh(std::string_view nifRelativePath);

    // Reads a Data-relative BodySlide/RaceMenu .tri path ("TRIP" or zlib-compressed "PIRT" format)
    // via RE::BSResourceNiBinaryStream, extracts morph deltas for the 80 landmark vertices in `outCache`,
    // and computes `isLocalizedBellyMorph` (Section 3.5).
    void ParseTriMorphDeltas(
        std::string_view triRelativePath,
        std::string_view targetShapeName,
        BodyTemplateCache& outCache);

}  // namespace BodyLanguage
```

### 4.2 Implementation Details for `NifTriParser.cpp`
1. **Reading VFS/BSA Files into `std::string` Buffer:**
   - Normalize relative path (`"meshes\\..."`), open `RE::BSResourceNiBinaryStream stream(normalizedPath)`.
   - Check `if (!stream.good()) return std::nullopt;`.
   - Read the entire stream into a `std::string buffer` (in 64 KB chunks using `stream.read(chunk, sizeof(chunk))`) and wrap it in `std::istringstream iss(buffer, std::ios::in | std::ios::binary)`.
2. **Parsing `.nif` with `nifly::NifFile` (`ParseNifBodyMesh`):**
   - Call `nifly::NifFile nif; if (nif.Load(iss) != 0 || !nif.IsValid()) return std::nullopt;`.
   - **Extract `"BODYTRI"` Extra Data:**
     - Iterate `nif.GetChildren<nifly::BSStringExtraData>(nif.GetRootNode(), true)` (and also check shape extra data via `shape->extraDataRefs` using `nif.GetHeader().GetBlock<nifly::BSStringExtraData>(ref)`).
     - If `extra && extra->name.get() == "BODYTRI"`, set `mesh.bodyTriPath = extra->stringData.get()`.
   - **Select Primary Body `nifly::NiShape*`:**
     - Call `auto shapes = nif.GetShapes();`.
     - Pick the skinned `NiShape*` (`shape->HasSkinInstance()`) with the largest vertex count (`nif.GetVertsForShape(shape, verts)`).
   - **Extract Vertices, UVs, Normal Map, and Bone Weights via `nifly`:**
     - `nif.GetVertsForShape(bestShape, nifVerts)` and `nif.GetUvsForShape(bestShape, nifUvs)`.
     - `nif.GetTextureSlot(bestShape, mesh.normalMapPath, 1)` (slot 1 is normal map `_msn.dds`).
     - `nif.GetShapeBoneList(bestShape, mesh.boneNames)`.
     - For each bone `b = 0 .. mesh.boneNames.size() - 1`:
       - Call `std::unordered_map<uint16_t, float> bWeights; nif.GetShapeBoneWeights(bestShape, b, bWeights);`.
       - For each `[vIdx, weight]` in `bWeights`, append `{static_cast<uint16_t>(b), weight}` to `mesh.vertices[vIdx].boneWeights`.
3. **Parsing BodySlide/RaceMenu `.tri` Files (`ParseTriMorphDeltas`):**
   - Read `.tri` file via `RE::BSResourceNiBinaryStream` into `std::vector<uint8_t> rawBytes`.
   - Check 4-byte header magic (`rawBytes.data()`):
     - If magic is `"PIRT"` (compressed RaceMenu `.tri`):
       - Read `uint32_t uncompressedSize` at offset 4 (`rawBytes[4..7]`).
       - Allocate `std::vector<uint8_t> decompressed(uncompressedSize)` and call `uLongf destLen = uncompressedSize; uncompress(decompressed.data(), &destLen, rawBytes.data() + 8, static_cast<uLong>(rawBytes.size() - 8));`.
       - Replace `rawBytes = std::move(decompressed)`.
     - Verify `rawBytes` starts with `"TRIP"` (4 bytes).
   - Parse `"TRIP"` binary stream (matching BodySlide's `TriFile::Read`):
     - Build a reverse lookup table `vertexToLandmarkSlots`: for each landmark slot `L = 0 .. 79`, map `meshVertexIdx -> std::vector<uint8_t>{L}` so sparse `.tri` vertex offsets map to our 80 landmarks in $O(1)$ time!
     - Read `uint16_t shapeCount` at offset 4.
     - Loop `i = 0 .. shapeCount - 1`:
       - Read `uint8_t shapeLength`, then `shapeName` (`shapeLength` chars).
       - Read `uint16_t morphCount`.
       - Check if `shapeName` matches `targetShapeName` (or if `shapeCount == 1` or `outCache.triDeltas.empty()`).
       - Loop `j = 0 .. morphCount - 1`:
         - Read `uint8_t morphLength`, then `morphName` (`morphLength` chars).
         - Read `float mult` (4 bytes) and `uint16_t morphVertCount` (2 bytes).
         - Create `MorphDeltaEntry entry{}`:
           - Loop `k = 0 .. morphVertCount - 1`:
             - Read `uint16_t id`, `int16_t x`, `int16_t y`, `int16_t z` (8 bytes total).
             - If `id` is one of the 80 landmark vertices, set `entry.deltas[slot] = RE::NiPoint3(x * mult, y * mult, z * mult)`.
         - **Section 3.5 Startup `.tri` Spatial Footprint Tagging:**
           - Compute forward $+Y$ displacement on Ring 5 (Mid-Belly front points: indices `5 * 8 + 1`, `5 * 8 + 2`, `5 * 8 + 3`):
             $$\Delta Y^{+}_5 = \max\Big(0.0\text{f}, \frac{1}{3}\sum_{c \in \{1,2,3\}} \text{entry.deltas}[5 \times 8 + c].y\Big)$$
           - Compute average radial displacement on Ring 7 (Upper Thigh, `7 * 8 + 0..7`) and Ring 9 (Upper Arm, `9 * 8 + 0..7`):
             $$\Delta R_7 = \frac{1}{8}\sum_{c=0}^7 \sqrt{\text{entry.deltas}[7 \times 8 + c].x^2 + \text{entry.deltas}[7 \times 8 + c].y^2}$$
             $$\Delta R_9 = \frac{1}{8}\sum_{c=0}^7 \sqrt{\text{entry.deltas}[9 \times 8 + c].x^2 + \text{entry.deltas}[9 \times 8 + c].z^2}$$
           - Check if lowercase `morphName` contains `"preg"`, `"fert"`, `"belly"`, or `"infl"`.
           - Set `entry.isLocalizedBellyMorph = (deltaY5 > 0.5f && deltaY5 > 4.0f * std::max(deltaR7, deltaR9)) || hasPregNameKeyword`.
         - Store `outCache.triDeltas[morphName] = entry`.

---

## Step 5: Implement Startup 10-Ring Auto-Slicer (`SKSE_Source/src/Geometry/NifRingIndexer.h` & `.cpp`)

Create `SKSE_Source/src/Geometry/NifRingIndexer.h` and `SKSE_Source/src/Geometry/NifRingIndexer.cpp`.

### 5.1 Public Interface (`NifRingIndexer.h`)
```cpp
#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Geometry/BodyTemplateCache.h"

namespace BodyLanguage {

    class NifRingIndexer {
    public:
        static NifRingIndexer& GetInstance();

        // Called at kDataLoaded: scans all playable/humanoid TESObjectARMA records for slot 32 (kBody).
        void IndexAllBodyTemplates();

        // Thread-safe lookup or on-demand indexing of a naked body NIF path.
        std::shared_ptr<const BodyTemplateCache> GetOrIndexTemplate(
            std::string_view nifModelPath,
            bool isFemale);

    private:
        NifRingIndexer() = default;

        std::shared_ptr<const BodyTemplateCache> BuildTemplateCache(
            std::string_view keyPath,
            bool isFemale);

        mutable std::shared_mutex m_cacheMutex;
        std::unordered_map<std::string, std::shared_ptr<const BodyTemplateCache>> m_cache;
    };

}  // namespace BodyLanguage
```

### 5.2 Auto-Slicing Algorithm (`NifRingIndexer.cpp`)
1. **Resolving `_0.nif` and `_1.nif` Pairs:**
   - Given a model path from `TESObjectARMA::bipedModels[sex].model`, derive both `_0.nif` and `_1.nif` paths (if one is missing, fallback to the other).
2. **Establishing Vertical Reference Axis ($H_0 = Z_{\text{neck}} - Z_{\text{ankle}}$):**
   - In `_1.nif`, `baseReferenceHeight = std::max(10.0f, mesh1.maxZ - mesh1.minZ)`.
   - Normalized height $z_{\text{norm}}(v) = (v.\text{pos}.z - \text{mesh1.minZ}) / \text{baseReferenceHeight} \in [0.0, 1.0]$.
3. **10-Ring Bone Weight & $Z$-Band Slicing:**
   - Helper `GetBoneWeight(vertex, boneNames, substring)` sums weights of bones whose name contains `substring` (case-insensitive).
   - Slice the 10 rings on `_1.nif` (with spatial coordinate fallbacks if a custom mesh lacks standard bone names):
     - **Ring 0 (`kNeck`):** `z_norm` in `[0.92, 1.00]`, central $|x| < 10.0\text{f}$; primaryBone = `"NPC Neck [Neck]"`.
     - **Ring 1 (`kShoulders`):** `z_norm` in `[0.82, 0.93]`, includes `"Clavicle"`, `"Spine2"`, and upper `"UpperArm"`; primaryBone = `"NPC L Clavicle [LClv]"`, secondaryBone = `"NPC R Clavicle [RClv]"`.
     - **Ring 2 (`kChestBust`):** `z_norm` in `[0.70, 0.84]` where anterior $+Y$ projection is maximal; exclude arm weights (`"UpperArm"`, `"Forearm"`, `"Hand"` weight $< 0.20$). PrimaryBone = `"NPC Spine2 [Spn2]"`, secondaryBone = `isFemale ? "NPC L Breast" : "NPC Spine2 [Spn2]"`.
     - **Ring 3 (`kUnderbust`):** `z_norm` in `[0.63, 0.71]` (`Spine2` / `Spine1`, excluding `"Breast"` weight $> 0.25$ and arm weights $> 0.15$). PrimaryBone = `"NPC Spine2 [Spn2]"`.
     - **Ring 4 (`kNaturalWaist`):** Find horizontal slice in `z_norm` `[0.53, 0.63]` (`Spine1`) with minimum coronal width ($X$-span), excluding arm weights. PrimaryBone = `"NPC Spine1 [Spn1]"`.
     - **Ring 5 (`kMidBelly`):** `z_norm` in `[0.43, 0.52]` (`Spine [Spn0]` + `NPC Belly`), excluding arm weights. PrimaryBone = `"NPC Spine [Spn0]"`, secondaryBone = `"NPC Belly"`.
     - **Ring 6 (`kHipsGlutes`):** `z_norm` in `[0.32, 0.43]` (`Pelvis` + `Butt`) at maximum backward ($-Y$) glute projection. PrimaryBone = `"NPC Pelvis [Pelv]"`, secondaryBone = `"NPC L Butt"`.
     - **Ring 7 (`kUpperThigh`):** Left leg ($x < 0$ or `"L Thigh"` weight $> 0.50$) in `z_norm` `[0.24, 0.32]`. PrimaryBone = `"NPC L Thigh [LThg]"`.
     - **Ring 8 (`kCalf`):** Left lower leg ($x < 0$ or `"L Calf"` weight $> 0.50$) in `z_norm` `[0.08, 0.18]`. PrimaryBone = `"NPC L Calf [LClf]"`.
     - **Ring 9 (`kUpperArm`):** Left upper arm (`"L UpperArm"` weight $> 0.50$ or $x < -14.0\text{f}$ in `z_norm` `[0.72, 0.84]`). Measure cross-section around the arm centroid. PrimaryBone = `"NPC L UpperArm [LUar]"`.
4. **8-Point Compass Selection & UV Bounding Boxes:**
   - For each ring, compute its 2D centroid $(C_x, C_y)$ and select the 8 outer boundary vertices at compass angles $\theta_k = k \cdot \frac{\pi}{4}$ ($k = 0 \dots 7$).
   - Copy their `_0.nif` and `_1.nif` positions into `verts0` and `verts1`.
   - Compute `abdomenUV` from front vertices of Rings 2–6 and `deltoidUV` from lateral vertices of Rings 1 and 9.
   - Call `ParseTriMorphDeltas` if `bodyTriPath` is non-empty (or try replacing `.nif` with `.tri`).

---

## Step 6: Implement Runtime 3D Reconstruction & Pregnancy-Decoupled Metrics (`SKSE_Source/src/Geometry/ActorBodyEvaluator.h` & `.cpp`)

Create `SKSE_Source/src/Geometry/ActorBodyEvaluator.h` and `SKSE_Source/src/Geometry/ActorBodyEvaluator.cpp`.

### 6.1 Public Interface (`ActorBodyEvaluator.h`)
```cpp
#pragma once

#include "Geometry/BodyTemplateCache.h"
#include "external/SKEEInterface.h"

namespace BodyLanguage {

    class ActorBodyEvaluator {
    public:
        static ActorBodyEvaluator& GetInstance();

        // Call at kPostPostLoad to acquire SKEE::IBodyMorphInterface from skee64.dll.
        void InitSKEE();

        // Returns true if the actor is not wearing armor in slot 32 (kBody) or slot 49 (kModChestPrimary).
        bool IsActorNaked(RE::Actor* actor) const;

        // Evaluates the actor's 80 naked landmark vertices, SKEE morphs, XPMSE bone scales,
        // and computes normalized geometric ratios + decoupled pregnancy belly score.
        ActorBodyMetrics EvaluateActorBody(RE::Actor* actor) const;

    private:
        ActorBodyEvaluator() = default;
        SKEE::IBodyMorphInterface* m_morphInterface{nullptr};
    };

}  // namespace BodyLanguage
```

### 6.2 Reconstruction & Metric Formulas (`ActorBodyEvaluator.cpp`)
1. **Resolve True Naked Skin (`TESObjectARMA`):**
   - Check `actor->GetSkin()`, then `actor->GetActorBase()->skin`, then `actor->GetRace()->skin`.
   - Find the `TESObjectARMA*` in `skinOutfit->armorAddons` valid for the actor's race and occupying `kBody` (Slot 32).
   - Call `NifRingIndexer::GetInstance().GetOrIndexTemplate(modelPath, isFemale)`.
2. **Weight Interpolation + SKEE Morphs + Bone Scales:**
   - Interpolate `verts0` and `verts1` by $w = \text{std::clamp}(actor->GetWeight() / 100.0\text{f}, 0.0\text{f}, 1.0\text{f})$.
   - Maintain a systemic copy of Ring 5 (`systemicRing5[8]`) alongside deformed `verts[80]`.
   - Query `m_morphInterface`:
     - Use `VisitMorphs` + `GetBodyMorphs(actor, morphName)` (which returns the net sum of all keys for `morphName`).
     - Apply `val * entry.deltas[i]` to `verts[i]`.
     - **Section 3.5 Pregnancy Morph Isolation:** If `entry.isLocalizedBellyMorph` is `true` and `val > 0.0f`, accumulate `pregMorphForwardDisp += val * entry.deltas[5 * 8 + 2].y` and do **not** apply this morph to `systemicRing5`.
     - Accumulate non-pregnancy muscle vs. fat morph name keywords into `morphKeywordSignal` $\in [-1.0, +1.0]$.
   - Query `actor->Get3D(false)` for XPMSE `NiNode` bone scales:
     - Read `NPC Belly` node scale (`bellyNodeScale`) separately and apply it to `verts` Ring 5 $+Y$, leaving `systemicRing5` unscaled by `NPC Belly`.
     - Apply primary/secondary bone scales (`NPC L/R Clavicle`, `NPC L Breast`, `NPC L Butt`, `NPC L Thigh [LThg]`, `NPC L Calf [LClf]`, `NPC L UpperArm [LUar]`) to radial/directional offsets around each ring's centroid.
3. **Decoupled Pregnancy & Systemic Build Scoring (§3.5):**
   - Normalize all linear ring measurements ($G_r, W_r, D_r, Y^{+}_r, Y^{-}_r$) by $H_0 = \text{baseReferenceHeight}$.
   - Compute `pregnancyBellyScore` ($S_{\text{preg}} \in [0.0, 1.0]$) from `pregMorphForwardDisp / H_0`, `bellyNodeScale - 1.05f`, and Ring 5 sagittal-to-coronal dome eccentricity ($\Delta Y_{\text{dome}} = Y^{+}_5 - Y^{+}_3 > 0.025\text{f}$ with $D_5 / W_5 > 0.82\text{f}$ and systemic flank flare $W_{5,\text{sys}} / W_4 < 1.16\text{f}$).
   - Compute systemic `bodyMassIndex` ($M_{\text{body}}$) from $G_3, G_4, G_6, G_7, G_9$ and systemic `geoToneSignal` ($S_{\text{geo\_tone}}$) using `systemicRing5` so pregnancy never misclassifies a slender/athletic NPC as overweight.

---

## Step 7: Implement On-The-Fly `_msn.dds` Roughness Analyzer Using `DirectXTex` (`SKSE_Source/src/Texture/NormalMapAnalyzer.h` & `.cpp`)

Create `SKSE_Source/src/Texture/NormalMapAnalyzer.h` and `SKSE_Source/src/Texture/NormalMapAnalyzer.cpp` using `<DirectXTex.h>`.

### 7.1 Public Interface (`NormalMapAnalyzer.h`)
```cpp
#pragma once

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Geometry/BodyTemplateCache.h"

namespace BodyLanguage {

    class NormalMapAnalyzer {
    public:
        static NormalMapAnalyzer& GetInstance();

        void LoadConfig(const std::filesystem::path& dataRoot);

        std::string ResolveActiveNormalMapPath(
            RE::Actor* actor,
            RE::TESObjectARMA* nakedArma,
            bool isFemale,
            std::string_view fallbackNifMsn) const;

        float EvaluateNormalMapTone(
            std::string_view msnPath,
            const UVRect& abdomenUV,
            const UVRect& deltoidUV);

    private:
        NormalMapAnalyzer() = default;

        float m_smoothBaseline{0.008f};
        float m_rippedBaseline{0.042f};
        mutable std::shared_mutex m_cacheMutex;
        std::unordered_map<std::string, float> m_scoreCache;
    };

}  // namespace BodyLanguage
```

### 7.2 `DirectXTex` Mipmap Decoding & 2D Laplacian Filter (`NormalMapAnalyzer.cpp`)
1. **Check Cache & Read `.dds` Bytes via `RE::BSResourceNiBinaryStream`:**
   - Normalize `msnPath` (lowercase, backslash, `"textures\\"` prefix).
   - Check `m_scoreCache` under `std::shared_lock`.
   - Read `.dds` file bytes from `RE::BSResourceNiBinaryStream` into `std::vector<uint8_t> ddsData`.
2. **Decode Target Mip Level ($256 \times 256$) with `DirectXTex`:**
   - Parse DDS metadata & images:
     ```cpp
     DirectX::TexMetadata metadata{};
     DirectX::ScratchImage scratchImg;
     HRESULT hr = DirectX::LoadFromDDSMemory(
         ddsData.data(), ddsData.size(), DirectX::DDS_FLAGS_NONE, &metadata, scratchImg);
     if (FAILED(hr) || scratchImg.GetImageCount() == 0) return 0.25f;
     ```
   - Find the mip level `mipLevel` whose $\min(\text{width}, \text{height})$ is closest to `256` (e.g. walk `0 .. metadata.mipLevels - 1` until `width <= 256 || height <= 256`).
   - Retrieve `const DirectX::Image* img = scratchImg.GetImage(mipLevel, 0, 0);`.
   - Decompress or convert that single mip image to `DXGI_FORMAT_R8G8B8A8_UNORM`:
     ```cpp
     DirectX::ScratchImage decompressed;
     if (DirectX::IsCompressed(img->format)) {
         hr = DirectX::Decompress(*img, DXGI_FORMAT_R8G8B8A8_UNORM, decompressed);
         if (FAILED(hr)) return 0.25f;
         img = decompressed.GetImage(0, 0, 0);
     } else if (img->format != DXGI_FORMAT_R8G8B8A8_UNORM && img->format != DXGI_FORMAT_B8G8R8A8_UNORM) {
         hr = DirectX::Convert(*img, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, decompressed);
         if (FAILED(hr)) return 0.25f;
         img = decompressed.GetImage(0, 0, 0);
     }
     ```
3. **2D High-Pass Laplacian Filter on `abdomenUV` and `deltoidUV`:**
   - For each UV rect (`abdomenUV` weighted $0.75$, `deltoidUV` weighted $0.25$), map `[uMin, uMax] x [vMin, vMax]` to pixel coordinates on `img->width x img->height`.
   - Convert each pixel's `R, G, B` bytes in `img->pixels` (`rowPitch = img->rowPitch`) into normalized $\vec{N}(x,y) \in [-1.0, +1.0]^3$.
   - Compute Laplacian:
     $$\vec{L}(x,y) = \vec{N}(x,y) - 0.25\text{f} \cdot \big(\vec{N}(x-1,y) + \vec{N}(x+1,y) + \vec{N}(x,y-1) + \vec{N}(x,y+1)\big)$$
     $$E_{\text{pixel}}(x,y) = \sqrt{1.2\text{f} \cdot L_x(x,y)^2 + 1.5\text{f} \cdot L_z(x,y)^2}$$
   - Normalize $\bar{E}_{\text{msn}}$ into $S_{\text{tex\_tone}} \in [0.0, 1.0]$ using `m_smoothBaseline` and `m_rippedBaseline`, cache, and return.

---

## Step 8: Create `PhysiqueClassifier` & Expand JSON / INI Configs

### 8.1 Update `SKSE/Plugins/BodyLanguage.ini`
Replace `[RSV]` in `SKSE/Plugins/BodyLanguage.ini` with:
```ini
; BodyLanguage.ini
; Place this file at: Data/SKSE/Plugins/BodyLanguage.ini

[NormalMapAnalysis]
SmoothBaseline=0.008
RippedBaseline=0.042
```

### 8.2 Create `SKSE/Plugins/BodyLanguage/geometryThresholds.json`
Create `SKSE/Plugins/BodyLanguage/geometryThresholds.json` with `female` and `male` threshold arrays for `body_mass_index`, `tone_index`, `waist_to_hip`, `v_taper`, `chest_prominence`, `glute_girth`, `thigh_girth`, `arm_girth`, `belly_drop`, and `pregnancy_score`.

### 8.3 Expand `FemaleParts.json` & `MaleParts.json`
1. In `SKSE/Plugins/BodyLanguage/bodyConfigs/FemaleParts.json`:
   - Add `"arms"` group (`thin`, `toned`, `average`, `soft`, `thick`, `muscular`, `colossal`).
   - Expand `"belly"` with `"ripped"`, `"toned"`, `"pregnant_early"`, `"pregnant_mid"`, `"pregnant_full"`.
   - Expand `"physique_condition"` with `"emaciated"`, `"slender"`, `"wiry"`, `"average"`, `"curvy"`, `"sturdy"`, `"hulking"`, `"amazonian"`.
2. In `SKSE/Plugins/BodyLanguage/bodyConfigs/MaleParts.json`:
   - Add `"chest"` group (`flat`, `average`, `broad`, `sculpted`, `heavyset`).
   - Expand `"midsection"` with `"distended"`.
   - Expand `"physique_condition"` with `"emaciated"`, `"slim"`, `"wiry"`, `"average"`, `"portly"`, `"strongman"`, `"hulking"`, `"herculean"`.

### 8.4 Implement `SKSE_Source/src/Description/PhysiqueClassifier.h` & `.cpp`
- Compute unified Tone Index:
  $$I_{\text{tone}} = \text{std::clamp}\Big(0.45\text{f} \cdot S_{\text{geo\_tone}} + 0.40\text{f} \cdot (2.0\text{f} \cdot S_{\text{tex\_tone}} - 0.5\text{f}) + 0.15\text{f} \cdot S_{\text{morph\_keywords}}, -1.0\text{f}, +1.0\text{f}\Big)$$
- Classify each anatomical region (checking `pregnancyBellyScore` first for `belly` / `midsection`) and assemble `"full"` and `"short"` sentences for `"clothed"` and `"naked"` contexts.

---

## Step 9: Wire Everything Together in `BodyDescription.h/.cpp`, `SkyrimNetIntegration.cpp`, and `plugin.cpp`

1. **`SKSE_Source/src/BodyDescription.h` & `.cpp`:**
   - Remove `ReloadRsvHashes` and `ReloadRsvDiffuseHashes`.
   - `InitBodyDescriptions(dataRoot)` loads `NormalMapAnalyzer` and `PhysiqueClassifier` configs.
   - `GetBodyDescription(actor, mode)` runs `ActorBodyEvaluator`, `NormalMapAnalyzer`, and `PhysiqueClassifier`.
2. **`SKSE_Source/src/SkyrimNetIntegration.cpp`:**
   - Replace `OBodyService` with `ActorBodyEvaluator::GetInstance().InitSKEE()` and `ActorBodyEvaluator::GetInstance().IsActorNaked(actor)`.
3. **`SKSE_Source/plugin.cpp`:**
   - Remove deleted scanner headers and detached threads.
   - Call `BodyLanguage::InitBodyDescriptions(GetDataRoot())` and `BodyLanguage::NifRingIndexer::GetInstance().IndexAllBodyTemplates()` at `kDataLoaded`.
