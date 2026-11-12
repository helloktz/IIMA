# IINA 移植记录

## 本批状态

本批是可审阅的 Windows 原生播放基础实现，不是已通过验收的播放器。
IINA 基准为 `references/iina-source.txt` 的 `685a40ee1d4165107182f7236751ad9d5873f267`；
mpv 接口基准为 `references/mpv-source.txt` 的 `bcde6b93cca4d128d84e1a60d1a6db2cd81c889e`。
参考快照保持不变。下列源码转写适用 IINA 的 GPL-3.0 及原作者版权，见原文件头及 `references/iina/LICENSE`。

## 第一条播放链路

| 上游入口 / 状态 | IIMA 目标 | 保留语义及边界 |
|---|---|---|
| `PlayerCore.swift: openURLs`, `openURL`, `open` | `PlayerSession::Open` | 文件路径以 UTF-8 进入 `loadfile`；接受异步命令不等于加载成功；本批仅单文件，不声称已移植目录展开、蓝光、播放列表或自动加载同目录文件 |
| `MPVController.swift: handleEvent` | `MpvClient` 单一事件消费者 | 每次 `mpv_wait_event` 后复制所需 payload，再投递 UI；区分 START_FILE、FILE_LOADED、END_FILE、命令返回和 shutdown |
| `PlaybackInfo.swift: state`, `videoPosition`, `videoDuration`, `constrainVideoPosition` | `PlaybackSnapshot` / 状态归约 | 未知时长与 0 秒不同；关闭终态不被迟到事件覆盖；播放事实由观察事件更新，不由按钮乐观修改 |
| `MPVController.handleEvent: SEEK / PLAYBACK_RESTART` | seek 状态 | seek 请求、进入 seek、完成重启分别处理；不把命令返回当播放恢复 |
| `PlayerCore.shutdown`, `mpvHasShutdown` | 异步关闭 / 窗口关闭协调 | 先拒绝命令和解除 UI 呈现，再停止事件消费者、销毁句柄；处理主动与被动 shutdown |
| `MainWindowController` / `VideoView` 的平台职责 | WinUI XAML / `VideoPresenter` | Windows 文件选择、XAML 控件、DPI、全屏和 COM RAII；不照搬 AppKit/OpenGL 显示回调 |

### 默认值和有意差异

- 不读用户全局 mpv 配置和外部脚本，以保证默认行为可重现。
- WinUI 线程投影状态；mpv 工作线程初始化、读取事件和销毁句柄。Swift ARC/闭包转为显式拥有对象与弱引用。
- 初始播放不暂停；seek 是绝对秒值。未知时长时禁用拖动，不制造假时长。
- 第一批没有续播、历史、目录扫描和 pauseWhenOpen；后续批次已接入这些链路，见下文。

## 合成路径的实际阻碍

首选仍是 `gpu-next` + D3D11 composition + `SwapChainPanel`，没有偷偷切换 Render API 或独立 mpv 进程。

上游实际调用链：

1. `player/command.c: mp_property_vo_display_swapchain` 调用 `vo_get_display_swapchain`。
2. `video/out/vo.c: vo_get_display_swapchain` 直接返回 `vo->display_swapchain`。
3. `video/out/w32_common.c: vo_w32_swapchain` 仅存储裸指针，没有对外持有引用的协议。
4. `video/out/d3d11/context.c: d3d11_uninit` 释放交换链，再清除 composition 地址。
5. `ra_d3d11_ctx_get_swapchain` 的 AddRef 用于 `gpu_next/context.c` 内部给 libplacebo，不是公开属性路径，不能据此推断公开地址已 AddRef。

因此，从属性取整数地址到 UI 调用 AddRef/SetSwapChain 存在生命周期缺口。异步观察属性、给事件贴代次、在 UI 上加锁都不能锁住 mpv 的 VO 线程。实现拒绝将该整数转为可用 COM 对象，也不会打开文件后只播放不可见视频的声音。`VideoPresenter` 仅接受明确拥有的交换链接口；第一批默认打开操作报告尚未满足的呈现条件；后续已加入下文的拥有引用扩展，具备配套 DLL 时进入真实播放链。

这不是“已经证明所有合成方案不可行”，而是当前固定上游接口未提供所需的安全获取协议。已在独立生产依赖补丁实现同步获取拥有引用的方案（见下文），参考快照不变；下一步仍需 Windows 编译和运行证据。不可通过忽略生命周期问题宣布里程碑 B 完成。

## 验证边界

本批云宿主为 Linux，无 Windows SDK、MSVC、XAML 编译器、libmpv Windows 二进制或交互式桌面。
静态 XML/工程检查和纯 C++ 状态测试不等于 Windows 构建成功。具体命令、结果和 Windows 复验步骤见 `VALIDATION.md`；依赖准备见 `LIBMPV.md`。

## 设置与历史

`Preference.swift` 默认 `softVolume=100`、`recordPlaybackHistory=true`、`resumeLastPosition=true`；
`HistoryController.add` 保留“同一媒体去重后移到最近位置”、禁用历史时不写入的规则；`save` 保留原子替换。
`PlayerCore.savePlaybackPosition` 的 watch-later 写入不直接移植：本架构明确由 IIMA 保存续播，不同时启用 mpv 独立写入。

纯类型/规则在 `IIMA.Core/Persistence/UserData.h`，Windows JSON 和原子文件操作在 `IIMA.App/Platform/UserDataStore`。
使用操作系统 Windows.Data.Json，不引入浮动第三方 JSON 库。文件分别位于 `%LOCALAPPDATA%/IIMA/settings.json`、`history.json`，schemaVersion=1；损坏/不支持的数据备份保留后恢复默认，备份失败则报错而非覆盖。
所有 IO 要由调用者在后台执行，保存完成后才能结束关闭；模块内部串行。Windows 差异：精确 UTF-8 路径作为本地历史键（不实现 IINA URL/watch-later MD5 兼容），最多保留最近 1000 条；到达 EOF 的历史重新打开从头开始。

独立纯规则测试覆盖去重与最近顺序、禁用历史/续播、EOF、无效位置和条数上限；Windows JSON/文件系统操作仍未运行。UI 集成另以实际代码和验证记录为准。

## 安全交换链实现（后续批次）

前述公开裸地址问题已用**独立生产依赖补丁**实现解决路径，而非直接忽略风险：
`build/patches/mpv-iima-owned-swapchain-v1.patch` 添加私有 C ABI
`mpv_iima_acquire_d3d11_swapchain_v1`。它持有播放核心调度锁，经同步 VO control 到 D3D11 VO 线程执行 AddRef，再返回一个拥有引用；不是可观察属性，因此不会因 mpv 属性轮询泄漏引用。
Core 工作线程调用扩展，将引用与 DLL 生命周期一起保留到 UI 绑定/释放完成。START_FILE、END_FILE、失败/关闭清理旧链；新链重新确认绑定后才允许当前媒体恢复播放。UI 在 DPI 变化时给 IDXGISwapChain2 设置逆缩放矩阵，物理像素大小发送给 mpv。

普通上游 DLL 没有此私有扩展，仍会明确阻止播放；必须按 LIBMPV.md 从固定源码应用补丁构建。补丁应用和接口头语法检查已经通过，但**修改后的 mpv、GPU/WinUI 组合尚未经 Windows 编译或运行**。不能把源级同步推导写成实机生命周期验证。

## 播放列表、轨道、章节与截图

