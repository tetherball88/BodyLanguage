# Agent Rules & C++ Development Guidelines (`BodyLanguage`)

## Mandatory Workflow Rules

1. **Do NOT Try to Build C++**
   - Never run CMake configuration or build commands (`cmake`, `msbuild`, `ninja`, `cl.exe`, `vcpkg install`, etc.).
   - Do not invoke build tasks or test compilation via terminal commands. The user manages all C++ compilation and build verification externally.

2. **Do NOT Leave Outdated or Deprecated Code**
   - When refactoring or replacing a subsystem, **delete** all superseded classes, functions, headers, `.cpp` files, and obsolete configuration/data files immediately.
   - Do not leave commented-out code blocks, `#if 0` dead regions, `[[deprecated]]` shims, unused helper functions, or legacy fallback paths unless explicitly requested by the user.
   - Keep `CMakeLists.txt`, `vcpkg.json`, `#include` directives, and `plugin.cpp` clean of any references to removed modules or unused third-party libraries.

3. **Strictly Body-Type Agnostic & Calibrated to Real-World Anthropometric Golden Standards**
   - The entire geometry and classification pipeline must remain 100% **body-type agnostic** (no hardcoded assumptions, branches, or slider-name checks for 3BA, BHUNP, UNP, CBBE, HIMBO, SAM, Vanilla, etc.).
   - All measurements, ratios, and thresholds in `geometryThresholds.json` / `PhysiqueClassifier.cpp` must be derived purely from 3D mesh geometry and calibrated against an objective **real-world human anthropometric "Golden Standard"** (cm / inches), never tuned to force an equal statistical distribution across a specific user's log or modlist.

---

## C++ Language & Architecture Standards

### 1. Language Standard & Target Environment
- **Standard:** C++23 (`cxx_std_23`) targeting MSVC (`x64-windows-static`).
- **Framework:** CommonLibSSE-NG (`RE::` and `SKSE::` namespaces).
- **Precompiled Header:** Every `.cpp` file must include `"PCH.h"` (which provides `RE/Skyrim.h`, `SKSE/SKSE.h`, and `WIN32_LEAN_AND_MEAN`).
- **Modern C++ Idioms:**
  - Prefer `std::string_view`, `std::span`, `std::optional`, `std::array`, and `<ranges>` / `<algorithm>` over raw pointers and C-style arrays.
  - Use `constexpr` and `static constexpr` for compile-time constants, thresholds, and lookup tables instead of preprocessor macros (`#define`).
  - Use RAII for all resources (streams, handles, locks). Never use raw `new` / `delete`.

### 2. Code Formatting & Naming Conventions (`.clang-format`)
- **Base Style:** Google C++ Style customized via `SKSE_Source/.clang-format`.
- **Indentation:** 4 spaces, never tabs (`UseTab: Never`, `IndentWidth: 4`).
- **Line Length:** Maximum 120 characters (`ColumnLimit: 120`).
- **Namespaces:** All code belongs under `namespace BodyLanguage` (with `NamespaceIndentation: All` or file-scoped internal helpers inside an anonymous `namespace`).
- **Access Modifiers:** Offset by `-4` (`public:`, `private:`, `protected:` aligned with `class`/`struct`).
- **Naming:**
  - Types, Classes, Structs, Enums: `PascalCase` (e.g., `BodyTemplateCache`, `NormalMapAnalyzer`).
  - Functions & Methods: `PascalCase` (e.g., `GetBodyDescription`, `EvaluateActorBody`).
  - Local Variables & Parameters: `camelCase` (e.g., `baseReferenceHeight`, `abdomenUV`).
  - Member Variables: `m_camelCase` for class private members (e.g., `m_interface`, `m_cacheMutex`) or `camelCase` for plain data structs.
  - Constants: `kPascalCase` (e.g., `kDefaultSmoothEnergy`, `kBodySlot`).

### 3. Skyrim Engine & CommonLibSSE-NG Best Practices
- **Virtual File System (VFS) & BSA Compatibility:**
  - Always read game assets (`.nif`, `.tri`, `.dds`) through `RE::BSResourceNiBinaryStream` using Data-relative paths (e.g., `"meshes\\..."`, `"textures\\..."`) so both loose files (MO2 VFS/Vortex) and BSA-packed assets work transparently.
  - Only use `std::filesystem` / `std::ifstream` for external JSON/INI configuration files stored under `Data/SKSE/Plugins/`.
- **Null Safety & Engine Pointers:**
  - Always validate `RE::Actor*`, `actor->GetActorBase()`, `actor->GetRace()`, `actor->Get3D(false)`, and `RE::TESDataHandler::GetSingleton()` before dereferencing.
  - Never assume an actor's 3D scene graph (`Get3D`) is loaded; fallback gracefully to base records (`TESObjectARMA` / `TESObjectARMO` / `BGSTextureSet`).
- **Thread Safety & Performance:**
  - SkyrimNet decorator callbacks (`ttlb_get_full_body_description`, `ttlb_get_short_body_description`, `ttlb_is_naked`) may be invoked asynchronously.
  - Protect shared runtime caches (`BodyTemplateCache`, normal map roughness cache) with `std::shared_mutex` (reader-writer locks) or lock-free atomic patterns.
  - Never allow C++ exceptions to escape across SKSE messaging listeners or SkyrimNet callback boundaries.
- **Logging:**
  - Use `spdlog` (`spdlog::info`, `spdlog::warn`, `spdlog::error`, `spdlog::debug`) with a consistent module prefix tag (e.g., `[NifRingIndexer]`, `[NormalMapAnalyzer]`, `[BodyDescription]`).
