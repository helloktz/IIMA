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

### 第十八批：插件菜单对象与原生点击回调

- `JavascriptAPIMenu.item/separator/addItem/items/removeAt/removeAllItems/forceUpdate`、`JavascriptPluginMenuItem` → QuickJS 菜单对象及回调生命周期；`MenuController.updatePluginMenu/add(menuItemDef:)` → `MainWindow.PluginMenu.cpp` 原生 MenuFlyout。插件菜单位于当前窗口的“插件菜单”按钮，不注册系统级菜单或跨窗口命令。保留默认 enabled=true/selected=false、可变标题与状态、嵌套菜单和点击回调收到自身 item 的规则。
- JS 对象与原生控件不共享裸指针：worker发布有界纯JSON快照，UI创建完整菜单后一次替换；点击携带实例generation、菜单revision和item ID，worker重新检查最新树中可达、启用及祖先状态，旧菜单点击不能作用于已移除或被停用的项。原生选中标记不自行改变插件值，只有脚本更新后的快照是权威。
- 每个JS执行回合后合并发布变更，不逐个setter同步调用UI；forceUpdate同样经过线程隔离。不支持非空keyBinding时明确拒绝，不绘制虚假的可用快捷键。最大128可达节点、8层、64KiB JSON；环路、非法对象或过量数据拒绝。不复制IINA超过5个顶层项合并“更多”的布局，也没有全局插件与开发工具菜单；重复引用同一个菜单项会被拒绝，子菜单只展开、不调用自身action，更新快照会关闭旧flyout再替换。标题限制1024 UTF-16单位/4096 UTF-8字节。这些平台和当前单插件范围差异保留。
- 停止、异常或关闭清空菜单并使旧callback代次失效；JS callback只在引擎worker调用，沿既有时间/内存/消息预算处理异常。无法投递或解析快照会停止插件并反馈，不继续展示可能失真的半更新菜单。

仅静态源码编写和人工阅读，未编译、测试、运行WinUI或执行脚本。菜单快捷键、菜单多插件汇总、插件安装/更新、HTML及其他未覆盖API仍非已完成功能。

### 第十九批：插件键盘观察与有效快捷键快照

- `JavascriptAPIInput.normalizeKeyCode/getAllKeyBindings/onKeyDown/onKeyUp`、`PluginInputManager.addListener/handle` → `PluginRuntime` 的键监听和输入快照，`MainWindow.PluginInput.cpp`、`MainWindow.Keyboard.cpp` 的真实 WinUI routed event 接线。键名复用已转写 `NormalizeMpvKey`，当前有效 input.conf 采用默认section/最后定义胜出的 `KeyBindingSet`，返回 key/action/isIINACommand 字段的深副本。保存/重置配置时投影新快照，未应用编辑区不泄漏为当前配置。保存滤镜快捷键和插件菜单不是 input.conf 项，不假装出现在列表中。
- 只提供异步低优先级观察，不在 UI 同步等待 JavaScript；应用绑定先提交，脚本回调随后在 worker 执行。高优先级/吞键返回值不能跨这种调度提供上游保证，故高优先级显式拒绝，回调返回值不阻断 Windows 默认处理。鼠标监听明确未实现，不伪造为可调用的空函数。输入只限当前播放器区域，文本、密码、数字输入、自动建议框、模态对话框、picker、系统键及原生控件拥有的导航/激活按键均不投递；不是全局键盘钩子。运行确认说明可读取完整绑定动作文本，用户不应将秘密置于动作中。
- 参数保留 isRepeat；WinUI 键事件不提供上游 NSEvent 的鼠标位置，x/y 为 null 而不是伪造坐标。按键在事件发生时按当前布局/修饰键规范化，修饰键释放顺序可能导致 up 键名不同；没有合成失焦释放事件或完整物理键追踪。只有声明低优先级监听的键被调用，相同键/事件的新注册替换旧回调，沿有界队列、时间和内存预算；关闭、停止、重新运行仍由实例代次隔离。队列或快照不能完整交付时停止插件并反馈，不把丢失事件视为可靠投递。
- 快照最多1024项、键256字节、动作16KiB、总JSON256KiB；超限保留实际播放器配置但停止插件投影，绝不截断后宣称完整。快照读取可能包含用户配置中的路径或自定义命令，明示于每次启动确认；插件仍不拥有任意命令执行能力。

本批仅静态源码编写和人工阅读，没有编译、自动测试、Windows运行或插件执行。高优先级输入、鼠标、插件HTTP/文件/HTML及其他IINA功能仍未完整移植；文件数与行数指标也不等价于语义完成。

### 第二十批：视频区域插件鼠标观察

- `JavascriptAPIInput.onMouseDown/onMouseUp/onMouseDrag`、`PluginInputManager`、`PlayerWindowController.mouseEventArgs` → 插件 worker 的鼠标监听、`MainWindow.PluginPointer.cpp` 和 `PluginPointerState`。左/右/中键分别映射 `MOUSE/RIGHT_MOUSE/OTHER_MOUSE`；下按、释放、拖动回调带 x/y/clickCount/pressure，键鼠共享256监听上限，同按钮/事件注册替换。异步低优先级，不支持高优先级或通过回调返回值吞掉输入；本批不改变播放器原生单/双击动作政策。
- 只订阅VideoPanel的原生鼠标事件，不捕获鼠标、不装全局钩子、不映射触摸/触笔/X按钮。对话框、picker、文本控件拥有焦点或事件已处理时不发布。x/y使用窗口XAML根的逻辑像素、左下原点，和上游窗口坐标方向一致，但不是物理像素或视频源像素；pressure直接使用Windows PointerPointProperties值，不承诺与macOS硬件压感等价。双击按Windows时间/矩形规则计算，第三击重新开始一对，而非无限NSEvent.clickCount。
- 必须在视频区域观察到按下才发送其释放/拖动。拖动每按钮最多约30Hz，作为有界观察而非原始所有采样；离开视频区域、捕获丢失、系统取消、窗口失活或插件停止清空本地按下状态，不产生伪造的释放回调。因不捕获鼠标，区域外释放不会被送达，这是明确范围限制。运行确认加入视频区域鼠标观察说明。
- worker只使用不可变事件数据和发生时的媒体状态快照；回调在队列消费时查当前注册监听，不声称UI发生瞬间的同步回调。有限坐标/压力、事件名称、按钮名称检查与既有队列/时间预算共同控制异常；不能完整入队时停止实例。未知事件不悄悄重映射。

未编译、测试、运行Windows或执行插件脚本；原生输入坐标、DPI、双击与关闭需要后续验证。完整高优先级输入兼容、HTTP/文件/HTML和大量其他上游操作仍未完成。

### 第二十一批：插件私有文本文件操作

- `JavascriptAPIFile.exists/list/read/write/delete` 与 `JavascriptAPI.parsePath` 的 `@data/`、`@tmp/` 分支 → `Platform/PluginFileStore`、QuickJS `iina.file` 与启动回调。保留私有路径无需 `file-system` 权限、持久数据按插件标识共享、临时数据按实例区分、私有文件允许覆盖、写入失败抛错等规则。运行确认明确包含私有文件读取、覆盖、删除；没有默认授予任意用户文件权限，声明外部文件权限的插件仍拒绝启动。
- Windows 适配为宿主选择的本地目录与普通 UTF-8 文本文件，拒绝路径穿越、设备/流名称、重解析点、离线及多硬链接文件；原子写入而非半文件更新。IINA 支持的外部绝对路径、媒体路径别名、其他文本编码、递归/子目录、文件句柄、回收站、Finder 等本批未接入。不能把此有界子集称为完整文件 API。
- JS 和 WinRT/Win32 不共享句柄或裸文件指针，worker 同步调用 JSON 参数/结果桥接；错误转为 JS 异常，未完成写入不返回成功。输入、文件数、存储总额和调用频率有界；系统磁盘阻塞仍不能被 JS 时间预算中断，也不是操作系统沙箱。相同插件标识共享数据不是安全隔离，不能用作秘密凭据库。

- 本批仅支持平铺文件名，1MiB单文件、128文件与16MiB单命名空间总额；每秒32调用、4次修改，超限抛错。正常worker退出时有界清理本实例临时普通文件，不递归、不追踪异常文件；空临时目录/锁、崩溃与清理失败残留不自动回收。永久数据无自动过期，配额不跨不同标识聚合。锁只串行遵守此实现的调用者，不承诺对外部编辑器的事务隔离。

仅静态编写和人工阅读，未编译、测试、运行 Windows 或执行插件。插件 HTTP/HTML/任意文件及其他 API 仍缺失，全部功能与数量目标未完成。

### 第二十二批：插件异步 HTTP 请求与域名许可

- `JavascriptAPIHttp.request/get/post/put/patch/delete`、`HTTPResult.toDict` 与 `JavascriptPluginInstance.canAccess` → `PluginHttpClient`、QuickJS Promise 网络工作队列及启动权限确认。请求在独立网络 worker，不在 WinUI 或 JS 事件线程等待；JS 只在其所有者线程创建/结算 Promise 和继续 jobs，跨线程仅传递有界请求/结果字符串与请求标识。HTTP 2xx resolve，其他HTTP状态 reject响应对象；传输/取消错误独立失败，不把网络提交当服务器成功。
- 必须在清单申请 network-request，运行确认展示完整 allowedDomains 及网络数据发送能力，拒绝未许可主机。仅HTTPS 443，无重定向、浏览器Cookie、默认凭据或登录UI。可显式传入有界头部/字符串参数/表单字段；不支持二进制、任意JSON请求体、XML-RPC和download。当前显式禁用系统代理，代理依赖网络环境可能不可用；IP地址与常见本地域名拒绝，但域名声明不是DNS或操作系统网络隔离，不能把本实现称为安全沙箱；受信任插件可发送其已能读到的数据，并可能执行有服务端副作用的请求。
- 每实例最多4个待结请求，独立网络线程按队列顺序处理；30秒预算从该请求开始执行时计时，排队等待不计入，因此四个串行慢请求的总等待可超过30秒。最多1MiB响应体，停止触发取消并等待网络worker回收。响应只按UTF-8解码，不实现Just其他字符集策略；JSON超过32层或结构令牌预算时保留text、data为null。系统认证与取消的实际平台行为仍未验证。请求与停止边界和Promise状态属于当前实例，不会在新插件实例上结算旧请求。所有响应仍需后续真实服务验证，不假装兼容Just的每个编码/重试/重定向细节。

本批无HTTP请求、编译、测试、Windows运行或插件执行。插件文件/HTTP仍只是受限子集，HTML、安装更新、多插件、全局实例及其他上游功能仍未完整移植。

### 第二十三批：插件播放列表查询与编辑

- `JavascriptAPIPlaylist.list/count/play/remove/move/playNext/playPrevious` → 插件播放状态快照、QuickJS playlist API 和 `MainWindow.PluginPlaylist`，最终调用既有 `PlayerSession` 操作。`list` 返回 filename/title/isPlaying/isCurrent；未知标题为 null。当前 mpv 载荷只有 current 标记，isPlaying 由 entryId 与 Loading/Loaded 关联推导，不冒充 mpv 原始独立 playing 标记。`count` 可读取保留队列，编辑仅在活动媒体下接受。add（涉及打开 URL 权限）与 registerMenuItemBuilder 仍明确拒绝。
- 启动确认增加完整播放列表文件路径、网络地址和标题读取，以及移除/移动/播放能力；网络权限确认说明这些地址也可被插件发送。路径可能含秘密，因此没有在不提示的情况下扩大读取范围。列表最多1024项、每字段有界并计入64KiB状态总额，超额失败而不悄悄截断。
- 脚本动作带发生时的媒体代次及全部按序 mpv 条目ID（十进制字符串保留64位精度）。UI消费前逐项核对，换片/重排/替换后拒绝旧索引；相同路径的不同条目仍保持ID身份。编辑繁忙不替换正在执行的事务。批量删除沿既有 `PlaylistPlanCursor` 新鲜快照和串行回复链；`move` 的目标是移除前条目位置，复用Drop计划而不是误用“最终下标”移动接口，保留上游mpv语义。
- JS成功值只代表请求进入宿主队列，不是mpv完成证明。UI发现过期、繁忙或非法请求会显示反馈；不向已返回JS函数伪造同步失败。连续依赖编辑必须等待观察到的新列表，不能假定两次同步调用会按旧下标都成功。检查与mpv执行之间仍无CAS，外部脚本可能竞争；播放跳转沿既有异步切换保护，不宣称原子列表/播放事务。

仅静态编写、源码阅读和Git发布核验，没有编译、测试、Windows运行或插件执行。完整插件API及数量目标仍未达到。