- `PlayerCore.openURLs`、`addToPlaylist`、`playlistMove`、`playFileInPlaylist`：多选先替换首项再追加其余；mpv playlist 的 ID/current 为权威；UI 不另行自动推进。上下项、移除、移动、双击选择均进会话命令入口，移动的最终索引换算为 mpv 插入前索引。
- `PlayerCore.getPlaylist/getTrackInfo/getChapters` 与 `MPVTrack`：深拷贝 node 列表，包括轨道 ID、type、title、lang、selected、external filename 和章节时间。UI 仅投影，不用 mock。
- `PlayerCore.setTrack`, `loadExternalSubFile`, `playChapter`：选择音轨/字幕、关闭轨道、外部字幕重复加载采用 sub-reload、章节跳转后播放。倍速、音量采用观察属性更新。
- `PlayerCore.screenshot` / `MPVController.handleEvent COMMAND_REPLY`：使用 `screenshot-to-file` 异步错误通道；Windows SaveFilePicker 选定 PNG 路径；对话期间换片不截图旧请求。当前 `video` 模式不包含字幕/窗口装饰，不声称实现 IINA 全部截图偏好。
- `client.h` 允许异步命令重排，故工作线程只在收到上一条 reply 后提交下一条。`loadfile` reply 的 `playlist_entry_id` 与实际 START_FILE 条目关联，不给迟到事件套上当前代次。

## Windows 文件与系统集成

原生多选、文件拖放和命令行首次启动文件进入同一会话；初始文件等待数据加载与内核 Ready。后续批次已补目录递归、单个播放列表委托 mpv、单实例路由和候选文件关联注册；实际 Windows 行为尚未验证。
快捷键：Ctrl+O 打开，Space 暂停/继续，F11/Escape 全屏往返，Ctrl+Left/Right 上下项，Ctrl+L 播放列表，Ctrl+Shift+S 截图。UI 事件与媒体键复用相同会话方法。

`NowPlayingInfoManager.updateInfo`、`AppDelegate.RemoteCommandController`、`SleepPreventer.updateSleepPrevention` 对应 `Platform/MediaIntegration`：HWND 绑定 SMTC，实际状态驱动播放/暂停和上下项能力，仅共享文件名而非完整路径；暂停/结束/关闭清除电源请求。PowerCreateRequest 按句柄 RAII 管理，避免线程绑定式防休眠跨线程清理。默认与 IINA preventScreenSaver=true/allowScreenSaverForAudio=false 一致。未实现封面/专辑元数据或系统进度条。

## 仍不属于已交付能力

没有 Windows 已构建可执行文件或已测试 ZIP；文件关联与单实例已在后续批次实现；没有安装器、签名、自动更新或正式发布。HDR、设备丢失/更换、跨屏色彩、多 DPI 实机表现、睡眠恢复仍需硬件验证。项目正式根许可证和完整二进制依赖版本锁定/源码交付义务仍需在发行前完成。

## libmpv 消费头适配

`src/IIMA.Core/Mpv/include/mpv/client.h` 是固定参考头的完整许可保留副本，唯一差异是 Windows 的 MPV_EXPORT 从 `__declspec(dllexport)` 变为空宏（含解释注释）。应用动态加载 C ABI，不应对外导出 mpv 声明；生产 mpv 构建头仍保留 dllexport。静态检查逐字比对除该行外完全相同；x64 指针、int 大小和 API 2.6 以 static_assert 约束。未用猜测结构重写 ABI，也不需要不同工具链的导入库。

## 后续设置与本地输入批次

设置映射追踪 `PlayerCore` 的循环、音量/速度、延迟、截图入口，`MPVController` 的 keep-open 与 pauseWhenOpen 事件分支，以及 `Preference` 默认值。新增 pauseWhenOpen=false、autoPlayNext=true、mute=false、subtitlesVisible=true、maxVolume=100（上游范围100..1000）、audio/sub delay=0、RepeatMode=Off、ScreenshotMode=Subtitles。

设置模型与快照分离：请求保存偏好不等于已生效，控件从 mpv 观察值投影。大多数设置即时异步生效；打开暂停和截图模式作用于随后操作，不必重启。禁用自动下一项采用 keep-open=always；开启采用 yes 保留最后帧。自动换片的 pauseWhenOpen 按 IINA 在事件工作线程 END_FILE 立即设置、FILE_LOADED 兜底，仍需真实事件竞态测试。UI 的延迟编辑范围为正负60秒，底层接受有限秒值，非有限值被拒绝。JSON schemaVersion=1 采用新增可选字段兼容旧配置。

目录解析依据 `Utility.resolveURLs`、`DraggingDetect.getPlayableFiles` 和 `PlayerCore.openURLs`：显式单个列表交给 mpv；显式多输入按上游黑名单过滤；目录发现使用媒体扩展白名单，排序去重后进入同一 OpenMultiple。Windows 适配在后台执行、stop_token取消与UI代次检查，跳过隐藏/reparse目录，不解析macOS alias/Windows .lnk，拒绝设备路径/NUL/URI/ADS。深度64/候选100000为资源上限，超过或权限错误给予反馈。数字自然排序+Unicode ordinal替代macOS本地化排序，故非ASCII排序可能不同，已列为有意差异。

## Windows 单实例与候选关联

IINA `AppDelegate` 打开文件事件由平台适配替换为 Win32 启动参数/当前登录会话命名管道。协议有版本、长度限制和严格文件路径校验；endpoint 使用登录 SID 隔离、显式 DACL、拒远端连接。后台收到请求仅投递 UI，不反向等待 UI；多次外部激活合批进入可取消解析器。协议确认表示已接收入队，不表示媒体已播放，未知投递结果不自动重发导致重复打开。

用户点击注册后只写当前用户 HKCU 的 IIMA ProgID/Capabilities/OpenWith 候选，完整引用可执行文件及 `%1` 路径；不改系统 UserChoice。默认应用交由 Windows 设置界面决定。撤销核对当前程序路径，避免删除另一位置副本的关联。移动免安装目录后需要重新注册。没有运行这些注册操作；本次只是实现代码。

## 有界本地诊断

`DiagnosticLog` 在后台串行写 `%LOCALAPPDATA%/IIMA/logs`，队列最多256条，单项截断512字节，文件1MiB轮转保留两份。关闭等待flush；日志失败不阻断播放。不发送遥测，不记录媒体路径/带令牌URL/用户配置，只记录生命周期、API/实际mpv版本、codec、hwdec和VO等技术状态变化。纯测试覆盖轮转、flush、关闭后拒绝写。

## 手动换片异步事务修复（2026-11-12）

目标：手动打开和播放列表切换不因旧画面重新绑定或迟到命令回复而提前自动恢复。

- 来源：IINA `PlayerCore.openMainWindow` 先暂停再加载；`playFileInPlaylist` 使用 `playlist-play-index`，即使选中当前条目也重新加载；`navigateInPlaylist` 暂停后切换；`notifyWindowVideoSizeChanged` 在媒体加载且画面就绪后按 `pauseWhenOpen` 恢复。默认自动恢复；手动暂停取消本次恢复。
- mpv 契约：`client.h` 异步请求的 `reply_userdata` 关联命令结果；命令成功不等于 `START_FILE` / `FILE_LOADED` 已发生。`loadfile` 返回条目 ID，播放列表切换沿用已知目标 ID。保持 MpvClient 的命令串行化，不改变参考快照。
- 修复：PlayerSession 原先只跟踪 `loadfile` 回复，未跟踪播放列表切换回复；重选当前项时旧 Loaded 状态和旧交换链可能满足恢复条件。现在同时等待暂停成功、切换成功、新 START_FILE、目标 Loaded 与当前呈现绑定；回复和生命周期事件可先后互换。图形恢复分支不能覆盖正在进行的手动切换事务。失败、被更新请求取代、用户暂停、Stop、Close 不再由旧成功回复恢复。
- Windows 差异：将 IINA 的同步命令与窗口尺寸回调转为异步命令 ID、生命周期门闩和拥有型交换链绑定确认，不用 macOS display link。此处只验证纯 C++ 会话协议，不证明真实 libmpv/WinUI 事件集成。
- 回归覆盖：当前项重播、事件先于回复、回复先于事件、暂停/加载/切换失败、提交被拒绝、A→B→C 快速替换、过期暂停回复、用户暂停/Stop/Close 取消。新测试对修复前源码能稳定触发提前恢复断言，修复后通过。

