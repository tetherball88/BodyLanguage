#include "OBodyService.h"

#include <spdlog/spdlog.h>

namespace BodyLanguage {

OBodyService& OBodyService::GetInstance()
{
    static OBodyService instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Readiness listener
// ---------------------------------------------------------------------------

void OBodyService::ReadinessListener::OBodyIsReady()
{
    auto& svc = OBodyService::GetInstance();
    svc.m_ready.store(true, std::memory_order_release);
    spdlog::info("[OBody] OBodyIsReady – API is now safe to use.");
}

void OBodyService::ReadinessListener::OBodyIsNoLongerReady()
{
    auto& svc = OBodyService::GetInstance();
    svc.m_ready.store(false, std::memory_order_release);
    spdlog::info("[OBody] OBodyIsNoLongerReady – API suspended (save/load in progress).");
}

// ---------------------------------------------------------------------------
// Init – send RequestPluginInterface via SKSE messaging
// ---------------------------------------------------------------------------

void OBodyService::Init()
{
    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        spdlog::warn("[OBody] SKSE messaging interface unavailable.");
        return;
    }

    OBody::API::SKSEMessages::RequestPluginInterface req{};
    req.version               = OBody::API::PluginAPIVersion::Latest;
    req.pluginInterface       = &m_interface;
    req.readinessEventListener = &m_readinessListener;

    messaging->Dispatch(
        OBody::API::SKSEMessages::RequestPluginInterface::type,
        &req,
        sizeof(req),
        "OBody");

    if (m_interface) {
        spdlog::info("[OBody] Interface acquired (API v{}).",
            static_cast<uint32_t>(m_interface->PluginAPIVersion()));
        // OBody may have already fired OBodyIsReady synchronously above.
    } else {
        spdlog::warn("[OBody] No interface returned – OBody is probably not installed.");
    }
}

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

std::string OBodyService::GetPresetName(RE::Actor* actor) const
{
    if (!m_ready.load(std::memory_order_acquire) || !m_interface || !actor)
        return "";

    OBody::API::PresetAssignmentInformation info{};
    info.flags = OBody::API::PresetAssignmentInformation::Flags::None;
    m_interface->GetPresetAssignedToActor(actor, info);
    return std::string(info.presetName);
}

bool OBodyService::IsNaked(RE::Actor* actor) const
{
    if (!m_ready.load(std::memory_order_acquire) || !m_interface || !actor)
        return false;
    return m_interface->ActorIsNaked(actor);
}

bool OBodyService::IsAvailable() const
{
    return m_ready.load(std::memory_order_acquire) && m_interface != nullptr;
}

}  // namespace BodyLanguage