### 第二十四批：系统睡眠暂停与恢复权取消

- `PlayerWindowController` 注册 `NSWorkspace.willSleepNotification` → `PowerLifecycle` 的本窗口 `WM_POWERBROADCAST/PBT_APMSUSPEND`；`Preference.pauseWhenGoesToSleep` 默认true → 实际设置界面和当前偏好读取；`PlayerCore.pause` 的 active guard → `PlayerSession.SystemWillSuspend` 的 Ready + Loading/Loaded 检查。不会向空闲、结束、失败或关闭内核提交pause，保留上游issue #4520避免睡眠中唤起空闲绘制的意图。
- Windows HWND同线程subclass直接调用短小会话通知，只排入异步mpv命令，不等UI队列、不等待mpv完成，也不延迟/否决系统睡眠。注册现代待机通知；失败保留传统广播适配并提示限制，不声称已成功订阅所有电源状态。每个窗口各自取消订阅，窗口销毁帧持有状态寿命，关闭后不再调用会话。
- 上游只在睡眠时pause，不在唤醒时自动resume；本适配同样不自动恢复。睡眠暂停撤销此前失焦/最小化与换片的自动恢复权；额外sleep latch阻止迟到Loaded/图形重绑重新取得恢复权，直到显式播放/切换/重新打开。属于异步Windows状态机所需的意图保持，不伪造暂停已确认状态。系统未给足时间时命令可能直到唤醒后处理，不能保证每种硬件在断电前已经静音。
- 本批仅处理睡眠暂停，不实现HDR/显卡设备重建或关屏即睡眠假设。现代待机、传统S3、休眠、外接显示器、设备恢复仍须平台验证；软件防休眠请求与系统强制睡眠是不同机制。

未编译、测试、运行Windows，未触发系统睡眠或电源设置变更。文件数可能达到阈值仍不证明功能完整或行数达标。

### 第二十五批：Inspector 自定义只读属性监视

- `InspectorWindowController.swift` 的 watchProperties、add/removeWatchAction、saveWatchList 和每秒 dynamicUpdate → `MainWindow.InspectorWatch.cpp`。Windows 使用每行一个属性名的整体编辑表单，空行为删除；保持上游空列表默认值，保存到现有设置 JSON，跨窗口以修改过的整个列表合并。不会把属性值保存到设置或诊断日志。面板展开才启动每秒刷新，折叠与关闭取消请求和定时器。
- `MPVController.getString` → `MpvClient` 工作线程串行 `mpv_get_property(..., MPV_FORMAT_STRING)`；每项独立显示错误，字符串由 mpv_free 对应释放。最多32项、名称128字节非空白ASCII，每值显示4096字节并避免截断UTF-8字符。这些限制区别于上游无限列表，用于限制UI投影与队列负担；mpv内部分配不受显示截断上限约束。
- `PlayerSession` 一次只接受一个监视批次，START_FILE（包含同条目重播）、END_FILE、失败、退出与关闭取消当前完成回调。UI使用编辑/折叠代次、路径与条目身份拒绝迟到结果。只读属性不是原子媒体快照：底层播放器仍可在批次中更新属性，单次同步属性读取也没有强制超时，后续需实际验证响应性。
- 与上游的可点击HTTP链接不同，属性值只作为可选择纯文本显示，不自动打开或预取地址。暂未加入独立Inspector窗口、列宽持久化和完整动态性能统计布局。本批只静态编写与源码阅读，不编译、测试或运行。

### 第二十六批：视频均衡与反交错反馈操作链

- `SidebarVideoPane.EqualizerView` 五条滑杆和逐项 resetAction → `MainWindow.VideoAdjustments.cpp` 的亮度、对比度、饱和度、Gamma、色相滑杆与重置按钮，范围保留 -100..100，重置为0。复用已转写的 `PlayerSession.setVideoEqualizer`，实际 mpv 属性观察反馈作为权威值，不仅移动UI滑块。
- `MPVController` 的视频均衡/反交错观察 → worker 的 INT64 均衡属性与 STRING 反交错属性、optional快照和 reducer。缺值/非法值清空，不伪装默认值。参考 mpv 快照支持 deinterlace=auto，保留该模式，以第三态显示；用户明确开关写yes/no，不擅自把auto当no。UI暂停投影回写，避免接收到属性后又发相同命令形成循环。
- Windows布局以原生Slider/CheckBox实现，未知属性禁用并显示状态。采用逐项复位而不是隐式清空整个滤镜链；不额外保存成持久偏好，因为这些是当前播放器属性。快捷键、脚本及mpv属性变化也可回填。音频-only及视频后端是否支持这些属性仍需实测。

本批仅静态源码移植，没有编译、自动化测试或Windows/mpv运行。反交错三态交互、命令失败恢复、快速拖动与异步反馈视觉体验仍待实际验证。

### 第二十七批：双字幕轨道、显示与参数反馈

- `SidebarSubtitlesPane.TrackSelector/VisibilitySwitch/SubPositionDelayView`、`PlayerCore.setTrack/toggleSecondSubVisibility/setSubDelay/setSubPos` → `MainWindow.SecondarySubtitles.cpp` 与既有会话 API。新增副字幕选择、显示开关、延迟/位置/缩放编辑与延迟复位，主副轨道分别使用实际 `sid/secondary-sid`，不再用 track-list 中可能同时为真的 selected 标记猜主字幕。`mp_property_switch_track` 的 -2/no 与 -1/auto 保留，未知值不伪装为关闭。
- `MPVController` 的副字幕属性观察 → worker 的独立 INT64/DOUBLE/FLAG 观察及 optional 快照。START/END 清除媒体轨道身份；FILE_LOADED 后主动读取两个选择值，避免同一文件重播时没有变化通知而永久未知。读取不是原子事务，随后属性事件仍为权威。插件 currentTrack 与 secondID 同样使用明确身份，补齐对应变化事件；旧文档所述副字幕ID总是null不再适用。
- IINA 的主/副分段位置和延迟控件映射为原生独立副字幕表单。为防止高频进度通知覆盖输入，用户先读取当前参数，再显式应用；当前观察值另行显示。换片、结束、尺寸无关的媒体重载都会使草稿失效，不把旧草稿当作新媒体数据。字幕样式对纯图形轨道禁用并提示；ASS排版显示警告。
- 固定 mpv 快照实际支持 secondary-sub-scale，范围0..100，及主副位置0..150；副表单支持完整范围并警告>100可能截断，IINA上游侧栏位置只提供0..100。主副通用setter接受mpv范围，原主字幕表单仍使用较窄范围。缩放0合法但可能不可见。多个选项分别异步提交，不是假装原子应用或成功播放；实际DLL若不支持属性，界面显示未知/不可用。

仅静态源码、文档映射、数量统计和Git发布核验。没有编译、自动化测试、Windows/mpv运行或插件执行。双字幕位图重叠、ASS排版、快速选轨、命令失败恢复及完整字幕样式仍待实际验证或后续移植。

### 第二十八批：精确时间跳转与自定义倍速

- `MainMenuActions.menuJumpTo/menuJumpToBegin` → `MainWindow.Navigation.cpp` 的原生时间对话框/回到开头按钮，沿用已转写 `VideoTime` 做时分秒换算，并最终走既有 `PlayerSession.Seek` 的 absolute+exact 命令。初值取最新已观察位置，保持3位小数；不另做UI线程同步读取，不因打开对话框暂停播放。窗口关闭Hide，取消不发命令；换片、重播、EOF、时长/seekable失效后不将旧输入应用于新媒体。
- 相比 IINA 宽松的 VideoTime 输入，本对话框拒绝空字段、超过3段、非法数字、负数、溢出和超过当前时长的目标；避免 `typo:30` 或溢出小时被悄悄解释为另一时刻。接受秒、分:秒、时:分:秒及小数秒，界面明确以点为小数分隔，和原生倍速 NumberBox 区域设置解析分开。
- `SidebarVideoPane.SpeedView.convertSpeedToSliderValue/sliderAction/constrainSpeed` → `Playback/SpeedControl`：0..24对数滑杆映射0.25..16，滑杆2位精度沿Swift最近/远离零舍入，精确输入按十进制最短表示做6位half-down量化。数值范围沿现有会话0.01..100；IINA原输入仅最低约束，本实现明确保留100上限。滑杆端点只夹显示位置，不把用户输入的0.1或32强行改成0.25或16。
- 控件观察反馈不会回发命令；数值编辑区不被每次time-pos覆盖，实际当前速度另行显示。应用与恢复正常速度沿既有用户倍速持久化策略。没有增加假的成功状态，命令结果仍由正常异步错误/观察链处理；精确UI输入不意味着硬件解码能满足所有倍速。

本批只静态编写与人工源码审阅、统计、Git发布核验。没有编译、自动化测试或Windows运行；区域数字输入、拖动时旧属性回填的视觉效果和极端倍速仍待实测。

### 第二十九批：A-B 循环端点精确编辑

- `PlayerCore.abLoopA/abLoopB`、`PlaySliderLoopKnob.mouseDragged`、`PlayerWindowController` 端点回填 → `MainWindow.ABLoop.cpp` 与 `PlayerSession.EditABLoopPoint`。保留先建立端点才可修改、最小编辑值0.000001秒、A可以在B之后的规则；复用已有ABLoopState命令生成，而非另一套播放器状态。
- Windows使用可键盘访问的原生NumberBox和逐端点应用按钮，暂不复制播放条双拖动thumb。先读取观察值形成草稿，变化、换片、同条目重播、EOF和关闭使草稿失效；高频进度不覆盖正在输入的数值。输入限当前可跳转媒体0..duration，0按上游最小值夹限，取消编辑不产生播放器命令。B-only异常状态不允许修改B；只能先循环或清除后建立正常端点。
- START_FILE清空旧端点和次数，FILE_LOADED后在mpv线程重新读取，解决同文件重播因属性值不变而没有通知的问题。显示保留“未设置或不可读取”，没有把失败当作0秒；次数0及相等端点提示不会形成有效重复区间。
- 一次只提交一个端点，提交接受不代表mpv完成，也不声称两个端点原子更新。草稿核对和底层执行之间没有CAS，外部脚本仍可能竞争。实际显示仍来自观察，失败进入既有异步错误反馈。精确数字输入不意味着解码器能够逐微秒定位。

本批只静态编写、人工源码阅读、统计与Git核验，没有编译、自动化测试或Windows运行。原生拖动双端点、完整循环次数编辑及其他未覆盖功能仍需继续移植。

### 第三十批：播放列表多选、批量移除与接下来播放

- `SidebarPlaylistPane.removeBtnAction/contextMenuRemove/contextMenuPlayNext` → `MainWindow.PlaylistSelection.cpp`、WinUI Extended列表选择与既有 `RemovePlaylistItems/QueuePlaylistItemsNext`。Ctrl/Shift选择多个条目，按视觉行顺序而非点击顺序处理；接下来播放跳过当前条目并保持所选相对顺序，复用已有逐命令成功回复+新鲜playlist读取的计划执行器。
- 列表控件每行保存mpv条目ID，刷新后按ID恢复多选，重复路径不会合并为同一条目。提交前核对显示队列和当前会话的完整有序ID，再将选中ID映射成当前行；旧索引拒绝。按钮在事务繁忙时禁用；多选时单行上移/下移禁用，避免悄悄只移动其中一项。
- 播放按钮从最上方选中项开始，不创建替代播放列表；双击明确使用被双击行并收敛选择。移除仅修改播放器队列，不删除磁盘文件。和上游Play Next后清除选择不同，Windows保留仍存在条目的选择，方便后续操作；条目被移除则不选择另一个同名文件。
- 删除/移动仍非原子事务，部分成功后失败不回滚；既有执行器负责错误和取消。mpv外部脚本在最后读取与执行之间仍可能修改队列，没有CAS保证。本批未增加拖放、多选文件删除、所有上下文菜单或搜索。

仅静态编写、人工源码阅读、统计与Git核验，没有编译、测试或Windows运行。Extended选择、可访问性和长列表交互均待实际验收。

### 第三十一批：本地打开的窗口策略

