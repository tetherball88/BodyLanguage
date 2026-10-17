#include "PCH.h"

#include "SkyrimNetIntegration.h"

#include <Windows.h>
#include <exception>
#include <functional>
#include <string>

#include <spdlog/spdlog.h>

#include "BodyDescription.h"
#include "Geometry/ActorBodyEvaluator.h"

// The target export signature as published by SkyrimNet's public API.
// std::function is passed by value intentionally — SkyrimNet was designed this way.
using PublicRegisterDecorator_t = bool (*)(
    const char* name,
    const char* description,
    std::function<std::string(RE::Actor*)> callback);

namespace BodyLanguage {

    namespace {

        // Look up the export from SkyrimNet.dll if mapped into the current process.
        PublicRegisterDecorator_t FindRegisterDecorator() {
            HMODULE hMod = GetModuleHandleW(L"SkyrimNet.dll");
            if (!hMod) {
                spdlog::warn("[SkyrimNet] SkyrimNet.dll not found in process - integration skipped.");
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

        void RegisterDecorator(
            PublicRegisterDecorator_t fn,
            const char* name,
            const char* description,
            std::function<std::string(RE::Actor*)> callback) {
            if (fn(name, description, std::move(callback))) {
                spdlog::info("[SkyrimNet] Registered decorator '{}'", name);
            } else {
                spdlog::warn("[SkyrimNet] Failed to register decorator '{}' (duplicate or invalid name?)", name);
            }
        }

    }  // namespace

    void RegisterSkyrimNetDecorators() {
        spdlog::info("[SkyrimNet] RegisterSkyrimNetDecorators called.");

        ActorBodyEvaluator::GetInstance().InitSKEE();

        auto* fn = FindRegisterDecorator();
        if (!fn) {
            return;
        }

        RegisterDecorator(
            fn,
            "ttlb_get_full_body_description",
            "Returns a full body description for this actor.",
            [](RE::Actor* actor) -> std::string {
                try {
                    // const char* name = actor ? actor->GetName() : nullptr;
                    // spdlog::info(
                    //     "[SkyrimNet] ttlb_get_full_body_description called for actor '{}' ({:08X}).",
                    //     name ? name : "<null>",
                    //     actor ? actor->GetFormID() : 0);
                    return GetBodyDescription(actor, "full");
                } catch (const std::exception& e) {
                    spdlog::error("[SkyrimNet] Exception in ttlb_get_full_body_description: {}", e.what());
                    return {};
                } catch (...) {
                    spdlog::error("[SkyrimNet] Unknown exception in ttlb_get_full_body_description");
                    return {};
                }
            });

        RegisterDecorator(
            fn,
            "ttlb_get_short_body_description",
            "Returns a short body description for this actor.",
            [](RE::Actor* actor) -> std::string {
                try {
                    // const char* name = actor ? actor->GetName() : nullptr;
                    // spdlog::info(
                    //     "[SkyrimNet] ttlb_get_short_body_description called for actor '{}' ({:08X}).",
                    //     name ? name : "<null>",
                    //     actor ? actor->GetFormID() : 0);
                    return GetBodyDescription(actor, "short");
                } catch (const std::exception& e) {
                    spdlog::error("[SkyrimNet] Exception in ttlb_get_short_body_description: {}", e.what());
                    return {};
                } catch (...) {
                    spdlog::error("[SkyrimNet] Unknown exception in ttlb_get_short_body_description");
                    return {};
                }
            });

        RegisterDecorator(
            fn,
            "ttlb_is_naked",
            "Returns whether this actor is naked.",
            [](RE::Actor* actor) -> std::string {
                try {
                    const bool naked = ActorBodyEvaluator::GetInstance().IsActorNaked(actor);
                    // const char* name = actor ? actor->GetName() : nullptr;
                    // spdlog::info(
                    //     "[SkyrimNet] ttlb_is_naked called for actor '{}' ({:08X}) -> {}",
                    //     name ? name : "<null>",
                    //     actor ? actor->GetFormID() : 0,
                    //     naked ? "true" : "false");
                    return naked ? "true" : "false";
                } catch (const std::exception& e) {
                    spdlog::error("[SkyrimNet] Exception in ttlb_is_naked: {}", e.what());
                    return "false";
                } catch (...) {
                    spdlog::error("[SkyrimNet] Unknown exception in ttlb_is_naked");
                    return "false";
                }
            });

        RegisterDecorator(
            fn,
            "ttlb_get_genitals",
            "Returns a JSON object with hasGenitals, size (XS..XL), and addon key for TNG/SOS when naked.",
            [](RE::Actor* actor) -> std::string {
                try {
                    // const char* name = actor ? actor->GetName() : nullptr;
                    // spdlog::info(
                    //     "[SkyrimNet] ttlb_get_genitals called for actor '{}' ({:08X}).",
                    //     name ? name : "<null>",
                    //     actor ? actor->GetFormID() : 0);
                    return GetActorGenitalsJson(actor);
                } catch (const std::exception& e) {
                    spdlog::error("[SkyrimNet] Exception in ttlb_get_genitals: {}", e.what());
                    return "{\"hasGenitals\":false}";
                } catch (...) {
                    spdlog::error("[SkyrimNet] Unknown exception in ttlb_get_genitals");
                    return "{\"hasGenitals\":false}";
                }
            });

        spdlog::info("[SkyrimNet] Decorator registration complete.");
    }

}  // namespace BodyLanguage
