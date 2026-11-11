# 构建与最小打包

当前产物是空白 WinUI 3 窗口，包含一个应用工程与一个原生静态库。没有播放业务、libmpv 运行库、安装器、签名、自动更新或文件关联。

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

## XAML 与后续研发边界

初始化入口为 `src/IIMA.App/Main.cpp`，直接用 C++/WinRT 创建 WinUI Application、Window 和空 Grid。它不是 Win32 控件替代品，也不是网页窗口。

本次没有 `.xaml` / IDL 自定义页面，也没有验证 XAML 编译链。后续引入业务页面时，在 Windows worker 上安装相应 C++ WinUI/XAML 构建组件，按官方模板补充页面、IDL 和生成代码配置；保留现有核心库边界及构建、打包入口。不要仅添加 XAML 文件就宣称其已接入工程。

libmpv 仍在下一阶段引入。`references/mpv/` 是参考快照，本工程没有把它当成生产依赖或复制进 ZIP。
