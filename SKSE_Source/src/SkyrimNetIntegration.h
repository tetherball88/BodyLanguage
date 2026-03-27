#pragma once

namespace BodyLanguage {

    // Attempts to locate SkyrimNet.dll in the current process and registers
    // BodyLanguage decorators via its PublicRegisterDecorator export.
    // Must be called at kPostPostLoad (after all SKSE plugins are loaded).
    void RegisterSkyrimNetDecorators();

}  // namespace BodyLanguage
