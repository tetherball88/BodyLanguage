#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "RE/Skyrim.h"

namespace SKEE {

    class IPluginInterface {
    public:
        IPluginInterface() = default;
        virtual ~IPluginInterface() = default;
        virtual std::uint32_t GetVersion() = 0;
        virtual void Revert() = 0;
    };

    class IInterfaceMap {
    public:
        virtual IPluginInterface* QueryInterface(const char* name) = 0;
        virtual bool AddInterface(const char* name, IPluginInterface* pluginInterface) = 0;
        virtual IPluginInterface* RemoveInterface(const char* name) = 0;
    };

    struct InterfaceExchangeMessage {
        enum : std::uint32_t { kMessage_ExchangeInterface = 0x9E3779B9 };
        IInterfaceMap* interfaceMap{nullptr};
    };

    class IBodyMorphInterface : public IPluginInterface {
    public:
        class MorphKeyVisitor {
        public:
            virtual void Visit(const char* key, float value) = 0;
        };

        class MorphValueVisitor {
        public:
            virtual void Visit(RE::TESObjectREFR* actor, const char* morphName, const char* key, float value) = 0;
        };

        class MorphVisitor {
        public:
            virtual void Visit(RE::TESObjectREFR* actor, const char* morphName) = 0;
        };

        // Version 4 (RaceMenu SE/AE standard vtable layout):
        virtual void SetMorph(RE::TESObjectREFR* actor, const char* morphName, const char* key, float relative) = 0;
        virtual float GetMorph(RE::TESObjectREFR* actor, const char* morphName, const char* key) = 0;
        virtual void ClearMorph(RE::TESObjectREFR* actor, const char* morphName, const char* key) = 0;
        virtual float GetBodyMorphs(RE::TESObjectREFR* actor, const char* morphName) = 0;
        virtual void ClearBodyMorphNames(RE::TESObjectREFR* actor, const char* morphName) = 0;
        virtual void VisitMorphs(RE::TESObjectREFR* actor, MorphVisitor& visitor) = 0;
        virtual void VisitKeys(RE::TESObjectREFR* actor, const char* morphName, MorphKeyVisitor& visitor) = 0;
        virtual void VisitMorphValues(RE::TESObjectREFR* actor, MorphValueVisitor& visitor) = 0;
        virtual void ClearMorphs(RE::TESObjectREFR* actor) = 0;
        virtual void ApplyVertexDiff(RE::TESObjectREFR* actor, RE::NiAVObject* rootNode, bool erase = false) = 0;
        virtual void ApplyBodyMorphs(RE::TESObjectREFR* actor, bool deferUpdate = true) = 0;
        virtual void UpdateModelWeight(RE::TESObjectREFR* actor, bool immediate = false) = 0;
        virtual void SetCacheLimit(std::uint64_t limit) = 0;
        virtual bool HasMorphs(RE::TESObjectREFR* actor) = 0;
        virtual std::uint32_t EvaluateBodyMorphs(RE::TESObjectREFR* actor) = 0;
        virtual bool HasBodyMorph(RE::TESObjectREFR* actor, const char* morphName, const char* key) = 0;
        virtual bool HasBodyMorphName(RE::TESObjectREFR* actor, const char* morphName) = 0;
        virtual bool HasBodyMorphKey(RE::TESObjectREFR* actor, const char* key) = 0;
        virtual void ClearBodyMorphKeys(RE::TESObjectREFR* actor, const char* key) = 0;
        virtual void VisitStrings(void* visitor) = 0;
        virtual void VisitActors(void* visitor) = 0;
        virtual std::uint64_t ClearMorphCache() = 0;
    };

}  // namespace SKEE

