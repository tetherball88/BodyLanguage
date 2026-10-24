#include "PCH.h"

#include <Windows.h>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "BodyDescription.h"
#include "Description/PhysiqueClassifier.h"
#include "Geometry/ActorBodyEvaluator.h"
#include "Geometry/NifRingIndexer.h"
#include "Geometry/NifTriParser.h"
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
};

extern "C" __declspec(dllexport) int BodyLanguage_EvaluateNifForTest(
    const char* dataRootPath,
    const char* nifFilePath,
    const char* baseTemplateNifPath,
    int isFemale,
    int isNaked,
    float texToneScore,
    char* outJsonBuffer,
    int outJsonBufferSize) {
    try {
        if (!dataRootPath || !nifFilePath || !outJsonBuffer || outJsonBufferSize <= 2) {
            return -1;
        }

        static std::string s_loadedDataRoot;
        if (s_loadedDataRoot != dataRootPath) {
            BodyLanguage::PhysiqueClassifier::GetInstance().LoadConfigs(dataRootPath);
            s_loadedDataRoot = dataRootPath;
        }

        const auto targetMeshOpt = BodyLanguage::ParseNifBodyMeshFromDisk(nifFilePath);
        if (!targetMeshOpt) {
            return -2;
        }
        const auto& targetMesh = *targetMeshOpt;

        std::shared_ptr<const BodyLanguage::BodyTemplateCache> tplCache;
        if (baseTemplateNifPath && *baseTemplateNifPath != '\0') {
            const auto baseMeshOpt = BodyLanguage::ParseNifBodyMeshFromDisk(baseTemplateNifPath);
            if (!baseMeshOpt) {
                return -3;
            }
            tplCache = BodyLanguage::NifRingIndexer::GetInstance().BuildTemplateCacheFromMeshes(
                baseTemplateNifPath, *baseMeshOpt, *baseMeshOpt, isFemale != 0, false);
        } else {
            tplCache = BodyLanguage::NifRingIndexer::GetInstance().BuildTemplateCacheFromMeshes(
                nifFilePath, targetMesh, targetMesh, isFemale != 0, false);
        }

        if (!tplCache) {
            return -4;
        }

        std::array<RE::NiPoint3, BodyLanguage::kTotalLandmarkVertices> verts{};
        for (std::size_t r = 0; r < BodyLanguage::kRingCount; ++r) {
            for (std::size_t c = 0; c < BodyLanguage::kPointsPerRing; ++c) {
                const std::size_t slot = r * BodyLanguage::kPointsPerRing + c;
                const std::uint16_t vIdx = tplCache->rings[r].vIdx[c];
                if (vIdx < targetMesh.vertices.size()) {
                    verts[slot] = targetMesh.vertices[vIdx].pos;
                } else {
                    verts[slot] = tplCache->verts1[slot];
                }
            }
        }

        std::array<RE::NiPoint3, BodyLanguage::kPointsPerRing> sysRing5{};
        constexpr std::size_t kRing5Base =
            static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kMidBelly) * BodyLanguage::kPointsPerRing;
        for (std::size_t c = 0; c < BodyLanguage::kPointsPerRing; ++c) {
            sysRing5[c] = verts[kRing5Base + c];
        }

        const float h0 = std::max(10.0f, tplCache->baseReferenceHeight);
        const auto metrics = BodyLanguage::ActorBodyEvaluator::ComputeMetricsFromVertices(
            verts,
            sysRing5,
            h0,
            isFemale != 0,
            isNaked != 0,
            100.0f,
            0.0f,
            1.0f);

        const auto& classifier = BodyLanguage::PhysiqueClassifier::GetInstance();
        const float toneIndex = BodyLanguage::PhysiqueClassifier::ComputeToneIndex(
            metrics.geoToneSignal, texToneScore);
        const auto kw = classifier.ClassifyRegions(metrics, texToneScore);
        const auto descPair = classifier.BuildBothDescriptions(metrics, texToneScore);

        const float toCm = h0 * BodyLanguage::kSkyrimUnitToCm;
        const float toIn = toCm * BodyLanguage::kCmToInches;
        const auto& r1 = metrics.rings[static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kShoulders)];
        const auto& r2 = metrics.rings[static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kChestBust)];
        const auto& r3 = metrics.rings[static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kUnderbust)];
        const auto& r4 = metrics.rings[static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kNaturalWaist)];
        const auto& r5 = metrics.rings[static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kMidBelly)];
        const auto& r6 = metrics.rings[static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kHipsGlutes)];
        const auto& r7 = metrics.rings[static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kUpperThigh)];
        const auto& r8 = metrics.rings[static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kCalf)];
        const auto& r9 = metrics.rings[static_cast<std::size_t>(BodyLanguage::AnatomicalRingId::kUpperArm)];

        nlohmann::json out = {
            {"measurements_cm", {
                {"shoulder_width", r1.width * toCm},
                {"chest", r2.girth * toCm},
                {"underbust", r3.girth * toCm},
                {"waist", r4.girth * toCm},
                {"belly", r5.girth * toCm},
                {"hips", r6.girth * toCm},
                {"thigh", r7.girth * toCm},
                {"calf", r8.girth * toCm},
                {"arm", r9.girth * toCm}
            }},
            {"measurements_in", {
                {"shoulder_width", r1.width * toIn},
                {"chest", r2.girth * toIn},
                {"underbust", r3.girth * toIn},
                {"waist", r4.girth * toIn},
                {"belly", r5.girth * toIn},
                {"hips", r6.girth * toIn},
                {"thigh", r7.girth * toIn},
                {"calf", r8.girth * toIn},
                {"arm", r9.girth * toIn}
            }},
            {"ratios", {
                {"bmi", metrics.bodyMassIndex},
                {"shldr", metrics.shoulderToHeight},
                {"vtaper", metrics.vTaperRatio},
                {"whr", metrics.waistToHipRatio},
                {"bustCup", metrics.bustToUnderbust},
                {"hips", metrics.hipsToHeight},
                {"thigh", metrics.upperThighGirth},
                {"calf", metrics.calfGirth},
                {"arm", metrics.upperArmGirth},
                {"bellyDrop", metrics.sagittalBellyDrop}
            }},
            {"geoToneSignal", metrics.geoToneSignal},
            {"toneIndex", toneIndex},
            {"keywords", kw},
            {"shortDesc", descPair.shortDesc},
            {"fullDesc", descPair.full}
        };

        const std::string dumped = out.dump();
        if (static_cast<int>(dumped.size()) >= outJsonBufferSize) {
            return -5;
        }
        std::memcpy(outJsonBuffer, dumped.data(), dumped.size());
        outJsonBuffer[dumped.size()] = '\0';
        return static_cast<int>(dumped.size());
    } catch (...) {
        return -99;
    }
}
