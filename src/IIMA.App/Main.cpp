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
        if (count > 257) throw winrt::hresult_invalid_argument(L"一次最多打开 256 个文件或文件夹。");
        for (int i = 1; i < count; ++i) {
            // Resolve relative inputs in the SENDER's working directory before
            // forwarding. No shell evaluation or filesystem IO occurs here.
            DWORD required = GetFullPathNameW(arguments[i], 0, nullptr, nullptr);
            if (!required || required > 32768) throw winrt::hresult_invalid_argument(L"启动路径无效或过长。");
            std::wstring absolute(required, L'\0');
            auto length = GetFullPathNameW(arguments[i], required, absolute.data(), nullptr);
            if (!length || length >= required) throw winrt::hresult_invalid_argument(L"无法解析启动路径。");
            absolute.resize(length);
            if (!iima::activation::ValidPath(std::u16string(absolute.begin(), absolute.end())))
                throw winrt::hresult_invalid_argument(L"启动参数必须是有效的本地或 UNC 文件系统路径。");
            encodedBytes += sizeof(uint32_t) + absolute.size() * sizeof(char16_t);
            if (encodedBytes > iima::activation::MaxPayload) throw winrt::hresult_invalid_argument(L"启动文件列表超过 1 MiB。请分批打开。");
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
        auto inputs = InitialPaths();
        auto router = std::make_shared<iima::InstanceRouter>();
        if (!router->IsPrimary()) {
            auto result = router->Forward(inputs);
            if (result == iima::InstanceRouter::ForwardResult::Accepted) return 0;
            auto message = result == iima::InstanceRouter::ForwardResult::Uncertain
                ? L"打开请求可能已交付，但未收到确认。请检查现有 IIMA 窗口；为避免重复播放，未自动重发。"
                : L"现有 IIMA 未接受打开请求，可能正在退出。请等待其退出后重试。";
            MessageBoxW(nullptr, message, L"IIMA", MB_OK | MB_ICONWARNING);
            return 1;
        }
        winrt::Microsoft::UI::Xaml::Application::Start([router, inputs = std::move(inputs)](auto const&) mutable {
            winrt::make<winrt::IIMA::implementation::App>(router, std::move(inputs));
        });
        router->StopAccepting();
        return 0;
    } catch (winrt::hresult_error const& error) {
        MessageBoxW(nullptr, error.message().c_str(), L"IIMA 启动失败", MB_OK | MB_ICONERROR);
    } catch (std::exception const& error) {
        MessageBoxW(nullptr, winrt::to_hstring(error.what()).c_str(), L"IIMA 启动失败", MB_OK | MB_ICONERROR);
    }
    return 1;
}