- `PlayerCore.openURLs/activeOrNew/findIdlePlayerCore`、`Preference.alwaysOpenInNewWindow/groupSimultaneousOpensInPlaylist/allowDuplicatePlayers` → `App.OpenPolicy.cpp`、`MainWindow.OpenPolicy.cpp` 和实际偏好编辑。保留默认 true/false/false：选择文件和外部文件激活按偏好进入当前窗口、一个新窗口的队列或逐文件窗口；分组请求有意忽略重复媒体过滤，单文件重复则激活已打开窗口而不重播。追加、拖放、历史重开仍显式作用当前窗口；网络打开和替代菜单反转尚未接该策略。
- 本地路径只在来源窗口工作线程解析一次，再将拥有型UTF-8路径交给目标窗口，等待其设置与mpv启动，不递归解析/路由。单个文件夹展开始终作为一组；多目录同时打开仍先按现有解析器合并排序再分配，并非上游逐目录建立窗口的完整等价。原解析器去重/排序规则保持不变。
- 空白窗口复用只允许本实例从未提交过媒体的窗口，且无解析、待打开、选择器或插件任务；提交后立即保留不可再复用标记，防止START_FILE尚未来临时被另一次请求覆盖。这比上游回收idle核心更保守。关闭窗口不作为目标；16窗上限先按整个批次计算，超限整批拒绝，不静默只打开前16个。创建中途失败可能留下空窗口，但开始交付前不替换任何播放中的媒体；后续解码失败仍按每窗反馈，非跨窗原子事务。
- 去重只比较已解析的完整字符串与实际Loading/Loaded/Ended路径或待打开首项，不访问磁盘猜硬链接身份，不做有损大小写折叠。mpv尚未发START_FILE且待打开队列已提交的极短窗口内不承诺去重；同一批次内已有分配按路径去重。路径别名/重解析点、文件URL等不承诺识别为同一媒体。
- 当前操作使用发起窗口已加载的偏好；未增加跨窗口实时偏好广播。OS请求仍先在活动窗口按既有200ms合并，随后应用策略；请求处理期间主动切换前台不改写已经选定的来源窗口。

只静态编写、人工阅读、统计与Git发布核验，没有编译、测试或Windows运行。完整窗口打开策略和其他未覆盖功能仍需继续。

### 第三十二批：目录选择、替代打开与网络窗口路由

- `AppDelegate.openFile/openURL` 的 `AlternativeMenuItemTag`、`PlayerCore.activeOrNewForMenuAction` → “更多打开方式”的显式替代菜单和文件/目录/网络入口参数。替代操作对alwaysOpenInNewWindow做异或反转，不修改持久偏好；原文件按钮及快捷键继续走普通入口。Windows不模拟按住Option才显示菜单，改为可键盘访问、始终可发现的菜单项。追加不参与反转且留在当前队列。
- `NSOpenPanel.canChooseDirectories` → 初始化到当前HWND的 `FolderPicker`，选一个本地目录交由既有后台解析器遍历，单目录保持一组；可选择打开或追加。取消不提交目录、不取消已有播放；关闭后返回的选择丢弃。目录与文件分开选择是Windows原生选择器差异，没有复制一个同时选文件和多目录的macOS面板；蓝光文件夹特殊处理仍未实现。
- `OpenURLWindowController.openBtnAction` → 正常/替代网络对话框将验证后的地址交给App窗口路由，按上游activeOrNew语义不执行文件打开分支的跨窗重复过滤。对话框清楚展示将复用空白/新建或替换当前窗口，只有确认后才取消旧待打开输入并提交。URL直接成为已解析输入，不经过本地文件枚举器，也不创建无限路由递归。目标尚未初始化时仍等其自身设置和mpv准备。
- 主窗口关闭主动Hide网络对话框并在恢复后核对closing，取消不启动网络；源窗口持有协程寿命而不把关闭视作成功打开。仍沿已有协议白名单/WinRT URI解析，缺少上游用户名密码字段、Keychain、加载遮罩、停止加载和失败后重试页面；本批不读写凭据、不发真实网络请求，也不宣称完整URL窗口等价。

仅静态源码编写、人工阅读、数量统计和Git核验，未编译、测试、运行或打开网络媒体。

### 第三十三批：播放列表内部多选拖放重排

- `SidebarPlaylistPane.validateDrop/acceptDrop/writeRowsWith` → `MainWindow.PlaylistDrag.cpp`：按行中线选择上方/下方插入边界，将多选升序行传给既有 `DropPlaylistItems/PlanPlaylistDrop` 串行事务。保留同路径不同条目的mpv ID，不采用ListView自动排序，避免UI先变化而播放器失败；拖放被接受仅表示提交计划，实际结果继续由mpv观察和执行器错误反馈决定。
- 每次拖动产生独立GUID，只传不含文件路径的私有数据，源窗口保留有序ID、选择行和列表revision。源与目标必须为同一仍活动的拖动窗口；拖动期间重排、换队列、事务繁忙或关闭均拒绝，拖动完成/取消释放本地快照。选择由真实拖动条目逐项核对，不信任OS传入的索引。
- 目标依据已实现的行容器坐标和中线，滚动条不成为插入位置；未实现容器不猜测远处行。通过原生拖动caption提示插入边界，不新增虚假的播放器状态。跨窗口及跨应用拖动不支持；不导出文件路径。外部文件落到列表仍沿既有根窗口DropFiles追加行为，不承诺精确插入位置。自动滚动与DPI布局须后续Windows验收。
- 不新增原子性承诺：多个mpv移动可能部分完成后失败，不回滚。最终快照读取与执行之间仍存在外部脚本竞争窗口。拖动命中仅遍历已实现的可见容器，身份检查仍遍历ID；尚无性能证据。

本批仅静态编写、人工源码阅读、数量统计与Git核验，未编译、测试或运行Windows。

### 第三十四批：字幕颜色、描边、背景与非破坏样式编辑

- `SidebarSubtitlesPane.SubStyleView.colorAction/chooseSubBorderSizeAction/updateControls`、`PlayerCore.setSubTextColor/setSubTextBorderColor/setSubTextBorderSize/setSubTextBgColor/setSubFont` → 原生文本颜色草稿/描边宽度/字体斜体编辑、`SubtitleColor.ParseHex`和 `PlayerSession.ApplySubtitleStyle`字段子集。以mpv手册的`#RRGGBB/#AARRGGBB`规则解析，转为已有RGBA浮点序列化；透明度00透明、FF不透明。只接受完整6或8位十六进制输入，不猜测无效字符串或其他颜色命名。
- 修复旧界面只显示字体/字号/粗体却提交整个默认SubtitleStyle、隐式重置文字色/描边/背景/斜体的问题。字体按钮只改字体字号粗斜体；文字色、描边和背景分别明确应用，不改未选择字段。会话在发送任何命令前验证样式及字段白名单；多字段仍是独立异步命令，失败不宣称回滚或原子更新。
- mpv八项样式属性以STRING观察进入有界固定键快照，单独显示实际内核值；缺值显示未知，不把命令参数投影成已成功状态，不在time-pos通知覆盖编辑中的草稿。该类选项跨媒体持久，沿mpv实际观察更新。编辑初值明确是草稿而非当前属性；未实现上游原生字体/颜色选择器、自动读取到草稿和全部样式预设持久化。
- 保留图形字幕禁用和ASS内置排版警告。字体样式选项共享主副字幕，不伪造独立副字幕色；背景色的视觉效果也受mpv现有边框/阴影样式影响，本批没有自动更改ASS override或阴影尺寸。颜色属性接收不代表视觉一定变化。

仅静态编写、人工审阅、统计与Git核验，未编译、测试、运行或验证字幕画面。


### 第三十五批：插件私有文件下载

- `JavascriptAPIHttp.download` → `PluginBootstrap.http.download`、既有HTTP Promise/network worker、`PluginHttpClient` 和 `PluginFileStore.WriteDownload`。保留GET默认、可选method/params/headers/form data、HTTP非2xx拒绝和保存后resolve无返回值。允许GET/POST/PUT/PATCH/DELETE，不补虚构HEAD等方法；支持上限1 MiB的原始二进制，不把JPEG/ZIP错误地当UTF-8解析。文件下载不自动执行、解压或打开。
- 目标在网络请求前校验为单层`@data/文件`或`@tmp/文件`，拒绝缺失/空目标、其他目录、Windows设备名、非法UTF-8和路径绕过。继承网络声明域名与用户启动确认，不跟随重定向/代理/共享Cookie。本地存储实际可用性和磁盘额度在写入时再次检查；非法目标不发请求，但磁盘不足可能在服务器已完成请求后才失败。
- HTTP失败不触碰目标；错误内容是有效UTF-8时保留响应字典，否则text/data为空并保留HTTP状态。成功下载按普通文件/链接校验、128文件/16 MiB命名空间额度和临时文件flush+rename原子覆盖保存。不同于上游捕获写入错误后仍可能resolve，IIMA写入失败拒绝Promise，不报告成功。
- JS文件调用和网络下载可并发，实例mutex及既有跨进程目录锁串行存储事务；网络worker退出后才清理临时文件，cleanup标记closed拒绝迟到写入。写入分块检查取消，rename前是最后可撤销点；提交后取消或实例关闭可能丢弃完成通知，不回滚已保存文件。网络有原有30秒截止，但磁盘/锁及同步OS调用不能即时中断，不能保证总关闭时间30秒。
- 仍不支持任意用户路径、嵌套目录、超1 MiB大文件/进度回调、重定向、其他编码、二进制file.handle、XML-RPC或完整插件兼容。相同插件identifier共享私有数据，域名许可与QuickJS均不是操作系统安全沙箱。启动说明新增下载与私有覆盖披露。

仅静态编写、人工源码阅读、数量统计和Git核验，未编译、测试、运行、执行插件或发送HTTP请求。


### 第三十六批：插件 XML-RPC 异步调用

- `JavascriptAPIHttp.xmlrpc`、`JavascriptAPIXmlrpc.call`、`JustXMLRPC.call/toValueElement/value(fromValueElement:)` → `PluginBootstrap.xmlrpcClient`、`PluginXmlRpc`和既有HTTP worker。创建client不联网；call把方法与参数快照传到worker，按许可HTTPS地址POST UTF-8 text/xml，再在JS线程完成Promise。成功返回唯一value；标准fault按上游reject(undefined)；HTTP错误映射httpCode/reason/description，XML解析错误映射Bad response。取消/传输失败沿已有无敏感URL诊断的Error路径，不伪造HTTP状态码。
- 保留布尔0/1、int/i4、有限double、string和无类型value、array/data以及struct/member的递归映射。整数采用XML-RPC int32；其他有限JSON数字用double，不冒充无损64位整型。JSON没有稳定Date/Data表达，本批明确拒绝null、日期、base64、nil和扩展类型，不用字符串猜测日期或二进制。对象键和值来自普通无getter/无环数据快照；重复响应struct键拒绝而非上游后者覆盖前者。
- 参数JSON64 KiB、方法256字节ASCII XML-RPC名字、值递归16、DOM节点4096、XML元素深度64、响应1 MiB、结果8 MiB；包裹元素使XML深度与业务值深度不同。XML序列化前保守估算大小并在序列化后确认；输入预限字节和深度，DOM解析明确ProhibitDtd、禁止ResolveExternals和验证。额外拒绝命名空间、属性、非标准结构、额外参数和非有限数字，不解析或引用外部文件。
- 标准fault严格检查faultCode(int32)/faultString，较IINA仅看到fault节点就失败更严格。HTTP权限/允许域名/启动披露/四个pending预算/网络取消和超时均沿已有实现，无新连接、登录或凭据持久化。启动说明补充XML-RPC服务器操作；代码仅建立能力，本次没有实际调用服务。
- 未覆盖Date/base64、完整Foundation桥接、任意XML扩展、重定向/代理/自动认证和所有IINA插件API；上述严格子集是明确兼容性差异，不宣称XML-RPC或插件全兼容。

本批只静态编写、人工源码阅读、统计和Git核验，未编译、测试、运行、执行插件或发起网络调用。

### 第三十七批：有界诊断日志查看、过滤与导出

