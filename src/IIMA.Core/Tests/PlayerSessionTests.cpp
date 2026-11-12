#include "../Playback/PlayerSession.h"
#include <cassert>
#include <limits>
using namespace iima;
namespace {
MpvClient::Sink events;
std::vector<std::pair<std::uint64_t,std::vector<std::string>>> commands;
std::uint64_t nextId{1};
bool pauseWhenOpen{};
bool rejectNextCommand{};
void Reply(std::uint64_t id,std::int64_t entry=-1,int error=0) {
    MpvEvent e{MpvEvent::Kind::CommandReply}; e.requestId=id; e.entryId=entry; e.error=error; e.text=error<0 ? "command failed" : ""; events(e);
}
}
// Test double only: exercise the session's command contracts without a DLL or UI.
namespace iima {
struct MpvClient::State {};
MpvClient::MpvClient(Sink sink) { events=std::move(sink); }
MpvClient::~MpvClient() = default;
void MpvClient::SetPauseWhenOpen(bool value) { pauseWhenOpen=value; }
void MpvClient::Start(std::wstring) { MpvEvent e{MpvEvent::Kind::Ready}; e.flag=true; events(e); }
std::uint64_t MpvClient::Command(std::vector<std::string> args) {
    if(rejectNextCommand) { rejectNextCommand=false; return 0; }
    auto id=nextId++; commands.emplace_back(id,std::move(args)); return id; }
void MpvClient::CloseAsync(std::function<void()> completion) { if(completion) completion(); }
}
namespace {
std::shared_ptr<PlayerSession> LoadedSession() {
    commands.clear();
    auto s=PlayerSession::Create([](auto work){work();},[](auto const&){});
    s->Start(L"test");
    MpvEvent start{MpvEvent::Kind::StartFile}; start.entryId=10; events(start);
    MpvEvent loaded{MpvEvent::Kind::Loaded}; loaded.swapChain=std::make_shared<int>(1); events(loaded);
    MpvEvent list{MpvEvent::Kind::Property}; list.name="playlist";
    list.playlist={{10,"a","",true},{11,"b","",false},{12,"c","",false}}; events(list);
    s->SetPresentationReady(true);
    return s;
}
void LoadAndBind(std::shared_ptr<PlayerSession> const& s,std::int64_t entry) {
    MpvEvent start{MpvEvent::Kind::StartFile}; start.entryId=entry; events(start);
    MpvEvent loaded{MpvEvent::Kind::Loaded}; loaded.swapChain=std::make_shared<int>(2); events(loaded);
    s->SetPresentationReady(true);
}
std::size_t ResumeCount() {
    std::size_t count{};
    for(auto const& command:commands)
        if(command.second==std::vector<std::string>({"set","pause","no"})) ++count;
    return count;
}
void TransitionRegressions() {
    // Reloading the same entry must await a fresh lifecycle, even if the old
    // swapchain is rebound and the command reply arrives first.
    {
        auto s=LoadedSession(); s->PlaylistPlay(0); Reply(commands.back().first);
        const auto change=commands.back().first;
        s->SetPresentationReady(true); assert(ResumeCount()==0);
        Reply(change); assert(ResumeCount()==0);
        LoadAndBind(s,10); assert(ResumeCount()==1);
        s->SetPresentationReady(true); assert(ResumeCount()==1);
        s->CloseAsync();
    }
    // Events may precede the reply. A failed switch cannot resume even if a
    // matching entry is loaded and bound, or more notifications arrive later.
    for(bool failed:{false,true}) {
        auto s=LoadedSession(); s->PlaylistNext(); Reply(commands.back().first);
        const auto change=commands.back().first;
        LoadAndBind(s,11); assert(ResumeCount()==0);
        Reply(change,-1,failed ? -1 : 0); assert(ResumeCount()==(failed ? 0u : 1u));
        s->SetPresentationReady(false); s->SetPresentationReady(true);
        assert(ResumeCount()==(failed ? 0u : 1u));
        if(failed) assert(s->Snapshot().error=="command failed");
        s->CloseAsync();
    }
    // A rejected pause must not submit the replacement command.
    {
        auto s=LoadedSession(); s->Open("b"); auto count=commands.size();
        Reply(commands.back().first,-1,-1); assert(commands.size()==count);
        LoadAndBind(s,11); assert(ResumeCount()==0); s->CloseAsync();
    }
    // loadfile errors have the same no-resume guarantee as playlist errors.
    {
        auto s=LoadedSession(); s->Open("broken"); Reply(commands.back().first);
        const auto load=commands.back().first;
        LoadAndBind(s,11); Reply(load,11,-1);
        s->SetPresentationReady(true); assert(ResumeCount()==0); s->CloseAsync();
    }
    // A client closing between CanOpen and command submission returns no ID.
    // Neither a rejected pause nor a rejected playlist command may arm recovery.
    for(bool rejectPause:{true,false}) {
        auto s=LoadedSession();
        rejectNextCommand=rejectPause; s->PlaylistPlay(1);
        if(!rejectPause) { rejectNextCommand=true; Reply(commands.back().first); }
        LoadAndBind(s,11); assert(ResumeCount()==0); s->CloseAsync();
    }
    // A→B→C while B is pending: B's reply cannot grant C's auto-resume.
    {
        auto s=LoadedSession(); s->Open("b"); Reply(commands.back().first);
        const auto b=commands.back().first;
        s->Open("c"); const auto cPause=commands.back().first;
        Reply(b,11); LoadAndBind(s,11); assert(ResumeCount()==0);
        Reply(cPause); const auto c=commands.back().first;
        LoadAndBind(s,12); assert(ResumeCount()==0);
        Reply(c,12); assert(ResumeCount()==1); s->CloseAsync();
    }
    // Superseded pause reply must not submit either the old or new load.
    {
        auto s=LoadedSession(); s->Open("b"); const auto old=commands.back().first;
        s->Open("c"); const auto current=commands.back().first; auto count=commands.size();
        Reply(old); assert(commands.size()==count);
        Reply(current); assert(commands.back().second==std::vector<std::string>({"loadfile","c","replace"}));
        s->CloseAsync();
    }
    // User pause, Stop, and Close all cancel deferred auto-resume. In particular
    // a late successful reply must never undo one of these user decisions.
    for(int cancel=0;cancel<3;++cancel) {
        auto s=LoadedSession(); s->PlaylistPlay(1); Reply(commands.back().first);
        const auto change=commands.back().first;
        if(cancel==0) s->Pause(true);
        else if(cancel==1) s->Stop();
        else s->CloseAsync();
        Reply(change); LoadAndBind(s,11); assert(ResumeCount()==0);
        s->CloseAsync();
    }
}
}
int main() {
    TransitionRegressions();
    commands.clear();
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
