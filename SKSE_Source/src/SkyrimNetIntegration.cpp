#include "SkyrimNetIntegration.h"

#include <Windows.h>
#include <functional>
#include <string>

#include <spdlog/spdlog.h>

#include "OBodyService.h"
#include "BodyDescription.h"

// The target export signature as published by SkyrimNet's public API.
// std::function is passed by value intentionally – SkyrimNet was designed this way.
using PublicRegisterDecorator_t = bool (*)(
    const char* name,
    const char* description,
    std::function<std::string(RE::Actor*)> callback);

namespace BodyLanguage {

namespace {

    // Helper: look up the export from any already-loaded DLL whose base name
    // matches (case-insensitive, without ".dll").
    static PublicRegisterDecorator_t FindRegisterDecorator()
    {
        // SkyrimNet ships its DLL as "SkyrimNet.dll" inside SKSE/Plugins/.
        // GetModuleHandleW only searches modules already mapped into this process.
        HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
        if (!hMod) {
            spdlog::warn("[SkyrimNet] SkyrimNet.dll not found in process – integration skipped.");
            return nullptr;
        }

        auto* fn = reinterpret_cast<PublicRegisterDecorator_t>(
            GetProcAddress(hMod, "PublicRegisterDecorator"));
        if (!fn) {
            spdlog::warn("[SkyrimNet] PublicRegisterDecorator export not found in SkyrimNet.dll.");
            return nullptr;
        }
        return fn;
    }

    // -----------------------------------------------------------------------
    // Individual decorator registrations
    // Each lambda receives an RE::Actor* and returns a std::string that
    // SkyrimNet injects into its prompt templates as {{ decoratorName(uuid) }}.
    // -----------------------------------------------------------------------

    static void RegisterDecorator(PublicRegisterDecorator_t fn,
        const char* name, const char* description,
        std::function<std::string(RE::Actor*)> callback)
    {
        if (fn(name, description, std::move(callback))) {
            spdlog::info("[SkyrimNet] Registered decorator '{}'", name);
        } else {
            spdlog::warn("[SkyrimNet] Failed to register decorator '{}' (duplicate or invalid name?)", name);
        }
    }

}  // anonymous namespace

void RegisterSkyrimNetDecorators()
{
    spdlog::info("[SkyrimNet] RegisterSkyrimNetDecorators called.");

    OBodyService::GetInstance().Init();

    auto* fn = FindRegisterDecorator();
    if (!fn) return;

    RegisterDecorator(fn,
        "ttlb_get_full_body_description",
        "Returns a full body description for this actor.",
        [](RE::Actor* actor) -> std::string {
            const char* name = actor ? actor->GetName() : nullptr;
            spdlog::debug("[SkyrimNet] get_full_body_description called for actor '{}'.",
                name ? name : "<null>");
            auto result = GetBodyDescription(actor, "full");
            spdlog::debug("[SkyrimNet] get_full_body_description result: '{}'", result);
            return result;
        });

    RegisterDecorator(fn,
        "ttlb_get_short_body_description",
        "Returns a short body description for this actor.",
        [](RE::Actor* actor) -> std::string {
            const char* name = actor ? actor->GetName() : nullptr;
            spdlog::debug("[SkyrimNet] get_short_body_description called for actor '{}'.",
                name ? name : "<null>");
            auto result = GetBodyDescription(actor, "short");
            spdlog::debug("[SkyrimNet] get_short_body_description result: '{}'", result);
            return result;
        });

    RegisterDecorator(fn,
        "ttlb_is_naked",
        "Returns whether this actor is naked.",
        [](RE::Actor* actor) -> std::string {
            const char* name = actor ? actor->GetName() : nullptr;
            spdlog::debug("[SkyrimNet] ttlb_is_naked called for actor '{}'.",
                name ? name : "<null>");
            bool naked = OBodyService::GetInstance().IsNaked(actor);
            spdlog::debug("[SkyrimNet] ttlb_is_naked result: '{}'", naked);
            return naked ? "true" : "false";
        });

    spdlog::info("[SkyrimNet] Decorator registration complete.");
}

}  // namespace BodyLanguage