- `Logger.Log/buffer`、`LogWindowController.flushBuffer/updatePredicate/menuCopy/save` → `DiagnosticLog.Snapshot` 与 `MainWindow.Diagnostics.cpp`。沿原记录快照、分类精确匹配、消息不区分大小写搜索、多选按显示顺序复制及全部/筛选导出规则；WinUI Expander 替代独立日志窗口。既有诊断只采集应用生命周期和去路径的播放状态，没有伪造未采集的严重级别、原始mpv文本或插件控制台。
- 每窗口内存保留最近1024条，稳定序号、调用时UTC Unix毫秒、每字段512字节；UTF-8按完整scalar截断，无效字节替换。记录与磁盘排队分离：256项磁盘队列满仍保留内存记录，分别显示历史淘汰和磁盘排队丢弃数量。快照不是完整磁盘历史，也不证明每条已flush；磁盘写入错误沿既有反馈，后续写入因关闭拒绝。
- 展开立即取快照，展开期间最多每500ms刷新；折叠和关闭停定时器。实时刷新可冻结，搜索过滤基于当前保留快照；普通追加保持已有行对象和选择，过滤重建按序号恢复选择。跟随最新是明确复选框，不复制上游自动判断“滚到底部”策略；Windows忽略大小写使用ordinal比较，不声称与Cocoa所有区域规则一致。时间不转换成本地显示格式。
- 复制只在明确按钮操作时写系统剪贴板；保存由原生FileSavePicker选目标，后台UTF-8同目录临时写/flush/原子替换。保存的是点击时快照，不是选择器期间新增记录；“全部”仅指当时内存保留项。取消选择不写磁盘，窗口关闭前未选目标则丢弃，已经开始的文件保存允许完成而不撤销。无自动上传/分享、旧磁盘日志读取或新增媒体路径采集。
- 上游日志级别、多个分类勾选、完整全局日志历史、独立窗口以及复制快捷键尚未移植。内存快照复制和原生控件虚拟化仍须实际性能验证，不能把有界缓存称为无成本。仅静态编写和人工源码审阅，没有编译、测试或运行。

### 第三十八批：字幕原生颜色选择器

- `SidebarSubtitlesPane.SubStyleView.updateTextStyle/colorAction`、`Extensions.NSColor.mpvColorString/init(mpvColorString:)` → `MainWindow.SubtitleColorPicker.cpp`、`SubtitleColor.ParseMpv/HexString`。文字、描边和背景分别进入 WinUI ColorPicker，支持透明度与8位sRGB通道。初值优先取mpv观察值，缺失或无效时用编辑草稿，再用明确提示的默认值；不把默认值冒充实际状态。
- mpv颜色读取支持严格的#RRGGBB/#AARRGGBB及3/4个单位区间有限浮点通道，RGB缺alpha为1。上游宽松扫描非法hex，本实现拒绝；默认背景透明。选色转换到8位有量化差异，输出草稿保留alpha。未尝试复刻NSColor色彩空间或系统全局颜色面板。
- 与上游颜色井立即提交不同，Windows模态选择器明确应用/取消；仅确认时发送所选颜色字段，不通过会联动描边宽度的旧编辑按钮。成功提交只更新草稿，不伪造内核成功，错误继续走异步会话反馈。主副字幕共享样式，位图字幕可用性和ASS覆盖提示沿现有规则。
- 打开时捕获媒体代次、条目、路径和双字幕ID，在按钮和await返回后两次校验；换片/字幕选择变化拒绝过期提交。关闭窗口Hide对话框，取消不改mpv或草稿，选择器结束释放互斥并继续待处理打开请求。尚未实现字体枚举选择器、全局样式持久化或完整系统颜色管理。

仅静态编写、人工源码审阅、数量统计与Git核验，未编译、测试或运行。

### 第三十九批：字幕字体家族选择器

- `PlayerCore.chooseSubFont/setSubFont`、`Utility.quickFontPickerWindow`、`FontPickerWindowController.windowDidLoad/controlTextDidChange/okBtnPressed/cancelBtnPressed` → `FontCatalog` 和 `MainWindow.SubtitleFontPicker.cpp`。后台 DirectWrite 枚举本机已安装字体家族，优先英文家族名作为 mpv 输入，用户区域名作为显示，按 ordinal 忽略大小写排序与搜索。隐藏点前缀/空名、非法 UTF-16 和超长名称排除；最多8192家族，超限整体失败而非隐藏截断。逐项坏数据跳过；不触发云字体下载或强制字体集合刷新。
- 界面提供家族搜索、手动名称、预览和应用/取消。初值来自当前 `sub-font` 观察值，未知时回退草稿并明确提示；枚举失败仍允许手动输入。空名称沿 IINA 使用 sans-serif；仅提交 `sub-font`，不改变字号、粗斜体或其他样式。草稿不冒充命令成功，最终属性以 mpv 观察为准。只允许枚举中的家族名进入 XAML FontFamily，任意手动文本不作为字体文件/URI源加载。
- 原生预览使用Windows字体回退，与 libass 实际解析/字体替代不保证一致。当前选择的是family而非上游PostScript/typeface成员，不提供字体面列表、变体轴或完整NSFont行为；手动输入可交由mpv识别，但本批不保证名称存在。搜索使用Windows ordinal规则，不是Cocoa区域折叠。字体资源在窗口间不缓存，枚举的同步原生调用只能在边界检查取消，不能保证立即终止。
- 枚举期间与对话框打开后均核对媒体/双字幕代次；换片或切换字幕拒绝过期提交，关闭取消枚举并Hide对话框。整个操作持有窗口到回调结束，恢复后只在UI线程访问控件；取消不写字幕草稿或mpv，结束后恢复待打开文件流程。位图字幕不可编辑、ASS覆盖及主副共享提示沿现有规则。

仅静态编写、人工阅读、数量统计和Git核验；未编译、测试、运行或确认字体渲染效果。

### 第四十批：外置音频文件加载

- `SidebarAudioPane.loadExternalAudioAction`、`MainMenuActions.menuLoadExternalAudio`、`PlayerCore.loadExternalAudioFile` → 音轨区“载入外置音频”原生FileOpenPicker与 `PlayerSession.LoadExternalAudio`。一个选择文件提交 `audio-add path select`，显式select等价于上游省略flags的默认值；用argv传递，不将路径拼入命令文本。允许视频容器中的音频流，不以扩展名声称解码可用；Windows选择器允许所有文件，由mpv异步报告不支持的输入。
- 当前Loaded媒体才允许选择，选完核对导航代次、条目和路径；换片、停止、关闭均不对新媒体提交旧文件。会话额外拒绝未完成的手动换片和播放列表编辑事务。取消不改变队列、轨道、暂停或音量；关闭后选择器完成结果丢弃。没有强制关闭系统FileOpenPicker的承诺，窗口协程等待其返回。
- 新轨道/所选音轨从既有track-list事件和音轨选择器显示；CommandReply错误走已有会话反馈，不添加虚构成功项。保留上游重复audio-add可产生重复外置轨道的行为，不擅自改成cached/reload。命令排队成功只代表提交；mpv自动EOF换片或外部脚本与最终命令之间仍可能竞争，不能宣称原子媒体绑定。
- 不修改原始音频文件，不复制到配置目录、不持久化绝对路径作为偏好，也不创建新播放列表条目。没有上游选择器“当前媒体目录”定位、外置视频添加、外部轨道删除/重载和专门错误类型翻译；底层解码格式取决于实际libmpv产物。

只静态编写、人工阅读与统计、Git核验。未编译、测试、运行或实际加载文件。

### 第四十一批：截图成功后的内存预览

- IINA `PlayerCore.screenshotCallback`、`ScreenshotOSDView.setImage` 和 `Preference.screenshotShowPreview` → `FinishScreenshot`、`MakeScreenshotPreview` 与 `MainWindow.ScreenshotPreview.cpp`。继承默认开启、成功截图后解码、300×200内适配和5秒显示。保存文件先原子发布；解码/显示失败不撤销已保存的截图，剪贴板保持原始分辨率。
- WIC解码和缩小位于后台，UI只复制最多240KB的预乘BGRA至WriteableBitmap。与原版NSImage缩放不同，缩图使用最近邻且不放大小图；不宣称色彩管理、HDR、透明度或视觉一致性。临时PNG由既有staging生命周期释放，图像不依赖已删除文件。
- 新预览替换旧预览并重新计时，关闭窗口或关闭预览/OSD偏好立即清空图像和计时器。偏好在开始与结果到达时均检查。截图是请求当时的历史结果，不在换片时把其改标为新媒体；本批不添加拖动、删除、编辑或定位按钮，也不覆盖其他截图格式。
- 保存和复制成功的文案不因预览失败改报为截图失败。所有格式支持仍受配套mpv与WIC约束，未执行编译、测试、播放或真实截图。

### 第四十二批：截图预览文件操作

- `ScreenshotOSDView.revealBtnAction/editBtnAction` → 预览下的“定位文件”“打开图片”。仅保存成功并有目标路径时显示这两个按钮；剪贴板截图只显示内存预览，不暴露即将清理的staging路径。沿IINA实际实现使用默认图片应用，而不是宣称存在集成图片编辑器。
- 点击复制当前目标和预览代次，禁用重复提交，异步StorageFile查找后再次核对窗口和代次。定位使用现有Shell PIDL接口，打开使用Windows Launcher，不拼命令。缺失文件/无关联/拒绝启动报告错误，成功关闭对应预览；旧操作完成不得隐藏新截图。已经提交给系统的启动不能撤回，不承诺阻止路径被其他进程替换的竞态。
- 用户悬停或键盘聚焦时暂停5秒隐藏，离开后重启；显式关闭清除图像和路径。与IINA差异：不提供不可恢复删除，不提供拖出，不处理全部外部应用生命周期；Shell定位调用仍可能阻塞UI。文件名/目标由用户原生保存操作决定，不用于自动打开或上传。
- 仅静态编写、人工阅读、数量统计与Git核验；未编译测试运行，未调用Windows Shell或打开图片。

### 第四十三批：字幕编码选择和重载

- `CharEncoding.list`、`MainMenuActions.menuSetSubEncoding`、`PlayerCore.setSubEncoding/reloadAllSubs` → 完整44项编码目录、WinUI候选列表、显式应用与`PlayerSession.SetSubtitleEncoding`。保留auto默认候选和上游iconv名称；不是Windows系统代码页映射。`sub-codepage`实际字符串观察与用户候选分开，不用提交值伪造成功。
- 等待set命令成功回复后才重载请求时存在且ID/外部路径仍匹配的外部字幕。失败不重载、错误走既有会话反馈；嵌入轨不尝试重载，与IINA遍历全部轨道不同，避免必然失败。mpv重载保留ID，不乐观修改所选轨。一次只允许一个编码事务，手动打开/切换、Stop、START_FILE/END_FILE、失败和关闭撤销后续重载权。
- 编码选项本身作用会话，已排队的set不会因取消回滚；底层自动换片或脚本与重载命令仍可能竞争。本批不宣称原子事务。当前媒体菜单不提供自定义`+encoding`强制转码输入、不改写已有高级设置`defaultEncoding`或添加每文件记忆；自动检测依赖实际uchardet构建。mpv先认可UTF-8，具体非UTF-8编码作为回退，界面不能保证纠正所有乱码；位图/内嵌字幕不适用。
- 仅静态编写、人工源码阅读、数量统计与Git核验。未编译、测试、运行或加载真实字幕。

### 第四十四批：上下翻转与左右镜像

- `MainMenuActions.menuToggleFlip/menuToggleMirror`、`PlayerCore.setFlip/setMirror/reloadSavedIINAfilters`、`Constants.FilterName` → 专用翻转和镜像按钮、`FindOrientationFilter` 与会话动作。沿用`iina_flip:vflip`和`iina_mirror:hflip`标签，不把自定义未标签滤镜误认作本开关管理对象。
- 显示由vf实际节点观察驱动：未添加、停用、启用、未知或冲突。启用时移除对应行；停用时启用；不存在则添加带标签滤镜。移除/启用复用worker新读快照比对，保留其他节点及未知字段。重复点击不乐观翻转状态，迟到重复编辑可因快照不匹配被拒绝。标签被其他滤镜占用或有重复标签时不覆盖；无法解析的链不猜测。
- 与IINA差异：区分被禁用的已存在节点，未知状态禁用按钮，保守处理标签冲突。添加仍使用mpv标签替换语义，外部脚本可能在观察和最终add之间竞争；不声称CAS或完整事务。其他未标签的hflip/vflip可以叠加抵消，不把按钮状态当最终像素方向证明。没有新增持久偏好、HDR或硬件路径保证。
- 仅静态编写与人工审阅、数量统计和Git核验，未编译、测试或运行。

### 第四十五批：比例裁剪草稿与安全重置

- `MainMenuActions.menuChangeCrop` → `PlayerCore.setCrop(fromString:)` → `Extensions.NSSize.crop(withAspect:)` 的保留最大内接矩形、正尺寸向零截断规则转写为 `CenteredAspectCrop`，接入源像素裁剪编辑器的比例输入、草稿生成、显式应用完整链。比例不是显示拉伸，不折算像素宽高比或旋转；极端比例不足一像素拒绝。沿现有Aspect解析语法，不把非法比例当重置。
- Windows差异：输入比例后先生成可编辑数值草稿，再点击应用；不在生成时提交或冒充实际状态。奇数余量的左上偏移采用整数除法，上游让mpv默认居中，色度对齐后的真实像素仍可能不同。现有vf观察/异步错误是最终依据。此入口不添加视频上拖动框或完整交互裁剪模式。
- 草稿增加导航代次，重复打开同一路径也不能沿用旧编辑权。已有尺寸/条目/路径检查保留。应用仅接受无其他空间滤镜或唯一正确类型的iina_crop，避免假设未知变换。重置扫描观察链，拒绝不可解析、重复标签或其他滤镜占用标签；用新鲜快照编辑删除目标，保留其他滤镜，不再盲目按标签删除。
- worker取快照与最终set之间的脚本竞态仍存在，没有CAS保证。仅静态编写、人工阅读、数量统计和Git核验；未编译、测试、运行或确认实际裁剪像素。

