# 初始化构建依赖

IIMA 版本由 `Directory.Build.props` 的 `IimaVersion` 定义。当前为 `0.1.0`，属于播放集成研发产物，尚未完成 Windows 播放验证。

| 项目 | 固定版本 / 选择 | 记录位置 |
|---|---|---|
| 主程序工具链 | MSVC v145，14.51.36231 | Directory.Build.props |
| 语言模式 | C++23，MSVC `/std:c++23preview` | build/Compiler.props 的 stdcpp23 |
| Windows SDK | 10.0.26100.0；目标最低声明 10.0.22000.0 | Directory.Build.props |
| Windows App SDK 组件 | 2.6.5，WinUI / Base / Foundation / InteractiveExperiences / Runtime | src/IIMA.App/packages.config |
| C++/WinRT | 3.0.260818.1 | src/IIMA.App/packages.config |
| NuGet CLI | 8.0.0 | Directory.Build.props |
| SDK 构建工具、WebView2 等传递依赖 | 完整精确版本见 packages.config | src/IIMA.App/packages.config |
| libmpv | 源码固定 bcde6b93cca4d128d84e1a60d1a6db2cd81c889e；C ABI 2.6 + IIMA owned-swapchain v1 私有补丁；x64 动态加载 libmpv-2.dll | docs/LIBMPV.md；具体构建工具与传递依赖由 SDK manifest.json 记录 |

仅使用需要的 Windows App SDK 组件包，未引入完整聚合包的 AI/ML 等组件。WebView2 是 WinUI 的上游传递依赖；IIMA 没有 WebView 界面或浏览器业务，也不安装 WebView2 浏览器运行时。

NuGet 包来自 https://api.nuget.org/v3/index.json，NuGet CLI 来自微软的 https://dist.nuget.org/。包版本和显式原生 MSBuild 导入分别保存在 packages.config 与 build/NuGet.props、build/NuGet.targets；更新时同时修改并重建验证。

打包脚本复制依赖包根目录中的许可/NOTICE 文件，并附带构建包清单。VC++ CRT 来自固定 MSVC 安装中的 Release redist 目录，其使用与分发遵循对应 Visual Studio 许可。此开发产物不代表项目根许可证或正式发行审核已经完成。

参考资料：

- [WinUI 官方 C++ 模板结构](https://github.com/microsoft/WindowsAppSDK/tree/main/dev/Templates/Source/ProjectTemplates/Desktop/CppWinRT/SingleProjectPackagedApp)
- [MSVC 语言标准开关](https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version)
- [Windows App SDK 部署](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/deploy-overview)
- [VC++ 本地部署](https://learn.microsoft.com/en-us/cpp/windows/walkthrough-deploying-a-visual-cpp-application-to-an-application-local-folder)

libmpv SDK 未随源码提供，当前云端没有可运行的 Windows DLL。scripts/stage-libmpv.ps1 对人工准备的上游构建产物执行 PE、导出、API 与选项探针，并保存 DLL 哈希防止打包前被替换。build.ps1 在 SDK 可用时复制运行 DLL；package.ps1 缺 SDK 时明确失败。动态调用只跨 C ABI，主程序无 MinGW 导入库依赖。探针不证明 D3D11 呈现成功。

## 插件 JavaScript 引擎（静态接入，未构建运行）

QuickJS-ng `v0.12.1`，固定官方提交 `58bdcf0ce35594df30bf5cfbba3be8b454799cc0`，使用上游 C11 `qjs` 静态目标。构建器固定 CMake `4.2.0`，MSVC、SDK 和 CRT 随本仓库固定工具链，配置分别 `/MDd` 和 `/MD`。`scripts/build-quickjs.ps1` 显式 stage，`build/QuickJS.props` 链接，`build.ps1` 验证 provenance 与文件哈希，`package.ps1` 携带 MIT 许可证。详见 [QUICKJS.md](../docs/QUICKJS.md)。不链接 `qjs-libc`，不引入 WebView UI，不代表全部 IINA 插件 API 已兼容。
