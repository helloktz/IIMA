// IINA ThumbnailCache / CacheManager platform adaptation. GPL-3.0.
// Copyright (c) 2017 lhc and IINA contributors; references/iina/LICENSE.
#pragma once
#include "ThumbnailExtractor.h"
#include <cstddef>

namespace iima {
// Worker-thread only. 0 disables both reads and writes, as an explicit privacy
// choice. Failure to access/cache never prevents uncached preview extraction.
ThumbnailExtractionResult ExtractThumbnailCached(ThumbnailRequest const& request,
    std::int64_t maxCacheMebibytes = 500, std::stop_token stop = {});
struct ThumbnailDiskCacheClearResult {
    std::size_t deletedFiles{}, failedFiles{};
    std::wstring error;
};
// Worker-thread only. Deletes only owned-format regular files inside the
// dedicated versioned cache directory; never recursively deletes a directory.
ThumbnailDiskCacheClearResult ClearThumbnailDiskCache();
}
