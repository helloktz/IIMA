# 架构与模块

IIMA 由 CMake 构建为两个主要目标。`IIMA.Core` 是包含播放与业务逻辑的 C++ 静态库，`IIMA` 是链接它的 WinUI 3 可执行程序。运行时，播放器从应用目录加载打过补丁的 `libmpv-2.dll`。有关版本、SDK 导入和打包步骤，见[依赖与构建](build.md)。

## 播放器界面与 Windows 集成

| 位置 | 职责 |
| --- | --- |
| [`IIMA.App/Main.cpp`](../src/IIMA.App/Main.cpp) | Windows 进程入口，初始化 WinRT 和本地化，解析启动路径，处理单实例转发。 |
| [`IIMA.App/App.xaml.cpp`](../src/IIMA.App/App.xaml.cpp) 与 [`App.OpenPolicy.cpp`](../src/IIMA.App/App.OpenPolicy.cpp) | 管理窗口的创建、激活和关闭，按打开策略将文件批次分配给播放器窗口。 |
| [`IIMA.App/MainWindow.xaml`](../src/IIMA.App/MainWindow.xaml) 与 `MainWindow.*.cpp` | 定义窗口和控件。C++ 文件按播放、菜单、音频、视频、字幕、播放列表、历史、设置等功能组织，负责事件绑定和界面更新。 |
| [`VideoSurface`](../src/IIMA.App/VideoSurface.cpp) 与 [`WheelSlider`](../src/IIMA.App/WheelSlider.cpp) | 自定义 XAML 控件。前者处理视频区域的指针，后者处理进度和音量滑块的拖动及滚轮输入。 |
| [`IIMA.App/Platform/`](../src/IIMA.App/Platform) | 封装 Windows 进程间通信、文件和 Shell 操作、窗口生命周期、系统媒体控制和电源事件。这里也实现用户数据的磁盘读写、缩略图提取与缓存，以及在线字幕服务的 HTTP 客户端。 |
| [`IIMA.App/Presentation/`](../src/IIMA.App/Presentation) | `VideoPresenter` 管理 DXGI 交换链与 WinUI `SwapChainPanel` 的绑定，并按窗口和 DPI 更新物理像素尺寸。 |
| [`IIMA.App/Settings/`](../src/IIMA.App/Settings) | 在 JSON 和核心层数据模型之间转换高级偏好、均衡器预设、已保存的滤镜等配置。 |
| [`IIMA.App/Strings/`](../src/IIMA.App/Strings) | 保存 `en-US` 与 `zh-CN` 的界面资源。`Resources.resw` 供 XAML 的 `x:Uid` 使用，`Messages.resw` 供 C++ 按键读取。 |

## 核心子模块

这些目录位于 [`src/IIMA.Core/`](../src/IIMA.Core)。业务规则和状态模型集中在这里，窗口和系统接口由应用层接入。

