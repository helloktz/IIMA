// Windows file/hash adaptation of IINA OpenSubSubtitle and ShooterSubtitle.
// Copyright (c) lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "SubtitleMediaHash.h"
#include "../../IIMA.Core/OnlineSubtitles/MediaHash.h"
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <limits>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace {
void Cancelled(std::stop_token stop) {
    if (stop.stop_requested()) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_CANCELLED));
}
bool DriveLetter(wchar_t ch) noexcept {
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}
void RequireLocalDrive(wchar_t letter) {
    const wchar_t root[]{letter, L':', L'\\', 0};
    const auto type = GetDriveTypeW(root);
    if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
        throw winrt::hresult_error(E_ACCESSDENIED, L"Subtitle hashing only reads local disk files");
}
struct StableFile {
    // Keep ancestor handles open without FILE_SHARE_DELETE to prevent changing
    // a verified ancestor into a junction while opening its next component.
    std::vector<winrt::handle> ancestors;
    winrt::handle file;
    FILE_BASIC_INFO before{};
    std::uint64_t size{};
};
StableFile OpenLocal(const std::filesystem::path& input, std::stop_token stop) {
    Cancelled(stop);
    const auto& text = input.native();
    if (text.size() < 4 || !DriveLetter(text[0]) || text[1] != L':' ||
        (text[2] != L'\\' && text[2] != L'/') || text.find(L'\0') != text.npos ||
        text.find(L':', 2) != text.npos)
        throw winrt::hresult_invalid_argument(L"Subtitle hashing requires an absolute local file path");
    RequireLocalDrive(text[0]);
    const auto path = input.lexically_normal();
    if (path.filename().empty() || path.filename() == L"." || path.filename() == L"..")
        throw winrt::hresult_invalid_argument(L"Subtitle hashing requires a media file");
    StableFile result;
    auto prefix = path.root_path();
    for (const auto& component : path.parent_path().relative_path()) {
        Cancelled(stop);
        prefix /= component;
        auto raw = CreateFileW(prefix.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (raw == INVALID_HANDLE_VALUE) winrt::throw_last_error();
        winrt::handle directory(raw);
        FILE_ATTRIBUTE_TAG_INFO info{};
        winrt::check_bool(GetFileInformationByHandleEx(directory.get(), FileAttributeTagInfo, &info, sizeof(info)));
        if (!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            info.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE))
            throw winrt::hresult_error(E_ACCESSDENIED, L"Linked and offline subtitle hash paths are not supported");
        result.ancestors.push_back(std::move(directory));
    }
    Cancelled(stop);
    auto raw = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (raw == INVALID_HANDLE_VALUE) winrt::throw_last_error();
    result.file.attach(raw);
    if (GetFileType(raw) != FILE_TYPE_DISK)
        throw winrt::hresult_error(E_ACCESSDENIED, L"Subtitle hashing requires an ordinary disk file");
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    winrt::check_bool(GetFileInformationByHandleEx(raw, FileAttributeTagInfo, &attributes, sizeof(attributes)));
    if (attributes.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE))
        throw winrt::hresult_error(E_ACCESSDENIED, L"Linked, offline and directory sources cannot be hashed");
    std::wstring finalPath(32768, L'\0');
    const auto count = GetFinalPathNameByHandleW(raw, finalPath.data(),
        static_cast<DWORD>(finalPath.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count >= finalPath.size())
        throw winrt::hresult_error(E_ACCESSDENIED, L"Cannot verify media hash source location");
    finalPath.resize(count);
    if (finalPath.size() < 7 || finalPath.compare(0, 4, L"\\\\?\\") != 0 ||
        !DriveLetter(finalPath[4]) || finalPath[5] != L':' || finalPath[6] != L'\\')
        throw winrt::hresult_error(E_ACCESSDENIED, L"Subtitle hash source resolved outside a local drive");
    RequireLocalDrive(finalPath[4]);
    FILE_STANDARD_INFO standard{};
    winrt::check_bool(GetFileInformationByHandleEx(raw, FileStandardInfo, &standard, sizeof(standard)));
    if (standard.Directory || standard.EndOfFile.QuadPart <= 0)
        throw winrt::hresult_invalid_argument(L"Subtitle hash source is empty or invalid");
    result.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    winrt::check_bool(GetFileInformationByHandleEx(raw, FileBasicInfo, &result.before, sizeof(result.before)));
    return result;
}
void ReadChunk(StableFile& source, std::uint64_t offset, std::span<std::uint8_t> output,
    std::stop_token stop) {
    Cancelled(stop);
    if (offset > source.size || output.size() > source.size - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))
        throw winrt::hresult_invalid_argument(L"Invalid subtitle hash chunk offset");
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    winrt::check_bool(SetFilePointerEx(source.file.get(), position, nullptr, FILE_BEGIN));
    std::size_t consumed{};
    while (consumed < output.size()) {
        Cancelled(stop);
        DWORD read{};
        const auto count = static_cast<DWORD>(std::min<std::size_t>(16 * 1024, output.size() - consumed));
        winrt::check_bool(ReadFile(source.file.get(), output.data() + consumed, count, &read, nullptr));
        if (!read) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_HANDLE_EOF), L"Media changed or was truncated while hashing");
        consumed += read;
    }
    Cancelled(stop);
}
void CheckStable(const StableFile& source, std::stop_token stop) {
    Cancelled(stop);
    FILE_STANDARD_INFO standard{};
    FILE_BASIC_INFO basic{};
    winrt::check_bool(GetFileInformationByHandleEx(source.file.get(), FileStandardInfo, &standard, sizeof(standard)));
    winrt::check_bool(GetFileInformationByHandleEx(source.file.get(), FileBasicInfo, &basic, sizeof(basic)));
    if (standard.EndOfFile.QuadPart < 0 || static_cast<std::uint64_t>(standard.EndOfFile.QuadPart) != source.size ||
        basic.LastWriteTime.QuadPart != source.before.LastWriteTime.QuadPart ||
        basic.ChangeTime.QuadPart != source.before.ChangeTime.QuadPart ||
        basic.FileAttributes != source.before.FileAttributes)
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_INVALID), L"Media changed while calculating its subtitle hash");
    Cancelled(stop);
}
void CheckCrypto(NTSTATUS status) {
    if (status < 0) throw winrt::hresult_error(E_FAIL, L"Windows could not calculate the subtitle MD5 hash");
}
struct Md5Provider {
    BCRYPT_ALG_HANDLE value{};
    Md5Provider() { CheckCrypto(BCryptOpenAlgorithmProvider(&value, BCRYPT_MD5_ALGORITHM, nullptr, 0)); }
    ~Md5Provider() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
    Md5Provider(const Md5Provider&) = delete;
    Md5Provider& operator=(const Md5Provider&) = delete;
};
struct Md5State {
    BCRYPT_HASH_HANDLE value{};
    ~Md5State() { if (value) BCryptDestroyHash(value); }
};
std::string Md5(BCRYPT_ALG_HANDLE provider, std::span<std::uint8_t> bytes) {
    // MD5 is required by Shooter's media-identification protocol, not used for
    // authentication, signatures or integrity of downloaded subtitle contents.
    DWORD objectSize{}, returned{}, hashSize{};
    CheckCrypto(BCryptGetProperty(provider, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &returned, 0));
    CheckCrypto(BCryptGetProperty(provider, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize), &returned, 0));
    if (!objectSize || objectSize > 1024 * 1024 || hashSize != 16)
        throw winrt::hresult_error(E_FAIL, L"Unexpected Windows MD5 provider layout");
    std::vector<std::uint8_t> object(objectSize);
    Md5State state;
    CheckCrypto(BCryptCreateHash(provider, &state.value, object.data(), objectSize, nullptr, 0, 0));
    CheckCrypto(BCryptHashData(state.value, bytes.data(), static_cast<ULONG>(bytes.size()), 0));
    std::array<std::uint8_t, 16> digest{};
    CheckCrypto(BCryptFinishHash(state.value, digest.data(), static_cast<ULONG>(digest.size()), 0));
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 0xf]; }
    return result;
}
}
namespace iima {
SubtitleMediaHash ReadOpenSubMovieHash(const std::filesystem::path& path, std::stop_token stop) {
    auto file = OpenLocal(path, stop);
    if (file.size <= OpenSubHashMinimumFileBytes)
        throw winrt::hresult_invalid_argument(L"OpenSubtitles hashing requires media larger than 128 KiB");
    // Heap storage avoids a 128 KiB Windows stack frame.
    std::vector<std::uint8_t> first(OpenSubHashChunkBytes), last(OpenSubHashChunkBytes);
    ReadChunk(file, 0, first, stop);
    ReadChunk(file, file.size - OpenSubHashChunkBytes, last, stop);
    const auto hash = OpenSubMovieHash(file.size, first, last);
    CheckStable(file, stop);
    return {file.size, hash};
}
SubtitleMediaHash ReadShooterMovieHash(const std::filesystem::path& path, std::stop_token stop) {
    auto file = OpenLocal(path, stop);
    if (file.size < ShooterHashMinimumFileBytes)
        throw winrt::hresult_invalid_argument(L"Shooter hashing requires media of at least 12 KiB");
    const auto offsets = ShooterChunkOffsets(file.size);
    Md5Provider provider;
    std::array<std::string, 4> digests;
    std::array<std::uint8_t, ShooterHashChunkBytes> chunk{};
    for (std::size_t index = 0; index != offsets.size(); ++index) {
        ReadChunk(file, offsets[index], chunk, stop);
        digests[index] = Md5(provider.value, chunk);
    }
    const auto hash = JoinShooterDigests(digests);
    CheckStable(file, stop);
    return {file.size, hash};
}
}
