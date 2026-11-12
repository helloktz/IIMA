# 初始化工程验证

日期：2026-11-11。范围：空白 WinUI 3 窗口、MSBuild 构建、ZIP 打包及启动关闭；不涉及播放器功能。

## 环境

- Windows x64，系统 build 26300；此机器安装有开发工具，不是干净测试镜像。
- Visual Studio Build Tools 2026，18.11.3。
- MSVC 工具目录 14.51.36231；编译器报告 19.51.36260。
- Windows SDK 10.0.26100.0。
- Windows App SDK 组件 2.6.5，C++/WinRT 3.0.260818.1。
- PowerShell 7.6.9，NuGet CLI 8.0.0。

## 已执行

| 检查 | 结果 |
|---|---|
| `scripts/build.ps1 -Configuration Debug -Rebuild` | 成功构建应用与静态库 |
| `scripts/package.ps1`（包含干净 Release x64 构建） | 成功，生成 IIMA-0.1.0-win-x64.zip |
| 编译日志中的标准模式 | 确认为 `/std:c++23preview` |
| dumpbin 检查可执行文件 | Machine 为 8664（x64），子系统为 Windows GUI |
| ZIP 解压到独立目录后运行 `scripts/smoke-test.ps1` | 原生窗口出现，标题 IIMA 0.1.0，正常关闭，退出码 0 |
| 检查运行进程实际加载的模块 | WinUI、Windows App Runtime、vcruntime140、msvcp140 从解压后的应用目录加载 |

构建日志与 binlog 保存在 `artifacts/logs/`，生成物不纳入 Git。

## 已知警告与尚未验证项

- MSBuild 的上游 RoslynCodeTaskFactory 任务产生三条 CS1668 警告，涉及环境中的可选 ATL/MFC、辅助库和 UM 搜索目录不存在。构建成功，未通过压制警告隐藏此现象；接手者可在完整 Windows 构建镜像中复核。
- 尚未在没有开发工具的干净 Windows 11 镜像、其他 Windows 11 build 或远程云端 runner 上验证。
- 当前直接由 C++/WinRT 创建 WinUI 对象，没有 XAML 页面或页面生成代码；XAML 编译链未验证。
- 没有 libmpv 运行依赖、播放功能、HDR 或其他媒体能力。
- ZIP 为未签名研发验证产物；没有安装、卸载、自动更新或商店发布验证。

以后有新的代码或验证结果时更新本文，不把这次初始化验证延伸为后续功能的测试结论。

## 云端播放基础批次（2026-11-11）

基线 `f5f649b`（已确认包含 `d64f404`），分支 `dev/native-playback`。
当前环境为 Linux，有 Git、Python 3、G++，没有 Windows/MSVC/XAML/PowerShell、Windows libmpv 或交互桌面。

已执行并通过：

```sh
python3 scripts/check-source.py
g++ -std=c++23 -Wall -Wextra -Werror -Wno-missing-field-initializers src/IIMA.Core/Playback/PlaybackState.cpp src/IIMA.Core/Tests/PlaybackStateTests.cpp -o artifacts/tests/playback-state
artifacts/tests/playback-state
g++ -std=c++23 -Wall -Wextra -Werror -Wno-missing-field-initializers -c src/IIMA.Core/Playback/PlayerSession.cpp -o artifacts/tests/player-session.o
git diff --check
```

结构检查解析 8 个 XML 文件，检查工程源文件存在与参考快照未变。纯状态测试覆盖未知/无效时长、加载状态、seek/restart、过期 END_FILE 条目、命令接受不等于加载完成、关闭后迟到事件。G++ 仅用于独立逻辑的检查，不替换产品的 MSVC 工具链。

未执行：Windows Debug/Release MSBuild、MIDL/XAML 代码生成、PowerShell 脚本运行、真实 DLL PE/ABI probe、GUI 启动、播放、音频、GPU/SwapChainPanel、设备重建、ZIP 解压运行。源码已有不代表这些步骤成功。

Windows 后续步骤：

