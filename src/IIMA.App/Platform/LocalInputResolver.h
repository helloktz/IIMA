#pragma once
#include <winrt/Microsoft.UI.Dispatching.h>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>
namespace iima {
struct LocalInputResult {
    std::vector<std::string> paths;
    std::vector<std::wstring> warnings;
    bool cancelled{};
    // Proven from the original single input's file attributes, not expansion
    // result count. Never true for a folder, playlist, URL or unknown extension.
    bool singleExplicitMedia{};
};
class LocalInputResolver {
public:
    // Does not retain the window. Completion is queued to its UI dispatcher.
    // Caller cancels previous jobs and rejects old generations after a new open.
    // regularMediaOnly is for history replay: never enumerate a replaced folder
    // or hand a playlist/reparse target to mpv.
    // Cancellation is cooperative between filesystem calls, not an I/O timeout.
    static std::stop_source Resolve(std::vector<std::wstring> inputs,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
        std::function<void(LocalInputResult)> completion, bool regularMediaOnly = false);
};
}
