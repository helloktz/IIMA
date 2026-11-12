#include "pch.h"
#include "UserDataStore.h"
#include "UserDataMerge.h"
#include "../Settings/PreferenceJson.h"
#include "../Settings/MediaPresetsJson.h"
#include <winrt/Windows.Data.Json.h>
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")
#include <filesystem>
#include <fstream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <map>
#include <array>

namespace iima {
namespace {
using namespace winrt;
using namespace Windows::Data::Json;
std::mutex ioMutex;
std::atomic<unsigned long> serial{};
// Only documents whose exact bytes were successfully preserved during Load
// may be replaced with recovered defaults later in this process.
std::map<std::filesystem::path, std::filesystem::path> preservedDocuments;
bool PreservedUnchanged(std::filesystem::path const& path) {
    auto found = preservedDocuments.find(path);
    if (found == preservedDocuments.end()) return false;
    std::ifstream current(path, std::ios::binary), backup(found->second, std::ios::binary);
    if (!current || !backup) return false;
    std::array<char, 16384> a{}, b{};
    do {
        current.read(a.data(), a.size()); backup.read(b.data(), b.size());
        if (current.bad() || backup.bad() || current.gcount() != backup.gcount() ||
            !std::equal(a.begin(), a.begin() + current.gcount(), b.begin())) return false;
    } while (!current.eof() && !backup.eof());
    return current.eof() && backup.eof();
}
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
    preservedDocuments.insert_or_assign(path, backup);
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
namespace {
UserData LoadUnlocked(bool recover) {
    UserData data;
    auto root=Root();
    try {
        if(auto json=Read(root/L"settings.json")) {
            if (json->HasKey(L"advancedPreferences")) {
                std::vector<settings::PreferenceIssue> issues;
                auto preferences = ReadPreferenceJson(json->GetNamedObject(L"advancedPreferences"), issues);
                data.settings.advancedPreferences = preferences.Overrides();
                if (!issues.empty()) data.warning = "部分高级偏好无效，已分别恢复默认值。";
            }
            if (json->HasKey(L"savedVideoFilters")) data.settings.savedVideoFilters = ReadSavedFiltersJson(json->GetNamedArray(L"savedVideoFilters"));
            if (json->HasKey(L"savedAudioFilters")) data.settings.savedAudioFilters = ReadSavedFiltersJson(json->GetNamedArray(L"savedAudioFilters"));
            if (json->HasKey(L"userEqualizers")) data.settings.userEqualizers = ReadEqualizersJson(json->GetNamedObject(L"userEqualizers"));
            if (json->HasKey(L"bindingLibrary")) {
                auto library = json->GetNamedObject(L"bindingLibrary");
                if (library.Size() > 32) throw std::runtime_error("Too many binding configurations");
                for (auto const& pair : library)
                    data.settings.bindingLibrary.emplace(to_string(pair.Key()), to_string(pair.Value().GetString()));
            }
            if (json->HasKey(L"inputConf")) data.settings.inputConf = to_string(json->GetNamedString(L"inputConf"));
            if (json->HasKey(L"watchProperties")) {
                auto names = json->GetNamedArray(L"watchProperties");
                if (names.Size() > 32) throw std::runtime_error("Too many watched properties");
                for (auto const& name : names) data.settings.watchProperties.push_back(to_string(name.GetString()));
            }
            data.settings.volume=json->GetNamedNumber(L"volume",100);
            data.settings.speed=json->GetNamedNumber(L"speed",1);
            data.settings.recordHistory=json->GetNamedBoolean(L"recordHistory",true);
            data.settings.resumePosition=json->GetNamedBoolean(L"resumePosition",true);
            data.settings.pauseWhenOpen=json->GetNamedBoolean(L"pauseWhenOpen",false);
            data.settings.autoPlayNext=json->GetNamedBoolean(L"autoPlayNext",true);
            data.settings.muted=json->GetNamedBoolean(L"muted",false);
            data.settings.subtitlesVisible=json->GetNamedBoolean(L"subtitlesVisible",true);
            data.settings.maxVolume=json->GetNamedNumber(L"maxVolume",100);
            data.settings.audioDelay=json->GetNamedNumber(L"audioDelay",0);
            data.settings.subtitleDelay=json->GetNamedNumber(L"subtitleDelay",0);
            auto repeat=json->GetNamedString(L"repeatMode",L"off");
            if(repeat==L"off") data.settings.repeatMode=RepeatMode::Off;
            else if(repeat==L"file") data.settings.repeatMode=RepeatMode::File;
            else if(repeat==L"playlist") data.settings.repeatMode=RepeatMode::Playlist;
            else throw std::runtime_error("Invalid repeat mode");
            auto screenshot=json->GetNamedString(L"screenshotMode",L"subtitles");
            if(screenshot==L"subtitles") data.settings.screenshotMode=ScreenshotMode::Subtitles;
            else if(screenshot==L"video") data.settings.screenshotMode=ScreenshotMode::Video;
            else if(screenshot==L"window") data.settings.screenshotMode=ScreenshotMode::Window;
            else throw std::runtime_error("Invalid screenshot mode");
            if(!ValidUserSettings(data.settings)) throw std::runtime_error("Invalid settings range");
        }
    } catch(...) {
        if (!recover && !PreservedUnchanged(root/L"settings.json")) throw;
        if (recover) Preserve(root/L"settings.json");
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
        if (!recover && !PreservedUnchanged(root/L"history.json")) throw;
        if (recover) Preserve(root/L"history.json");
        data.history.clear(); data.warning+=" 播放历史无法读取，原文件已保留。";
    }
    return data;
}
void SaveUnlocked(UserData const& data) {
    if(!data.writable) throw std::runtime_error("Refusing to overwrite user data that could not be safely loaded");
    auto root=Root();
    if(!ValidUserSettings(data.settings)) throw std::runtime_error("Invalid settings range");
    auto settings=Document();
    settings.Insert(L"savedVideoFilters", WriteSavedFiltersJson(data.settings.savedVideoFilters));
    settings.Insert(L"savedAudioFilters", WriteSavedFiltersJson(data.settings.savedAudioFilters));
    settings.Insert(L"userEqualizers", WriteEqualizersJson(data.settings.userEqualizers));
    settings.Insert(L"advancedPreferences", WritePreferenceJson(data.settings.advancedPreferences));
    JsonObject bindingLibrary;
    for (auto const& [name, text] : data.settings.bindingLibrary)
        bindingLibrary.Insert(to_hstring(name), JsonValue::CreateStringValue(to_hstring(text)));
    settings.Insert(L"bindingLibrary", bindingLibrary);
    if (data.settings.inputConf) settings.Insert(L"inputConf", JsonValue::CreateStringValue(to_hstring(*data.settings.inputConf)));
    JsonArray watchNames;
    for (auto const& name : data.settings.watchProperties) watchNames.Append(JsonValue::CreateStringValue(to_hstring(name)));
    settings.Insert(L"watchProperties", watchNames);
    settings.Insert(L"volume",JsonValue::CreateNumberValue(data.settings.volume));
    settings.Insert(L"speed",JsonValue::CreateNumberValue(data.settings.speed));
    settings.Insert(L"recordHistory",JsonValue::CreateBooleanValue(data.settings.recordHistory));
    settings.Insert(L"resumePosition",JsonValue::CreateBooleanValue(data.settings.resumePosition));
    settings.Insert(L"pauseWhenOpen",JsonValue::CreateBooleanValue(data.settings.pauseWhenOpen));
    settings.Insert(L"autoPlayNext",JsonValue::CreateBooleanValue(data.settings.autoPlayNext));
    settings.Insert(L"muted",JsonValue::CreateBooleanValue(data.settings.muted));
    settings.Insert(L"subtitlesVisible",JsonValue::CreateBooleanValue(data.settings.subtitlesVisible));
    settings.Insert(L"maxVolume",JsonValue::CreateNumberValue(data.settings.maxVolume));
    settings.Insert(L"audioDelay",JsonValue::CreateNumberValue(data.settings.audioDelay));
    settings.Insert(L"subtitleDelay",JsonValue::CreateNumberValue(data.settings.subtitleDelay));
    settings.Insert(L"repeatMode",JsonValue::CreateStringValue(data.settings.repeatMode==RepeatMode::File ? L"file" :
        data.settings.repeatMode==RepeatMode::Playlist ? L"playlist" : L"off"));
    settings.Insert(L"screenshotMode",JsonValue::CreateStringValue(data.settings.screenshotMode==ScreenshotMode::Video ? L"video" :
        data.settings.screenshotMode==ScreenshotMode::Window ? L"window" : L"subtitles"));
    AtomicWrite(root/L"settings.json",settings);
    preservedDocuments.erase(root/L"settings.json");
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
    preservedDocuments.erase(root/L"history.json");
}

} // namespace
UserData UserDataStore::Load() {
    std::lock_guard lock(ioMutex);
    return LoadUnlocked(true);
}
void UserDataStore::Save(UserData const& data) {
    std::lock_guard lock(ioMutex);
    SaveUnlocked(data);
}
UserData UserDataStore::SaveMerged(UserData const& base, UserData const& edited) {
    std::lock_guard lock(ioMutex);
    // Do not recover new corruption here. Only an unchanged document already
    // backed up by Load may still use its recovered defaults.
    auto merged = MergeUserData(base, edited, LoadUnlocked(false));
    SaveUnlocked(merged);
    return merged;
}
} // namespace iima
