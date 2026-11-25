#pragma once

namespace BodyLanguage {

    // Returns true if SkyrimNet.dll is loaded in the current process.
    bool IsSkyrimNetInstalled() noexcept;

    // Returns true if SkyrimNet decorators were successfully registered.
    bool IsSkyrimNetActive() noexcept;

    // Attempts to locate SkyrimNet.dll in the current process and registers
    // BodyLanguage decorators via its PublicRegisterDecorator export.
    // No-op when SkyrimNet is not installed.
    // Must be called at kPostPostLoad (after all SKSE plugins are loaded).
    void RegisterSkyrimNetDecorators();

}  // namespace BodyLanguage
