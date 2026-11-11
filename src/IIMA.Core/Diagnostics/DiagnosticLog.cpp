#include "DiagnosticLog.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace iima {
namespace {
constexpr std::uintmax_t Limit=1024*1024;
std::string Safe(std::string text) {
    if(text.size()>512) text.resize(512);
    for(auto& c:text) if(static_cast<unsigned char>(c)<32 || c==127) c=' ';
    return text;
}
}
struct DiagnosticLog::State {
    std::filesystem::path directory;
    std::function<void(std::string)> failure;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::string> queue;
    std::vector<std::function<void()>> completions;
    bool closing{},closed{};
    std::size_t dropped{};
    void Run() noexcept {
        try {
            std::filesystem::create_directories(directory);
            auto path=directory/"player.log";
            std::ofstream stream;
            std::uintmax_t size=std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
            auto rotate=[&] {
                stream.close();
                auto old=directory/"player.previous.log";
                if(std::filesystem::exists(old)) std::filesystem::remove(old);
                if(std::filesystem::exists(path)) std::filesystem::rename(path,old);
                size=0;
            };
            if(size>=Limit) rotate();
            stream.open(path,std::ios::binary|std::ios::app);
            if(!stream) throw std::runtime_error("Cannot open diagnostic log");
            while(true) {
                std::deque<std::string> pending; std::size_t lost{}; bool stopping{};
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock,[&]{return closing || !queue.empty();});
                    pending.swap(queue); lost=dropped; dropped=0; stopping=closing;
                }
                if(lost) pending.push_front("queue-dropped "+std::to_string(lost));
                for(auto const& entry:pending) {
                    auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    auto line=std::to_string(stamp)+" "+entry+"\n";
                    if(size+line.size()>Limit) {
                        rotate(); stream.open(path,std::ios::binary|std::ios::app);
                        if(!stream) throw std::runtime_error("Cannot rotate diagnostic log");
                    }
                    stream.write(line.data(),static_cast<std::streamsize>(line.size()));
                    if(!stream) throw std::runtime_error("Cannot write diagnostic log");
                    size+=line.size();
                }
                stream.flush();
                if(!stream) throw std::runtime_error("Cannot flush diagnostic log");
                if(stopping) break;
            }
            stream.close();
        } catch(std::exception const& error) { if(failure) try {failure(error.what());} catch(...) {} }
        catch(...) { if(failure) try {failure("Diagnostic logging failed");} catch(...) {} }
        std::vector<std::function<void()>> done;
        { std::lock_guard lock(mutex); closed=true; closing=true; queue.clear(); done.swap(completions); }
        for(auto& complete:done) try {complete();} catch(...) {}
    }
};
DiagnosticLog::DiagnosticLog(std::filesystem::path directory,std::function<void(std::string)> failure)
    :state_(std::make_shared<State>()) {
    state_->directory=std::move(directory); state_->failure=std::move(failure);
    std::thread([state=state_]{state->Run();}).detach();
}
DiagnosticLog::~DiagnosticLog() {CloseAsync();}
void DiagnosticLog::Write(std::string category,std::string value) {
    auto line=Safe(std::move(category))+" "+Safe(std::move(value));
    { std::lock_guard lock(state_->mutex);
      if(state_->closing) return;
      if(state_->queue.size()>=256) {++state_->dropped;return;}
      state_->queue.push_back(std::move(line)); }
    state_->changed.notify_one();
}
void DiagnosticLog::CloseAsync(std::function<void()> complete) {
    bool immediate{};
    {std::lock_guard lock(state_->mutex);state_->closing=true;immediate=state_->closed;
     if(!immediate && complete) state_->completions.push_back(complete);}
    state_->changed.notify_one();
    if(immediate && complete) complete();
}
}
