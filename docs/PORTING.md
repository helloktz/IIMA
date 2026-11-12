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
