# 远程 Windows 验证入口

`.github/workflows/windows-validation.yml` 提供手动 GitHub Actions 构建入口，使用预先配置的自托管 Windows runner。

仓库所有者准备 Windows x64 runner，标签 `iima-msvc-v145`，预装 BUILD.md 的固定 MSVC、SDK、PowerShell、WinUI/XAML 组件。`IIMA_LIBMPV_SDK` repository variable 指向 runner 本机已经构建并备齐 manifest/许可证的 SDK 目录，位于工作区外，避免 checkout clean 清理它。SDK 的源码、补丁、架构和 ABI 仍由 staging 脚本验证。

工作流使用完整 SHA 固定官方 checkout v4.2.2 与 upload-artifact v4.6.2；这两个 tag 的 SHA 已通过其官方 Git 远端核对。checkout 不保留 Git 凭据；工作流仅 contents:read，不推送、不发布 Release、不合并。

在 GitHub Actions 中选择该工作流和要验证的分支，再运行 `Run workflow`。任务暂存 SDK、构建 Debug x64、全新构建并打包 Release，保存构建日志和未签名验证 ZIP 七天。远程构建结果待验证。

构建服务进程一般没有交互桌面，工作流故意不调用 GUI 冒烟。下载 ZIP 到独立交互式 Windows 11 x64 环境后，按 VALIDATION.md 执行声音/视频/关闭/DPI 等检查。不要把无桌面的构建成功当作播放验收。

由仓库所有者配置 runner 和仓库变量，并限制自托管 runner 可执行的分支。