### 第四十六批：视频轨选择与三类轨道循环

- `MainMenuActions.menuChangeTrack/menuCycleTrack`、`MPVController.handlePropertyChange(vid/aid)`、`PlayerCore.vidChanged/aidChanged` → 视频轨下拉、视频/音频/主字幕循环按钮及 `PlayerSession.CycleTrack`。视频列表沿既有track-list元数据和标题投影，包含关闭项。循环直接传mpv的video/audio/sub别名，候选顺序、关闭槽与有效性由内核决定，不自行递增ID、不误改副字幕。
- 新增vid/aid整数观察和播放重启主动读取，START_FILE/END_FILE清空；沿no=-2、auto=-1与未知null的区别。下拉选择来自实际vid/aid，而不是命令目标或可能迟到的track-list.selected；未知/auto/暂未出现在列表的ID保持未选中，不冒充关闭。mpv可能回退到其他轨，观察结果才是事实。
- 选择和循环限定已加载、Ready且未手动换片或播放列表编辑事务；未知类型拒绝，显式选择ID须存在于同类轨道。用户仍能通过视频关闭项进入音频播放，重新选择视频沿现有VO重配/交换链生命周期。当前不提供视频外部文件添加、不加入音视频auto菜单项，也未新增轨道OSD或复刻IINA隐藏音量控件规则。
- 切轨与外部脚本/自动EOF的最终命令竞态仍存在，不宣称原子媒体绑定。视频多轨切换、关闭再开、音乐布局与合成资源重配需要后续运行证据；仅静态源码阅读和编写，未编译测试运行。

### 第四十七批：历史多选打开、批量移除与选择恢复

- `HistoryWindowController.selectedEntries/selectedPlayableEntries/playAction/deleteAction/doubleAction` → WinUI历史列表Extended选择、按可见顺序解析身份、批量打开和移除。复用 `SelectHistoryRows/PlayableHistoryPaths/RemoveHistoryEntries`，去重且排除分组标题，不按Ctrl点击顺序构建播放队列。双击仅打开实际点击条目，不误开先前多选的另一行。
- 搜索、分组与历史刷新按完整身份恢复仍存在的选择，不按旧行号恢复；同路径更新后的播放样本是新身份，保守取消旧选择。重建期间阻止SelectionChanged读取混合的新旧投影。打开/移除按钮无有效选择时禁用。
- 移除只改历史记录，不删媒体文件；延续单批内存撤销和持久化差分保存。与IINA删除前确认不同，本项目沿已有立即移除/撤销设计，本批不引入回收站操作。打开复用现有异步输入解析、容量限制、取消代次与错误反馈；缺失文件最终由解析器识别，不为列表投影阻塞UI做逐文件IO。多窗口打开、右键菜单、组展开折叠和完整快捷键未包含。
- 仅静态编写、人工阅读及数量/Git核验；未编译、测试或运行，不保证跨窗口合并与实际队列行为已通过验收。

### 第四十八批：临时软件解码与恢复偏好

- `SidebarVideoPane.HwdecSwitch` → `PlayerCore.toggleHardwareDecoding` → `MPVController`的hwdec观察/`PlaybackInfo.hwdecEnabled`，转写为临时软件解码和恢复已保存偏好两个明确按钮。沿上游恢复hardwareDecoder偏好值而非无条件auto；偏好为no时恢复仍为软件，不偷偷改变用户设置。
- 新增hwdec请求模式观察，和已存在hwdec-current实际输出分开显示；auto/auto-copy并不证明当前硬件在用，软件回退明确显示。未知请求不当默认no，媒体未加载时实际状态显示不可用；保存偏好单独标注。当前按钮只改会话，不持久化、不擅改hwdec-codecs、不引入Mac专属VideoToolbox workaround。
- 会话只接受no/auto/auto-copy并拒绝未就绪、未加载、手动换片和队列编辑期间的命令；错误沿既有异步反馈，不乐观修改观察。外部脚本设置的其他合法模式可显示但不作为本按钮任意输入。后续高级偏好修改仍按既有立即应用规则生效。
- Windows解码器可用性、软硬切换时帧丢失/位置/暂停状态、D3D11重配和HDR正确性尚待运行证据。没有执行编译、测试或运行；不将按钮或属性字符串当硬件验证。

### 第四十九批：章节前后导航和已有操作的快捷键闭环

- `MainMenuActions.menuNextChapter/menuPreviousChapter`直接提交`add chapter ±1`的规则接入前后章节按钮与导入快捷键；不按UI索引自己减一，因为mpv的chapter-seek-threshold允许“上一章”先回到当前章开头。与`PlayerCore.playChapter`明确选章并resume不同，步进不主动改变暂停状态。末章/首章行为由内核处理，不冒充固定环绕行为。
- 已加载、可seek、有章节且非队列编辑时启用导航；会话另拒绝手动换片。显式选章也补齐这些保护，观察索引越界/未知不截断为有效行。最终章属性和时间事件投影实际结果，不乐观改选中项；异步失败沿已有错误链反馈。
- 将第46批循环视频/音频/主字幕接入严格白名单的`cycle video|vid|audio|aid|sub|sid`快捷键，并将第44批flip/mirror实际处理器注册到已有IINA应用命令路由。文本输入、原生控件和模态界面仍沿原先键盘门控；未实现任意mpv命令透传或修改默认绑定。
- 外部脚本/自动换片仍可能与最终命令竞争，章节边界、暂停/播放与原生控件键盘行为尚未运行验证。仅静态编写、源码阅读、数量和Git核验，未编译测试运行。

### 第五十批：历史与播放列表批量定位文件

- `HistoryWindowController.selectedFiles/showInFinderAction`与`SidebarPlaylistPane.contextMenuShowInFinder` → 两列表的所选文件定位入口、`MainWindow.RevealSelection`和Shell PIDL批量操作。点击时复制选择路径；播放列表沿有序ID快照检查，历史沿已显示条目。网络媒体URL跳过，不执行URL或拼接shell命令。
- Windows同目录文件合并为一次`SHOpenFolderAndSelectItems`请求，最多64文件、16目录，超目录上限在任何打开请求之前拒绝。路径词法规范化并按精确路径去重，不把大小写/符号链接猜成相同身份。绝对路径、NUL、空文件名检查；PIDL与COM初始化按RAII/配对释放，解析/目录打开失败计数。UNC文件路径仍属于Windows文件路径，可能触发系统网络访问，不声称是离线操作。
- 原生Shell解析和提交在后台，UI只取点击时快照与显示提交/失败/跳过数量；同窗口一次一个任务。关闭请求取消后续步骤，不能中断正在进行的Shell调用或撤回已交给Explorer的请求。成功只表示Shell接受请求，不证明窗口可见或文件未被其他进程替换。保留列表选择而非照搬IINA定位后取消选择；不删除媒体或改历史。
- 仅静态编写与人工源码阅读、统计、Git核验；未编译测试运行，Windows Shell/COM/UNC及资源管理器多选行为仍待验证。

### 第五十一批：双字幕时间对齐与快捷键

- IINA自带`config/input.conf`的Ctrl+Shift+LEFT/RIGHT → `PlayerWindowController.handleKeyBinding` → `sub-step ±1`实际调用链转为主/副字幕时间对齐按钮及白名单快捷键分发。沿固定mpv的primary/secondary flags，时间对齐只改变字幕延迟，区别于原先上一句/下一句的`sub-seek`视频跳转。
- 主副选择ID必须实际观察且大于0，不猜测auto或未知轨；手动换片、未加载、队列事务拒绝。sub-seek增加可seek门控并支持副字幕参数。快捷键只接受-1/0/1及明确slot，不泛化为任意原始命令透传；默认按键不改变，导入的IINA绑定可用。
- 显示观察得到的主副延迟，缺失显示未知；不计算虚构时间或改写全局默认延迟，不主动resume。内嵌字幕的可用事件受mpv预读范围限制，命令接受不代表一定找到目标句；错误继续走异步反馈，真实字幕事件、ASS/位图限制与重复操作需后续实机确认。
- 仅静态编写、源码追踪、数量与Git核验，未编译测试运行。没有额外字幕解析器、自动音频对齐或永久修改字幕文件。

### 第五十二批：字幕非线性缩放步进

- `MainMenuActions.menuChangeSubScale` → `PlayerCore.setSubScale`逐式转写为`StepSubtitleScale`，接入字幕缩小/放大/原始比例按钮。保留大于等于1直接显示、小于1采用负倒数、两位舍入、±0.1步进和跨越(-1,1)间隙规则；不是简单线性加减实际缩放。
- 输入来自mpv观察的sub-scale，重置明确为1。零/未知/非有限值不做除法，超当前mpv范围拒绝并反馈；观察值与独立数值草稿分别显示，不在命令提交时伪造成功。按钮只调整主字幕比例，不联动副字幕、位置、字体大小或保存偏好。
- 不主动刷新编辑中的数值草稿，后续明确应用草稿仍可覆盖快调结果。快速重复点击在属性反馈前可能基于同一观察值提交相同目标，不声称累计步数已实现或同步读取语义完全等价。未知/零时增减禁用，重置可恢复；加载/换片/关闭和队列事务门控沿会话规则。
- 仅静态编写、源码阅读、数量和Git核验，未编译测试运行。实际ASS/位图缩放与字幕渲染效果仍待确认。

### 第五十三批：修正快捷键前置分类遗漏

静态追踪`ClassifyBindingAction` → `MainWindow.DispatchKeyBinding`时发现，第49/51批新增章节/轨道循环/sub-step/sub-seek的分发处理之前仍有旧白名单，导致导入快捷键被提前拒绝；对应按钮路径不受影响。本批补齐前置分类，轨道属性只允许cycle、chapter只允许add，具体数量/slot仍由后置严格校验，不开放任意命令。第49/51批快捷键“已接入”应理解为当时不完整的静态实现，至本修复才贯通源码调用链。未执行编译、测试或运行，不能据此声称实际键盘行为验证通过。

### 第五十四批：命名快捷键配置库

- `SettingsPageKeyBindings.ConfigEditor.newConfFileAction/duplicateConfFileAction/importConfigAction/deleteConfFileAction/loadConfigFile`的命名配置操作链 → `BindingLibrary`、`MainWindow.BindingLibrary.cpp`及现有settings.json持久化。支持编辑区另存、明确覆盖、选择载入、重命名、移除和单步撤销覆盖/移除；空编辑区可成为空配置，默认绑定可复制但不改内建默认。导入/导出仍复用原生选择器，不恢复上游任意目录文件管理。
- Windows差异：库保存的是完整input.conf文本快照，当前生效inputConf独立。选择下拉不换配置；“载入所选并替换编辑区”明确替换草稿后仍须“保存并应用”才生效，避免浏览库时改变按键。保存快照也不隐式应用。删除库快照不删除当前生效副本或外部文件，不强制回退默认；撤销仅本窗口内存一批，已有后继内容变化时不覆盖。重命名不纳入撤销。
- Core限制32配置、单份256KiB、全库1MiB；名称1..128 UTF-8字节，拒绝控制字符、分隔符和边界空格，ASCII大小写冲突拒绝，不谎称完整Unicode文件系统等价。保存/覆盖/重命名/恢复时复用input.conf解析；语法错误拒绝，语法正确但未实现的动作保留文本，由运行分发明确拒绝。JSON读取保留文本并验证结构界限，载入不会执行任何配置动作。
- UserDataStore读取/保存可选bindingLibrary字段，旧设置自然为空库，沿既有原子保存和失败保留；未增加版本号。多窗口按命名条目差分合并，不覆盖其他窗口独立增加的配置；同名创建、修改、删除冲突拒绝整次保存并走已有未保存提醒，不 silently last-wins。当前生效inputConf仍沿原有字段级最后提交策略，库不实时广播到别窗，不能宣称跨窗活动配置同步。
- UI成功文案仅指内存修改及正在保存，磁盘完成由既有持久化链决定。不是完整IINA行级可视化编辑、多个内建配置集、目录监视或外部文件编辑同步；现有命名目录catalog模型未被伪装成已接文件系统。仅静态源码追踪和编写、数量/Git核验，未编译测试运行。

