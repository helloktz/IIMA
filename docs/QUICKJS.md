# QuickJS-ng 插件执行依赖

## 固定来源和范围

- 源仓库：<https://github.com/quickjs-ng/quickjs>
- 选定版本：`v0.12.1`
- 固定提交：`58bdcf0ce35594df30bf5cfbba3be8b454799cc0`
- 构建目标：上游 CMake `qjs` 静态库，产物 `qjs.lib`，头文件 `quickjs.h`。
- `QJS_BUILD_LIBC=OFF`；不链接 `qjs-libc`，不构建/分发 `qjs.exe`、`qjsc.exe` 或示例。
- 许可证：MIT，保留 Bellard、Gordon、Noordhuis、Ibarra Corretgé 的版权和完整许可。

上游该提交的 `CMakeLists.txt` 使用 C11 并含 MSVC 选项；`.github/workflows/ci.yml` 的 `windows-msvc` 明确覆盖 x64 Debug/Release。这里是对上游构建支持的源码核对，不是 IIMA 固定 MSVC 工具链下的成功构建证据。

IINA 使用 JavaScriptCore。IIMA 选择 QuickJS-ng 是 Windows 原生进程内 JS 引擎适配，不引入 WebView UI，不声明 JavaScriptCore ABI、行为或整个 IINA 插件 API 等价。引擎能力与应用层实际暴露的 API 是两回事。没有链接 `qjs-libc` 也不构成可运行不可信代码的操作系统沙箱；只应运行用户明确选择并信任的本地插件，运行入口和限制另见 `PORTING.md`。

## Windows 后续构建步骤

本轮只写了脚本与工程配置，**没有执行以下构建、编译或运行**。

1. 按 `BUILD.md` 配置 `Directory.Build.props` 固定的 MSVC、Windows SDK 和 PowerShell 7。
2. 准备官方 Git 客户端和官方 [CMake 4.2.0](https://cmake.org/download/)。该版本存在于 Kitware 官方 `v4.2.0` 标签；版本固定是为了避免构建器随宿主升级漂移。脚本不自动安装、更新或从非官方来源获取工具。
3. 使用现有构建入口：

   ```powershell
   ./scripts/build-quickjs.ps1 -Configuration Debug
   ./scripts/build-quickjs.ps1 -Configuration Release
   ./scripts/build.ps1 -Configuration Debug
   ./scripts/build.ps1 -Configuration Release
   ```

   CMake 不在 PATH 时，将实际官方 `cmake.exe` 路径传给 `-CMakePath`。

脚本从官方仓库获取**确切提交**到 `.deps/sources/quickjs-ng-0.12.1`，不追随分支或再次解析可变标签。已有 checkout 必须同 origin、同提交且无已修改或未跟踪文件；不自动覆盖或删除用户改动。中断留下的不完整 checkout 会显式报错，应先检查再自行处理该目录。

使用 `VsDevCmd.bat` 选择仓库固定工具版本和 SDK，CMake 的 `NMake Makefiles` 生成器显式使用该安装的 `Hostx64/x64/cl.exe` 与 `nmake.exe`，无需新增 Ninja 或依赖 CMake 选择某个 VS 实例。上游 C 文件保持 C11，不套用 IIMA 的 C++23。`CMP0091=NEW` 和 `CMAKE_MSVC_RUNTIME_LIBRARY` 分别选择 `/MDd` 与 `/MD`，不跨配置混用静态库。两配置构建目录隔离。

未来 Windows 验证还必须确认 CMake 识别该 MSVC、上游 C11 atomics 支持、库目标路径及运行时匹配。脚本发现构建失败、缺少 `qjs.lib` 或工具版本不符时停止；不会用旧 DLL 或系统自带 JS 引擎冒充本依赖。

## 接入和发布

IIMA.App 在 `Compiler.props` 之后、`Microsoft.Cpp.targets` 之前导入 `build/QuickJS.props`。它加入头文件路径、当前配置静态库并将程序默认栈 reserve 设为 8 MiB；这是上游针对 Windows 默认 1 MiB 栈可能小于 JS 栈预算的要求，不等于每个线程实际提交了 8 MiB 物理内存。

暂存布局：

```text
.deps/quickjs/
  include/quickjs.h
  lib/Debug/qjs.lib
  lib/Release/qjs.lib
  licenses/QuickJS-ng-LICENSE
  Debug-manifest.json
  Release-manifest.json
```

每份 manifest 记录来源提交、工具链、SDK、CMake、配置、架构和运行库，以及实际头文件、库和许可证的 SHA-256。`build.ps1` 在链接前校验配置和这些文件；直接调用 MSBuild 只检查存在性，统一构建入口仍是 `build.ps1`。修改依赖后应重新 stage，不应手改 manifest 绕过失败。

`package.ps1` 仍先执行全新 Release 应用构建，复制 QuickJS-ng 许可证和 Release provenance。由于静态链接，没有待复制的 QuickJS DLL。第三方源码、生成产物和头文件不计入产品 C++ 新增数量；正式发行仍需单独处理项目的 GPL 来源和整体许可材料。
