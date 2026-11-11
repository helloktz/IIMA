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