### 第五十五批：快捷键逐行编辑与冲突投影

- `SettingsPageKeyBindings.ConfigEditor.addKeyMappingAction/editKeyMappingAction/removeKeyMappingAction`、`KeyMapping.readableAction`及既有`BindingConfiguration.Search/Conflicts/Move`接入WinUI行编辑器：从文本读取、搜索按键/动作/注释、选择回填、追加、替换、移除、上下移动、重复键及最后优先行显示。筛选行保留原模型索引，移动按完整列表而非筛选后位置；IINA动作沿`@iina`前缀解析。
- 行模型、文本编辑区、当前生效键表是三个明确阶段。行修改不执行命令、不改文本或磁盘；“生成规范化文本并替换文本区”后才可沿既有保存应用/导出/命名库另存链交付。生成检查当前文本仍等于读取源，导入、默认恢复或直接改文后拒绝旧模型覆盖。重新读取是显式丢弃行草稿，按钮含义已标注。
- 复用单行验证、10000条上限及256KiB文本界限，候选副本通过验证后才替换模型；空键、换行、无法往返的#动作等拒绝。重复默认section键保留last-wins而非偷偷去重；其他section和未支持动作可编辑但不保证运行分发支持。搜索沿模型ASCII折叠，不宣称Unicode区域搜索。字段须点击追加/替换才进入行草稿。
- 规范化序列化保留每条绑定的行内注释，但不保留独立注释与原空白；界面在生成前明确提示先导出原文。内建默认本体仍只读，行编辑操作的是文本副本；不冒充外部配置目录实时编辑或键盘录制器。当前只单行操作，无行级撤销/多选拖动，重新读取可恢复尚未生成的文本版本。
- 仅静态编写和人工源码追踪、数量/Git核验，未编译、测试或运行。键盘事件、XAML控件及大列表性能仍需后续验证。

### 第五十六批：焦点内快捷键录入

- `KeyRecordView.keyDown` → `KeyRecordViewController.recordedKeyDownWith` → `KeyCodeHelper.mpvKeyCode`转为显式启动的WinUI只读录入框，使用与实际播放器分发相同的`WindowsMpvKey`转换。当前键盘布局、Shift可打印字符、小键盘Enter与修饰键语义一致，不另造与分发不匹配的键名。
- 一个有效组合仅填入行编辑器按键字段，动作/模型/文本/生效配置均不自动提交；用户仍须追加或替换行、生成文本、保存应用。没有全局键盘hook、后台记录、持久保存按键流水或向插件发布所录键。纯修饰键、IME、死键、AltGr和Windows组合不录入；系统窗口管理快捷键不主动吞掉。
- Esc取消并保留原字段，Tab取消并继续原生焦点导航；可手填Esc/Tab但现有播放器保留Tab导航。失焦、关闭取消捕获。成功后焦点留在只读框，避免同一键的重复/释放触发播放器动作；按住重复不产生多次录入。窗口外和系统先拦截的组合不能保证可获，手工字段仍保留。
- 不是IINA完整规则树动作编辑器或所有布局的兼容证明。仅静态编写、源码阅读、数量和Git核验，未编译测试运行；真实焦点顺序、系统组合、IME和辅助功能读屏仍待Windows验证。

### 第五十七批：播放器属性驱动的OSD扩展

- 沿`MPVController.handlePropertyChange` → `PlayerCore.aidChanged/vidChanged/sidChanged/secondarySidChanged/subScaleChanged/subDelayChanged/subPosChanged`与`OSDMessage`，新增`PlaybackOSD`轻量观察投影。接入主副轨选择、主副字幕显隐/延迟/位置、主字幕缩放、音频延迟、五项画面均衡、反交错/解码请求和循环模式通知，保留原有暂停/音量/速度/静音链。
- 只根据收到的播放事实生成消息，不在命令提交或按钮点击时伪造结果。首个Loaded快照、媒体条目变化建立基线；optional未知→已知不作为变化提示，离开Loaded/Ready清空。仅保存标量/短模式字符串，不复制播放列表或路径。字幕缩放显示沿IINA正值/负倒数的两位舍入语义，未知/零不倒数；双字幕轨通知明确区分槽。
- 反交错保留auto原文，硬件消息明确标为“请求”并附当时实际解码器观察，不将auto误称硬件已生效；轨ID先到而track-list未到时显示ID，不制造标题。音频延迟旧状态仍来自现有非optional字段，因此首次Loaded之后的初始延迟反馈也可能触发提示，不承诺所有初始化通知都被抑制。
- 单个快照多项变化按固定优先级显示最后一个符合偏好的消息，不排队播放过期通知；暂停/音量等常用操作优先。禁用某类OSD后可显示同快照另一合格变化，不漏更新基线；总开关关闭、CompactOverlay或退出播放清除已显示内容并停计时。沿既有自动隐藏时长和Core展示模型，未新增完整动画、持久OSD、错误优先队列或全量中文资源。
- 尚未覆盖seek测量/seek OSD、截图及全部插件/网络通知等所有上游OSD入口。本批仅静态编写、源码追踪、数量与Git核验，未编译测试运行；并发属性顺序和真实UI展示仍需后续验证。

### 第五十八批：IINA滤镜预设参数编辑器

- `FilterWindowController.NewFilterSheetViewController.showSettings/quickInput/sheetAddBtnAction`与`FilterPresets` → 动态WinUI参数面板，接入已有Core预设实际转换器。视频11项：crop、expand、sharpen、blur、delogo、negative、vflip、hflip、lut3d及两种custom；音频2项custom。按paramOrder显示，所有未编辑字段采用真实默认值。
- 文本/整数/浮点/选择四类输入对应固定schema；数字有限性、范围、整数及步进先检查，再经`FilterPresetInstance.Set/CreateFilter`。锐化/模糊保留3..13奇数矩阵及原amount范围/符号，反色等无参数预设可直接生成。自定义名称为空拒绝；参数16KiB与最终表达式64KiB界限，NUL/结构错误不覆盖手工表达式。
- Windows采用明确“生成并替换下方表达式”→既有添加/替换→vf/af观察/异步错误链，生成不是成功应用；可在提交前人工检查或保存预设。链切换会清除仍等于上次生成结果的旧表达式，避免视频结果误用于音频；独立手工修改保留。切换预设/链或重置明确丢弃参数草稿，不自动改播放。
- LUT文件字段仍遵循上游的FFmpeg表达式参数，不是原生路径选择器或自动Windows路径转义器；驱动器冒号、反斜杠及特殊字符需要FFmpeg多层转义，界面已标注，不能宣称直接输入任意Windows绝对路径可用。custom_ffmpeg同样是高级图语法，未对实际滤镜/文件可达性作探测。滤镜可用性、硬件下载/上传、画质和性能取决于实际libmpv/FFmpeg。
- 仅静态编写与人工源码追踪、数量/Git核验，未编译测试运行。不是上游所有FFmpeg滤镜的完整UI、LUT浏览器或已验证的渲染效果。

## 第五十九批：LUT 文件选择与 Windows 字面路径

- 追踪 IINA `FilterWindowController.quickInput/sheetAddBtnAction`、`FilterPresets.vfPresets` 的 `lut3d(file,interp)`，保留 nearest 默认及三种插值。上游文本参数直接进入 `MPVFilter(lavfiName:paramDict:)`；Windows 盘符冒号和反斜杠需要平台适配，本批不再要求用户输入 FFmpeg 转义文本。
- `MainWindow.FilterPresets` 增加绑定窗口的原生单文件选择，只填写参数草稿，不自动生成/添加。取消保留旧值；重建参数页的代次、关闭和路径编辑冲突阻止迟到覆盖；复用全窗口 picker 门禁及延迟文件激活。文件扩展名不当作解码证据。
- `FilterPresetInstance::CreateFilter` 针对 LUT 的字面路径先做 FFmpeg 选项层、再做图层转义，再用 mpv `graph=%UTF8字节数%内容` 包装。依据固定 `mpv/options/m_option.c::read_subparam/split_subconf` 的长度消费及 `filters/f_lavfi.c::precreate_graph` 交给 `avfilter_graph_parse2` 的边界。不走 shell，不用遇到文件名方括号会冲突的外层括号引用。空路径/NUL/超过16KiB拒绝；其他预设和手写表达式不改变语义。
- 第58批“LUT需手工转义”限制由本批静态代码替代：现在 LUT 参数输入为原始路径，已转义文本不兼容该字段，应在手写表达式中使用。运行时 FFmpeg 路径解析、文件读取和 LUT 像素结果均未验证；不声称任意路径已通过测试。
- 未编译、测试或运行。仅源码追踪和静态审阅，未扩展为完整滤镜参数反解/编辑器。

## 第六十批：保存滤镜的整理与单步撤销

- 追踪 IINA `FilterWindowController.saveEditedFilterAction/deleteSavedFilterAction/syncSavedFilter`：修改保存定义、更新表格/菜单与持久化，不改变已加载的 vf/af。IIMA 原有保存/覆盖/移除链本批统一进入编辑提交边界，新增复制、上下移动、单步撤销。
- 复制使用选中保存定义的表达式及名称字段中的新名称，不复制快捷键；复用 `SavedFilterLibrary.Copy/Move` 的重复名、位置、总量和表达式校验。失败或边界移动不消耗上一次撤销。移动后选中同一预设的新位置；编辑后重新投影字段，删除清除选择。
- 撤销保存编辑前库快照及预期 revision；新编辑替换单步快照，重新载入使其失效。恢复前重新核对 input.conf 键冲突，并在候选副本中完整校验后替换；不恢复已播放滤镜、不反转播放器命令、不提供重做。撤销是新增的 Windows 可恢复编辑行为，上游没有此完整快照操作链。
- 观察状态现在区分未知、缺席、停用、启用，避免未知/停用都显示成不存在。持久化沿既有 UserData 路径，不新增跨窗口实时同步或跨窗口撤销保证；并发保存策略仍是现有设置合并策略，不声称解决多窗口同时编辑同一滤镜库的冲突。
- 未编译、测试、运行；仅静态源码审阅，需原生控件和实际保存重载验证。

## 第六十一批：播放列表文本复制与定位粘贴

- 沿 IINA `SidebarPlaylistPane.copyToPasteboard/contextCopyURL/pasteFromPasteboard` 追踪选择顺序、仅网络URL复制和首个选中行之前插入（未选择为0）。Windows新增显式按钮：复制所选路径、仅复制网络URL、粘贴路径文本；复用稳定ID选择校验和 `PlayerSession.InsertPlaylist` 的异步队列计划。
- 复制按可见顺序保留重复项，不包含标题/行索引；遇到不能单行表示的路径拒绝整个复制，不破坏原剪贴板。复制URL为原文，按钮和结果提示可能含私密参数；使用 `ClipboardContentOptions` 禁止历史和跨设备漫游，不主动Flush。操作仍将文本交给本机剪贴板，不能防止其他本机应用读取。
- 粘贴只在点击时读取，一行一个绝对Windows路径或白名单网络URL，CRLF/LF均可；拒绝相对路径、控制字符、设备路径、非法/不支持的网络协议；保留原始文件路径的空格。最多1000条和1MiB UTF-8，整批解析成功才提交。异步读后校验媒体导航代次和播放列表revision，拒绝旧位置；重复点击有门禁。剪贴板API返回文本后才能执行大小限制，不宣称读取阶段具有1MiB分配上限。
- 平台差异：不复制Cocoa归档行索引，不读取系统文件对象，不实现剪切和通用Ctrl+C/V拦截；不扩展目录，不预检文件存在或可解码，文件/播放列表实际解析由既有mpv载入链承担。支持已解析条目文本往返，不等同于IINA完整pasteboard格式集。
- 未编译、测试、运行；只完成静态代码和来源追踪，真实剪贴板/并发修改/异步插入仍待验证。

## 第六十二批：Explorer 文件剪贴板与可取消目录解析