## 2026-11-12 静态扩展批次：滤镜、输入与媒体值模型

本批按用户要求不编译、不运行测试或应用。以下“实现”仅指源码已写入与调用接线，不代表原生产品验收。

- `MPVFilter.swift` → `Filters/MPVFilter.*`：type、命名/位置参数、原始格式、lavfi、crop、flip、mirror、unsharp、相等判定。`%n%` 引用按 UTF-8 字节计数而非 Swift grapheme，解析可跨内嵌冒号/等号，避免位置参数越界。`FilterPresets.swift` / `SavedFilter.swift` → 同名 C++ 模块：11 视频/2 音频预设、参数 schema、默认值、校验、保存字典与快捷键显示；不把未知/坏值 force unwrap。Windows LUT 路径的 FFmpeg 多层 escaping 未完整解决。
- `PlayerCore.setVideoAspect/setVideoRotate/toggleDeinterlace/setVideoEqualizer/addVideoFilter/removeVideoFilter` → `PlayerSession.Video.cpp`：白名单视频参数、滤镜添加/标签删除；比例、旋转和滤镜接入 `MainWindow.Video.cpp`。操作反馈走已有 mpv 异步错误路径；不提前将请求值标为已确认状态。现阶段 UI 是显式提交操作表单，不是完整滤镜属性投影器。
- `KeyMapping.swift`、`KeyCodeHelper.swift`、`PlayerCore.setKeyBindings/filterSectionBindings` → `Input/KeyMapping.*`：input.conf、@iina、section、注释、修饰键规范、last-wins、默认 section。支持 UTF-8 BOM/CRLF，安全跳过空动作；Windows 显示 Win 而非 macOS glyph。非 ASCII 不做 Foundation 全 Unicode 大小写折叠。尚未接入真实按键路由和设置编辑界面，不宣称快捷键完整移植。
- `VideoTime.swift`、`Extensions.roundedHalfDown`、`MPVChapter.swift` 与 `SidebarChaptersPane` → `Media/VideoTime.*`、`MediaChapter.*`：宽松冒号解析、毫秒截断比较、半舍下与小数进位、End、区间；已接入时间栏及章节标签。非有限/超范围值显示未知，而非整数溢出。
- `Aspect.swift`、`GeometryDef.swift`、`MainWindowController.windowFrameFromGeometry` → `Geometry/*`：保留 width 优先、百分比、边缘偏移、尺寸居中；Windows 使用顶左坐标并保留未指定轴的绝对位置，防重复叠加显示器原点。拒绝零分母/非有限比例。Geometry 尚未连接窗口初始布局。

来源均为固定 IINA 快照 `685a40ee1d4165107182f7236751ad9d5873f267`，GPL 与作者信息保留。完整移植范围和数量口径见 STATIC_PORT.md；达到数量也不能独立证明行为完整。

### 第二批：音频均衡、字幕操作、匹配与轨道呈现

- `Equalizations.swift` / `PlayerCore.setAudioEq` / `SidebarAudioPane` → `Audio/Equalizations.*`：22 预设原值和拼写、10 频段、原带宽公式、`iina_aeq` 标签、用户预设内存增删改名和滑块回填。`PlayerSession.Audio.cpp` 与 WinUI 预设选择/应用/移除接线；用户预设 JSON 持久化及10频段编辑器尚未接入。
- `PlayerCore.setSub*` / `Preference.swift` → `Subtitles/SubtitleStyle.*` / `PlayerSession.Subtitles.cpp`：字体、字号、粗体/斜体、RGBA、边缘与背景，主/次字幕位置缩放延迟选择，外部字幕重载与逐句 seek。WinUI 接入样式/主字幕布局、重载/逐句跳转。设置按显式应用提交，不伪装 mpv 已确认状态；多属性异步执行不是可回滚事务。UI 本批只有字体/字号/粗体，其余颜色采用默认值；完整样式编辑和持久化仍待扩展。
- `AutoFileMatcher.swift` / `FileGroup.swift` / `ObjcUtils.m` → `Matching/*`：系列分组、互为最近、插删1替换4的距离、集数、名称包含、优先词、10000对兜底限额，取消和媒体 generation。Windows 原路径用 filesystem::path，比较用 Unicode scalar；确定性数字排序不冒充 Cocoa 本地化排序，数字分类不是完整 Unicode Nd。
- `MainWindow.SubtitleMatching.cpp` 提供显式同目录字幕匹配操作：后台枚举（10000项上限、不递归）、取消、entry/path/generation 门控后只选择首候选。没有声称实现用户配置搜索路径、自动 playlist 扩展或自动后台加载；手动操作的 Windows 差异已明确。
- `MPVTrack.swift` / `ISO639Helper.swift` → `Media/TrackPresentation.*`：元数据复制、标题/格式/分辨率/fps/声道/采样率、字幕类型与 None/Auto/ID；已扩展 MpvClient 事件载荷并接 UI 标签。语言暂显示原代码而不是伪造译名，完整 ISO639 资源未接入。

本批仅静态实现与人工源码审阅，未编译、测试或运行；数量仍远未达到完整移植指标。

轨道解析的明确兼容性差异：IINA 构造器要求全部布尔字段存在；IIMA 对可识别 id/type 的轨道保留缺失可选标志为 false，避免部分 mpv 属性快照令可用轨道消失。滤镜单项解析额外拒绝非法名称/标签字符，防止单项 UI 意外成为多项滤镜列表表达式。

### 第三批：应用命令、播放列表计划、网络打开与 OSD

- `IINACommand.swift`、`PlayerWindowController` → `Commands/ApplicationCommand.*`：21 命令的严格类型化解析与可用性路由。`KeyBindingTranslator.swift` → 描述模板和 seek/add/cycle 参数转换，暂未接完整按键编辑器。`MPVCommandWrappers` 的4个队列参数构造也已移植。
- WinUI 已通过路由接入打开文件/网络、保存播放列表、面板焦点、尺寸动作和悬浮小窗。Windows `CompactOverlay` 是原窗口原播放器的小窗模式，隐藏侧栏/控制栏并保留返回按钮；**不是** IINA 的独立 Cocoa PiP 窗口，也不是音乐模式。其他未注册命令明确不可用，不以枚举存在宣称功能实现。
- `PlayerCore` / `SidebarPlaylistPane` / `MainMenuActions` → `Playlist/*`：基于真实ID的排序/插删/拖动/Play Next/Clear/Shuffle计划、逐命令回复与刷新快照的 cursor、新条目ID绑定、M3U8序列化。批量编辑 executor 尚未接入，不能把计划一次性盲发。重排使用 move 而非清空重建，保留条目身份。M3U8 保存已接 Windows picker 与同目录临时文件 flush+replace，后台执行；保存的是发起时队列快照。
- `OpenURLWindowController.getURL` → `Network/MediaUrl.*` 与 `MainWindow.Network.cpp`：trim、缺省http、Windows Uri解析、显式网络打开。当前支持HTTP(S)/RTSP(S)/RTMP(S)/FTP/SFTP输入；无凭据持久化、独立用户名密码字段或未知协议兼容承诺。网络地址不写历史以避免保存签名查询和凭据，和IINA历史行为有意不同。libmpv是否支持协议仍未经运行确认。
- `OSDMessage.swift` / `OSDView.swift` → `OSD/OSDMessage.*`：56消息、5展示类型、禁用/强制/窗口状态门控、文本进度、模板变量及语义文本runs。WinUI已经接入已确认的暂停/继续、音量、速度、静音状态及1秒隐藏计时器；其余消息尚需各功能触发。当前默认英文fallback，完整中文资源/动画/语义runs视觉样式未接入。

