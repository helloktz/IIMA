# 构建与最小打包

当前开发分支包含 WinUI XAML 播放页面、原生播放核心及 libmpv staging 入口。Windows 编译和真实播放尚未验证；视频呈现需要固定源码加 owned-swapchain 私有补丁的 libmpv，不能视为可用播放器。没有安装器、签名、自动更新或文件关联。

## Windows 构建环境

- PowerShell 7 或更高版本。
- Visual Studio 2026 / Build Tools，安装 C++ 桌面开发工具；固定 MSVC v145 / 14.51.36231。
- Windows SDK 10.0.26100.0，包括头文件、库、WinRT 元数据及构建工具。
- MSVC 对应的 x64 Release CRT redist 目录，用于 app-local 打包。
- 首次还原需要访问 NuGet 官方源及 dist.nuget.org；下载写入仓库的 `.deps/`。

版本见 [Directory.Build.props](../Directory.Build.props) 和 [packages.config](../src/IIMA.App/packages.config)，依赖说明见 [DEPENDENCIES.md](../build/DEPENDENCIES.md)。脚本通过 vswhere 定位具备固定工具集的安装，不使用个人目录，也不自动以管理员身份安装或升级工具链。

C++23 对应当前工具集的 MSBuild `stdcpp23`，实际编译开关为 `/std:c++23preview`，不是 `/std:c++latest`。这是编译器对其 C++23 实现的命名，不表示项目可以使用任意后续标准。Core 含编译期检查，避免静默退回 C++20 或更早模式。

## 构建

从仓库根目录执行：

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

## 云端使用

Windows 云端 worker 使用相同构建和打包命令，并预装上述固定工具链。没有桌面的 worker 可以编译和打包，但不能将 GUI 冒烟标为已验证；在独立交互式 Windows 11 测试环境运行该检查。

Linux 云端研发环境可以编辑源码、检查脚本与项目文件；最终构建仍需 Windows MSVC worker。当前没有绑定特定 CI 提供商或自动创建云端机器，也没有执行远程 CI。

## XAML 与 libmpv 边界

`App.xaml` / `MainWindow.xaml`、MIDL 与生成代码已加入原应用工程；保留显式 Main.cpp 入口，禁用 XAML 自动 main。云端只做了 XML 和输入路径检查，尚未执行 XAML 生成、MSVC 编译或 Windows 启动。

依赖准备与 probe 见 [LIBMPV.md](LIBMPV.md)。缺少 SDK 时可编译源码并显示依赖错误，但打包会拒绝缺失 libmpv 的目录。依赖 probe 不证明真实视频输出；当前公开交换链属性的所有权问题见 [PORTING.md](PORTING.md)。

现有初始化版本的成功构建记录只适用于该历史版本，不延伸为本批 XAML/播放实现已经构建成功。