- 继续追踪 IINA `SidebarPlaylistPane.pasteFromPasteboard` 的文件路径集合 → `getPlayableFiles/resolveURLs` → 指定行 `addToPlaylist`，补齐第61批仅文本格式的限制。Windows在同时存在多种格式时优先读取 `StorageItems`，提取本地路径后交给既有 `LocalInputResolver`，不重复实现目录枚举。
- 原生文件/文件夹对象无本地路径（虚拟对象）、设备路径、输入超过1000项/1MiB时拒绝整批；解析沿既有隐藏项过滤、链接目录跳过、深度64/枚举100000上限、排序去重策略。解析结果再限制1000项/1MiB；因此原生文件格式可能去重排序，与保留逐行顺序/重复的文本格式有意区分。超过最终上限不提交，不声称后台枚举内存受1MiB约束。
- 新增“取消待处理粘贴”：递增操作代次、请求协作取消并释放门禁，迟到剪贴板/目录结果不会覆盖新请求。关闭请求取消；解析后重新核对媒体导航和队列revision再插入。取消无法中断正在阻塞的文件系统调用，也不撤回已提交的mpv编辑，界面明确说明。
- 解析中部分项目失败沿现有解析器处理：可用条目提交，显示部分跳过提示；完全为空则不提交。不做静默虚拟文件下载、不处理系统剪切后源文件移动，也未增加Ctrl+V拦截或跨应用拖出。
- 未编译、测试或运行；仅静态审阅，剪贴板StorageItems提供者行为、目录枚举和播放器插入需实际验证。

## 第六十三批：视频表面的原生滚轮播放控制

- 追踪 `PlayerWindowController.scrollWheel`、`MainWindowController.scrollWheel`、`AppData` 三种量表和 `Preference.ScrollAction`：纵向默认音量、横向默认seek，none不操作，倍速0.05..4且保留三位小数。新增WinUI视频表面PointerWheelChanged接线，侧栏、编辑器、滑块、列表保留原生滚动；模态、关闭、未载入、修饰键及输入焦点不允许时不抢事件。
- Windows按120单位一格归一化，保留高分辨率小delta，限制单次32格；纵向系数2、横向系数1继承IINA普通鼠标规则。Windows直接使用已交付方向，不再次套Cocoa natural-scroll反转。seek量表0.5/1/2/4（默认档3），倍速比例0.001/0.002/0.005/0.01（默认档3）；不可seek媒体不处理seek。
- 设置通过既有schema驱动的高级设置界面和JSON持久化：verticalScrollAction/horizontalScrollAction/relativeSeekAmount/playbackSpeedScrollAmount/wheelExactSeek。wheelExactSeek默认false保持上游relative默认；尚未接入自动seek延迟选择，不提供伪auto选项。
- 音量使用 `PlayerSession.AdjustVolumeByWheel` → mpv `add volume`，避免连续滚动用旧观察值覆盖增量；固定mpv `mp_property_volume` 的constricted type按softvol_max限制。倍速保持上游观察值计算，快速连续事件在观察延迟下不保证逐格累计；OSD仍依赖确认属性事件，非乐观状态。
- Windows未提供此路由的Cocoa手势phase/精确像素语义，因此不模拟触控板开始暂停/结束恢复，也不包含音量/进度滑块override、mini-player弹出层或插件可消费滚轮。未编译、测试、运行，方向和硬件输入需实际确认。

## 第六十四批：控制栏空闲隐藏与键鼠恢复

- 追踪 IINA `MainWindowController.showUI/hideUI/createTimer/destroyTimer/mouseMoved`：单次空闲计时，操作恢复，控制栏悬停不隐藏，PIP有独立可见性策略。此前 `enableControlBarAutoHide/controlBarAutoHideTimeout` 只有schema，本批真正接到WinUI底部控制栏和既有偏好持久化。
- Root键盘预览、鼠标活动、窗口激活及控制栏焦点显示控制栏并重置单次DispatcherQueueTimer。到期再次核对载入状态、关闭/模态、焦点/悬停、窗口激活及CompactOverlay，只有允许时隐藏。媒体属性高频更新只检查策略，不重置正在运行的截止时间；时间变化重新配置，零延时为最小1ms单次请求而非轮询。
- 键盘焦点仍在控制栏时保持可见，LostFocus延后一轮重新读取FocusManager。切换CompactOverlay保持现有底栏隐藏规则，返回普通窗口重启可见性策略；关闭停止timer并依靠弱引用丢弃迟到回调。隐藏时取消缩略图预览。
- 平台差异：只隐藏底部播放控制栏，不隐藏侧栏/系统标题栏，不全局隐藏光标，不加入Cocoa淡入淡出动画，不强迫迁移键盘焦点。暂停时仍可空闲隐藏，遵循上游非仅播放中的策略。未增加独立点击hideOSC动作。
- 未编译、测试或运行。代码静态接通不代表实际XAML路由、焦点和硬件鼠标交互已验证。

## 第六十五批：视频单击/双击/右击动作

- 沿 IINA `PlayerWindowController.mouseUp/rightMouseUp/performMouseActionLater`、`MainWindowController.performMouseAction` 和 `Preference.MouseClickAction` 追踪动作及默认值。新增视频表面单击隐藏控制栏、双击全屏、右击暂停的默认行为，三个偏好均可选无操作/全屏/暂停/隐藏控制栏/PIP/AB循环/重置倍速，复用现有会话及窗口命令，不另存播放器状态。
- 配置双击动作时，单击等Windows `GetDoubleClickTime` 单次计时后执行；双击取消待处理单击。无双击动作时立即执行单击，并适配WinUI第二次点击进入DoubleTapped事件的行为。单击保存媒体导航代次，换片后不作用于新文件；修改偏好、关闭、失活、离开表面、捕获取消均取消待处理单击。
- 只接收鼠标及视频表面，触摸/笔、修饰键、模态、关闭、未载入和不适合播放快捷操作的输入焦点不处理。插件鼠标仍是原有观察路径，不新增插件抢占或消费控制权。
- hide-controls遵守自动隐藏关闭偏好、焦点及悬停保护；不隐藏系统光标。PIP使用Windows CompactOverlay既有命令。中键/X键未新增动作，也不暴露未接线的中键偏好；不是全部Cocoa鼠标语义等价移植。不同位置连续单击在单次延迟窗口内只保留最后一次待处理动作，需后续交互评估。
- 未编译、测试或运行；静态源码接线，真实WinUI手势事件顺序和系统双击阈值仍待确认。

## 第六十六批：可切换时间标签与速度相关剩余时间

- 追踪 IINA `DurationDisplayTextField.switchMode/updateText/setPrecision`、`PlayerCore.fileLoaded` 及周期同步时选择 `time-remaining/full` 或 `playtime-remaining/full` 的规则。此前三项时间显示偏好只有schema，本批接通showRemainingTime、scaleRemainingTime和timeDisplayPrecision。
- MpvClient观察两个剩余时间属性的DOUBLE值；PlaybackSnapshot分别持有可选观察值，START_FILE清除，失败/非有限/负数置未知。使用固定mpv `mp_property_remaining/mp_property_playtime_remaining`，不根据客户端旧position/speed自行猜测，DOUBLE无需字符串/full解析。
- WinUI当前时间和右侧时间分离；右侧为可键盘操作按钮，点击在总时长/剩余时间之间切换并保存，右击菜单选择秒/100ms/10ms/1ms精度。使用已移植VideoTime格式化，剩余时间前置负号；未知显示--:--，非载入状态不保留旧媒体时间。辅助名称说明当前模式及切换动作，缩放剩余时间偏好仍在高级设置中。
- 平台差异：右侧按钮替代Cocoa可点击标签，不提供Touch Bar；精度菜单不绘制当前项勾选，仅修改设置和即时文字。负剩余时间不展示双负号而视为未知。总时长/剩余均是媒体估计，不是实际完成时间保证。既有多窗口设置没有实时广播，本批不宣称所有窗口立刻同步。
- 未编译、测试或运行，mpv属性事件顺序、XAML菜单及辅助技术实际反馈仍待验证。

## 第六十七批：文件载入与seek观察型OSD

- 追踪 IINA `MPVController.handleEvent` 的START_FILE→fileStart消息、SEEK→时间同步/seek消息和PLAYBACK_RESTART→更新位置。`PlaybackOSD` 增加每条目一次文件通知及seek状态/位置比较，补齐此前只有消息模型而无生产接线的两类OSD。
- 文件通知在Loaded且path已观察后发送，不因迟到轨道/metadata再次弹出；首次属性仍建立基线，不把初始音量等当作用户修改。为避免网络凭据/签名参数出现在瞬时叠层或截图，网络统一显示Network media，本地只显示文件名；不保留整条路径到OSD历史状态。
- seek仅在观察到seeking状态改变、或seeking期间实际position/duration变化时更新，并在restart恢复时刷新一次；普通时间推进不持续弹出。使用已观察位置和时长，未知显示占位，不使用请求目标假装成功。消息与既有音量/暂停等变化按原有最后合格项策略合并，不积压过时队列。
- 真正接入disableOSDFileStartMsg/disableOSDSeekMsg偏好。全局禁用及CompactOverlay仍推进比较状态，不在重新开启后重播旧消息。
- 有意差异：IINA的fileStart在START_FILE阶段且网络显示URL，本批为Loaded阶段的隐私收敛通知；加载失败无fileStart成功叠层。seek观察不能识别发起者，也不声称解决事件因果归属/自动精确seek性能测量。未编译、测试、运行。

## 第六十八批：真实视频上的交互裁剪操作链（可证明映射子集）

### 源码与几何边界

- 逐步追踪 IINA `InteractiveModeController.enter/exit/selectedRectUpdated/predefinedAspectValueAction/cropAction/cancelAction`、`CropBoxView` 的拖动命中和坐标处理，复用此前转写的 `CropSelection`：新选区、整体移动、八方向边/角拖动、源像素量化、边缘吸附、居中比例、整幅不加裁剪。Windows左上原点由既有模型统一处理，不重复套Cocoa Y轴翻转。
- 新增 `video-out-params`、`osd-dimensions`、`video-rotate` 权威观察。映射从固定mpv `command.c` 图像参数/OSD属性和D3D合成尺寸契约推导：实际渲染边距映射到SwapChainPanel DIPs，不假定视频一定填满窗口；兼容非方像素和letterbox，裁剪输入仍为未变换源像素。
- 首批只接受vf完整且为空、旋转选项及输入/输出rotate均0、输入输出w/h一致、内建crop字段缺省或整幅、VO已配置、OSD尺寸与合成物理尺寸相符。无法证明的旋转、已有crop/其他滤镜、内建video-crop、音频或未稳定输出拒绝进入。`mappingConfirmed`只表示静态契约前提齐备，不是GPU/运行验证声明。

### Windows完整操作链

- 从视频设置“在视频上交互裁剪”进入，真实视频上覆盖透明Canvas、选框和八个手柄，侧栏替换为可滚动编辑栏。支持指针捕获拖选/移动/缩放、数字坐标草稿、全画面与4:3/16:9/16:10/21:9/3:2/5:4居中比例、确认和取消。画布裁切于视频视口，选区越出可见范围不覆盖侧栏。
- 进入时记录媒体导航/entry/滤镜revision，记录原暂停状态并请求暂停；屏蔽播放器键盘/鼠标插件观察及播放手势，不屏蔽本裁剪工具的Esc/Enter与数字编辑。Enter不越过未提交数字草稿直接提交旧选区；数字更改需先点击更新草稿。指针取消/捕获丢失/失活回滚当前拖动，不把半次拖动当已完成。
- 调整窗口/DPI时拒绝旧OSD映射：暂停画布交互和确认，等待匹配的新几何，再保留源像素选区重映射。媒体/源尺寸/滤镜链变化取消编辑；关闭清理捕获和UI，不恢复播放。一次正常确认或取消可按勾选项恢复进入前播放状态，换片或自动失效不擅自恢复。
- 确认再次验证当前媒体/空滤镜链及映射，将 `CropSelection.Confirm` 结果提交已有会话AddFilter链；全画面不添加filter。取消不提交vf编辑。提交仅为请求，实际效果与错误仍以mpv事件为准；不把Overlay隐藏当作滤镜生效。

### 限制及下一分解

- 未实现“已有受管裁剪临时移除→编辑→取消恢复”事务，现有裁剪需先用原重置按钮移除；未支持任意空间滤镜、旋转后的反向映射、delogo、Cocoa浮动窗口动画或光标形状切换。这是较大功能的第一完整可用路径，不是整个交互模式已完整移植。
- 恢复播放基于进入前状态与相同媒体保护，不能证明外部SMTC/脚本暂停意图的所有权；用户可取消恢复勾选。媒体映射失效后保持当前播放状态，不自动唤醒。
- 未编译、测试或运行。静态核对了实际映射属性、源尺寸、指针生命周期和请求边界；真实WinUI/MPV/D3D坐标一致性仍需以下独立验证。

## 第六十九批：已有受管裁剪的异步预览/恢复事务

### 源码与新增回执边界

