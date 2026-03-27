#pragma once

#include <atomic>
#include <string>

#include <RE/Skyrim.h>

#include "../extern/OBody-API/OBodyAPI.h"

namespace BodyLanguage {

    // Singleton wrapper around the OBody plugin interface.
    // Call Init() once at kPostPostLoad.
    // The interface is not safe to use until OBody fires OBodyIsReady();
    // IsAvailable() returns true only after that point.
    class OBodyService {
    public:
        static OBodyService& GetInstance();

        // Send the RequestPluginInterface SKSE message to OBody.
        // Must be called at kPostPostLoad.
        void Init();

        // Returns the preset name assigned to actor, or "" if unavailable.
        std::string GetPresetName(RE::Actor* actor) const;

        // Returns true if OBody considers the actor naked.
        bool IsNaked(RE::Actor* actor) const;

        // True once OBody has confirmed it is ready for API calls.
        bool IsAvailable() const;

    private:
        OBodyService() = default;

        // Readiness listener — notified by OBody when the interface is safe.
        class ReadinessListener : public OBody::API::IOBodyReadinessEventListener {
        public:
            void OBodyIsReady() override;
            void OBodyIsNoLongerReady() override;
        };

        OBody::API::IPluginInterface* m_interface{ nullptr };
        std::atomic<bool>             m_ready{ false };
        ReadinessListener             m_readinessListener;
    };

}  // namespace BodyLanguage