1. 按 BUILD.md 安装固定工具链与 WinUI C++/XAML 组件；运行 Debug、Release 两个 `scripts/build.ps1 -Rebuild`，检查 IDL、XAML、生成的 MainWindow/App、链接。
2. 按 LIBMPV.md 准备固定源构建的 x64 SDK，执行 staging 与 ABI probe；保留实际编译依赖版本和探针结果。
3. 启动应用检查缺 DLL/错架构/不兼容 API 的错误；恢复 SDK 后重复启动和关闭。初始化中关闭、快速重复关闭不能遗留进程。
4. 在安全交换链协议完成前，打开文件必须明确报告未可播放，不得隐式无画面播放。协议完成后再检查中文/空格/特殊字符路径、损坏文件、文件不存在、取消和重复打开。
5. 检查暂停/继续、seek、快速换片、音视频、未知时长、控制栏覆盖；连续缩放、DPI 变化、F11/Escape 往返以及播放中关闭。
6. 执行 package.ps1，在独立目录解压后 smoke-test；再在无开发工具的 Windows 11 x64 检查依赖闭包。HDR、跨屏/设备丢失另列硬件结果。

## 后续功能代码批次（同日）

已实现代码链：安全扩展获取 COM 引用并在 UI 确认绑定后恢复播放；多文件队列和编辑；轨道/字幕/章节、音量/倍速/截图；设置、历史及续播；原生文件拖放/初始命令行；系统媒体键与防休眠。此列表表示代码存在并经源码审阅，**不表示 Windows 功能已实测**。

本批实际执行：

- `python3 scripts/test-portable.py`：playback-state、mpv-nodes、player-session、user-data 四套通过。
- `python3 scripts/check-source.py`：XML、项目源文件输入、参考快照未改检查通过。
- 独立源码副本 `git apply --check` / apply / `git apply --reverse --check`：私有 mpv 补丁通过。
- 补丁后的公开 C API 头：GCC C11 与 G++ C++23 语法检查通过。
- `git diff --check`：通过。

测试覆盖命令回复前不发 loadfile、reply 条目 ID 与播放加载/绑定的协调、未知时长不 seek、移动索引换算、字幕重复 reload、关闭拒绝命令；node 数据深拷贝；历史去重/禁用/EOF/上限。测试替身只证明输入输出契约，不证明 libmpv 实际事件顺序或 WinUI 行为。

新增 Windows 验证必须覆盖：

1. 构建补丁版 mpv（含 libplacebo D3D11），确认扩展导出，运行 stage-libmpv/test-libmpv。普通上游 DLL 应收到明确缺扩展错误，而不是解引用裸地址。
2. 快速连续替换 A→B→C，loadfile reply 前后交叉的 START_FILE/FILE_LOADED/VIDEO_RECONFIG；旧链解绑、新链重新绑定、关闭时无悬空 COM 引用。
3. 列表追加/移动/删除当前项、最后一项的上一下一、多轨字幕（含同一路径 reload）、章节、截图选择器取消/期间换片。
4. settings/history 首次缺文件、损坏文件备份、未知 schema、写入失败、关闭时保存/重试/明确退出不保存；历史路径中文、加载完成后续播、不重复 seek。
5. 拖入多个文件和目录拒绝、命令行文件初始化等待、媒体键、暂停/关闭后 `powercfg /requests` 不遗留请求。
6. 连续 DPI/窗口缩放验证逆矩阵和像素大小一致；全屏、设备重建、纯音频 force-window 合成、关闭时输出停止。这些当前未运行。

最终发行仍缺真实 Windows 构建与完整二进制依赖锁定、无开发工具镜像验证、项目正式许可/签名等。不合并 main、不发布 Release。

## 后续设置 / Windows 集成源码批次

`test-portable.py` 新增 diagnostic-log、local-input；设置测试覆盖默认/范围/enum、pauseWhenOpen、启动前配置、观察状态不乐观变化。诊断测试验证1MiB轮转、flush和关闭；输入测试验证扩展策略、自然排序与去重。Windows 文件关联、命名管道、目录权限与取消、设置控件和实际音频延迟均未运行。

Windows 新增验收步骤：

- 同一登录会话同时启动多个副本、连续打开多个中文/空格文件、无文件第二次启动激活窗口；确认没有重复加载/丢失消息。关闭首实例期间转发结果须明确，不能无限挂起。不同用户/登录会话及远端连接不得注入。
- 用户点击注册后检查 HKCU 候选；验证默认应用没有被强改，设置页可以选择 IIMA；路径带空格时打开正确。移到另一目录注册，再从旧副本撤销必须拒绝；正常撤销不删除其他播放器键。
- 目录含隐藏项、reparse循环、权限拒绝、大量文件和取消；只解析受支持媒体，有可理解反馈，新请求不被旧解析结果覆盖。
- pauseWhenOpen、自动下一项、循环、音量上限、静音、字幕可见/延迟、截图三种模式实际生效及重启保持；mpv拒绝时不得显示成成功。
- 日志不含完整媒体路径；关闭时写入完成；目录只读、写盘失败仍能关闭/播放，不留下日志线程等待UI。

