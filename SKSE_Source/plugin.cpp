#include <Windows.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <thread>

#include "PCH.h"
#include "src/RsvNormalMapScanner.h"
#include "src/RsvDiffuseScanner.h"
#include "src/SliderPresetScanner.h"
#include "src/SkyrimNetIntegration.h"
#include "src/BodyDescription.h"

using namespace SKSE;

namespace {
    // Returns the "Data" root by walking two directories up from the DLL's location.
    // DLL lives at <dataRoot>/SKSE/Plugins/BodyLanguage.dll, so:
    //   parent       -> <dataRoot>/SKSE/Plugins/
    //   parent.parent -> <dataRoot>/SKSE/
    //   parent.parent.parent -> <dataRoot>/
    std::filesystem::path GetDataRoot() {
        wchar_t buf[MAX_PATH]{};
        HMODULE hSelf = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetDataRoot),
            &hSelf);
        GetModuleFileNameW(hSelf, buf, MAX_PATH);
        return std::filesystem::path(buf)   // .../SKSE/Plugins/BodyLanguage.dll
                   .parent_path()           // .../SKSE/Plugins/
                   .parent_path()           // .../SKSE/
                   .parent_path();          // <dataRoot>/
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

    void PrintToConsole(std::string_view message) {
        SKSE::log::info("{}", message);
        if (auto* console = RE::ConsoleLog::GetSingleton()) {
            console->Print("%s", message.data());
        }
    }
}

SKSEPluginLoad(const LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLogging();
    SKSE::log::info("BodyLanguage plugin loading...");

    if (const auto* messaging = SKSE::GetMessagingInterface()) {
        if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
                switch (message->type) {
                    case SKSE::MessagingInterface::kPostPostLoad:
                        BodyLanguage::RegisterSkyrimNetDecorators();
                        break;

                    case SKSE::MessagingInterface::kPreLoadGame:
                        SKSE::log::info("PreLoadGame...");
                        break;

                    case SKSE::MessagingInterface::kPostLoadGame:
                    case SKSE::MessagingInterface::kNewGame:
                        SKSE::log::info("New game/Load...");
                        break;

                    case SKSE::MessagingInterface::kDataLoaded: {
                        SKSE::log::info("Data loaded successfully.");
                        BodyLanguage::ScanAndGeneratePresetDescriptions(GetDataRoot());
                        BodyLanguage::InitBodyDescriptions(GetDataRoot());
                        std::thread([]() {
                            BodyLanguage::ScanRsvNormalMaps(GetDataRoot());
                            BodyLanguage::ReloadRsvHashes(GetDataRoot());
                            BodyLanguage::ScanRsvDiffuseMaps(GetDataRoot());
                            BodyLanguage::ReloadRsvDiffuseHashes(GetDataRoot());
                        }).detach();
                        break;
                    }

                    default:
                        break;
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
