#pragma once

#include <filesystem>

namespace BodyLanguage {

    // Scans all .xml files in <dataRoot>/Bodyslide Preset/CalienteTools/BodySlide/SliderPresets,
    // extracts every <Preset name="..."> attribute, and writes (or merges into)
    // <dataRoot>/SKSE/Plugins/BodyLanguage/descriptions/3ba.json.
    // Existing keys in 3ba.json are preserved; only missing preset names are added.
    void ScanAndGeneratePresetDescriptions(const std::filesystem::path& dataRoot);

}  // namespace BodyLanguage
