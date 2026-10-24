# BodyLanguage — Offline Ground-Truth & C++ DLL Verification Scripts

This directory (`utility_scripts/`) provides a two-stage offline verification and visual blueprint suite for testing **compiled C++ code** in `BodyLanguage.dll` against exact 3D triangle-slice measurements of sample `.nif` body meshes.

> **Important Architecture Rule:**
> Neither `test_body_logic.py` nor `generate_visual_report.py` duplicates C++ geometry or classification math in Python. Both load `BodyLanguage.dll` via `ctypes` (`BodyLanguage_EvaluateNifForTest`) so every test runs the real `NifTriParser.cpp`, `NifRingIndexer.cpp`, `ActorBodyEvaluator.cpp`, and `PhysiqueClassifier.cpp` code paths, plus live `SKSE/Plugins/BodyLanguage/*.json` thresholds and descriptions. Every C++ build automatically copies the self-contained `BodyLanguage.dll` into `utility_scripts/bin/BodyLanguage.dll`, so anyone who pulls the repo can run the Python scripts immediately without building C++.

---

## Quick Start (Run from Repo Root in PowerShell)

### 1. Full Pipeline (After Adding or Changing Sample `.nif` Meshes)
Run both Stage 1 (heavy 3D triangle-slice Ground Truth) and Stage 2 (C++ DLL test + HTML visual report):
```powershell
python "utility_scripts\analyze_body_samples.py"
python "utility_scripts\generate_visual_report.py"
python "utility_scripts\test_body_logic.py"
```

### 2. Fast C++ / JSON Iteration (After Rebuilding `BodyLanguage.dll` or Editing `geometryThresholds.json`)
If you did **not** add or change any sample `.nif` files, skip `analyze_body_samples.py` — `body_samples_ground_truth.json` is already cached:
```powershell
# Print CLI comparison table (~0.5 seconds):
python "utility_scripts\test_body_logic.py"

# Test a single preset by name substring:
python "utility_scripts\test_body_logic.py" --preset "pregnant"

# Regenerate the HTML visual blueprint report (~4 seconds):
python "utility_scripts\generate_visual_report.py"
```

---

## Script Reference & Command-Line Options

### 1. `analyze_body_samples.py` — Stage 1: Heavy 3D Triangle-Slice Ground Truth
Scans all `.nif` files under `utility_scripts/body samples/<body_type>/<preset_name>/`, slices all `35,904` triangles across exact anatomical planes, computes 2D convex-hull tape-measure perimeters, anterior breast projection, perpendicular upper-arm bicep slices, and saves the results to `utility_scripts/body_samples_ground_truth.json`.

```powershell
python "utility_scripts\analyze_body_samples.py" [--samples-dir <path>] [--output <json_path>]
```
- `--samples-dir`: Root folder containing sample `.nif` meshes (default: `utility_scripts/body samples`).
- `--output`: Output JSON path (default: `utility_scripts/body_samples_ground_truth.json`).

---

### 2. `test_body_logic.py` — Stage 2A: Fast CLI C++ DLL Test Runner
Loads `body_samples_ground_truth.json` and calls `BodyLanguage.dll` (`BodyLanguage_EvaluateNifForTest`) for each preset in two modes:
- **`GT`**: Exact 3D triangle-slice Ground Truth from `body_samples_ground_truth.json`.
- **`DIR` (Direct NIF Mode)**: Evaluates the preset `.nif` as its own standalone base template (`NifRingIndexer::BuildTemplateCacheFromMeshes` + `ActorBodyEvaluator::ComputeMetricsFromVertices`).
- **`RTM` (Runtime Morph Mode)**: Indexes `3ba/cbbe vanilla/femalebody_1.nif` as the base template and evaluates the target preset's vertices through those cached rings (matching how runtime `.tri` morph sliders are evaluated in-game).

