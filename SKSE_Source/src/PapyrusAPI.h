#pragma once

#include "RE/Skyrim.h"

namespace BodyLanguage {

    // Registers all BodyLanguage global native Papyrus functions on the "BodyLanguage" script.
    // Pass this callback to SKSE::GetPapyrusInterface()->Register(...) during SKSEPluginLoad.
    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm);

}  // namespace BodyLanguage
