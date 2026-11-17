---
name: bodylanguage-release
description: >-
  Prepare, bump, and audit a new BodyLanguage version release (vX.Y.Z) locally without creating git tags or pushing to remote. Activate when asked to prepare a release, bump version numbers, generate or finalize CHANGELOG.md for a release, audit release readiness (BodyLanguage.dll, manifest.json, configs, prompts), or prepare release notes.
---

# BodyLanguage Version Release Runbook

Use this workflow whenever preparing a new BodyLanguage release (`vX.Y.Z`).

## Mandatory Guardrails

1. **Never Tag or Push to Remote:** Do **not** create git tags (`git tag`, `tag-push.bat`) and do **not** push to any remote (`git push`). Do **not** commit (`git commit`) unless the user explicitly asks you to.
2. **Do NOT Build C++ (`AGENTS.md`):** Never run `cmake`, `msbuild`, `ninja`, `cl.exe`, or `vcpkg`. After bumping `CMakeLists.txt` and `version.rc`, report any stale or unrebuilt DLL checks to the user so they can reconfigure and compile `BodyLanguage.dll` externally.

## How BodyLanguage Releases Work

- When the user pushes a `v*` tag externally, [.github/workflows/release-zip.yml](../../../.github/workflows/release-zip.yml) runs on `ubuntu-latest` (it does **not** compile C++ in CI; it packages the committed `SKSE/` directory directly into `BodyLanguage_vX.Y.Z.zip`).
- On a live tag push (or `workflow_dispatch` with `dry_run: false`), the workflow:
  1. Extracts the `## vX.Y.Z` section from `CHANGELOG.md` (and converts it to plain text for Nexus Mods).
  2. Creates the GitHub Release with the extracted Markdown changelog and `BodyLanguage_vX.Y.Z.zip` attached.
  3. Renames the archive to `BodyLanguage_X.Y.Z.zip` and publishes the new main file + plain-text changelog to **Nexus Mods** via `Nexus-Mods/upload-action@v1.0.0-beta.11` using the `nexusmods` GitHub Environment (`secrets.NEXUSMODS_API_KEY`, `vars.NEXUSMODS_MOD_ID` / `secrets.NEXUSMODS_MOD_ID`, `vars.NEXUSMODS_FILE_ID` / `secrets.NEXUSMODS_FILE_ID`, optional `vars.NEXUSMODS_DISPLAY_NAME` defaulting to `BodyLanguage`).
- Manual `workflow_dispatch` with `dry_run: true` can be run on GitHub Actions to verify zip packaging and `CHANGELOG.md` extraction without publishing to GitHub Releases or Nexus Mods.
- Both `SKSE/Plugins/BodyLanguage.dll` (release plugin) and `utility_scripts/bin/BodyLanguage.dll` (offline Python test harness copy) are tracked in git and updated automatically by the CMake post-build steps when the user builds Release locally.

---

## Release Preparation Steps

### 1. Inspect Changes Since Previous Release

1. Find the latest release tag (or previous `CHANGELOG.md` heading) and inspect all changes since then:
   ```powershell
   git tag --list "v*" --sort=-creatordate | Select-Object -First 5
   git log <prev_tag_or_commit>..HEAD --stat
   git status -s
   ```
2. Confirm the target semantic version `vX.Y.Z` (e.g., `v2.3.0`).

### 2. Bump Version Metadata

Run [prepare_release.ps1](./scripts/prepare_release.ps1) with `-ApplyVersion`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/bodylanguage-release/scripts/prepare_release.ps1" -Version vX.Y.Z -ApplyVersion
```

This updates version strings across:
- `SKSE/Plugins/SkyrimNet/external/tetherball88.bodylanguage/manifest.json` (`"version"`)
- `SKSE_Source/CMakeLists.txt` (`project(BodyLanguage VERSION ... LANGUAGES CXX)`)
- `SKSE_Source/version.rc` (`FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, `ProductVersion`)

### 3. Update `CHANGELOG.md` & Documentation

1. Prepend a `## vX.Y.Z` section (`### Added`, `### Changed`, `### Fixed`, `### Removed` as applicable) above the previous release in `CHANGELOG.md`, matching existing formatting (clear user-facing bullets with bold lead-ins).
2. Inspect changes since the previous release for new decorators, `.ini` / JSON settings, prompt subcomponents, or optional mod integrations (e.g., `FashionPolice`, `SkyrimNet`, `RaceMenu`, `XPMSE`, `RSV`, `TNG`/`SOS`).
3. Update `README.md` and `USER_CUSTOMIZATION.md` if any decorators, configuration keys, thresholds, or requirements changed.

### 4. Run Pre-Release Audit & Optional Python Regression Check

1. Run [prepare_release.ps1](./scripts/prepare_release.ps1) in read-only audit mode:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/bodylanguage-release/scripts/prepare_release.ps1" -Version vX.Y.Z
   ```
2. Once the user has rebuilt `BodyLanguage.dll` externally (so `SKSE/Plugins/BodyLanguage.dll` and `utility_scripts/bin/BodyLanguage.dll` are fresh), you can optionally run the offline NIF/preset regression suite if geometry or classification changed:
   ```powershell
   python utility_scripts/test_body_logic.py
   ```

### 5. Hand-off Summary (Local Only — No Tagging or Pushing)

Do **not** commit, tag, or push to remote. Provide:
1. The `prepare_release.ps1` audit summary (`PASS` / `WARN` / `FAIL`).
2. Clear notice if the user still needs to reconfigure CMake and rebuild `BodyLanguage.dll` externally (closing `SkyrimSE.exe` first if locked) so the bumped `CMakeLists.txt` / `version.rc` version is embedded into `SKSE/Plugins/BodyLanguage.dll` and `utility_scripts/bin/BodyLanguage.dll`, after which `prepare_release.ps1 -Version vX.Y.Z` can be re-run to verify all `[PASS]`.
3. Any Nexus Mods / documentation reminders if requirements or user-facing configs changed.