完整源码覆盖和仍缺的具体证据见 COVERAGE.md。远程构建配置见 WINDOWS-RUNNER.md；没有注册runner或发起构建。

本批收拢时已实际重跑 `scripts/test-portable.py` 七组全部通过：playback-state、mpv-nodes、player-session、user-data、diagnostic-log、local-input、instance-protocol。单实例纯协议覆盖 Unicode/emoji/UNC、每个截断点、错误头/长度/尾垃圾、NUL/引号/ADS/设备路径、非法代理对及各资源上限；不等同于命名管道在 Windows 上经过测试。`check-source.py` 与 `git diff --check` 通过；手动workflow仅做YAML解析，官方actions tag SHA已从远端核对，未运行Actions。

## 大列表进度投影收尾

源级审阅发现每次 time-pos 更新会比较整个列表，历史采样还复制完整 PlaybackSnapshot。现改为集合 revision 驱动 UI/SMTC 更新，历史只保留 entryId/path/position/duration 标量；纯归约测试确认无关进度/时长事件不改变列表 revision。七组 portable tests 再次通过。此项消除可见的 O(n) 高频比较/拷贝，不代替真实大列表 WinUI 性能测试；列表本身改变时仍需重建对应 UI 项。

## 手动换片事务回归（2026-11-12）

限定 goal：完成可在云端验证的手动换片暂停/恢复事务修复，不扩展为 Windows 播放完成。

- 新回归测试配合修复前 `PlayerSession.cpp`：失败于“重选当前项、旧交换链重新绑定后不得恢复”的断言，证实原缺陷可重现。
- 修复后 `python3 scripts/test-portable.py`：七组通过，player-session 新增同项重播、事件/回复两种顺序、失败和提交拒绝、快速替换、迟到回复、暂停/Stop/Close 取消场景。
- `python3 scripts/check-source.py` 与 `git diff --check`：通过。
- 未执行 Windows/MSVC/XAML、真实 libmpv、GPU/音视频测试。以上测试使用 MpvClient 替身，检验会话发出的命令，不是实际播放证据。

Windows 后续验收：播放 A 后重新双击 A，在加载期间反复缩放；快速选择 B、C；加载损坏/不存在文件；换片尚未就绪时暂停、停止或关闭。记录命令回复、START_FILE/FILE_LOADED 和交换链绑定次序，确认旧画面不提前恢复，失败有错误提示，取消后无意外播放且正常退出。

## 2026-11-12 静态扩展批次

用户明确要求本阶段无需编译、测试与运行。因此本批新增滤镜、键映射、几何、时间/章节模块和 WinUI 接线仅经源码阅读、人工静态审阅；未运行 C++ 编译、portable tests、MSBuild、XAML 或 Windows 应用。旧提交的测试记录不适用于本批。

后续实际验收应覆盖：比例恢复与非法输入、四个旋转角、带标签滤镜添加替换删除及不支持滤镜错误、Unicode 与冒号/反斜杠 LUT 路径、时间精度和章节最后区间、input.conf 导入与键盘分发。未接入 UI 的键映射、滤镜预设持久化和窗口几何要先完成接线，不能直接标为可用功能。

第二批同样未编译/测试/运行。后续需确认音频 EQ 实际滤镜和预设应用、字幕样式/ASS 覆盖关系、外部字幕重载、逐句 seek、同目录匹配排序/中文与非 BMP 名称/网络共享取消/加载切换/关闭竞态，以及轨道元数据类型差异。portable 脚本仅增加新元数据依赖声明，未执行，不代表回归通过。

第三批未编译/测试/运行。后续需验证：M3U8保存覆盖/失败恢复/云文件占用/非ASCII路径，CompactOverlay进入退出与全屏和DPI转换，网络URL解析/取消/加载失败和历史排除，OSD计时取消与状态变化。播放列表计划需实现真正的命令回复后属性刷新执行器后再验证批量编辑；当前只保存功能接入应用。
