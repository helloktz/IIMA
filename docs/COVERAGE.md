# HANDOFF 覆盖与剩余工作

这是源码完成度表，不是 Windows 验收通过表。所有产品运行结论仍需对应 Windows 证据。

| HANDOFF 项 | 当前代码/入口 | 验证与具体剩余 |
|---|---|---|
| A 固定工程、XAML 生成、构建打包 | 原 sln/vcxproj、App/MainWindow XAML/IDL、build/package | 本地 Debug 和 Release x64 构建、打包、启动与关闭通过 |
| B libmpv 依赖 | prepare-mpv-source/stage-libmpv/test-libmpv、固定源与生产补丁 | 已构建暂存 x64 SDK，ABI、导出和依赖加载探针通过 |
| B 打开、异步状态、关闭 | MpvClient / PlayerSession / PlaybackState | 中文本地文件、实际音频会话、多窗口收尾已验收 |
| B gpu-next/D3D11/SwapChainPanel | 私有 owned-swapchain 同步获取、VideoPresenter | 在 RTX 3060 Ti 观察到画面和 D3D11VA；设备丢失及跨 DPI 屏移动待验收 |
| B 暂停/继续/seek/换片/缩放/全屏/错误 | 同一会话命令及原生控件 | 本机验证暂停/继续、seek、切换、缩放、全屏和损坏媒体处理 |
| C 播放列表 | 观察 mpv playlist，编辑/上下项/双击/移动 | 已验证批量追加、下一项、损坏条目后的自动推进、300 项 M3U8 导入及本地音频清单按偏好自动播放。导入保留重复项 |
| C 音轨/字幕/章节/倍速/音量/截图 | observed nodes +命令+WinUI控件/SaveFilePicker | 两条 AAC 轨可选，中文 SubRip、章节跳转、2 倍速、静音、音量调节和 PNG 截图另存已验证 |
| C 快捷键/控制栏统一入口 | MainWindow键盘及SMTC均进入PlayerSession | 代码已接线；焦点/无障碍实际体验待交互桌面 |
| C 设置 | 类型化默认/验证、WinUI设置、版本化JSON | 已覆盖常用播放策略/循环/延迟/字幕/音量/截图；不是逐项复制IINA所有偏好，未移植macOS专属、插件/网络服务偏好 |
| C 历史/续播 | UserDataStore / RecordHistory / ResumePosition | 关闭后写入 JSON，重新打开从已保存位置继续。持久化异常和历史编辑按具体改动复验 |
| C 文件/目录/播放列表输入 | LocalInputResolver + LocalInputPolicy | 过滤/排序测试通过；真实Unicode/权限/reparse/取消待Windows；不下载URL、不解析macOS别名 |
| D 文件关联/激活 | HKCU候选注册/撤销及系统默认设置；同登录会话单实例路由 | 连续三次命令行输入转发到同一进程。隔离注册表验证 18 种扩展名的候选注册和撤销；系统默认应用界面与管道权限边界待验收 |
| D 系统媒体/防休眠 | HWND SMTC、PowerCreateRequest | Windows 媒体会话暂停、跳转、继续与关闭移除已验证。物理媒体键、防休眠待验收 |
| D 日志 | 有界后台DiagnosticLog、双文件轮转 | 轮转/flush/关闭逻辑测试；只记录技术状态，不写媒体路径/凭据；Windows接线待运行 |
| D HDR/多显示器/音频设备/睡眠恢复 | libmpv管理核心输出，应用已有链重绑/错误暂停 | 已记录单显示器研发环境，其他硬件场景按交付承诺和缺陷另行验证 |
| E 远程构建 | 手动 Windows workflow + WINDOWS-RUNNER.md | 没有注册runner/账号权限或运行任务；需要匹配固定工具链的Windows机器和SDK |
| E 可分发ZIP | package.ps1携依赖/许可证/补丁 | 已生成研发 ZIP，独立解压、系统 PATH 和运行库加载路径验证通过 |
| E 许可证/安装/签名/更新 | 保留上游来源许可，未发布 | 项目整体许可、签名身份和分发方式待确定 |

## 范围说明

IINA 插件与第三方扩展机制不纳入移植范围。产品所需功能直接使用 Windows 原生接口实现。

本机验收范围和硬件见 [VALIDATION.md](VALIDATION.md)。状态、序列化、过滤及日志规则另有独立逻辑测试。
