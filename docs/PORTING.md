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
- 当前没有移植 IINA 自动续播、历史、目录扫描和 `pauseWhenOpen` 设置；不把硬编码默认值说成设置功能。

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

原生多选、文件拖放和命令行首次启动文件进入同一会话；初始文件等待数据加载与内核 Ready。目录递归、播放列表文件展开策略、多实例路由和注册文件关联尚未实现。
快捷键：Ctrl+O 打开，Space 暂停/继续，F11/Escape 全屏往返，Ctrl+Left/Right 上下项，Ctrl+L 播放列表，Ctrl+Shift+S 截图。UI 事件与媒体键复用相同会话方法。

`NowPlayingInfoManager.updateInfo`、`AppDelegate.RemoteCommandController`、`SleepPreventer.updateSleepPrevention` 对应 `Platform/MediaIntegration`：HWND 绑定 SMTC，实际状态驱动播放/暂停和上下项能力，仅共享文件名而非完整路径；暂停/结束/关闭清除电源请求。PowerCreateRequest 按句柄 RAII 管理，避免线程绑定式防休眠跨线程清理。默认与 IINA preventScreenSaver=true/allowScreenSaverForAudio=false 一致。未实现封面/专辑元数据或系统进度条。

## 仍不属于已交付能力

没有 Windows 已构建可执行文件或已测试 ZIP；没有注册文件关联、单实例路由、安装器、签名、自动更新或正式发布。HDR、设备丢失/更换、跨屏色彩、多 DPI 实机表现、睡眠恢复仍需硬件验证。项目正式根许可证和完整二进制依赖版本锁定/源码交付义务仍需在发行前完成。

## libmpv 消费头适配

`src/IIMA.Core/Mpv/include/mpv/client.h` 是固定参考头的完整许可保留副本，唯一差异是 Windows 的 MPV_EXPORT 从 `__declspec(dllexport)` 变为空宏（含解释注释）。应用动态加载 C ABI，不应对外导出 mpv 声明；生产 mpv 构建头仍保留 dllexport。静态检查逐字比对除该行外完全相同；x64 指针、int 大小和 API 2.6 以 static_assert 约束。未用猜测结构重写 ABI，也不需要不同工具链的导入库。