仍为静态源码批次，未编译、测试或运行。已写入不代表可运行；全量移植和用户数量指标尚未完成。

### 第四批：队列执行器、高级播放与缓存基础

- 队列计划已接入 `PlayerSession.Playlist.cpp`：`MpvClient::GetPlaylistAsync` 与普通命令使用同一单worker串行队列；首次编辑前和每条命令成功后主动读取playlist，独立GET_PROPERTY_REPLY推进cursor，普通property更新只刷新显示。迟到取消回复不复活事务，失败停止剩余操作、不伪称回滚；Open/Stop/Close/换片取消。WinUI排序、shuffle/unshuffle、清空和原移除/移动已接此路径。现有mpv不提供对外部脚本的原子事务保证，检测偏离后停止。快照型计划有32MiB保守估计预算，超大编辑拒绝而非无限分配，未来可改惰性计划。
- `PlayerCore` / `PlaybackInfo` / `AppData` → `PlaybackActions.*`：AB三态、端点/有效区间、可选0秒、50ms自动seek精度策略、百分比/绝对/相对seek、帧进退、音量/速度常量、截图命令选择。`PlayerSession.Actions.cpp` 已接相对seek、逐帧、AB循环；AB端点和次数由mpv属性观察更新，命令不预测状态。UI按钮及左右5秒键接入，文本输入时不截获空格/方向键。自动seek延时测量和截图剪贴板后处理仍未接线。
- `ThumbnailCache.swift` / `CacheManager.swift` → `Thumbnails/*`：v2格式、17字节头、小端整数/binary64、媒体size+mtime失效、逐条JPEG索引/编码/有界读取、访问时间与最旧优先驱逐。坏长度/非有限时间/超预算拒绝，损坏读取不自动删除，JPEG边界检查不是图像解码。没有缩略图生成/图形预览/缓存原子落盘，不能称缩略图功能完成。
- `MainMenuActions.menuShowCurrentFileInFinder` → Windows Shell PIDL + `SHOpenFolderAndSelectItems`，不拼接shell命令行；原生Explorer定位接入应用命令路由。
- `MPVCommand.swift` / `MPVOption.swift` / `MPVProperty.swift` → Catalog静态目录：82命令、773选项、444固定属性、81动态属性，保留全部1380真实声明及层级。提供分类/名称/字面值检索、搜索与参数化属性展开；帧步进、AB和比例命令已引用对应目录常量。生成器仅机械转写固定快照，不生成占位业务逻辑；目录不证明运行libmpv支持每个条目。真实查询表 `.inc` 不改扩展名凑 C++ 计数，数字统计不包含它。
- 增加全部208个Swift文件的保守覆盖清单与生成脚本，映射不自动视为完整移植。

本批未编译/测试/运行。旧portable测试stub和依赖列表仅随异步查询契约静态更新，不能据此报告通过。

### 第五批：可编辑快捷键、高级偏好与截图后处理

本批仍按最新要求仅静态编写，不编译、测试或运行。

- `SettingsPageKeyBindings.ConfigEditor` / `KeyMapping` / `PlayerCore.setKeyBindings` → `Input/BindingConfiguration.*`：只读/复制、增删改排序、搜索、规范键冲突、最后绑定胜出、带 revision 的保存确认和一次 fallback。`MainWindow.Keyboard.cpp` 实际连接 UTF-8 input.conf 导入/导出、文本编辑、保存与恢复默认，存入现有 settings.json 可选字段，空字符串表示主动取消全部绑定。多个命名配置目录模型尚未接多配置切换 UI，不称全套配置管理完成。
- `PlayerWindowController.handleKeyBinding/keyDown` / `KeyCodeHelper` → Windows `PreviewKeyDown` 与 `Platform/KeyboardInput`；移除旧硬编码 accelerators 避免自定义键不能覆盖。键盘布局字符通过不改写死键状态的 ToUnicodeEx 获取，跳过 IME/AltGr/Windows 系统快捷键，文本输入与原生选择器不截获。按键重复只用于连续播放器动作。应用命令走既有路由；普通命令只接受已实现 typed session 动作，不执行 input.conf 中的 run/script、命令链、变量展开或任意 mpv 字符串。这是明确兼容性差异，不宣称任意 mpv 配置兼容。
- `Preference.swift` / `MPVController.mpvInit/setUserOption` → `Settings/PreferenceSchema.*`：65 项真实默认值与枚举、严格类型校验、恢复时逐项回退、仅存覆盖值、显式 mpv 属性与时机。`PreferenceJson`/现有存储和 WinUI schema 控件接入可用映射，Immediate 经异步队列提交；NextMedia 在明确打开/切换时捕获、暂停成功后先设置再加载，自动 EOF advance 不套用尚未提交的新值。界面显示的是偏好而非内核已确认值，失败沿现有错误渠道反馈，不承诺事务回滚。已接置顶/打开全屏/进出全屏暂停偏好与 OSD 开关/隐藏时间/字号；窗口失焦/最小化的异步恢复政策模型已写但尚未接线。
- `PlayerCore.screenshot/createImage/screenshotCallback` / `Preference.ScreenshotFormat` → `Screenshots/ScreenshotOptions.*`、`ScreenshotImage`、`ScreenshotStaging`：截图命令成功回复是文件写完的屏障，精确关联请求，不按“最新文件”猜测；关闭/故障等待 worker 结束后回调。PNG SaveFilePicker 通过唯一临时目录再原子替换目标，独立复制截图按钮经 WIC 有界解码再向 CF_DIBV5 交付所有权。无视频轨道拒绝截图。剪贴板锁失败保留原内容；清空成功后 SetClipboardData 失败无法恢复原剪贴板，明确报错。更多格式/模板的策略模型不表示 UI 已支持任意格式；没有 HDR/ICC 保真或截图浮动预览的运行证据。

快捷键编辑和高级偏好为新增可选 JSON 字段，沿用 schemaVersion=1；旧文件缺字段使用默认值，不需要破坏性迁移。未知高级项不执行，保留问题提示。英文上游 key 作为高级设置标签，完整本地化仍待完成。

### 第六批：窗口暂停恢复、系统进度、历史与媒体信息

