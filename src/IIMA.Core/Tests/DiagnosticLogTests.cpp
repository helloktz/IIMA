#include "../Diagnostics/DiagnosticLog.h"
#include <cassert>
#include <future>
#include <fstream>
#include <iterator>
#include <chrono>
int main() {
    auto root=std::filesystem::temp_directory_path()/("iima-log-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    auto path=root/"player.log";
    // Test directory contains only this suite's generated files.
    {std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<std::string(1024*1024,'x');}
    iima::DiagnosticLog log(root);
    log.Write("state","ready\nspoof");
    std::promise<void> done;
    log.CloseAsync([&]{done.set_value();}); done.get_future().get();
    log.Write("ignored","after-close");
    std::ifstream file(path);std::string text{std::istreambuf_iterator<char>(file),{}};
    assert(text.find("state ready spoof")!=std::string::npos);
    assert(text.find("ignored")==std::string::npos);
    assert(std::filesystem::file_size(root/"player.previous.log")==1024*1024);
    bool immediate=false;log.CloseAsync([&]{immediate=true;});assert(immediate);
    file.close();std::filesystem::remove(path);std::filesystem::remove(root/"player.previous.log");std::filesystem::remove(root);
}
