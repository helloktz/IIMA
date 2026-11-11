#include "pch.h"
#include "UserDataStore.h"
#include <winrt/Windows.Data.Json.h>
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")
#include <filesystem>
#include <fstream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>

namespace iima {
namespace {
using namespace winrt;
using namespace Windows::Data::Json;
std::mutex ioMutex;
std::atomic<unsigned long> serial{};
std::filesystem::path Root() {
    PWSTR local{};
    check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&local));
    std::filesystem::path root;
    try { root=std::filesystem::path(local)/L"IIMA"; }
    catch(...) { CoTaskMemFree(local); throw; }
    CoTaskMemFree(local);
    return root;
}
std::optional<JsonObject> Read(std::filesystem::path const& path) {
    if(!std::filesystem::exists(path)) return {};
    if(std::filesystem::file_size(path)>8*1024*1024) throw std::runtime_error("User data exceeds size limit");
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("Cannot read user data");
    std::string text{std::istreambuf_iterator<char>(file),{}};
    auto json=JsonObject::Parse(to_hstring(text));
    if(json.GetNamedNumber(L"schemaVersion")!=1) throw std::runtime_error("Unsupported user data schema");
    return json;
}
void Preserve(std::filesystem::path const& path) {
    if(!std::filesystem::exists(path)) return;
    auto backup=path;
    backup+=L".corrupt-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(serial++);
    std::filesystem::copy_file(path,backup); // failure propagates; never overwrite an unreadable file silently
}
void AtomicWrite(std::filesystem::path const& path, JsonObject const& json) {
    std::filesystem::create_directories(path.parent_path());
    auto temp=path;
    temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(serial++);
    auto bytes=to_string(json.Stringify());
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) throw_last_error();
    try {
        size_t offset=0;
        while(offset<bytes.size()) {
            DWORD written{};
            if(!WriteFile(file,bytes.data()+offset,static_cast<DWORD>(bytes.size()-offset),&written,nullptr) || !written) throw_last_error();
            offset+=written;
        }
        if(!FlushFileBuffers(file)) throw_last_error();
        CloseHandle(file); file=INVALID_HANDLE_VALUE;
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw_last_error();
    } catch(...) {
        if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
        DeleteFileW(temp.c_str());
        throw;
    }
}
JsonObject Document() { JsonObject json; json.Insert(L"schemaVersion",JsonValue::CreateNumberValue(1)); return json; }
}
UserData UserDataStore::Load() {
    std::lock_guard lock(ioMutex);
    UserData data;
    auto root=Root();
    try {
        if(auto json=Read(root/L"settings.json")) {
            data.settings.volume=json->GetNamedNumber(L"volume",100);
            data.settings.speed=json->GetNamedNumber(L"speed",1);
            data.settings.recordHistory=json->GetNamedBoolean(L"recordHistory",true);
            data.settings.resumePosition=json->GetNamedBoolean(L"resumePosition",true);
            if(!std::isfinite(data.settings.volume) || data.settings.volume<0 || data.settings.volume>100 ||
               !std::isfinite(data.settings.speed) || data.settings.speed<0.01 || data.settings.speed>100)
                throw std::runtime_error("Invalid settings range");
        }
    } catch(...) {
        Preserve(root/L"settings.json");
        data.settings={}; data.warning="设置文件无法读取，已保留原文件并使用默认设置。";
    }
    try {
        if(auto json=Read(root/L"history.json")) {
            auto entries=json->GetNamedArray(L"entries");
            if(entries.Size()>1000) throw std::runtime_error("Too many history entries");
            for(auto const& value:entries) {
                auto item=value.GetObject();
                HistoryEntry entry;
                entry.path=to_string(item.GetNamedString(L"path"));
                entry.position=item.GetNamedNumber(L"position");
                double time=item.GetNamedNumber(L"playedAt",0);
                if(!std::isfinite(time) || time<0 || time>1e12) throw std::runtime_error("Invalid timestamp");
                entry.playedAt=static_cast<std::int64_t>(time);
                if(item.HasKey(L"duration")) entry.duration=item.GetNamedNumber(L"duration");
                if(entry.path.empty() || entry.path.find('\0')!=std::string::npos || !std::isfinite(entry.position) || entry.position<0 ||
                   (entry.duration && (!std::isfinite(*entry.duration) || *entry.duration<0))) throw std::runtime_error("Invalid history entry");
                data.history.push_back(std::move(entry));
            }
        }
    } catch(...) {
        Preserve(root/L"history.json");
        data.history.clear(); data.warning+=" 播放历史无法读取，原文件已保留。";
    }
    return data;
}
void UserDataStore::Save(UserData const& data) {
    std::lock_guard lock(ioMutex);
    auto root=Root();
    auto settings=Document();
    settings.Insert(L"volume",JsonValue::CreateNumberValue(data.settings.volume));
    settings.Insert(L"speed",JsonValue::CreateNumberValue(data.settings.speed));
    settings.Insert(L"recordHistory",JsonValue::CreateBooleanValue(data.settings.recordHistory));
    settings.Insert(L"resumePosition",JsonValue::CreateBooleanValue(data.settings.resumePosition));
    AtomicWrite(root/L"settings.json",settings);
    auto history=Document(); JsonArray entries;
    for(auto const& entry:data.history) {
        JsonObject item;
        item.Insert(L"path",JsonValue::CreateStringValue(to_hstring(entry.path)));
        item.Insert(L"position",JsonValue::CreateNumberValue(entry.position));
        item.Insert(L"playedAt",JsonValue::CreateNumberValue(static_cast<double>(entry.playedAt)));
        if(entry.duration) item.Insert(L"duration",JsonValue::CreateNumberValue(*entry.duration));
        entries.Append(item);
    }
    history.Insert(L"entries",entries); AtomicWrite(root/L"history.json",history);
}
}