- `MainWindowController.windowDidResignKey/windowDidBecomeKey/windowWillMiniaturize/windowDidDeminiaturize` → `WindowLifecycle`、`MainWindow.WindowPolicy`、`PlayerSession.WindowPolicy`：Win32 subclass 观察与 UI 弱引用投递；同时计算失焦/最小化原因，mpv 请求 ID 与媒体代次关联，成功暂停回复才获得恢复权。用户操作、换片、关闭取消旧权利，恢复要求当前 Loaded 且呈现绑定。同主窗拥有的对话框不算离开播放器。默认两项均 false。不是对硬件睡眠恢复的支持承诺。
- `NowPlayingInfoManager.updateInfo` / `RemoteCommandController` → `SystemMediaInfo`、`MediaIntegration`：SMTC 位置/时长/播放速率发布、系统 seek 回调、未知时长/不可 seek 门控、跨媒体代次丢弃迟到系统请求；音乐章节标题/专辑/艺术家、真实视频和封面轨区分。系统共享面板不发布完整路径、URL或形似路径的标签；不沿用 macOS 的暂停速率0做法，Windows 用 PlaybackStatus。未实现封面/专辑图片、chapter performer、远程变速/循环。
- `HistoryWindowController.prepareData/searchFieldAction/selectedEntries` / `HistoryController.remove/removeAll` / `PlaybackHistory` → `HistoryBrowser`、`HistoryLocale`、`MainWindow.History`：搜索文件名或完整路径、最近日期/文件位置分组，过滤后的选中行为按原始身份打开；只删除历史、支持当前会话内撤销，撤销不覆盖同路径新记录。Win32 当前时区与本地化日期/NLS比较替代 Cocoa，本地转换失败显示未知日期；不在 UI 线程探测文件/网络存在性，故记录存在不代表媒体还可访问。
- `InspectorWindowController.GeneralVC/FileVC` / `PlayerCore.getMusicMetadata` / mpv `metadata/video-params/audio-params/audio-device-list/seekable` → `MediaInformation`、有界节点拷贝、快照与 WinUI 信息折叠面板：媒体标签、码率、格式、像素/色彩参数、音频参数、真实输出设备列表与选用。节点数据在下一 mpv event 前复制，metadata限1024项/256KiB，超大项跳过。只展示内核报告，不据此宣称 HDR 或硬件功能正确。面板未覆盖上游全部 inspector 状态字段或任意属性观察编辑。

仍只完成静态代码/接线。没有编译、自动化测试、Windows 运行或系统交互验证；前批和既往验证记录不覆盖本批。完整移植及文件/行数硬指标仍未达到。

### 第七批：可操作滤镜链与本地悬停缩略图

- `FilterWindowController.reloadTable/removeFilterAction/tableView(setObjectValue)`、`MPVController.getFilters/removeFilter/setNode` → `Filters/FilterChain.*`、`PlayerSession.Filters.cpp`、`MainWindow.Filters.cpp`：观察 vf/af 真实节点，显示未知坏行；按 revision/index 生成删除、移动、启停、结构化替换。复制整棵拥有型树保留未知字段，限制4096行/32768节点/1MiB载荷/深度8。编辑先核对UI选择，再在串行mpv worker新读列表核对，使用原生node属性写入和SET_PROPERTY_REPLY；不通过逗号拼接丢参数。mpv没有CAS，外部脚本仍可能在最终读取与写入之间修改，不承诺原子事务。raw参数仅能添加，结构化替换明确拒绝，完整保存滤镜预设管理仍待接线。
- `PlayerCore.generateThumbnails`、`FFmpegController.getPeeksForFile`、主窗悬停预览 → `Platform/ThumbnailExtractor.*`、`MainWindow.Thumbnails.cpp`：实际本地文件句柄读取、Media Foundation单帧解码、WIC JPEG质量0.75、像素比例/旋转和缩放、WinUI进度条悬停图像。180ms防抖，同窗口最多一个原生提取任务，停止源和代次丢弃过期结果；离开/换片/关闭隐藏图像并取消，不在UI等待原生解码。内存缓存最多24张/16MiB，不保存路径或图像到外部服务。
- 明确差异：按需单帧而非IINA的全片FFmpeg预生成；仅本地固定/可移动磁盘普通文件，拒绝网络、离线和重解析路径，不实现远程缩略图偏好；codec覆盖取决于Windows，不与libmpv等价。最多128MiB原帧/1024px输出/180次读取/合作式3秒预算，原生codec阻塞不能硬中断。实际缩略图时间标签来自解码时间；不保证色彩/HDR等价。已有磁盘缓存格式尚未连接这条按需链；内存缓存只在当前媒体使用，外部原地修改文件不会实时失效。
- `enableThumbnailPreview`与`thumbnailWidth`接入设置；提取宽高上限1024px，UI显示240×135等比框，不把上游2048设置范围当作无限解码许可。预览失败不影响主播放器，也不自动反复请求同一位置。

本批只完成静态源码及人工接口/生命周期审阅，未编译、测试或运行。完整滤镜管理、缩略图缓存及全部IINA移植仍未完成。

### 第八批：保存滤镜、用户EQ与裁剪操作

- `FilterWindowController`保存/编辑/删除/toggle、`SavedFilter`字典与`MenuController.updateSavedFilters` → `SavedFilterLibrary.*`、`MainWindow.SavedFilters.cpp`、`MediaPresetsJson.*`：视频/音频分类，保存当前行或输入表达式，编辑名称/表达式/快捷键、删除、基于观察列表应用或移除；禁用匹配项原位启用，不盲目重复添加。JSON新字段兼容旧设置，已有后台原子保存链处理失败与退出；保存定义不改变活动滤镜。模型还支持复制/移动，但本批UI只暴露保存/更新/删除。重复名称按ASCII大小写折叠拒绝，非ASCII精确；跨类别快捷键冲突拒绝，input.conf优先且保存时检查冲突。后续修改input.conf仍可遮蔽已保存快捷键，不宣称全局冲突自动解决。Windows系统键不拦截；从外部存储恢复的Win键字段保留但不是可用键承诺。
- `SidebarAudioPane.audioEqSliderAction/eqPopUpButtonAction`、`Equalizations/EQProfile` → 十段NumberBox编辑、显式应用、从观察af读取、用户预设另存/载入/重命名/删除，及JSON持久化。与上游即时滑块不同，本UI显式应用整组，防半编辑值被异步提交；载入编辑器不改变播放。存储保留有限增益，编辑器限制±12dB；载入超范围值会夹到编辑范围，已保存数据本身不改写。内建英文键用于重名检查，完整本地化尚未完成。
- `InteractiveModeController/CropBoxView`、`PlayerCore.setCrop/removeCropFilter` → `CropSelection.*`：顶左坐标、黑边/缩放/平移/明确90度旋转与源轴flip/mirror的逆变换，八方向/移动/自由框选、四像素吸附、取消、确认及代次返回。未知变换/其他空间滤镜/未确认映射拒绝，不猜测GPU实际画面。**本批未接鼠标拖动裁剪覆盖层**；只将像素矩形校验和mpv crop命令接到 `MainWindow.Crop.cpp` 源像素表单，读取video-params/w,h、换片失效、拒绝其他vf，`@iina_crop`替换/移除走已有异步链。保持所有输入为源像素而不是窗口坐标，避免不可靠的显示映射。

仅静态编写与人工审阅，未编译、测试或运行；交互裁剪UI、完整预设编辑本地化、多窗口、插件/在线字幕等仍未完成。新增JSON字段不迁移或删除既有文件，加载不合法字段沿原备份/default策略处理，不能称数据迁移已实测。

### 第九批：Assrt 在线字幕搜索与下载操作链