- 追踪 IINA `SidebarVideoPane.cropAction` 的removeCropFilter→延后enter(crop,whole)及 `InteractiveModeController.cropAction/cancelAction/exit`。上游进入会移除原裁剪，取消本身不恢复；本批按Windows产品要求新增显式取消恢复，仍以全幅初始选区进入，避免把已裁剪图像坐标当作源坐标。
- MpvClient滤镜编辑保持现有串行命令和fresh node比较，新增set-property reply后在同一worker读取完整vf/af节点及当前播放entry/path。PlayerSession提供最多8个请求ID关联回调，校验读回媒体身份并更新观察快照；关闭/失败释放回调且报告结果未确认。写成功与读回完整/匹配分别判断，不把收到reply等同于恢复成功。
- 跟踪型编辑在worker写入前也读取playlist-playing-pos→entry ID和path，拒绝已切换媒体。此检查、节点比较和mpv写入并非原子事务，外部脚本仍可能在最后检查后竞争；不声称拥有mpv CAS或消除所有竞态。

### 操作阶段

1. 仅一个结构完整的 `@iina_crop:crop` 条目可进入（包括停用条目）；其他滤镜/重复标签拒绝。保存整个原始节点树、媒体身份、原暂停状态，暂停播放并提交临时移除。
2. 等移除reply且完整读回确认为预期空链后才进入“等待源几何”；随后接第68批交互画布。等待时取消不会猜测移除结果，而是记录取消请求并等读回后恢复；未完成前不显示已经恢复。
3. 取消以预期空链作为before、原始节点树作为after写回，保留原启用/停用及未知字段。确认则以原节点结构替换crop参数后写回，保留原启用状态；确认全画面意味着保留已经确认的空链、明确移除旧裁剪。
4. 最终写回等待reply及匹配读回；成功再关闭编辑/按勾选恢复播放。确认新裁剪失败且读回仍为空时，最多自动尝试一次恢复原裁剪。已提交最终写入不可撤销，取消控件暂禁用并提示等待；不通过重复写入猜测结果。
5. 入队失败可重新点击取消请求恢复；外部vf修改、换片、读回不完整/身份不符或恢复失败，不覆盖新状态，退出并把原裁剪表达式填入现有滤镜编辑器用于人工处理。表达式恢复不等于保留完整未知节点字段或停用标记，提示不声称自动恢复完成。关闭直接结束播放会话，不对将销毁的句柄重放旧滤镜。

### 仍未验证及限制

- 前批无旋转/无其他空间变换的几何限制不变；原裁剪移除后仍无法确认映射时可等待或取消恢复。先前暂停恢复勾选在等待阶段修改后不会被再次进入画布覆盖。
- 命令未回复时不设置“超时即假定失败并重发”，用户可关闭会话；不能保证底层挂起调用的终止时间。外部播放意图与同一entry重新加载的全部竞态仍需验证，不做原子性承诺。
- 未编译、测试或运行。本批是完整可审阅的静态事务链和读回基础设施，不是已证明恢复/播放正确。

## 第七十批：首次裁剪的异步确认闭环及原节点保留修正

继续追踪 `InteractiveMode.cropAction` → `PlayerCore.setCrop` 的全幅移除/非全幅命名crop路径。首次空vf编辑现在也保存完整空链基线，提交通过 `FilterChainEditor.CreateSingle` 的结构化参数校验、既有媒体身份防护和reply后读回，不再通过无回调AddFilter后立即退出。取消/全幅确认不写无意义空链；非全幅确认只有成功reply及匹配读回才恢复播放。失败读回仍为空时明确报告未应用；冲突不自动写回，关闭不重放请求。没有扩展几何支持范围。

修正第69批静态审阅发现的缺口：通用 `Replace` 重建行，实际未保留未知字段。受管crop身份不变的替换现在仅以新params覆盖原完整行，保留未知字段及enabled；不改变通用Replace语义。提交最终请求前结束拖动、释放捕获并冻结画布；数字/比例更新按Editing阶段守卫，防止等待期间改动草稿或覆盖状态提示。

首次与已有裁剪统一采用等待最终确认再退出，是对上游exit后setCrop顺序的有意调整。mpv不是原子CAS，外部脚本仍可在检查后竞争。本批仅静态编码/源码审阅，未编译测试运行。

## 第七十一批：交互去标志完整编辑链

### 源码追踪

- `MainWindowMenuActions.menuSetDelogo`：未启用时进入 `.delogo`，已启用时移除 `info.delogoFilter`。
- `InteractiveMode.enter`：保存播放状态、暂停并显示共享CropBoxView；上游delogo默认零大小选区。`doneAction → delogoAction`：将翻转后的源坐标x/y/w/h构造成 `lavfi delogo`，移除既有滤镜、添加带 `Constants.FilterName.delogo`（AppData.swift中为iina_delogo）的滤镜，失败反馈。cancel退出。
- `MPVFilter.init(lavfiName:label:paramDict:)` 与 mpv `filters/f_lavfi.c` 的graph字符串选项：IIMA使用同一delogo参数，但通过结构化 `lavfi` 节点的graph字段传递，不向原始表达式拼接用户文本。

### Windows实现与差异

复用真实视频选区、数字草稿、移动/八向缩放、捕获/媒体代次保护和异步滤镜事务。新增去标志入口、专用标题/确认文案，隐藏裁剪比例选项。初始为源画面中间半宽半高选区而不是零面积；只生成草稿，不自动应用。delogo量化不使用裁剪的4像素贴边吸附，并要求四边至少保留1源像素，拒绝全幅/贴边区域，降低FFmpeg边缘取样失败风险；实际滤镜格式/色度对齐仍以底层结果为准。

空链首次提交以完整读回为准；唯一iina_delogo/lavfi且graph为单一delogo表达式可临时移除后编辑、确认替换、取消完整恢复。多滤镜、另一模式的受管滤镜、复杂lavfi图形、重复标签拒绝，不假定输出坐标等于源坐标。替换只更新params、保留原enabled及未知节点字段。已有去标志可在编辑器明确选择“移除原去标志滤镜”，保留已读回确认的空链；这替代上游菜单同一入口的开关语义，避免误把编辑当删除。错误/换片/关闭保护沿用69/70批事务。没有扩大旋转/空间变换支持，也不是AI对象移除。

未编译、测试或运行，未验证FFmpeg是否包含delogo、具体像素效果或硬解格式协商。

## 第七十二批：播放列表磁盘文件删除与部分结果对账

### 源码追踪

`SidebarPlaylistPane.contextMenuDeleteFile`读取所选条目，跳过网络资源，逐一调用`FileManager.trashItem`，累计成功索引，最后`player.playlistRemove(successes)`；失败逐项提示。`contextMenuRemove`只改列表，`contextMenuDeleteFileAfterPlayback`在快照中仍WIP。本批不声称实现播放后自动删除。

### Windows实现

- 明确区分“移除”与“删除所选磁盘文件”按钮；展示全部待处理路径并以取消为默认，最多64个不同文件、24K字符确认文本。重复选择同一路径只删一次，保留各选择条目的稳定ID供对账。
- 排除网络资源及当前播放文件（含路径大小写相同的重复项），不自动停播/解锁。后端只接受普通盘符绝对路径及固定/可移动盘文件，拒绝通配符、设备/UNC/ADS、目录、文件及父目录重解析点、超长路径等。不递归目录，不执行shell命令字符串。
- 专用STA上逐文件调用Windows `SHFileOperationW(FO_DELETE)`；保留系统确认/错误界面，设置`FOF_ALLOWUNDO | FOF_WANTNUKEWARNING`。**与macOS trashItem的差异：这是Windows优先回收的原生删除流程，不承诺一定可恢复；不能回收时由系统另行确认永久删除。**不设置静默/免确认标志，不提供失败后的永久删除重试。UI使用同样措辞，不把返回成功描述成“已入回收站”。原生对话框无播放器窗口owner，播放器关闭后仍可在其中取消。
- 停止按钮和关闭只取消后续文件，不能撤回已完成或正在系统界面处理的操作。收到取消即结束批次；每个实际完成的路径独立记录，不用批次成功推断所有文件成功。
- 返回UI后，仅在相同导航代次且没有其他列表编辑时，按原ID及原路径匹配仍存在、不是当前项的成功条目，提交列表移除；不使用旧索引，不移除新插入的重复路径。列表已变时允许留存缺失文件条目而不冒险删错项。列表命令仍异步，报告“已请求移除”，不伪造最终完成。

### 限制

目录/符号链接/网络/超长路径不支持。硬链接/短路径等别名未做完整文件身份解析；路径检查与系统删除之间存在文件系统TOCTOU，不能承诺原子身份锁定。系统对话框期间外部控制仍可能改变播放状态；本批不自动释放新打开的句柄。系统错误码不当作HRESULT解析；错误只报告未完成并由系统提供细节。未实际删除任何文件，未编译测试运行。

## 第七十三批：自动相对seek策略的实际事件接线

`PlayerCore.seek(relativeSecond:option:)`按Preference.SeekOption的relative=0/exact=1/auto=2选择命令；auto每文件首次请求记录耗时，`MPVController.handleEvent`在SEEK记录起点、PLAYBACK_RESTART计算间隔，低于0.05秒使用exact，否则relative。`PlayerCore.fileLoaded`重置试验标志，但保留上一文件决策。此前IIMA只有AutomaticSeekPolicy模型，实际SeekRelative创建临时对象且未测量，不能算已接通auto。

现在会话持有策略，START_FILE启动新代次。新增`useExactSeek`枚举偏好（默认relative），两个±5秒按钮及滚轮seek读取它；已有wheelExactSeek=true仍强制exact，显式input.conf/插件seek参数不被偏好覆盖。相对seek拒绝不可seek、零/非有限参数。accepted仍仅表示入队，不表示已完成。

MpvClient为首次自动请求标记测量窗口，在worker用steady_clock记录SEEK→RESTART，排除UI调度延迟。成功command reply与完整事件对同时到齐后才上报请求ID/间隔，支持reply先于或晚于restart；失败、第二次SEEK、无SEEK的RESTART、超过10秒样本、其他写命令/滤镜修改、换片/结束/关闭都会放弃样本。只读查询不使样本失效。会话仅接收自己的待测request，放弃样本时保留原决策且本文件不反复试验。新文件可再次试验。

**限制：mpv的SEEK/RESTART没有命令因果ID。这里的request只是本机相关窗口，无法证明与外部客户端、内部自动循环或尚未消费事件的绝对因果关系；不能称为精确性能测量。**相比上游加了重叠/失败拒绝，但没有屏蔽其他客户端。50ms阈值沿用上游，不代表Windows上的最佳阈值；没有执行性能或播放验证。未编译测试运行。

## 第七十四批：Shooter指纹搜索、选择下载与加载链

### 源码依据

`ShooterSubtitle.swift`的`Fetcher.hash`读取至少12288字节的本地影片四段4096字节，MD5用分号连接；`FileInfo.dictionary`向固定`https://www.shooter.cn/api/subapi.php`提交filehash/pathinfo/format=json。`request`读取Desc、Delay、Files中的Ext/Link；`Subtitle.download`只下载files[0]。本批实际接通此前已有的`ReadShooterMovieHash`，并沿用在线字幕搜索/下载媒体票据、暂存直到mpv释放、加载请求与另存链。

### 实现与有意差异

新增服务选择，无令牌。点击发送查询后在后台读取稳定本地影片指纹，向固定API提交四段哈希及用户可编辑查询词。**pathinfo使用查询词而非上游完整本地路径**；界面明确说明哈希必发、不上传影片、不发送完整路径。该差异可能降低服务匹配效果，但避免泄露目录。前置条件失败不发网络请求。

新`ShooterClient`使用独立WinHTTP句柄、正常TLS验证、禁用自动认证/cookies/重定向，固定POST和无凭据GET。单次30秒协作期限、各阻塞阶段5秒超时；取消/期限在网络调用间检查，不能声称能瞬时打断当前调用。JSON上限2MiB、下载8MiB、200组/每组8文件/总400选择；非法字段/数值/超过预算拒绝，不使用上游强制类型转换。服务返回负数而非数组时报告协议错误，不凭猜测当作无结果。

每个受支持文本文件成为独立选择，不静默取files[0]；支持srt/ass/ssa/vtt，不解压归档。生成固定安全叶文件名，不信任响应文件名。下载只允许443端口HTTPS shooter.cn及其子域，拒绝含认证/fragment的链接，外部CDN/HTTP不支持；空结果提示也可能是全部链接/格式不支持。Delay显示供参考，不自动设置字幕延迟，与上游记录但未应用一致。

取消/换片/窗口关闭丢弃旧结果，既有暂存预算和媒体身份保护不变。凭据框在Shooter禁用，不传其他服务token/key。没有联网请求或验证Shooter当前服务可用性；未编译测试运行。
