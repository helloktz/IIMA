# IIMA

IIMA 是面向 Windows 的原生桌面播放器项目，以 IINA 源码为移植依据，采用 C++/WinRT、WinUI 3 和 libmpv，目标是实现完整、自然的 Windows 播放体验。

项目采用有源码参照的移植方式：复用和转写适用的业务逻辑，将 macOS 专用界面与系统接口替换为 Windows 实现。IIMA 是独立项目，不代表 IINA 或 mpv 官方。

## 当前状态

已建立 C++23 / WinUI 3 空白窗口工程、IIMA.Core 静态库、MSBuild 构建脚本和最小 ZIP 打包入口，并导入 IINA 与 mpv 源码参考快照。当前没有播放业务或 libmpv 运行依赖，也没有公开发布版本。

## 架构与研发交接

- [基础架构](docs/ARCHITECTURE.md)：工程布局、模块边界、线程与状态、视频合成、配置及构建交付。
- [研发交接](docs/HANDOFF.md)：当前状态、阶段任务、完成条件及接手入口。
- [开发约定](AGENTS.md)：源码移植、工具链和验证要求。

开发分支已加入 XAML 页面、播放核心、依赖准备脚本和状态测试。当前尚不是可播放交付物：Windows 构建及运行未验证，安全交换链获取仍待完成，详见 [移植记录](docs/PORTING.md)。构建和打包方法见 [BUILD.md](docs/BUILD.md)。

## 交付平台

最终交付物为与 **Windows 11 兼容的 x64 原生应用程序**。开发、构建、打包及验收以此平台为准，不承担其他系统或架构的兼容工作。

不额外限制其他系统运行；若满足实际依赖与 API 要求，可自然运行，但不属于承诺支持或验证的范围。

## 技术方向

- C++/WinRT + WinUI 3：原生窗口、界面与交互。
- libmpv：播放控制、音视频处理、字幕及视频输出。
- 按需使用 Win32、D3D11、DXGI 等接口，完成 Windows 系统与图形集成。
- 开发时核对并固定依赖版本；参考源码快照不等于实际使用的 libmpv 二进制版本。

## 构建工具链

IIMA 自有代码采用 **C++23**，主程序使用 **MSVC + MSBuild**：由 MSVC 完成 C++ 编译和链接，由 MSBuild 管理工程构建。工具集和标准模式已固定，见 [构建说明](docs/BUILD.md)；第三方依赖遵循上游的语言标准要求。

**libmpv 按上游支持的方式构建**，其依赖遵循相应的上游构建要求，不强制与主程序统一工具链。集成时确认 x64 架构与调用接口兼容，并记录依赖版本和构建方式。

从仓库根目录执行：

```powershell
pwsh -NoProfile -File scripts/build.ps1 -Configuration Release
pwsh -NoProfile -File scripts/package.ps1
```

需要预先配置 Windows 构建环境，详见 [构建说明](docs/BUILD.md)。

## 目录

```text
IIMA/
├─ AGENTS.md                 # 开发与移植约定
├─ README.md
├─ IIMA.sln                 # 应用与核心库解决方案
├─ Directory.Build.props    # 固定工具链与公共构建设置
├─ build/                   # 原生依赖导入与打包说明
├─ scripts/                 # 构建、打包、冒烟检查
├─ src/                     # IIMA 实现
├─ docs/                    # 架构、移植与验证文档
└─ references/              # 源码参考，不直接参与应用编译
   ├─ iina/
   ├─ iina-source.txt        # IINA 来源分支与提交
   ├─ mpv/
   └─ mpv-source.txt         # mpv 来源分支与提交
```

生成物写入 `artifacts/`，依赖缓存写入 `.deps/`，均不纳入 Git。

## 源码参考

- [IINA](https://github.com/iina/iina)：参考产品行为、播放控制、业务逻辑和界面交互。快照版本见 [iina-source.txt](references/iina-source.txt)。
- [mpv](https://github.com/mpv-player/mpv)：参考 libmpv API、播放核心和视频输出实现。快照版本见 [mpv-source.txt](references/mpv-source.txt)。

两份源码通过浅克隆导入，内部 `.git` 和 `.github` 已移除，以普通文件形式由 IIMA 主仓库管理，不使用 Git 子模块。后续更新应同步维护来源记录。

## 移植原则

优先阅读对应的 IINA 实现，保留已明确的行为与规则；可移植逻辑转写为 C++，平台相关部分按 Windows 的接口和交互习惯实现。按完整功能链逐项迁移，并通过实际运行验证。

首先打通打开文件、视频呈现、播放控制、状态同步和关闭释放，再扩展播放列表、字幕、音轨、快捷键与设置。视频合成路径及 HDR、多显示器表现需要原型验证，不视为已实现能力。

## 许可与来源

参考源码保留各自的许可和版权声明：

- IINA：[GPLv3 许可证](references/iina/LICENSE)。
- mpv：[版权与许可说明](references/mpv/Copyright)，以及源码中的具体许可文件和文件头。

移植代码应保留适用的版权、许可与来源信息。IIMA 自身的正式许可证尚未在仓库根目录声明；发布前需明确覆盖范围并满足所采用代码及构建依赖的许可要求。