- `OnlineSubtitle._search/Provider.fetchSubtitles`、`Assrt.Fetcher.search/showSubSelectWindow/loadDetails`、`SubChooseViewController` → `OnlineSubtitles/SearchTransaction.*`、`Platform/AssrtClient.*`、`MainWindow.OnlineSubtitles.cpp`：显式填写查询词和个人令牌、搜索、单项选择、详情、逐个下载、提交当前媒体的字幕轨道；空结果、服务状态、限额、HTTP失败、取消分别反馈。只在用户点击时请求，不上传影片。应用级 `find-online-subs` 打开面板，不默默发起网络请求。当前选择单个搜索结果并下载其文件列表，不等于上游任意多选批量操作。
- 上游固定 search/detail 地址、POST字段、状态码和空 `subs:{}` 行为保留；JSON严格类型与边界替代Swift强制unwrap。**不复用上游硬编码令牌**；用户在PasswordBox输入个人令牌，搜索时明确展示接收方和用途，令牌仅留在操作内存、不写settings/log。成功下载、取消、换片或失败清除持有值，不承诺所有分配器/系统字符串的安全擦除。后台MTA使用WinRT HttpClient，无UI线程阻塞、无cookie、无交互认证、无自动重定向。每次请求30秒合作式超时，JSON2MiB/200结果/8文件、下载每文件8MiB上限。下载链接仅允许HTTPS assrt.net及其子域443端口、不带userinfo；可能拒绝上游返回的HTTP或第三方CDN，不宣称完整服务可用性或DNS重绑定防护。
- `Assrt.Subtitle.download` → `SubtitleStaging`：唯一临时目录、CREATE_NEW、完整写入flush、独占拥有路径清理，拒绝归档/执行文件特征和二进制字幕，只允许`.srt/.ass/.ssa/.vtt/.lrc/.smi/.sami/.sub`文本。允许UTF16 BOM及旧单字节编码交给mpv解析，不等于内容安全扫描。每批32MiB、每窗口64MiB/8个已成功批次；失败批次不部分载入。mpv会话完全结束后才清除文件，取消中尚未发布结果由worker释放；崩溃残留清理未实现。
- 新搜索、停止/换片、关闭使ticket失效；同一路径重开经过Loading仍取消，迟到网络/下载结果不能载入另一媒体。取消后等待后台实际返回才允许新任务，避免重复点击产生无限工作线程。成功下载先保留临时文件，再通过PlayerSession的 `sub-add select/auto` 提交第一条选中、其余添加；界面只称“已提交”，不以HTTP成功宣称mpv加载成功。加载错误沿现有播放器状态反馈。

仍未实现OpenSubtitles/Shooter、在线字幕插件提供商、帐号持久化/注销、多结果多选、保存下载字幕至媒体目录等上游操作。未调用任何在线服务、未传输用户凭据，未编译、测试或运行；这里只完成静态实现与来源/生命周期审阅。

### 第十批：OpenSubtitles、影片哈希与下载字幕另存

- `OpenSubClient.subtitles/download/RateLimiter`、`OpenSub.Fetcher.search` → `Platform/OpenSubClient.*`及现有在线字幕面板：个人API Key、可选用户JWT、语言代码、关键词/可选moviehash的固定REST查询；每个文件ID为独立选项，显示评分/听障标记，搜索仅第一页并明确提示缩小查询。下载先取得服务票据/配额，再获取文件；不自动重试耗额度的POST。remaining=-1、HTTP鉴权、限流和过期等错误分别建模；API请求保守串行限流，只展示有界的重置时间字段、不展示服务原始错误体。UI明确提示点击下载即可能消耗额度，取消无法撤回服务侧额度。
- 不复制IINA API key，User-Agent使用IIMA。只访问固定api.opensubtitles.com API和HTTPS opensubtitles.com及子域下载，不接受服务返回的base_url/VIP域迁移、cookie、跳转或第三方CDN。用户Key/JWT仅操作内存、无密码登录/自动刷新/凭据持久化；不代表完整IINA帐号功能。API当前部署、发行所需应用注册与域名限制是否覆盖实际链接均待后续验证；本批没有发送请求。
- `OpenSubSubtitle.hash` / `ShooterSubtitle.hash` → `OnlineSubtitles/MediaHash.*`、`Platform/SubtitleMediaHash.*`：OpenSub严格>128KiB、首尾64KiB显式小端uint64模2^64和、16位hex；Shooter>=12KiB、4096/(size/3)*2/size/3/size-8192的4096字节块、Windows CNG MD5串联。MD5只作协议媒体识别，不作安全摘要。Windows读取仅本地普通盘文件，不访问URL/网络/离线/reparse/ADS；同一句柄完整读取、取消检查、前后size/mtime/changetime校验。显式勾选后OpenSub查询才发送hash，失败不默默退回或上传原文件。Shooter hash已实现但服务仍未接，不能称Shooter在线字幕完成。
- `MainMenuActions.saveDownloadedSub` → `MainWindow.OnlineSubtitleSave.cpp`、`SubtitleStaging::Publish`：可选本窗口下载列表、原生SaveFilePicker、捕获文件身份和暂存所有权、后台复制到目标同目录新文件、flush后替换；失败保留原字幕，不像上游先删除目标再复制。播放器继续用暂存副本，另存不偷偷切换当前字幕；关闭或切换期间已开始的保存保持捕获的文件。当前提供下载列表选项，而不是仅按当前外部轨道猜测tmp目录。

未编译、测试、运行或联网。全量移植仍未完成：多结果分页、完整登录/刷新/注销、自动搜索、插件提供商、字幕配额本地化等不在本批完成范围。源码静态可审阅不等于服务或Windows功能已通过验证。

### 第十一批：多窗口播放与并发持久化

- `PlayerCore.active/lastActive/newPlayerCore/createPlayerCore/openURLs`、`AppDelegate.menuNewWindow` → `PlayerWindowRegistry`、App窗口集合、`MainWindow.Windows.cpp`：显式新建独立WinUI窗口，每个窗口仍拥有独立PlayerSession/mpv/交换链/取消源/SMTC，最近激活非关闭窗口接收外部文件批次。下个窗口按创建顺序切换并恢复最小化；一个窗口关闭只释放自身，最后一个实际Closed才停止外部实例路由。关闭开始的窗口立即停止接收激活，但仍占名额直到异步保存、mpv、日志释放完成，保守上限16。没有实现上游alwaysOpenInNewWindow/重复媒体去重/逐文件拆窗偏好；显式新建始终创建，未使用空闲播放器池，不能称完整上游多窗口政策。
- 原共享整份文件保存会使旧窗口覆盖新窗口，因此增加 `UserDataStore::SaveMerged` 与 `UserDataMerge`：同进程锁内重读、按本窗口已保存基线和待保存快照差分提交，标量/偏好key/均衡器名字/滤镜名字独立合并；同字段冲突最后提交胜出。历史删除只删除所见的相同记录，不删除另一窗口后来播放的同路径记录；未修改旧记录不复活其他窗口的删除。延迟旧播放不覆盖较新时间戳，合并后按最近播放限制1000项。保存成功基线更新为本窗口本次提交快照，而非直接把合并结果当作未刷新UI的基线，避免下一次保存反向覆盖。各窗口界面与播放偏好不实时广播，需新建窗口读取最新磁盘；这是与IINA全局UserDefaults的明确差异。
- 每个窗口使用独立日志子目录，避免多个旋转worker争用同一日志文件；单窗口1MiB轮换边界不变，但跨启动目录总量清理仍未实现。Windows SMTC保持每窗口原生session，未实现IINA只把lastActive投影到单个系统媒体栏的策略，不对系统面板选取行为作验证承诺。
- 新出现的损坏文件阻止合并保存；仅与本次进程已安全备份的损坏原文完全一致时允许默认恢复。settings/history仍为两个独立原子文件，不声称跨文件事务或与外部编辑器跨进程互斥。失败保留既有错误/重试/不保存退出渠道。

仅静态编写和人工源码审阅，未编译、测试或运行。窗口资源上限、关闭竞态、SMTC多实例、合并保存和图形并行需Windows独立验收；插件运行时、完整多窗口策略、音乐模式等仍未完成。

### 第十二批：音乐布局、音频播放就绪与窗口位置

