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

因此，从属性取整数地址到 UI 调用 AddRef/SetSwapChain 存在生命周期缺口。异步观察属性、给事件贴代次、在 UI 上加锁都不能锁住 mpv 的 VO 线程。当前实现拒绝将该整数转为可用 COM 对象，也不会打开文件后只播放不可见视频的声音。`VideoPresenter` 仅接受明确拥有的交换链接口；默认打开操作报告尚未满足的呈现条件。

这不是“已经证明所有合成方案不可行”，而是当前固定上游接口未提供所需的安全获取协议。下一步应在独立的生产依赖工作目录评估同步获取拥有引用的扩展/修复或有契约保证的其他方案，保留参考快照不变，并用 Windows 运行证据验证。不可通过忽略生命周期问题宣布里程碑 B 完成。

## 验证边界

本批云宿主为 Linux，无 Windows SDK、MSVC、XAML 编译器、libmpv Windows 二进制或交互式桌面。
静态 XML/工程检查和纯 C++ 状态测试不等于 Windows 构建成功。具体命令、结果和 Windows 复验步骤见 `VALIDATION.md`；依赖准备见 `LIBMPV.md`。
