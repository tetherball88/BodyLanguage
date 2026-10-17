#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Geometry/BodyTemplateCache.h"

namespace BodyLanguage {

    class NifRingIndexer {
    public:
        static NifRingIndexer& GetInstance();

        // Called at kDataLoaded: scans all race skins, custom NPC skins, and standalone body ARMA forms
        // occupying slot 32 (kBody) and pre-populates the 80-vertex ring cache.
        void IndexAllBodyTemplates();

        // Thread-safe lookup or on-demand indexing of a naked body NIF path.
        std::shared_ptr<const BodyTemplateCache> GetOrIndexTemplate(
            std::string_view nifModelPath,
            bool isFemale);

    private:
        NifRingIndexer() = default;

        std::shared_ptr<const BodyTemplateCache> BuildTemplateCache(
            std::string_view canonicalKey,
            std::string_view rawModelPath,
            bool isFemale);

        mutable std::shared_mutex m_cacheMutex;
        std::unordered_map<std::string, std::shared_ptr<const BodyTemplateCache>> m_cache;
    };

}  // namespace BodyLanguage