- `PlaybackInfo.isAudio`、`PlayerCore.trackListChanged/switchToMiniPlayer/switchBackFromMiniPlayer`、`MiniPlayerWindowController` → `MusicModePolicy`与`MainWindow.Music.cpp`：依据全部轨道而不是当前选中视频判断（禁用视频不把电影当音乐），封面不是实视频，网络保持上游不自动当音乐。默认autoSwitchToMusicMode=true；全屏/Compact不自动进入，实视频自动退出；手动成功转换翻转播放器生命周期override，不随换片复位。代次和待提交ticket防过期布局建议。WinUI在同一个窗口内改变布局/尺寸而非创建第二Cocoa窗口并移动videoView，保留原PlayerSession/交换链，标题艺术家专辑、返回按钮、列表显示和封面显示偏好实际接线。
- 音乐布局沿用既有控制/队列侧栏；并非IINA的逐像素mini-player复刻，未实现其专用触控动画/背景模糊/独立封面布局。手动切入可以显示视频；音频封面依赖mpv已有输出，隐藏封面仅设panel opacity，不宣称停止解码。普通窗口尺寸在音乐模式返回时恢复，未保存独立音乐布局大小。没有插件musicModeChanged事件运行时。
- 音频播放就绪与音乐布局分类分开：已加载、轨道已知且存在音轨而无实视频（包括网络音频），不再强制等待交换链才允许继续/暂停/进度与续播；封面输出可选。未知轨道和有实视频仍须安全交换链绑定；打开仍要求固定libmpv扩展构建，不借此声称普通DLL兼容。错误显示仅忽略已知音频的“Composition output”未准备提示，其余错误保留。原脚本链接源清单随新纯逻辑依赖更新，但没有执行测试。
- `MainWindowLastPosition`、`determineScreenToUse/windowFrameFromGeometry` → `WindowPlacement`、`MainWindow.Placement.cpp`：普通还原态窗口物理坐标与工作区/DPI单独持久化，加载时映射当前显示器可移动区间与逻辑大小，夹限到工作区；后台加载期间用户已移动则不覆盖其动作。音乐/全屏/最大最小化/Compact不是普通尺寸采样；关闭等待后台原子位置保存，失败保留旧文件但不困住播放器退出。捕获顺序仅进程内比较，多个窗口旧任务不会覆盖新采样，重启不受旧序号阻挡。不是每个媒体或每个窗口身份独立的恢复档案；磁盘只保留最近接受的普通布局，无显示器名持久化。
- 位置JSON上限16KiB，拒绝非法坐标、更新schema或目标重解析文件；损坏原文件不自动覆盖。此数据与设置/历史独立，不声称跨文件事务。普通位置恢复默认开启，暂未接IINA全部窗口resize/geometry偏好，首次显示后可能异步调整位置。

本批未编译、测试或运行，尤其需后续验证纯音频FILE_LOADED/track-list顺序、同媒体重播、音乐/全屏/Compact交互、显示器断开/DPI、独立窗口关闭与位置持久化。全量移植及数量要求依然未完成。

### 第十三批：本地插件清单与权限检查

- `JavascriptPlugin.init(filename:externalURL:)`、`Permission`、`JavascriptPluginInstance.canAccess`、`PrefPluginPermissionListView` → `Plugins/PluginManifest.*`：必填字段、反向域名标识、入口、仓库更新元数据、权限、字幕提供商声明、危险权限排序和域名匹配。未知权限保留警告而不授予。声明域名不是用户授权或网络安全边界；此批没有执行网络请求。
- `SettingsPagePlugin` / `PluginViewController` → `MainWindow.Plugins.cpp` / `Platform/PluginInspection.*`：用户选择目录→后台有界读取Info.json→解析并展示基础名称/作者/版本/权限/域名/错误→检查入口普通文件存在性。原生FolderPicker由当前窗口初始化，关闭时取消并丢弃结果；不解压、安装、复制、启用或运行第三方脚本。
- Windows路径明确收紧：只读本地固定/可移动磁盘，祖先句柄阻止路径替换，不跟随重解析点/离线文件；相对路径拒绝穿越、设备名、ADS、尾点空格、百分号与控制字符。与上游开发目录符号链接、Unicode标识符等支持不同。Info.json上限1MiB/32层；字符串和列表分别设界限。仅检查一次快照，不代表以后读取安全，更不是插件安全审计。
- `localized`尚未选择系统语言；显示基础文本，preferenceDefaults不应用。HTML偏好/帮助/侧栏、插件字幕注册、脚本运行时、插件安装更新和JavaScript API仍未实现。界面明确叫“检查”，不提供虚假的“启用”。静态校验成功不代表入口存在、脚本合法或可运行；入口错误单独列出。
- 更新能力模型按`checkNewVersion`真实路径要求同时存在ghRepo与ghVersion，不照搬上游supportAutoUpdate的反向条件。无更新请求发生。此批未编译、测试、运行或加载任何插件。

### 第十四批：按需缩略图磁盘缓存与清空

- `ThumbnailCache.fileExists/write/read/clearThumbnailCache`、`CacheManager.getCacheSize/clearOldCache` → `Platform/ThumbnailDiskCache.*` 接入 `MainWindow.Thumbnails` 真实后台提取链。命中复用既有version 2记录格式与真实actualSeconds；缺失/坏缓存/无权限/磁盘压力/锁超时均回退到Media Foundation按需提取，不阻断主播放。不是FFmpeg全片预生成，也不是跨平台缓存目录兼容。
- SHA256文件名包含路径、卷/文件ID、大小、高精度修改/创建时间、请求时间、输出尺寸和实现修订；仅缓存元数据保存秒级原格式。读源持只读共享句柄，禁止修改/替换直至本次提取结束。命中WIC核对JPEG容器与尺寸<=1024和请求上限后再交BitmapImage；大小界限为单图8MiB，目录扫描2万项。内存层仍限当前媒体24张/16MiB，媒体正在播放期间原地修改不会主动刷新已在内存中的图像。
- 缓存目录 `%LOCALAPPDATA%/IIMA/Cache/ThumbnailFrames-v1`；保存图像而不在文件名明文暴露源路径。拒绝网络/重解析/离线/硬链接文件，祖先目录持锁，命名mutex串行缓存访问，最多等待3秒；临时文件写完刷新后同卷原子替换。按访问时间驱逐，上游半额度策略同时满足新图可容纳，不故意超出配置额度。崩溃留下的未识别临时文件不自动清理，故真实目录占用可能包括这些残留。
- `maxThumbnailPreviewCacheSize`接高级偏好与持久化，单位MiB，0禁用磁盘读写；不因设0隐式删除旧缓存。显式“清空缩略图缓存”确认后后台删除专用目录中符合自有文件名的普通文件，不递归删除、不触碰视频或其他文件。进程级epoch使其他窗口先前开始的提取不再写回；跨进程只有mutex而无共享epoch，其他进程仍可新写。清空当前窗口内存，其他窗口已有内存预览不强行广播清除。
- 第十三批插件检查补充UI调度失败时直接结束，避免关闭后的后台继续修改界面状态。

仅静态编写/人工审阅，未编译、测试、运行或实际写入缓存。全部功能和数量目标仍未达成。

### 第十五批：固定 JavaScript 引擎与受限本地插件执行

