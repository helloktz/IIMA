# 自动窗口缩放接续交接

状态更新（2026-11-12）：已接入来源关联、窗口有界状态机及偏好消费者，
并补齐首视频几何例外、后续媒体保持面积及播放中尺寸重配置。只有静态审阅，
不计入原生验收完成。具体范围与差异见 PORTING.md 最新自动窗口尺寸章节；
以下为实现前交接与检查依据，历史基线为开发分支第九十三批
`fdf93314fe2d9bc9dac56e32b0b91f4b0026066c`。本文件是接续设计，不是已验证接口。

## 已有可复用部分

- `MainWindow.VideoSize.cpp`：手动0.5/1/1.5/2倍、适合工作区；检查 Loaded、VO、旋转、内建 crop、原生普通还原窗口、DPI、布局和屏幕，调用 `ScaledVideoWindow`。
- `Geometry/WindowGeometry`：外框几何解析及 ResolveVisible；初始启动几何现已接通，但不按首个视频重新应用。
- `MainWindow.Placement.cpp`：Capture/Restore 与初始几何，避免在用户操作之后覆盖位置。
- 偏好 schema 已声明 `resizeWindowTiming`（0 always / 1 onlyWhenOpen 默认 / 2 never）和 `resizeWindowOption`（0 fitScreen / 1 0.5 / 2 1 默认 / 3 1.5 / 4 2），尚未加入应用适配器曝光，也没有消费者。

## 必须追踪的原实现

固定快照 `references/iina/iina/`：

1. `MainWindowController.windowDidLoad` 初始化几何；`windowFrameFromGeometry` 的宽高优先、中心/边缘位置和屏幕偏移。
2. `MainWindowController` 约2430–2515行处理视频尺寸：`justStartedFile`、`justOpenedFile`、`shouldApplyInitialWindowSize`、`keepWindowSize`；`onlyWhenOpen` 不是每次 Loaded。
3. `PlayerCore.openURLs/openMainWindow` 与 mpv 文件开始/加载路径如何设置和清除这些标记；`onVideoReconfig` 和 `windowScaleChanged` 是不同触发源。
4. `Preference.ResizeWindowTiming/ResizeWindowOption/usePhysicalResolution`，注意 fitScreen 上游先 shrink，不一定等于现有手动最大化适配工作区。
5. mpv `video-out-params`、`dwidth/dheight`、`video/out/aspect.c` 的旋转/像素比例规则。已有手动实现拒绝未知或内建非全幅 crop，不应取消该保护来假装支持。

## 先解决媒体来源与时序，再接 UI

当前 `PlayerSession::OpenMultiple` 的非 append 路径和 `PausedCommand` 都先异步 pause，之后排 load/playlist 命令；`pauseRequest_`、`loadRequest_`、`transitionIssued_`、`waitingForStart_`、`loadReturnsEntry_` 和 `expectedEntry_` 共同保护播放握手。

- 显式 Open、播放列表切换、自然 EOF 自动下一项、reload 同一 entry、打开失败是不同来源。
- load command reply 返回 entry ID 与 START_FILE 的相对次序不能假定。新的 Open 意图产生时，旧媒体事件可能已排队。
- 不应在 Open 时设置一个 nextStartIsExplicit bool，然后无条件让下一个 START_FILE 消费它。
- 不应单靠 path、entry ID 或 Loaded 判断新一代：同一路径/同一 playlist entry 能重载。
- 不应复用窗口暂停策略的 generation 为公开自动缩放契约而不分析其递增点和生命周期。

建议先实现独立、有界的 open-origin 跟踪状态：意图序号、待提交/已提交 request ID、返回 entry ID、实际 START_FILE 代数；只有可以关联的命令回复和文件事件都到达才向窗口发布来源。来源不确定时保守跳过 onlyWhenOpen，不默认视为用户打开。失败、关闭、被新打开取代须明确取消。需要沿 mpv 当前 pinned 命令返回/事件契约判断能否做到可靠关联；若无法，记录受限范围，不承诺完全兼容。

## 窗口侧状态机建议

- 单独模块承载 pending media generation、已消费代数、手动交互/布局修订，不在通用 Apply 每帧无条件移动窗口。
- 等用户设置载入、Loaded、VO、可信尺寸、普通还原 presenter、非音乐/裁剪、可用屏幕和有效布局均成立。
- 全屏/最大化/PIP/音乐模式下的处理须明确“跳过”或“等回普通窗口”；不能迟到几分钟突然改变用户窗口。
- LayoutUpdated/SizeChanged 或单次 dispatcher 延迟只能驱动重新评估，不是无界轮询。关闭撤销，过期代数忽略，用户手动缩放优先。
- 将手动 `ResizeToVideo` 的几何计算拆出为可返回成功/暂不可用/永久不支持的结果；自动路径不要按每个属性事件弹错误提示。
- 应用一次后标记消费，不能用 AppWindow Changed 引发又一次自动缩放。初始几何和保存位置的优先顺序须清楚，避免本批初始应用与首视频应用互相覆盖。
- `always`/`onlyWhenOpen`/`never` 与首次几何例外分别对照上游；中途 video-reconfig 的 keepWindowSize 语义需单独实现或明确延期。
- 偏好编辑不应伪装为新的文件打开。只有消费者完整后才加入 HasApplicationAdapter，使 UI 选项不是空壳。

## 必须覆盖的静态事件序列与后续原生验证

当前用户不要求编译/测试/运行。继续者仍应逐路径静态推演，并将尚未执行的原生步骤写入 VALIDATION：

- 显式单文件/批量替换、仅追加、手选下一项、自然 EOF、同 entry 重载。
- START_FILE 在 load reply 前/后；旧事件排队时新的 Open；pause/load 阶段失败；连续两次 Open；关闭或 stop 中断。
- 音频无视频、延迟 tracks/VO、网络视频稍后才有尺寸；格式切换/旋转/滤镜尺寸改变。
- 首窗口恢复位置与显式几何；设置加载期间用户拖动；副屏负坐标与不同 DPI；工作区小于最小尺寸。
- 普通窗口/最大化/全屏/PIP/音乐模式；在等待布局时用户改变模式、手动缩放或换片。
- 每个媒体代数至多应用一次；未知来源/未知几何不移动；无递归布局循环、重复弹错或无限计时器。

## 本次交接边界

没有增加自动缩放功能代码，也未改变 main。第九十三批及此前代码保持不变。
产品 C++ 计数仍为新增274文件、31,880物理行，距离61,220行还差29,340行。
不为满足数量复制参考源码或添加无用声明。后续按完整操作链分批 commit/push，
并继续区分实现、静态审阅与真实 Windows 验证。
