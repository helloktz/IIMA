// Memory snapshot semantics adapt IINA Logger / LogWindowController; GPL v3.
// Disk diagnostics retain IIMA's bounded, best-effort, non-telemetry policy.
#include "DiagnosticLog.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace iima {
namespace {
constexpr std::uintmax_t Limit=1024*1024;
constexpr std::size_t RecordLimit=1024;
std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string Safe(std::string const& text) {
    // Preserve complete Unicode scalars at the byte bound. Invalid UTF-8 is
    // replaced bytewise, so neither the viewer nor a saved text file receives
    // an incomplete character. No user data redaction is implied by this helper.
    std::string result;
    result.reserve(std::min<std::size_t>(text.size(),512));
    for(std::size_t i=0;i<text.size() && result.size()<512;) {
        const auto first=static_cast<unsigned char>(text[i]);
        if(first<0x80) {
            result.push_back(first<32 || first==127 ? ' ' : text[i]);
            ++i; continue;
        }
        std::size_t length{};
        std::uint32_t scalar{};
        if(first>=0xC2 && first<=0xDF) {length=2;scalar=first&0x1F;}
        else if(first>=0xE0 && first<=0xEF) {length=3;scalar=first&0x0F;}
        else if(first>=0xF0 && first<=0xF4) {length=4;scalar=first&0x07;}
        bool valid=length!=0 && text.size()-i>=length;
        if(valid) for(std::size_t n=1;n<length;++n) {
            const auto next=static_cast<unsigned char>(text[i+n]);
            if((next&0xC0)!=0x80) {valid=false;break;}
            scalar=(scalar<<6)|(next&0x3F);
        }
        valid=valid && !(length==3 && scalar<0x800) &&
            !(length==4 && scalar<0x10000) && scalar<=0x10FFFF &&
            !(scalar>=0xD800 && scalar<=0xDFFF);
        if(!valid) {result.push_back('?');++i;continue;}
        if(result.size()+length>512) break;
        result.append(text,i,length); i+=length;
    }
    return result;
}
}
struct DiagnosticLog::State {
    std::filesystem::path directory;
    std::function<void(std::string)> failure;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::string> queue;
    std::deque<DiagnosticLogRecord> records;
    std::uint64_t sequence{},revision{},evicted{},diskQueueDropped{};
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
                if(lost) pending.push_front(std::to_string(NowMs())+" queue-dropped "+std::to_string(lost));
                for(auto const& entry:pending) {
                    auto line=entry+"\n";
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
        { std::lock_guard lock(mutex); closed=true; closing=true; ++revision; queue.clear(); done.swap(completions); }
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
    category=Safe(category); value=Safe(value);
    { std::lock_guard lock(state_->mutex);
      if(state_->closing) return;
      const auto timestamp=NowMs();
      auto line=std::to_string(timestamp)+" "+category+" "+value;
      state_->records.push_back({++state_->sequence,timestamp,std::move(category),std::move(value)});
      if(state_->records.size()>RecordLimit) {state_->records.pop_front();++state_->evicted;}
      ++state_->revision;
      if(state_->queue.size()>=256) {++state_->dropped;++state_->diskQueueDropped;return;}
      state_->queue.push_back(std::move(line)); }
    state_->changed.notify_one();
}
DiagnosticLogSnapshot DiagnosticLog::Snapshot() const {
    std::lock_guard lock(state_->mutex);
    DiagnosticLogSnapshot result;
    result.records.assign(state_->records.begin(),state_->records.end());
    result.revision=state_->revision;
    result.evicted=state_->evicted;
    result.diskQueueDropped=state_->diskQueueDropped;
    result.closing=state_->closing;
    result.closed=state_->closed;
    return result;
}
void DiagnosticLog::CloseAsync(std::function<void()> complete) {
    bool immediate{};
    {std::lock_guard lock(state_->mutex);
     if(!state_->closing) {state_->closing=true;++state_->revision;}
     immediate=state_->closed;
     if(!immediate && complete) state_->completions.push_back(complete);}
    state_->changed.notify_one();
    if(immediate && complete) complete();
}
}
