// IINA window geometry behavior adapted to Windows physical-pixel work areas.
// Copyright (c) 2017 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "WindowPlacement.h"
#include <winrt/Windows.Data.Json.h>
#include <ShlObj.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#pragma comment(lib, "shell32.lib")

namespace iima {
namespace {
constexpr std::size_t MaximumDocumentBytes = 16 * 1024;
constexpr double MaximumCoordinate = 1000000;
std::atomic<std::uint64_t> captures{1};
std::mutex placementMutex;
std::uint64_t lastSavedCapture{};
std::uint64_t tempSerial{};
bool ValidRect(GeometryRect const& rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
        std::isfinite(rect.width) && std::isfinite(rect.height) &&
        std::abs(rect.x) <= MaximumCoordinate && std::abs(rect.y) <= MaximumCoordinate &&
        rect.width >= 1 && rect.height >= 1 && rect.width <= MaximumCoordinate &&
        rect.height <= MaximumCoordinate && std::abs(rect.x + rect.width) <= MaximumCoordinate &&
        std::abs(rect.y + rect.height) <= MaximumCoordinate;
}
bool ValidDpi(double dpi) noexcept { return std::isfinite(dpi) && dpi >= 48 && dpi <= 960; }
bool ValidSnapshot(WindowPlacementSnapshot const& value) noexcept {
    return ValidRect(value.bounds) && ValidRect(value.workArea) && ValidDpi(value.dpi);
}
struct FileHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    FileHandle() = default;
    FileHandle(FileHandle const&) = delete;
    FileHandle& operator=(FileHandle const&) = delete;
    void Close() noexcept { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); value = INVALID_HANDLE_VALUE; }
};
std::filesystem::path Path() {
    PWSTR local{};
    winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local));
    std::filesystem::path result;
    try { result = std::filesystem::path(local) / L"IIMA" / L"window-placement.json"; }
    catch (...) { CoTaskMemFree(local); throw; }
    CoTaskMemFree(local);
    return result;
}
using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonValue;
JsonObject EncodeRect(GeometryRect const& rect) {
    JsonObject object;
    object.Insert(L"x", JsonValue::CreateNumberValue(rect.x));
    object.Insert(L"y", JsonValue::CreateNumberValue(rect.y));
    object.Insert(L"width", JsonValue::CreateNumberValue(rect.width));
    object.Insert(L"height", JsonValue::CreateNumberValue(rect.height));
    return object;
}
GeometryRect DecodeRect(JsonObject const& object) {
    return {object.GetNamedNumber(L"x"), object.GetNamedNumber(L"y"),
        object.GetNamedNumber(L"width"), object.GetNamedNumber(L"height")};
}
std::optional<WindowPlacementSnapshot> ReadUnlocked(std::filesystem::path const& path) {
    FileHandle file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
        winrt::throw_hresult(HRESULT_FROM_WIN32(error));
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.value, &info)) winrt::throw_last_error();
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nFileSizeHigh || !info.nFileSizeLow || info.nFileSizeLow > MaximumDocumentBytes)
        throw std::runtime_error("Window placement file has an invalid type or size");
    std::string bytes(info.nFileSizeLow, '\0');
    std::size_t offset{};
    while (offset < bytes.size()) {
        DWORD read{};
        if (!ReadFile(file.value, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &read, nullptr))
            winrt::throw_last_error();
        if (!read) throw std::runtime_error("Window placement file was truncated");
        offset += read;
    }
    const auto json = JsonObject::Parse(winrt::to_hstring(bytes));
    if (json.GetNamedNumber(L"schemaVersion") != 1)
        throw std::runtime_error("Unsupported window placement schema");
    WindowPlacementSnapshot result{DecodeRect(json.GetNamedObject(L"bounds")),
        DecodeRect(json.GetNamedObject(L"workArea")), json.GetNamedNumber(L"dpi"), 0};
    if (!ValidSnapshot(result)) throw std::runtime_error("Invalid window placement coordinates");
    return result;
}
}
std::optional<WindowPlacementSnapshot> CaptureWindowPlacement(GeometryRect bounds,
    GeometryRect workArea, double dpi, WindowPlacementMode mode) noexcept {
    WindowPlacementSnapshot result{bounds, workArea, dpi};
    if (mode != WindowPlacementMode::Normal || !ValidSnapshot(result)) return {};
    result.captureOrder = captures.fetch_add(1, std::memory_order_relaxed);
    if (!result.captureOrder) return {}; // Never treat a wrapped token as disk-loaded state.
    return result;
}
std::optional<GeometryRect> RestoreWindowPlacement(WindowPlacementSnapshot const& saved,
    GeometryRect current, double dpi, GeometrySize minimum) noexcept {
    if (!ValidSnapshot(saved) || !ValidRect(current) || !ValidDpi(dpi) ||
        !std::isfinite(minimum.width) || !std::isfinite(minimum.height) ||
        minimum.width <= 0 || minimum.height <= 0) return {};
    GeometryRect result;
    const auto scale = dpi / saved.dpi;
    result.width = std::clamp(saved.bounds.width * scale, std::min(minimum.width, current.width), current.width);
    result.height = std::clamp(saved.bounds.height * scale, std::min(minimum.height, current.height), current.height);
    // Position uses the fraction of available travel, not desktop absolute values.
    // A removed monitor or changed taskbar cannot strand the title bar off-screen.
    auto fraction = [](double origin, double oldOrigin, double size, double oldSize) {
        const auto travel = oldSize - size;
        return travel > 0 ? std::clamp((origin - oldOrigin) / travel, 0.0, 1.0) : 0.5;
    };
    result.x = current.x + fraction(saved.bounds.x, saved.workArea.x, saved.bounds.width,
        saved.workArea.width) * (current.width - result.width);
    result.y = current.y + fraction(saved.bounds.y, saved.workArea.y, saved.bounds.height,
        saved.workArea.height) * (current.height - result.height);
    // Windows MoveAndResize uses integral pixels. Round inward to retain bounds.
    result.width = std::max(1.0, std::floor(result.width));
    result.height = std::max(1.0, std::floor(result.height));
    result.x = std::clamp(std::round(result.x), current.x, current.x + current.width - result.width);
    result.y = std::clamp(std::round(result.y), current.y, current.y + current.height - result.height);
    return ValidRect(result) ? std::optional{result} : std::nullopt;
}
std::optional<WindowPlacementSnapshot> WindowPlacementStore::Load() {
    std::lock_guard lock(placementMutex);
    return ReadUnlocked(Path());
}
bool WindowPlacementStore::Save(WindowPlacementSnapshot const& snapshot) {
    if (!ValidSnapshot(snapshot)) throw std::runtime_error("Invalid window placement");
    std::lock_guard lock(placementMutex);
    if (!snapshot.captureOrder || snapshot.captureOrder <= lastSavedCapture) return false;
    const auto path = Path();
    // Preserve malformed or newer data instead of destroying it on close.
    (void)ReadUnlocked(path);
    JsonObject json;
    json.Insert(L"schemaVersion", JsonValue::CreateNumberValue(1));
    json.Insert(L"bounds", EncodeRect(snapshot.bounds));
    json.Insert(L"workArea", EncodeRect(snapshot.workArea));
    json.Insert(L"dpi", JsonValue::CreateNumberValue(snapshot.dpi));
    const auto bytes = winrt::to_string(json.Stringify());
    if (bytes.size() > MaximumDocumentBytes) throw std::runtime_error("Window placement exceeds size limit");
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++tempSerial);
    FileHandle file;
    file.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) winrt::throw_last_error();
    try {
        std::size_t offset{};
        while (offset < bytes.size()) {
            DWORD written{};
            if (!WriteFile(file.value, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr))
                winrt::throw_last_error();
            if (!written) throw std::runtime_error("Could not finish writing window placement");
            offset += written;
        }
        if (!FlushFileBuffers(file.value)) winrt::throw_last_error();
        file.Close();
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            winrt::throw_last_error();
        lastSavedCapture = snapshot.captureOrder;
        return true;
    } catch (...) { file.Close(); DeleteFileW(temporary.c_str()); throw; }
}
}
