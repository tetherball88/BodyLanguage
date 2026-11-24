#include "PCH.h"

#include "SkyrimNetIntegration.h"

#include <Windows.h>
#include <cstring>
#include <excpt.h>
#include <exception>
#include <functional>
#include <string>
#include <utility>

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

        bool ExecuteSehProtected(
            void (*fn)(void*) noexcept,
            void* ctx,
            unsigned long& outExceptionCode) noexcept {
            __try {
                fn(ctx);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                outExceptionCode = GetExceptionCode();
                return false;
            }
        }

        template <typename Callable>
        std::string RunDecoratorSafe(
            const char* decoratorName,
            RE::Actor* actor,
            const char* fallbackValue,
            Callable&& callable) noexcept {
            struct CallContext {
                Callable* fn;
                const char* name;
                const char* fallback;
                std::string result;
            } ctx{&callable, decoratorName, fallbackValue, {}};

            unsigned long sehCode = 0;
            const bool ok = ExecuteSehProtected(
                [](void* rawCtx) noexcept {
                    auto* c = static_cast<CallContext*>(rawCtx);
                    try {
                        c->result = (*c->fn)();
                    } catch (const std::exception& e) {
                        spdlog::error("[SkyrimNet] Exception in {}: {}", c->name, e.what());
                        c->result = c->fallback ? c->fallback : "";
                    } catch (...) {
                        spdlog::error("[SkyrimNet] Unknown exception in {}", c->name);
                        c->result = c->fallback ? c->fallback : "";
                    }
                },
                &ctx,
                sehCode);

            if (!ok) {
                spdlog::error(
                    "[SkyrimNet] SEH hardware exception 0x{:08X} in {} (actor={:p})",
                    sehCode,
                    decoratorName,
                    static_cast<const void*>(actor));
                return fallbackValue ? std::string(fallbackValue) : std::string{};
            }
            return std::move(ctx.result);
        }

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
                return RunDecoratorSafe("ttlb_get_full_body_description", actor, "", [actor]() {
                    return GetBodyDescription(actor, "full");
                });
            });

        RegisterDecorator(
            fn,
            "ttlb_get_short_body_description",
            "Returns a short body description for this actor.",
            [](RE::Actor* actor) -> std::string {
                return RunDecoratorSafe("ttlb_get_short_body_description", actor, "", [actor]() {
                    return GetBodyDescription(actor, "short");
                });
            });

        RegisterDecorator(
            fn,
            "ttlb_is_naked",
            "Returns whether this actor is naked.",
            [](RE::Actor* actor) -> std::string {
                return RunDecoratorSafe("ttlb_is_naked", actor, "false", [actor]() -> std::string {
                    const bool naked = ActorBodyEvaluator::GetInstance().IsActorNaked(actor);
                    return naked ? "true" : "false";
                });
            });

        RegisterDecorator(
            fn,
            "ttlb_get_beast_form",
            "Returns the actor's active beast form ('werewolf', 'vampire_lord', 'werebear', or '' if human).",
            [](RE::Actor* actor) -> std::string {
                return RunDecoratorSafe("ttlb_get_beast_form", actor, "", [actor]() {
                    const BeastFormType beastForm = ActorBodyEvaluator::GetInstance().GetActorBeastForm(actor);
                    return std::string(ActorBodyEvaluator::BeastFormToKey(beastForm));
                });
            });

        RegisterDecorator(
            fn,
            "ttlb_get_genitals",
            "Returns a JSON object with hasGenitals, size (XS..XL), and addon key for TNG/SOS when naked.",
            [](RE::Actor* actor) -> std::string {
                return RunDecoratorSafe("ttlb_get_genitals", actor, "{\"hasGenitals\":false}", [actor]() {
                    return GetActorGenitalsJson(actor);
                });
            });

        RegisterDecorator(
            fn,
            "ttlb_get_body_parts",
            "Returns a JSON object with valid, isFemale, breasts, butt, and thighs classification tiers.",
            [](RE::Actor* actor) -> std::string {
                return RunDecoratorSafe("ttlb_get_body_parts", actor, "{\"valid\":false}", [actor]() {
                    return GetActorBodyPartsJson(actor);
                });
            });

        RegisterDecorator(
            fn,
            "ttlb_is_excluded",
            "Returns whether this actor is excluded from BodyLanguage evaluation and description via BodyLanguage.ini.",
            [](RE::Actor* actor) -> std::string {
                return RunDecoratorSafe("ttlb_is_excluded", actor, "false", [actor]() -> std::string {
                    return IsActorExcluded(actor) ? "true" : "false";
                });
            });

        RegisterDecorator(
            fn,
            "ttlb_get_height",
            "Returns a JSON object with the actor's effective height scale.",
            [](RE::Actor* actor) -> std::string {
                return RunDecoratorSafe("ttlb_get_height", actor, "{\"height\":1.0}", [actor]() {
                    return GetActorHeightJson(actor);
                });
            });

        spdlog::info("[SkyrimNet] Decorator registration complete.");
    }

    namespace {

        size_t CopyJsonToBuffer(const std::string& json, char* outBuf, size_t bufSize) noexcept {
            if (!outBuf || bufSize == 0 || json.size() >= bufSize) {
                return 0;
            }
            std::memcpy(outBuf, json.data(), json.size() + 1);
            return json.size();
        }

    }  // namespace

    extern "C" {

    __declspec(dllexport) size_t BodyLanguage_GetActorGenitalsJson(
        RE::Actor* actor,
        char* outBuf,
        size_t bufSize) noexcept {
        if (!actor || !outBuf || bufSize == 0) {
            return 0;
        }
        const std::string json = RunDecoratorSafe(
            "BodyLanguage_GetActorGenitalsJson",
            actor,
            "{\"hasGenitals\":false}",
            [actor]() { return GetActorGenitalsJson(actor); });
        return CopyJsonToBuffer(json, outBuf, bufSize);
    }

    __declspec(dllexport) size_t BodyLanguage_GetActorBodyPartsJson(
        RE::Actor* actor,
        char* outBuf,
        size_t bufSize) noexcept {
        if (!actor || !outBuf || bufSize == 0) {
            return 0;
        }
        const std::string json = RunDecoratorSafe(
            "BodyLanguage_GetActorBodyPartsJson",
            actor,
            "{\"valid\":false}",
            [actor]() { return GetActorBodyPartsJson(actor); });
        return CopyJsonToBuffer(json, outBuf, bufSize);
    }

    }  // extern "C"

}  // namespace BodyLanguage
