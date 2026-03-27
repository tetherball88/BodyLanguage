#pragma once
// Verbatim from https://github.com/Aietos/OBody-NG (GPL-3.0)
// Reduced to the declarations needed for external plugin interop.

#include <cstdint>
#include <limits>
#include <string_view>

using Actor   = RE::Actor;
using TESForm = RE::TESForm;

namespace OBody::API {

    enum class PluginAPIVersion { Invalid = 0, v1 = 1, Latest = v1 };

    class IActorChangeEventListener;
    struct PresetCounts;
    enum PresetCategory : uint64_t;
    struct PresetAssignmentInformation;
    struct AssignPresetPayload;

    // Version-independent base (contains owner / context fields).
    class IPluginInterfaceVersionIndependent {
    public:
        const char* owner;
        void*       context = nullptr;

        virtual PluginAPIVersion PluginAPIVersion() = 0;
        virtual const char*      SetOwner(const char* owner) = 0;
    };

    class IPluginInterface : public IPluginInterfaceVersionIndependent {
    public:
        virtual bool ActorIsNaked(Actor* actor) = 0;
        virtual bool ActorIsNaked(Actor* actor, bool actorIsEquippingArmor,
                                  const TESForm* armor) = 0;
        virtual bool ActorHasORefitApplied(Actor* actor) = 0;
        virtual bool ActorIsProcessed(Actor* actor) = 0;
        virtual bool ActorIsBlacklisted(Actor* actor) = 0;
        virtual bool IsORefitEnabled() = 0;

        virtual bool RegisterEventListener(IActorChangeEventListener& eventListener) = 0;
        virtual bool DeregisterEventListener(IActorChangeEventListener& eventListener) = 0;
        virtual bool HasRegisteredEventListener(IActorChangeEventListener& eventListener) = 0;

        virtual void   GetPresetCounts(PresetCounts& payload) = 0;
        virtual size_t GetPresetNames(PresetCategory category, std::string_view* buffer,
                                      size_t bufferLength, size_t offset = 0,
                                      size_t limit = (std::numeric_limits<size_t>::max)()) = 0;

        virtual void EnsureActorIsProcessed(Actor* actor) = 0;
        virtual void ApplyOBodyMorphsToActor(Actor* actor) = 0;
        virtual void RemoveOBodyMorphsFromActor(Actor* actor) = 0;
        virtual void ForcefullyChangeORefitForActor(Actor* actor,
                                                    bool orefitShouldBeApplied) = 0;

        // IMPORTANT: you MUST initialise payload.flags before calling this.
        virtual void GetPresetAssignedToActor(Actor* actor,
                                              PresetAssignmentInformation& payload) = 0;
        virtual bool AssignPresetToActor(Actor* actor, AssignPresetPayload& payload) = 0;
    };

    // Implement this to be notified when it is safe (or no longer safe) to
    // call methods on IPluginInterface.
    class IOBodyReadinessEventListener {
    public:
        virtual void OBodyIsReady() = 0;
        virtual void OBodyIsNoLongerReady() = 0;
        virtual void OBodyIsBecomingReady() {}
        virtual void OBodyIsBecomingUnready() {}
    };

    struct PresetCounts {
        uint32_t female;
        uint32_t femaleBlacklisted;
        uint32_t male;
        uint32_t maleBlacklisted;
    };

    enum PresetCategory : uint64_t {
        PresetCategoryNone               = 0,
        PresetCategoryFemale             = 1 << 0,
        PresetCategoryFemaleBlacklisted  = 1 << 1,
        PresetCategoryMale               = 1 << 2,
        PresetCategoryMaleBlacklisted    = 1 << 3
    };

    struct PresetAssignmentInformation {
        enum Flags : uint64_t {
            None     = 0,
            IsFemale = 1 << 0
        };
        // You MUST initialise this before passing to GetPresetAssignedToActor.
        Flags            flags = Flags::None;
        std::string_view presetName;
    };

    struct AssignPresetPayload {
        enum Flags : uint64_t {
            None                              = 0,
            ForceImmediateApplicationOfMorphs = 1 << 0,
            DoNotApplyMorphs                  = 1 << 1
        };
        Flags            flags = Flags::ForceImmediateApplicationOfMorphs;
        std::string_view presetName;
    };

    // IActorChangeEventListener — default no-op implementations so you only
    // override the events you care about.
    class IActorChangeEventListener {
    public:
        struct OnActorGenerated {
            struct Payload {
                IPluginInterfaceVersionIndependent* responsiblePluginInterface;
                const std::string_view              presetName;
            };
            enum Flags : uint64_t { None = 0, IsClothed = 1<<0, IsORefitApplied = 1<<1, IsORefitEnabled = 1<<2 };
            enum class Response : uint64_t { None = 0 };
        };
        virtual OnActorGenerated::Response OnActorGenerated(
            [[maybe_unused]] Actor* actor,
            [[maybe_unused]] OnActorGenerated::Flags flags,
            [[maybe_unused]] OnActorGenerated::Payload& payload) { return OnActorGenerated::Response::None; }
    };

    // Entry-point: send a RequestPluginInterface SKSE message to "OBody".
    namespace SKSEMessages {
        struct RequestPluginInterface {
            static constexpr uint32_t type = 0xc0B0D9cc;

            PluginAPIVersion             version;
            IPluginInterface**           pluginInterface;
            IOBodyReadinessEventListener* readinessEventListener;
        };
    }

}  // namespace OBody::API
