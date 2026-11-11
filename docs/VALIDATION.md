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