- `JavascriptPluginInstance.createJSContext/evaluateFile/deinit` → QuickJS-ng `v0.12.1` 固定官方提交（详见 QUICKJS.md）、`Plugins/PluginRuntime.*`、`MainWindow.PluginRuntime.cpp`。用户选择本地普通插件目录后，先在后台读取/验证清单与入口 UTF-8 文本，再确认运行这份已读取的代码；不重新读文件造成确认后内容替换。每窗口最多一个临时实例，不安装、不自动启用、不持久化授权。已声明但未实现的权限和 globalEntry 拒绝启动，避免静默授予能力。入口在独立 worker 执行，运行失败、停止、关闭取消所有定时器/监听器和排队动作；释放 QuickJS 在后台 join，最终窗口关闭等待释放而不让 worker 同步等 UI。
- `JavascriptAPICore.pause/resume/stop/seek/seekTo/setSpeed/osd` → 受限宿主函数，通过 generation 守卫投递到原 typed PlayerSession。OSD 还需清单 show-osd 和运行确认；命令入队不冒称 mpv 已成功。`StatusAPI` → 复制的观察状态 JSON（暂停、空闲、位置、时长、速度、尺寸、是否网络资源）；不提供原生句柄。此首版 grant 不允许完整 URL/路径或标题访问，`url/title` 为 null，明确不同于上游。不实现 open/history/recentDocuments、audio/video/subtitle 子 API 或任意 mpv 命令。
- `JavascriptAPIEvent.on/off` / `EventController` → 有界事件监听及投影，只接受实际接入的 `iina.file-started/file-loaded/music-mode.changed` 与 `mpv.pause/time-pos/duration/speed.changed`。事件载荷在 worker 消费前深拷贝；回调可注册/注销而不破坏迭代。文件事件来自既有 PlayerSession 的有序状态投影，不宣称完整 mpv 原始事件流；运行后才注册的插件不伪造过去的文件/窗口事件，关闭是强制取消而非执行不受控关闭钩子。
- `JavascriptPolyfill` → 字符串 ID 定时器、取消和 Promise job drain。超时至少16ms、interval至少50ms，最大一天；每实例最多128定时器/256监听器，与上游未限定数目的行为有意不同。CommonJS require、模块加载、全局实例、DOM/HTML、文件、HTTP、WebSocket、菜单/输入/字幕提供商和偏好 API 尚未实现，不能称完整 IINA 插件兼容。未知 namespace/API 显式抛错，不返回假成功。
- `JavascriptAPIConsole` → 控制台多参数/类型显示、可序列化对象和环回回退；日志只显示在当前窗口有界文本框，不落磁盘。循环对象用 `<unserializable>` 回退。计时/入站事件/出站日志命令均有限额；64MiB引擎内存、1MiB JS 栈，入口500ms、每事件/定时器100ms预算，Promise job共享预算。溢出或超时停止并反馈，不悄悄假设所有事件送达。

这些限制不是 OS 安全沙箱，也不能保证面对引擎漏洞的不可信代码隔离；产品只允许用户明确选择受信任来源。没有链接 QuickJS std/os 模块，不代表全部 JS 特性或任何插件已实际可运行。QuickJS 源、MSVC CI及 API 契约只做静态阅读；本批未编译、测试、执行脚本或联网运行插件。MIT 依赖许可证及固定来源随打包保留，IINA 转写继续保留 GPL 来源。完整插件 API 和全量移植目标仍未完成。

### 第十六批：本地插件模块、轨道/章节 API 与属性事件

- `JavascriptPolyfill.register(require)` / `JavascriptPluginInstance.evaluateFile(asModule)` → `PluginModulePath`、`PluginInspection.ReadModules`、运行时模块缓存。选择目录时在后台对普通本地 `.js` 文件做 UTF-8 源快照，确认后 worker 只使用快照、不任意读盘；每文件1MiB、总量8MiB、128模块、16层/128目录，跳过重解析点、离线文件和`.git`。`require`按调用模块目录解析`./`和`../`，不得越根；不自动补扩展、index、node_modules搜索、JSON模块或ES Module，文件名/大小写按快照键精确匹配，缺模块明确抛错。
- 保留上游`const module = {}`和`module.exports`返回，不伪造Node的`exports`别名。缓存采用规范相对键并缓存undefined/falsy，修正上游按原请求查缓存/按绝对路径存缓存的重复求值风险；32层限制和循环依赖显式报错。每个模块捕获词法require，使定时器/事件回调后仍按定义模块解析，不依赖上游临时currentFileStack在回调后已为空的状态；属于有意缺陷修复。嵌套求值、Promise及回调共享原预算，不因require重新计时。
- `JavascriptAPICore.getChapters/playChapter`、`AudioAPI/SubtitleAPI/VideoAPI` → 状态投影新增轨道/章节以及已接入typed会话命令：音量、静音、音频/字幕延迟、音频/字幕/视频轨道ID、章节跳转。`tracks/currentTrack`只读；未知数据仍null，不声称全部代理属性已实现；`secondID`无可靠主次来源时null，设置暂不支持。`loadTrack/open/history/recentDocuments/任意mpv`等仍未接。轨道选择和章节命令携带worker观察到的媒体代次，UI消费时再次核对，换片后丢弃旧ID命令；不是同步内核完成承诺。
- `MPVController`属性事件与上述API → 增加`mpv.volume.changed/mute.changed/audio-delay.changed/sub-delay.changed`；读取均来自有界已复制快照。轨道/章节元数据读取已加入运行确认，外部轨道路径、完整媒体URL和媒体title仍不投影。256轨、2048章、单文本4096字节、总JSON64KiB上限，超限明确停止插件，不静默截断为“完整列表”。同时选择多个同类型轨而无法判主次时`id/currentTrack`为null，不猜测。

本批继续仅静态源码编写和人工阅读；没有编译、测试、执行任何JS或运行Windows。已写模块与API不表示插件整体兼容；偏好持久化、菜单、HTTP/文件/HTML、全局控制器、完整字幕提供商和任意mpv接口仍缺失。数量和全量功能目标仍未达到。

### 第十七批：插件偏好默认值、显式保存与跨窗口合并

- `JavascriptAPIPreferences.get/set/sync`、`JavascriptPlugin.defaultPreferences/preferences/syncPreferences` → `PluginRuntime` 的 `iina.preferences`、`PluginProgram` 默认值快照与 `Platform/PluginPreferenceStore`。先从当前插件清单读取 `preferenceDefaults`，运行前加载该标识已保存值；运行确认明确说明同标识共享偏好和不应用于秘密存储。`get` 先查已设置值，再查默认值；未定义返回 undefined 并警告。`set` 只更新当前实例内存，`sync` 才同步调用后台 worker 的原子保存，不在 UI 阻塞磁盘，不自动在停止时保存。
- Windows 使用应用私有 JSON 而非 macOS plist；运行时最多256个顶层键、键1..256 UTF-8字节（比存储UTF-16上限更保守）、4096节点、16层容器、64KiB总JSON，sync至少间隔250ms。严格 JSON 值、深度和容量限制，拒绝不可表达对象而不是保存后悄悄改变值。读取返回深副本，避免未经 `set` 的对象修改偷偷改变持久状态。默认值不自动拷入用户文件；已有保存值（包括 null）覆盖默认。HTML 偏好界面仍未实现。
- 通过应用专用独占锁文件串行、按插件标识差分合并，未改的字段保留其他窗口的新值，同字段最后成功提交胜出；成功后推进当前实例本次提交基线，不将其他窗口的值反向视为当前实例已编辑。损坏文件拒绝加载/覆盖；保存失败变成 JS 异常，与 IINA 只记录错误不同，不冒称成功。文件与目录路径由宿主决定，JS 不能指定任意目标。
- 原生磁盘 I/O 不可被 QuickJS 中断；输入有界、锁等待有界，但系统存储阻塞仍可延迟 worker 关闭。偏好并非加密凭据存储，也不是不同同标识插件之间的安全隔离。锁保护使用同一存储实现的进程；外部编辑器未遵循锁协议时不承诺跨进程事务，也没有实时跨窗口偏好广播。

本批只静态编写和人工阅读，未编译、自动测试、运行 Windows 或执行插件。插件菜单、输入、HTML、文件/网络等其余 API 仍未完成，数量与完整移植目标仍未达到。
