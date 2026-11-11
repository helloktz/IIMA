# libmpv 运行依赖

## 固定范围与当前证据

主程序编译采用 `references/mpv/include/mpv/client.h`，源提交固定为 `bcde6b93cca4d128d84e1a60d1a6db2cd81c889e`，C API 2.6。运行 DLL 也要求从该提交加 `build/patches/mpv-iima-owned-swapchain-v1.patch` 构建；参考快照不参与第三方构建。生产二进制、构建工具版本与传递依赖尚未取得，不能把参考提交视为已经验证的发行 SDK。

运行时采用 `LoadLibraryEx` + 导出函数指针调用 C ABI，不链接 MinGW `.a`，不需要 MSVC 导入库。DLL 固定为 app-local `libmpv-2.dll`，连同其运行依赖放在应用目录。mpv 返回的内存由 mpv 释放，不能由主程序 CRT 释放。

## 上游支持的构建路线

依据本提交 `references/mpv/DOCS/compile-windows.md` 的 MSYS2/CLANG64、Building libmpv 与 Linking libmpv with MSVC Programs 段。mpv 不支持使用 MSVC cl.exe 构建自身。准备独立的 CLANG64 环境及上游文档列出的 Meson、pkgconf、FFmpeg、libplacebo、libass 等依赖；不要在 `references/` 内构建，也不要为主程序换工具链。工具及传递依赖版本必须记录在 SDK manifest 中，当前尚未建立可重现的完整依赖锁。

先执行 `pwsh -NoProfile -File scripts/prepare-mpv-source.ps1`，将固定上游源码检出到 `.deps/mpv-source` 并应用私有 owned-swapchain 补丁。该脚本不覆盖已有改动、不安装工具链、不运行构建。然后在 CLANG64 的该独立源码目录中执行（这是待执行方案，不是成功构建记录）：

```sh
meson setup build -Dlibmpv=true -Ddefault_library=shared
meson compile -C build
```

检查 Meson 配置实际启用 gpu-next/libplacebo 和 D3D11；不要仅因配置命令成功就认定图形后端可用。按上游说明收集 `libmpv-2.dll` 和全部非系统运行 DLL。记录 `meson configure build`、工具链版本、依赖版本和构建日志。最终还需在无开发环境 PATH 的干净 Windows 镜像执行加载与播放验证。

## SDK 输入与验证

准备以下目录；只使用可信上游构建产物。探针会执行 DLL 的初始化代码。

```text
sdk/
  bin/libmpv-2.dll
  bin/<全部非系统依赖>.dll
  licenses/<mpv 与全部依赖许可、源码获取材料>
  manifest.json
```

`manifest.json` 必须含 `sourceCommit`（上述完整 SHA）、`sourceUrl`（上游地址）、`buildCommand`、`toolchain`、`dependencies`、`sourcePatch`（`mpv-iima-owned-swapchain-v1`）及 `patchSha256`（准备脚本打印的实际值）；填写实际版本与来源，不填 `latest` 或虚构已经成功的构建。保留完整源码及发行义务所需材料；仅复制许可证文本并不能自动满足所有依赖的分发要求。

```powershell
pwsh -NoProfile -File scripts/stage-libmpv.ps1 -SdkDirectory '<准备好的 sdk 目录>'
pwsh -NoProfile -File scripts/build.ps1 -Configuration Debug
pwsh -NoProfile -File scripts/package.ps1
```

staging 在 x64 Windows PowerShell 中检查所有 DLL 的 AMD64 PE 标记、所需导出、实际 C ABI 主版本 2/次版本至少 6、`mpv_create`/`mpv_initialize`，以及 gpu-next、D3D11 composition 与尺寸选项是否接受。另要求 `mpv_iima_acquire_d3d11_swapchain_v1` 导出存在并检查空闲状态的返回契约。DLL 搜索只允许其自身目录和 System32，不依赖 PATH。通过后写入 `.deps/libmpv/` 并保存二进制哈希，构建前复核。已有 SDK 不自动覆盖，先将其移走再重新 staging，避免残留旧 DLL。

探针没有加载视频，不能证明 GPU 创建、`display-swapchain`、声音、HDR、硬件解码或 ABI 所有路径正确。必须继续执行 docs/VALIDATION.md 的原生运行检查。尚未在本次 Linux 云环境执行上述 PowerShell 脚本；当前只完成源码审阅及静态检查。

## 私有交换链扩展

安全所有权和同步调用链见 [补丁说明](../build/patches/README.md)。上游原始 `display-swapchain` 是借用地址，不直接用于 UI；私有 C API 在 VO 线程 AddRef 后转移所有权。标准 libmpv 没有该符号时不回退不安全路径，需重新构建 SDK。已完成补丁适用性检查；本次没有原生运行证据。