| 子目录 | 职责 |
| --- | --- |
| `Audio/` | 定义音频均衡器预设和用户配置。 |
| `Commands/` | 定义应用命令和播放列表命令，将快捷键动作映射为播放器操作。 |
| `Diagnostics/` | 保存应用诊断记录和 mpv 日志缓冲。 |
| `Filters/` | 解析和组织音视频滤镜链，管理预设、保存的滤镜和 LUT。 |
| `Geometry/` | 计算画面宽高比、缩放、裁剪选区和窗口几何。 |
| `History/` | 将播放历史投影为可搜索、筛选和分组的列表。 |
| `Input/` | 处理按键表示、快捷键配置和库、菜单绑定和本地打开输入规则。 |
| `Matching/` | 按文件名对媒体分组，匹配同目录字幕，并解析字幕搜索路径。 |
| `Media/` | 整理章节、媒体信息、轨道详情和系统媒体信息，并处理播放时间的格式。 |
| `Mpv/` | 封装 libmpv 的动态加载、命令队列、属性观察和事件转换。`Catalog/` 保存供应用使用的 mpv 命令、选项和属性目录，`include/mpv/` 保存 C API 头文件。 |
| `Network/` | 解析和校验媒体 URL。应用层将符合要求的地址交给播放会话。 |
| `OnlineSubtitles/` | 计算字幕搜索用的媒体指纹，管理搜索、选择、下载和取消的事务状态。 |
| `OSD/` | 将音量、速度、轨道等播放状态变化转为屏幕提示。 |
| `Persistence/` | 定义用户偏好和历史数据模型、记录排序与续播位置规则。`IIMA.App/Platform/UserDataStore.cpp` 负责磁盘上的 JSON。 |
| `Playback/` | 维护 `PlayerSession` 和 `PlaybackSnapshot`，协调打开媒体、播放控制、进度拖动、缓冲、外部轨道及窗口相关策略。 |
| `Playlist/` | 计算队列操作，维护条目与外部字幕关联，并导出 M3U 播放列表。 |
| `Screenshots/` | 定义截图选项和文件名规则。应用层负责截图文件和系统交互。 |
| `Settings/` | 定义偏好项、默认值、取值校验和生效时机，维护设置搜索、控制栏布局和窗口播放策略。 |
| `Subtitles/` | 定义字幕编码与样式规则。字幕发现和装载分别由 `Matching/` 与播放会话处理。 |
| `Tests/` | 保存核心层测试源，覆盖播放状态、会话、mpv 数据解析、诊断和用户数据。 |
| `Thumbnails/` | 管理缩略图缓存及淘汰策略。Windows 提取和磁盘访问在应用层。 |
| `Windows/` | 管理播放器窗口的登记、激活选择和音乐模式规则。窗口对象由 `IIMA.App/App` 持有。 |

核心库根目录的 [`BuildInfo.cpp`](../src/IIMA.Core/BuildInfo.cpp) 将构建配置中的版本号提供给应用。

## 常见功能的代码入口

| 功能 | 入口与调用方向 |
| --- | --- |
| 打开媒体 | `Main.cpp` 或 `MainWindow` 接收请求，`App` 选择目标窗口，`PlayerSession` 负责打开，`MpvClient` 发送 libmpv 命令。 |
| 播放状态 | `MpvClient` 收到事件后经 `DispatcherQueue` 交给 `PlayerSession`，窗口读取更新后的 `PlaybackSnapshot`。 |
| 视频呈现 | `MpvClient` 取得 libmpv 的 D3D11 合成交换链，并经播放状态快照交给 `MainWindow`。`VideoPresenter` 将它接到 WinUI 视频面板。 |
| 偏好与历史 | `IIMA.Core/Settings` 与 `Persistence` 提供规则和模型，应用通过 `UserDataStore` 持久化到本机。 |
| 自动匹配和在线字幕 | `Matching`、`OnlineSubtitles` 提供匹配与事务规则，`IIMA.App/Platform` 负责访问文件系统或字幕服务。 |

## 构建、上游快照和产物

| 目录 | 职责 |
| --- | --- |
| [`build/`](../build) | 持有工具链和 NuGet 依赖配置、WinUI XAML/WinMD/PRI 生成脚本、WiX 安装器定义及其翻译，以及 libmpv 补丁。 |
| [`scripts/`](../scripts) | `build.ps1` 驱动 CMake 和 Ninja，`resolve-nuget-packages.ps1` 解析共享缓存，`stage-libmpv.ps1` 导入 SDK，`verify-libmpv.ps1` 和 `test-libmpv.ps1` 校验 SDK，`package.ps1` 生成分发包。 |
| [`references/iina/`](../references/iina) | 保存 IINA 的源码和许可，作为移植行为的直接依据。 |
| [`references/mpv/`](../references/mpv) | 保存与媒体 SDK 基线对应的 mpv 源码和许可。 |
| [`.github/workflows/`](../.github/workflows) | 在自托管 Windows x64 runner 上执行 Debug 和 Release 构建验证。 |
| `.build/` | 本地 CMake/Ninja 构建树、WinUI 生成输出、已暂存的媒体 SDK 和日志。 |
| `.dist/` | 绿色版 ZIP、MSI 安装包和版本文件。 |
