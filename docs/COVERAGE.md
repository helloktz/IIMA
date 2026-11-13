# HANDOFF 覆盖与剩余工作

这是源码完成度表，不是 Windows 验收通过表。所有产品运行结论仍需对应 Windows 证据。

| HANDOFF 项 | 当前代码/入口 | 验证与具体剩余 |
|---|---|---|
| A 固定工程、XAML 生成、构建打包 | 原 sln/vcxproj、App/MainWindow XAML/IDL、build/package | 本地 Debug 和 Release x64 构建通过，Release 正常启动和关闭。打包待补丁版 libmpv |
| B libmpv 依赖 | prepare-mpv-source/stage-libmpv/test-libmpv、固定源与生产补丁 | 源补丁/头契约已查；没有实际 Windows x64 DLL/依赖闭包，需按文档构建并记录真实依赖版本 |
| B 打开、异步状态、关闭 | MpvClient / PlayerSession / PlaybackState | 纯逻辑与命令测试通过；真实事件顺序、声音、关闭线程需 DLL+Windows |
| B gpu-next/D3D11/SwapChainPanel | 私有 owned-swapchain 同步获取、VideoPresenter | 所有权协议和 DPI 代码已实现；GPU/WinUI 实测未做，不能保证正确呈现或设备恢复 |
| B 暂停/继续/seek/换片/缩放/全屏/错误 | 同一会话命令及原生控件 | 逻辑契约测试通过；原生完整操作链未运行 |
| C 播放列表 | 观察 mpv playlist，编辑/上下项/双击/移动 | 索引及队列逻辑已测试；大列表性能、真实自动推进/EOF 待运行 |
| C 音轨/字幕/章节/倍速/音量/截图 | observed nodes +命令+WinUI控件/SaveFilePicker | node深拷贝/命令测试通过；字幕渲染、音频效果、截图文件待运行 |
| C 快捷键/控制栏统一入口 | MainWindow键盘及SMTC均进入PlayerSession | 代码已接线；焦点/无障碍实际体验待交互桌面 |
| C 设置 | 类型化默认/验证、WinUI设置、版本化JSON | 已覆盖常用播放策略/循环/延迟/字幕/音量/截图；不是逐项复制IINA所有偏好，未移植macOS专属、插件/网络服务偏好 |
| C 历史/续播 | UserDataStore / RecordHistory / ResumePosition | 纯规则测试通过；Windows JSON/磁盘损坏/权限/对话框待运行 |
| C 文件/目录/播放列表输入 | LocalInputResolver + LocalInputPolicy | 过滤/排序测试通过；真实Unicode/权限/reparse/取消待Windows；不下载URL、不解析macOS别名 |
| D 文件关联/激活 | HKCU候选注册/撤销及系统默认设置；同登录会话单实例路由 | 代码已写；真实注册/清理/并发启动/pipe授权待Windows，不强改UserChoice |
| D 系统媒体/防休眠 | HWND SMTC、PowerCreateRequest | 接线代码完成；媒体键/系统UI/powercfg检查待Windows |
| D 日志 | 有界后台DiagnosticLog、双文件轮转 | 轮转/flush/关闭逻辑测试；只记录技术状态，不写媒体路径/凭据；Windows接线待运行 |
| D HDR/多显示器/音频设备/睡眠恢复 | libmpv管理核心输出，应用已有链重绑/错误暂停 | 需要实际所选DLL、设备与事件证据才能确定恢复策略，不能凭接口名称宣称HDR/零复制/设备恢复已支持；不盲目改渲染后端 |
| E 远程构建 | 手动 Windows workflow + WINDOWS-RUNNER.md | 没有注册runner/账号权限或运行任务；需要匹配固定工具链的Windows机器和SDK |
| E 可分发ZIP | package.ps1携依赖/许可证/补丁 | 尚无实际本批ZIP，也没有干净Windows镜像；不得以源码完成替代交付 |
| E 许可证/安装/签名/更新 | 保留上游来源许可，未发布 | 项目整体许可、签名身份和分发方式待确定 |

## 范围说明

IINA 插件与第三方扩展机制不纳入移植范围。产品所需功能直接使用 Windows 原生接口实现。

当前尚不能将任何 Windows 图形功能称为“已验收”。Linux 可验证的状态/序列化/过滤/日志规则已有测试；剩余硬件依赖问题必须取得明确复现后再改，避免凭猜测替换既定架构。