```powershell
python "utility_scripts\test_body_logic.py" [--preset <substring>] [--tex-tone <float>] [--dll <dll_path>] [--ground-truth <json_path>]
```
- `--preset`: Case-insensitive substring filter (e.g., `--preset "warrior"`, `--preset "pregnant"`, `--preset "chubby"`).
- `--tex-tone`: Simulated normal-map muscularity score passed to `PhysiqueClassifier` (default: `0.25`).
- `--dll`: Path to compiled `BodyLanguage.dll` (default: auto-selects newest between `SKSE_Source/build/release/Release/BodyLanguage.dll` and bundled `utility_scripts/bin/BodyLanguage.dll`).
- `--ground-truth`: Path to Ground Truth JSON (default: `utility_scripts/body_samples_ground_truth.json`).

---

### 3. `generate_visual_report.py` — Stage 2B: Interactive Visual Blueprint Report (`body_visual_report.html`)
Generates `utility_scripts/body_visual_report.html` containing a 4-panel visual inspection card for every preset:
1. **`1. SAGITTAL (Y-Z) PROFILE` (1:1 Scale Side View):**
   - Outer body contour (breast apex & glute peak).
   - **Blue dashed line:** Centerline skin contour (`|X| < 1.2`) down the sternum, solar plexus, navel/belly, and pubis.
   - **Cyan solid line:** Sternum-to-Underbust ribcage wall (`0.65 * sternumY + 0.35 * underbustY`).
   - **Pink solid bar:** Pure Anterior Breast Projection (`ΔY = apexY - ribWallY`) used to compute `bustCup = 1 + 6 * (ΔY / H0)` with zero ribcage width contamination.
   - **Yellow dashed line & horizontal bar:** Underbust-to-Hip flat-stomach reference chord and forward belly bulge (`Belly Drop`).
2. **`2. FRONT (X-Z) SILHOUETTE` (1:1 Scale Front View):**
   - Full-body front silhouette with separate left/right arm polygons (preserving the underarm/waist gap).
   - Horizontal measurement slice bars (`Shldr`, `Bust`, `Under`, `Waist`, `Belly`, `Hips`, `Thigh`, `Calf`) and 45° perpendicular `Arm` bicep slice bars (`Ring 9`).
3. **`3A`, `3B`, `3C` TOP-DOWN CROSS-SECTIONS (`X-Y` Plane):**
   - **`3A`**: Bust slice (`pink` + dashed convex tape hull) overlaid on Underbust ribcage (`cyan`).
   - **`3B`**: Hips/Glutes slice (`orange` + dashed convex tape hull) overlaid on Natural Waist (`green`).
   - **`3C`**: Upper Thigh (`purple`), Lower Leg / Calf (`blue`), and perpendicular Upper Arm / Bicep (`red`).
4. **Comparison Table & Live Descriptions:**
   - Side-by-side `Ground Truth (3D Tris)` vs. `C++ DLL (Direct NIF)` vs. `C++ DLL (Runtime Morph)` measurements, dimensionless ratios, classified keywords, and generated `shortDesc` / `fullDesc`.

```powershell
python "utility_scripts\generate_visual_report.py" [--dll <dll_path>] [--ground-truth <json_path>] [--output <html_path>]
```
Open `utility_scripts/body_visual_report.html` in any browser after running.

---

## Adding New Body Presets for Testing

1. Create a folder under `utility_scripts/body samples/<body_type>/<preset_name>/` (for example, `utility_scripts/body samples/3ba/my new preset/`).
2. Open **BodySlide**, select the base body outfit (e.g., `CBBE 3BBB Body Amazing`), choose the **specific preset** you want to test in the **Preset** dropdown (**do NOT use `Zeroed Sliders`** — the preset shape must be baked into the `.nif` vertices), and click **Build**.
3. Copy the newly built `femalebody_1.nif` (or `malebody_1.nif`) from your BodySlide output folder (e.g., `<Your Mod Manager Mods Folder>\<Your BodySlide Output>\meshes\actors\character\character assets\femalebody_1.nif`) into your new `utility_scripts/body samples/<body_type>/<preset_name>/` folder.
4. Run:
   ```powershell
   python "utility_scripts\analyze_body_samples.py"
   python "utility_scripts\generate_visual_report.py"
   ```
5. Refresh `utility_scripts/body_visual_report.html` in your browser.

