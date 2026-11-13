# 构建与最小打包

主分支包含 WinUI XAML 播放页面、原生播放核心及 libmpv staging 入口。真实播放和打包需要固定源码加 owned-swapchain 私有补丁构建的 libmpv。当前验证结果见 [VALIDATION.md](VALIDATION.md)。

## Windows 构建环境

- PowerShell 7 或更高版本。
- Visual Studio 2026 / Build Tools 18.12，安装 C++ 桌面及 C++ WinUI 构建工具；固定 MSVC v145 / 14.52.37025。
- Windows SDK 10.0.26100.0，包括头文件、库、WinRT 元数据及构建工具。
- MSVC 对应的 x64 Release CRT redist 目录，用于 app-local 打包。
- 首次还原需要访问 NuGet 官方源及 dist.nuget.org；下载写入仓库的 `.deps/`。

版本见 [Directory.Build.props](../Directory.Build.props) 和 [packages.config](../src/IIMA.App/packages.config)，依赖说明见 [DEPENDENCIES.md](../build/DEPENDENCIES.md)。脚本通过 vswhere 定位具备固定工具集的安装，不使用个人目录，也不自动以管理员身份安装或升级工具链。

C++23 对应当前工具集的 MSBuild `stdcpp23`，实际编译开关为 `/std:c++23`。Core 含编译期检查。

## 构建

从仓库根目录执行。libmpv SDK 可按 [LIBMPV.md](LIBMPV.md) 单独准备。

```powershell
pwsh -NoProfile -File scripts/build.ps1 -Configuration Debug
pwsh -NoProfile -File scripts/build.ps1 -Configuration Release
```

脚本先还原固定 NuGet 依赖，再调用完整 MSBuild（不是 `dotnet build`）。加入 `-Rebuild` 会删除对应配置的 `artifacts/bin/x64/` 和 `artifacts/obj/x64/` 子目录后重新构建；不会删除参考源码、其他配置或依赖缓存。运行中的应用需先关闭。

产物位于 `artifacts/bin/x64/<Configuration>/IIMA.App/`。构建日志与 binlog 位于 `artifacts/logs/`。还原后也可以用 Visual Studio 打开 `IIMA.sln`。

## 最小打包

```powershell
pwsh -NoProfile -File scripts/package.ps1
```

打包始终先做干净的 Release x64 构建，避免将旧版本残留 DLL 混入包。然后：

1. 复制应用输出，包括 WinUI / Windows App SDK 自包含运行文件。
2. 复制固定 MSVC 的 x64 Release CRT DLL，避免依赖研发机器已安装的 VC++ Redistributable。
3. 去除 PDB、链接中间产物，附上依赖清单、上游许可材料和使用说明。
4. 输出 `artifacts/dist/IIMA-<版本>-win-x64.zip`，包内为同名应用目录。

版本读取自 `Directory.Build.props`，遵守项目数字约束。不要只分发 IIMA.exe；用户需要解压完整目录。Debug 不作为分发包。

此 ZIP 是未签名的研发验证产物。正式公开发行仍需落实项目许可证和完整分发要求；当前脚本不创建 Release，也不上传文件。

## 启动与关闭冒烟验证

在有交互桌面的 Windows 环境，将 ZIP 解压到一个独立目录，然后执行：

```powershell
pwsh -NoProfile -File scripts/smoke-test.ps1 -Executable '<解压目录>/IIMA-0.1.0-win-x64/IIMA.exe'
```

此检查启动应用，等待原生窗口出现，检查标题，验证关键 WinUI / Windows App SDK / CRT DLL 确实从应用目录加载，再发送正常关闭请求并确认退出码为 0。失败时只清理此次启动的进程。它不证明视觉布局、HDR、播放能力或全部 Windows 11 版本的兼容性。

## XAML 与 libmpv 边界

`App.xaml` 和 `MainWindow.xaml` 由 WinUI XAML 编译链生成，MIDL 和 C++/WinRT 输出位于相应配置的 `artifacts/obj/`。程序入口为 `Main.cpp`。

依赖准备与 probe 见 [LIBMPV.md](LIBMPV.md)。缺少 SDK 时可编译源码并显示依赖错误，但打包会拒绝缺失 libmpv 的目录。依赖 probe 不证明真实视频输出；当前公开交换链属性的所有权问题见 [PORTING.md](PORTING.md)。

## 脚本用途

| 场景 | 入口 |
|---|---|
| 日常编译、打包 | `build.ps1`、`package.ps1`。`Common.ps1` 提供共用的工具链定位和输出目录操作。 |
| 准备 libmpv | `prepare-mpv-source.ps1` 检出固定源码并应用补丁，`stage-libmpv.ps1` 暂存已构建的 SDK。`test-libmpv.ps1` 供暂存和构建流程调用，检查 DLL 的架构、ABI 和初始化。 |
| 开发验证 | `smoke-test.ps1` 检查原生窗口启动关闭和本地运行库。`check-source.py` 检查项目输入和参考快照，`test-portable.py` 运行 C++ 逻辑测试。Python 脚本按需单独运行。 |
| 更新 IINA 参考快照 | `generate-mpv-catalog.py` 重生成源码中的 mpv 命令、选项及属性目录。 |

远程构建入口见 [Windows runner](WINDOWS-RUNNER.md)。
