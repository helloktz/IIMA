#include "pch.h"
#include "AtomicTextFile.h"
#include <algorithm>
#include <objbase.h>
namespace iima {
void WriteUtf8Atomically(std::filesystem::path const& destination, std::string_view text) {
    GUID id{};
    winrt::check_hresult(CoCreateGuid(&id));
    wchar_t identifier[40]{};
    if (!StringFromGUID2(id, identifier, static_cast<int>(std::size(identifier))))
        throw winrt::hresult_error(E_FAIL, L"Cannot allocate temporary file name");
    auto temporary = destination.parent_path() / (std::wstring(L".iima-") + identifier + L".tmp");
    auto raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE) winrt::throw_last_error();
    winrt::handle file(raw);
    try {
        std::size_t offset{};
        while (offset < text.size()) {
            DWORD written{};
            auto count = static_cast<DWORD>(std::min<std::size_t>(text.size() - offset, 1024 * 1024));
            winrt::check_bool(WriteFile(file.get(), text.data() + offset, count, &written, nullptr));
            if (!written) throw winrt::hresult_error(E_FAIL, L"Cannot complete file write");
            offset += written;
        }
        winrt::check_bool(FlushFileBuffers(file.get()));
        file.close();
        winrt::check_bool(MoveFileExW(temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    } catch (...) {
        file.close();
        DeleteFileW(temporary.c_str());
        throw;
    }
}
}
