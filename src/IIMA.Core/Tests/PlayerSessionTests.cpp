#include "../Playback/PlayerSession.h"
#include <cassert>
#include <limits>
using namespace iima;
namespace {
MpvClient::Sink events;
std::vector<std::pair<std::uint64_t,std::vector<std::string>>> commands;
std::uint64_t nextId{1};
bool pauseWhenOpen{};
void Reply(std::uint64_t id,std::int64_t entry=-1) {
    MpvEvent e{MpvEvent::Kind::CommandReply}; e.requestId=id; e.entryId=entry; events(e);
}
}
// Test double only: exercise the session's command contracts without a DLL or UI.
namespace iima {
struct MpvClient::State {};
MpvClient::MpvClient(Sink sink) { events=std::move(sink); }
MpvClient::~MpvClient() = default;
void MpvClient::SetPauseWhenOpen(bool value) { pauseWhenOpen=value; }
void MpvClient::Start(std::wstring) { MpvEvent e{MpvEvent::Kind::Ready}; e.flag=true; events(e); }
std::uint64_t MpvClient::Command(std::vector<std::string> args) { auto id=nextId++; commands.emplace_back(id,std::move(args)); return id; }
void MpvClient::CloseAsync(std::function<void()> completion) { if(completion) completion(); }
}
int main() {
    auto session=PlayerSession::Create([](auto work){work();},[](auto const&){});
    session->Start(L"test");
    session->OpenMultiple({"C:/中文 空格/a.mp4","C:/b.mp4"});
    assert(commands.size()==1 && commands.back().second==std::vector<std::string>({"set","pause","yes"}));
    auto pause=commands.back().first;
    Reply(pause); assert(commands.size()==4);
    assert(commands[1].second==std::vector<std::string>({"set","force-window","yes"}));
    assert(commands[2].second==std::vector<std::string>({"loadfile","C:/中文 空格/a.mp4","replace"}));
    assert(commands[3].second.back()=="append");
    assert(session->Snapshot().media==MediaState::Empty);
    MpvEvent start{MpvEvent::Kind::StartFile}; start.entryId=10; events(start);
    MpvEvent loaded{MpvEvent::Kind::Loaded}; loaded.swapChain=std::make_shared<int>(1);
    events(loaded); session->SetPresentationReady(true);
    assert(commands.size()==4); // Await command result identity, not just loaded.
    Reply(commands[2].first,10);
    assert(commands.back().second==std::vector<std::string>({"set","pause","no"}));
    auto count=commands.size(); session->SetPresentationReady(true); assert(commands.size()==count);
    session->Seek(20); assert(commands.size()==count); // Unknown duration.
    MpvEvent duration{MpvEvent::Kind::Property}; duration.name="duration"; duration.number=60; events(duration);
    session->Seek(120); assert(commands.back().second[1]=="60.000000");
    session->Seek(std::numeric_limits<double>::quiet_NaN()); assert(commands.back().second[1]=="60.000000");
    MpvEvent playlist{MpvEvent::Kind::Property}; playlist.name="playlist";
    playlist.playlist={{10,"a","",true},{11,"b","",false},{12,"c","",false}}; events(playlist);
    session->PlaylistMove(0,2); assert(commands.back().second==std::vector<std::string>({"playlist-move","0","3"}));
    session->PlaylistPrevious(); assert(commands.back().second[0]=="seek");
    session->PlaylistRemove(99); assert(commands.back().second[0]=="seek");
    MpvEvent tracks{MpvEvent::Kind::Property}; tracks.name="track-list";
    tracks.tracks={{2,"sub","","","C:/字幕.srt",true,true}}; events(tracks);
    session->AddSubtitle("C:/字幕.srt"); assert(commands.back().second==std::vector<std::string>({"sub-reload","2"}));
    session->SelectTrack("sub",0); assert(commands.back().second==std::vector<std::string>({"set","sid","no"}));
    session->TogglePause(); session->TogglePause(); assert(commands.back().second[0]=="cycle");
    session->SetMaxVolume(200); session->SetVolume(250);
    assert(commands.back().second==std::vector<std::string>({"set","volume","200.000000"}));
    assert(session->Snapshot().maxVolume==100 && session->Snapshot().volume==100);
    count=commands.size(); session->SetMaxVolume(50); session->SetAudioDelay(std::numeric_limits<double>::infinity());
    assert(commands.size()==count);
    session->SetMuted(true); assert(commands.back().second==std::vector<std::string>({"set","mute","yes"}));
    assert(!session->Snapshot().muted);
    session->SetAutoPlayNext(false); assert(commands.back().second==std::vector<std::string>({"set","keep-open","always"}));
    session->SetRepeatMode(RepeatMode::Playlist);
    assert(commands[commands.size()-2].second==std::vector<std::string>({"set","loop-playlist","inf"}));
    assert(commands.back().second==std::vector<std::string>({"set","loop-file","no"}));
    tracks.tracks.push_back({1,"video","","","",true,false}); events(tracks);
    session->SetScreenshotMode(ScreenshotMode::Subtitles); session->ScreenshotToFile("C:/shot.png");
    assert(commands.back().second.back()=="subtitles");
    session->SetScreenshotMode(ScreenshotMode::Window); session->ScreenshotToFile("C:/shot.png");
    assert(commands.back().second.back()=="window");
    session->SetPauseWhenOpen(true); assert(pauseWhenOpen);
    session->Open("C:/paused.mp4"); auto newPause=commands.back().first; Reply(newPause);
    auto newLoad=commands.back().first;
    start.entryId=20; events(start); events(loaded); session->SetPresentationReady(true);
    count=commands.size(); Reply(newLoad,20); assert(commands.size()==count); // No auto-resume.
    bool closed{}; session->CloseAsync([&]{closed=true;});
    assert(closed && session->Snapshot().session==SessionState::Closed);
    count=commands.size(); session->Open("x"); session->Pause(false); assert(commands.size()==count);
    auto preconfigured=PlayerSession::Create([](auto work){work();},[](auto const&){});
    UserSettings settings; settings.pauseWhenOpen=true; settings.autoPlayNext=false; settings.maxVolume=150; settings.volume=120;
    preconfigured->ApplySettings(settings); assert(commands.size()==count);
    preconfigured->Start(L"test"); assert(commands.size()>count && pauseWhenOpen);
    assert(preconfigured->Snapshot().volume==100); // Still waiting for observed properties.
    count=commands.size(); settings.subtitleDelay=std::numeric_limits<double>::infinity();
    preconfigured->ApplySettings(settings); assert(commands.size()==count && !preconfigured->Snapshot().error.empty());
    preconfigured->CloseAsync();
}
