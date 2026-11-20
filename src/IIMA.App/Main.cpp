#include "pch.h"
#include "App.xaml.h"
#include "Platform/InstanceRouter.h"
#include "Platform/InstanceProtocol.h"
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
namespace {
std::vector<std::wstring> InitialPaths()
{
    int count{};
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) winrt::throw_last_error();
    std::vector<std::wstring> paths;
    size_t encodedBytes = 12;
    try {
        if (count > 257) throw winrt::hresult_invalid_argument(iima::loc::Text(L"OpensAMaximumOf256FilesOr"));
        for (int i = 1; i < count; ++i) {
            // Resolve relative inputs in the SENDER's working directory before
            // forwarding. No shell evaluation or filesystem IO occurs here.
            DWORD required = GetFullPathNameW(arguments[i], 0, nullptr, nullptr);
            if (!required || required > 32768) throw winrt::hresult_invalid_argument(iima::loc::Text(L"LaunchPathIsInvalidOrTooLong"));
            std::wstring absolute(required, L'\0');
            auto length = GetFullPathNameW(arguments[i], required, absolute.data(), nullptr);
            if (!length || length >= required) throw winrt::hresult_invalid_argument(iima::loc::Text(L"CouldNotResolveTheStartupPath"));
            absolute.resize(length);
            if (!iima::activation::ValidPath(std::u16string(absolute.begin(), absolute.end())))
                throw winrt::hresult_invalid_argument(iima::loc::Text(L"StartParametersMustBeAValidLocal"));
            encodedBytes += sizeof(uint32_t) + absolute.size() * sizeof(char16_t);
            if (encodedBytes > iima::activation::MaxPayload) throw winrt::hresult_invalid_argument(iima::loc::Text(L"StartsTheFileListMoreThan1"));
            paths.push_back(std::move(absolute));
        }
    } catch (...) { LocalFree(arguments); throw; }
    LocalFree(arguments);
    return paths;
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        iima::loc::Initialize();
        auto inputs = InitialPaths();
        auto router = std::make_shared<iima::InstanceRouter>();
        if (!router->IsPrimary()) {
            auto result = router->Forward(inputs);
            if (result == iima::InstanceRouter::ForwardResult::Accepted) return 0;
            auto message = result == iima::InstanceRouter::ForwardResult::Uncertain
                ? iima::loc::Text(L"OpenTheRequestMayHaveBeenDelivered")
                : iima::loc::Text(L"TheCurrentIIMADoesNotAcceptThe");
            MessageBoxW(nullptr, message, L"IIMA", MB_OK | MB_ICONWARNING);
            return 1;
        }
        winrt::Microsoft::UI::Xaml::Application::Start([router, inputs = std::move(inputs)](auto const&) mutable {
            winrt::make<winrt::IIMA::implementation::App>(router, std::move(inputs));
        });
        router->StopAccepting();
        return 0;
    } catch (winrt::hresult_error const& error) {
        MessageBoxW(nullptr, error.message().c_str(), iima::loc::Text(L"CouldNotStartIIMA"), MB_OK | MB_ICONERROR);
    } catch (std::exception const& error) {
        MessageBoxW(nullptr, winrt::to_hstring(error.what()).c_str(), iima::loc::Text(L"CouldNotStartIIMA"), MB_OK | MB_ICONERROR);
    }
    return 1;
}
