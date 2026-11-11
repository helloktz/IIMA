# 远程 Windows 验证入口

`.github/workflows/windows-validation.yml` 提供**手动** GitHub Actions 构建入口；没有注册或启动 runner，没有把托管 Windows 镜像误称为具备固定工具链。

仓库所有者准备 Windows x64 runner，标签 `iima-msvc-v145`，预装 BUILD.md 的固定 MSVC、SDK、PowerShell、WinUI/XAML 组件。`IIMA_LIBMPV_SDK` repository variable 指向 runner 本机已经构建并备齐 manifest/许可证的 SDK 目录，位于工作区外，避免 checkout clean 清理它。SDK 的源码、补丁、架构和 ABI 仍由 staging 脚本验证。

工作流使用完整 SHA 固定官方 checkout v4.2.2 与 upload-artifact v4.6.2；这两个 tag 的 SHA 已通过其官方 Git 远端核对。checkout 不保留 Git 凭据；工作流仅 contents:read，不推送、不发布 Release、不合并。

在 GitHub Actions 中选择该 workflow 与开发分支，再手动 Run workflow：执行 SDK staging、Debug x64 构建、干净 Release 打包，保存 binlog 和未签名验证 ZIP 七天。尚未运行，不能据此宣布远程构建通过。新 workflow 若仅在开发分支、尚未存在于默认分支，GitHub 的网页发现/dispatch 入口可能暂不提供；不得为出现按钮擅自合并 main。

构建服务进程一般没有交互桌面，工作流故意不调用 GUI 冒烟。下载 ZIP 到独立交互式 Windows 11 x64 环境后，按 VALIDATION.md 执行声音/视频/关闭/DPI 等检查。不要把无桌面的构建成功当作播放验收。

注册 runner、仓库变量与任何长期访问凭据由用户另行配置；本次只添加可审阅配置，没有改变这些账号或机器权限。不要在自托管 runner 上执行不可信分支代码。
