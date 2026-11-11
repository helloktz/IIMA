#include "../Persistence/UserData.h"
#include <cassert>
#include <limits>
int main() {
    iima::UserData data;
    assert(data.settings.volume==100 && data.settings.recordHistory && data.settings.resumePosition);
    iima::RecordHistory(data,{"C:/影片/a.mp4",10,60,100});
    iima::RecordHistory(data,{"C:/b.mp4",20,60,101});
    iima::RecordHistory(data,{"C:/影片/a.mp4",30,60,102});
    assert(data.history.size()==2 && data.history.front().position==30);
    assert(iima::ResumePosition(data,"C:/影片/a.mp4")==30);
    iima::RecordHistory(data,{"C:/影片/a.mp4",90,60,103});
    assert(data.history.front().position==60 && !iima::ResumePosition(data,"C:/影片/a.mp4"));
    data.settings.recordHistory=false;
    iima::RecordHistory(data,{"C:/ignored.mp4",10,60,104});
    assert(data.history.size()==2);
    data.settings.recordHistory=true;
    iima::RecordHistory(data,{"",10,60,104});
    iima::RecordHistory(data,{"invalid",std::numeric_limits<double>::infinity(),60,104});
    assert(data.history.size()==2);
    data.settings.resumePosition=false;
    assert(!iima::ResumePosition(data,"C:/b.mp4"));
    for(int i=0;i<1100;++i) iima::RecordHistory(data,{std::to_string(i),1,2,i});
    assert(data.history.size()==1000 && data.history.front().path=="1099");
}
