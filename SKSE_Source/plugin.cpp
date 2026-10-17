#include "PCH.h"

#include <Windows.h>
#include <exception>
#include <filesystem>
#include <memory>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "BodyDescription.h"
#include "Geometry/NifRingIndexer.h"
#include "SkyrimNetIntegration.h"

using namespace SKSE;

namespace {

    // Returns the "Data" root by walking three parent directories up from the DLL's location.
    // DLL lives at <dataRoot>/SKSE/Plugins/BodyLanguage.dll
    std::filesystem::path GetDataRoot() {
        wchar_t buf[MAX_PATH]{};
        HMODULE hSelf = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetDataRoot),
            &hSelf);
        GetModuleFileNameW(hSelf, buf, MAX_PATH);
        return std::filesystem::path(buf)
            .parent_path()
            .parent_path()
            .parent_path();
    }

    void SetupLogging() {
        auto logDir = SKSE::log::log_directory();
        if (!logDir) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print("BodyLanguage: log directory unavailable");
            }
            return;
        }

        std::filesystem::path logPath = *logDir;
        if (!std::filesystem::is_directory(logPath)) {
            logPath = logPath.parent_path();
        }
        logPath /= "BodyLanguage.log";

        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
        if (ec) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print("BodyLanguage: failed to create log folder (%s)", ec.message().c_str());
            }
            return;
        }

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("BodyLanguage", std::move(sink));
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::info);
        logger->set_pattern("[%H:%M:%S] [%l] %v");

        spdlog::set_default_logger(std::move(logger));
    }

}  // namespace

SKSEPluginLoad(const LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLogging();
    SKSE::log::info("BodyLanguage v2.0.0 plugin loading...");

    if (const auto* messaging = SKSE::GetMessagingInterface()) {
        if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
                if (!message) {
                    return;
                }
                try {
                    switch (message->type) {
                        case SKSE::MessagingInterface::kPostPostLoad:
                            BodyLanguage::RegisterSkyrimNetDecorators();
                            break;

                        case SKSE::MessagingInterface::kPreLoadGame:
                            SKSE::log::info("PreLoadGame...");
                            BodyLanguage::ClearActorDescriptionCache();
                            break;

                        case SKSE::MessagingInterface::kPostLoadGame:
                        case SKSE::MessagingInterface::kNewGame:
                            SKSE::log::info("New game/Load...");
                            BodyLanguage::ClearActorDescriptionCache();
                            break;

                        case SKSE::MessagingInterface::kDataLoaded: {
                            SKSE::log::info("Data loaded successfully.");
                            const auto dataRoot = GetDataRoot();
                            BodyLanguage::InitBodyDescriptions(dataRoot);
                            BodyLanguage::NifRingIndexer::GetInstance().IndexAllBodyTemplates();
                            break;
                        }

                        default:
                            break;
                    }
                } catch (const std::exception& e) {
                    SKSE::log::error("Unhandled exception in SKSE message listener: {}", e.what());
                } catch (...) {
                    SKSE::log::error("Unknown unhandled exception in SKSE message listener.");
                }
            })) {
            SKSE::log::critical("Failed to register messaging listener.");
            return false;
        }
    } else {
        SKSE::log::critical("Messaging interface unavailable.");
        return false;
    }

    return true;
}
