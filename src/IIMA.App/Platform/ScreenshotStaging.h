// Windows temporary-file adaptation of IINA screenshotCacheURL/callback.
// Source logic: IINA contributors, GPL-3.0, references/iina/LICENSE.
#pragma once
#include <filesystem>
#include <string_view>

namespace iima {
// Keep this object alive until the mpv reply or completed shutdown. Destruction
// before that barrier may race the worker writing the file. Move-only owner.
class ScreenshotStaging final {
public:
    static ScreenshotStaging Create(std::wstring_view extension = L"png");
    ScreenshotStaging(ScreenshotStaging&& other) noexcept;
    ScreenshotStaging& operator=(ScreenshotStaging&& other) noexcept;
    ~ScreenshotStaging();
    ScreenshotStaging(const ScreenshotStaging&) = delete;
    ScreenshotStaging& operator=(const ScreenshotStaging&) = delete;
    const std::filesystem::path& Path() const noexcept;
    // Publish an explicitly user-chosen destination. Synchronous; worker only.
    // Copy+flush+rename uses a temp file next to the destination so a cross-
    // volume source does not make the final replacement non-atomic.
    void Publish(const std::filesystem::path& destination) const;
    // Worker only. Explicitly selected, existing absolute directory; no links.
    static void ValidateDirectory(const std::filesystem::path& directory);
    // Managed automatic output: atomically publishes without replacing an
    // existing file, including across windows. Returns the actual unique path.
    std::filesystem::path PublishUnique(const std::filesystem::path& directory,
        std::wstring_view leaf) const;
private:
    ScreenshotStaging(std::filesystem::path directory, std::filesystem::path path);
    void Cleanup() noexcept;
    std::filesystem::path directory_;
    std::filesystem::path path_;
};
}
