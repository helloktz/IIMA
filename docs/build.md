# 依赖与构建

## 构建依赖

在 Windows x64 上构建。将 `pwsh`、`cmake`、`ninja` 和 `nuget` 加入 `PATH`。

| 依赖 | 要求 |
| --- | --- |
| Visual Studio 或 Build Tools | 安装 C++ 桌面开发工具、x64 MSVC v145 工具集 `14.52.37025` 和 Windows SDK `10.0.26100.0`。版本固定在 [`build/config.json`](../build/config.json)。 |
| CMake、Ninja、PowerShell | CMake `3.31` 或更新版本，Ninja，PowerShell 7。项目使用 C++23，通过 [`CMakePresets.json`](../CMakePresets.json) 提供 Debug 和 Release 预设。 |
| NuGet 和 .NET SDK | 通过 `nuget.exe` 查询本机共享缓存。首次缺包时通过 `dotnet restore` 恢复，需要 .NET SDK 和可用包源。缓存齐全时可离线构建。 |
| WinUI 与本机组件 | [依赖清单](../build/IIMA.Dependencies.csproj) 固定 C++/WinRT 和 Windows App SDK 的 WinUI、Foundation、InteractiveExperiences 包。清单中的 WebView2 提供当前 WinUI 生成和链接所需的元数据与二进制。播放器界面采用 WinUI 3。 |
| libmpv SDK | 播放和打包需要带 IIMA 补丁的 x64 SDK，包括 `libmpv-2.dll` 及其 FFmpeg 等运行时依赖。 |
| WiX Toolset | 打包使用配置中固定的 WiX `5.0.2`。脚本优先使用 `PATH` 中的匹配版本，缺少时通过 `dotnet tool install` 安装到 `.build/tools`。 |

## 编译

在仓库根目录用 PowerShell 7 执行。构建脚本会启用固定版本的 MSVC 和 Windows SDK，配置 CMake 并启动 Ninja。

```powershell
./scripts/build.ps1 -Configuration Release
# 调试构建
./scripts/build.ps1 -Configuration Debug
```

Release 程序位于 `.build/cmake/release/bin/IIMA.exe`，Debug 程序位于 `.build/cmake/debug/bin/IIMA.exe`。配置和编译日志位于 `.build/logs/`。保留 `.build/cmake/` 可继续增量构建。需要干净重编时传入 `-Rebuild`。

### 准备播放 SDK

还没有 libmpv SDK 时，可以先完成应用编译。要验证播放或生成可分发的安装包，先从已有的 SDK 目录导入媒体运行时。

```powershell
./scripts/stage-libmpv.ps1 -SdkDirectory 'D:/path/to/libmpv-sdk'
./scripts/build.ps1 -Configuration Release
```

也可将环境变量 `IIMA_LIBMPV_SDK` 指向该目录，首次构建时会自动导入。导入后的 SDK 位于 `.build/libmpv/`，后续构建可按版本和内容校验复用。更换 SDK 时，先移走该目录，再导入新版本。

SDK 目录包含以下内容。

```text
bin/
  libmpv-2.dll
  ... 其余媒体运行时 DLL
licenses/
  ... 上游许可和源码来源资料
manifest.json
```

自行准备 SDK 时，以 [mpv 固定提交](../references/mpv/IIMA-SOURCE.txt) 为基线，在独立构建目录应用 [IIMA 的 D3D11 交换链补丁](../build/patches/mpv-iima-owned-swapchain-v1.patch)，构建 x64 共享库及配套依赖。历史发布包的 `libmpv-manifest.json` 和 `licenses/libmpv/` 可用来核对构建配方、工具链和库版本。

`manifest.json` 需记录 `sourceCommit`、`sourceUrl`、`sourcePatch`、`patchSha256`、`buildCommand`、`toolchain` 和 `dependencies`。将 `sourcePatch` 设为 `mpv-iima-owned-swapchain-v1`，从当前补丁文件计算 `patchSha256`。导入时会校验提交和补丁，检查 DLL 的 AMD64 架构、libmpv C ABI 和初始化，并记录 DLL 的 SHA-256。

## 打包

准备好媒体 SDK 后，在仓库根目录执行。

```powershell
./scripts/package.ps1
```

脚本复用 Release 增量构建，生成以下产物。

| 路径 | 内容 |
| --- | --- |
| `.dist/IIMA-<版本>-win-x64.zip` | 绿色版。解压后运行 `IIMA.exe`，根据 Windows 显示语言选择简体中文或 English。 |
| `.dist/IIMA-<版本>-win-x64.msi` | Windows 安装包。安装时可选「简体中文」或「English」，选择会用作播放器默认语言。 |
| `.dist/version` | 版本号，单行且无末尾换行。 |

界面语言的资源目录统一命名为 `en-US` 和 `zh-CN`。发布包附带 IINA、libmpv 及其依赖的许可与源码来源资料。上游许可见 [IINA LICENSE](../references/iina/LICENSE) 和 [mpv Copyright](../references/mpv/Copyright)。
