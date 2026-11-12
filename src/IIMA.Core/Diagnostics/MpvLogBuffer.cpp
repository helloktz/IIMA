// IINA MPVController.handleEvent / LogWindowController bounded memory projection.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "MpvLogBuffer.h"
#include <chrono>
#include <string_view>
namespace iima {
namespace {
std::string Display(char const* source,std::size_t limit,bool& truncated) {
    if (!source) return {};
    std::size_t length{};
    while (length<limit && source[length]) ++length;
    if (length==limit) truncated=true; // Never scan an unbounded borrowed field.
    std::string out;
    for (std::size_t i=0;i<length;) {
        const auto first=static_cast<unsigned char>(source[i]);
        unsigned count=first<0x80 ? 1 : first>=0xc2 && first<=0xdf ? 2 : first>=0xe0 && first<=0xef ? 3 : first>=0xf0 && first<=0xf4 ? 4 : 0;
        std::uint32_t code=count==1 ? first : count ? first & ((1u<<(7-count))-1) : 0;
        bool valid=count && i+count<=length;
        for (unsigned n=1;valid && n<count;++n) {
            auto c=static_cast<unsigned char>(source[i+n]);
            if ((c&0xc0)!=0x80) valid=false; else code=(code<<6)|(c&0x3f);
        }
        valid=valid && !(count==2 && code<0x80) && !(count==3 && code<0x800) &&
            !(count==4 && code<0x10000) && code<=0x10ffff && !(code>=0xd800 && code<=0xdfff);
        std::string_view piece;
        if (!valid) { piece="\xef\xbf\xbd";count=1; }
        else if (code<32 || (code>=0x7f && code<=0x9f) || code==0x61c || code==0x200e || code==0x200f ||
            (code>=0x202a && code<=0x202e) || (code>=0x2066 && code<=0x2069)) {
            piece=code=='\n' ? "\\n" : code=='\r' ? "\\r" : code=='\t' ? "\\t" : "?";
        } else piece=std::string_view(source+i,count);
        if (piece.size()>limit-out.size()) { truncated=true;break; }
        out+=piece;i+=count;
    }
    return out;
}
}
bool MpvLogBuffer::ValidLevel(std::string const& level) noexcept {
    return level=="fatal" || level=="error" || level=="warn" || level=="info" || level=="v" || level=="debug" || level=="trace";
}
bool MpvLogBuffer::Start(std::string level) {
    if (!ValidLevel(level)) return false;
    std::lock_guard lock(mutex_);if(closed_) return false;
    level_=std::move(level);records_.clear();bytes_=0;dropped_=0;truncated_=0;error_.clear();
    capturing_=false;pending_=true;++epoch_;++revision_;return true;
}
void MpvLogBuffer::Stop() {
    std::lock_guard lock(mutex_);if(closed_) return;
    level_="no";capturing_=false;pending_=true;error_.clear();++epoch_;++revision_;
}
void MpvLogBuffer::Clear() {
    std::lock_guard lock(mutex_);if(closed_) return;
    records_.clear();bytes_=0;dropped_=0;truncated_=0;error_.clear();capturing_=false;
    pending_=true;++epoch_;++revision_;
}
void MpvLogBuffer::Close() {
    std::lock_guard lock(mutex_);
    if(closed_) return;
    closed_=true;capturing_=false;pending_=false;level_="no";error_.clear();
    records_.clear();bytes_=0;dropped_=0;truncated_=0;++epoch_;++revision_;
}
std::pair<std::uint64_t,std::string> MpvLogBuffer::Desired() const {
    std::lock_guard lock(mutex_);return {epoch_,level_};
}
void MpvLogBuffer::Complete(std::uint64_t epoch,int error) {
    std::lock_guard lock(mutex_);if(closed_ || epoch!=epoch_) return;
    pending_=false;capturing_=error>=0 && level_!="no";
    error_=error<0 ? "mpv log subscription failed ("+std::to_string(error)+")" : "";++revision_;
}
void MpvLogBuffer::Append(std::uint64_t epoch,int level,char const* prefix,char const* text) {
    std::lock_guard lock(mutex_);if(closed_ || pending_ || !capturing_ || epoch!=epoch_) return;
    MpvLogRecord record;
    record.prefix=Display(prefix,128,record.truncated);record.text=Display(text,4096,record.truncated);
    // mpv reports its separate log-buffer overflow using this synthetic prefix.
    // The message does not provide a stable machine-readable exact lost count.
    if(record.prefix=="overflow") ++dropped_;
    record.level=level;record.sequence=++sequence_;
    record.timestampMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto bytes=record.prefix.size()+record.text.size();
    while (!records_.empty() && (records_.size()>=1024 || bytes_+bytes>1024*1024)) {
        bytes_-=records_.front().prefix.size()+records_.front().text.size();records_.pop_front();++dropped_;
    }
    if(record.truncated) ++truncated_;
    records_.push_back(std::move(record));bytes_+=bytes;++revision_;
}
void MpvLogBuffer::Overflow(std::uint64_t epoch) {
    std::lock_guard lock(mutex_);if(closed_ || pending_ || !capturing_ || epoch!=epoch_) return;
    ++dropped_;++revision_; // At least one native event lost; not an exact count.
}
MpvLogSnapshot MpvLogBuffer::Snapshot() const {
    std::lock_guard lock(mutex_);
    MpvLogSnapshot snapshot;
    snapshot.records.assign(records_.begin(),records_.end());snapshot.revision=revision_;
    snapshot.dropped=dropped_;snapshot.truncated=truncated_;snapshot.capturing=capturing_;
    snapshot.pending=pending_;snapshot.closed=closed_;snapshot.level=level_;snapshot.error=error_;return snapshot;
}
}
