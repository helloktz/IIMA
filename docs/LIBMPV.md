# libmpv 运行依赖

## 固定范围与当前证据

主程序编译采用固定提交 `bcde6b93cca4d128d84e1a60d1a6db2cd81c889e` 的头文件，C API 2.6。运行 DLL 要求从该提交加 `build/patches/mpv-iima-owned-swapchain-v1.patch` 构建，参考快照不参与第三方构建。本地已构建并暂存 x64 SDK，包含 `libmpv-2.dll` 和 35 个运行依赖。实际版本、导入及来源记录在 `.deps/libmpv/manifest.json`，对应 ZIP 中的 `libmpv-manifest.json`。

运行时采用 `LoadLibraryEx` + 导出函数指针调用 C ABI，不链接 MinGW `.a`，不需要 MSVC 导入库。DLL 固定为 app-local `libmpv-2.dll`，连同其运行依赖放在应用目录。mpv 返回的内存由 mpv 释放，不能由主程序 CRT 释放。

## 上游支持的构建路线

依据本提交 `references/mpv/DOCS/compile-windows.md` 的 MSYS2/CLANG64、Building libmpv 与 Linking libmpv with MSVC Programs 段。在 CLANG64 中用 Clang 和 Meson 单独构建 mpv，IIMA 主程序继续使用 MSVC。构建输出放在 `.deps/`。SDK 记录工具及传递依赖版本，MSYS2 提供的 DLL 附带精确的软件包和对应源码配方地址。

先执行 `pwsh -NoProfile -File scripts/prepare-mpv-source.ps1`，将固定上游源码检出到 `.deps/mpv-source` 并应用私有 owned-swapchain 补丁。该脚本不覆盖已有改动、不安装工具链、不运行构建。

初次本地 SDK 构建选用 FFmpeg 正式版 `9.0.2`（`946fcce07b6dcd0331c8cc609192aeff5e1924f8`）。在 CLANG64 中准备以下工具与库。保留 SDK manifest 中记录的版本用于复现。

```sh
p=$MINGW_PACKAGE_PREFIX
pacman -S --needed diffutils make "$p-clang" "$p-lld" "$p-pkgconf" "$p-meson" "$p-nasm" \
    "$p-libass" "$p-lcms2" "$p-libjpeg-turbo" "$p-libplacebo" \
    "$p-vulkan-headers" "$p-dav1d" "$p-zimg"
```

从仓库根目录执行，确保 Git 在 CLANG64 的 PATH 中。FFmpeg 单独装入 `.deps`。网络使用 SChannel，保留本地播放、D3D11VA 及常用字幕和滤镜的依赖。

```sh
git -c core.autocrlf=false clone --depth 1 --branch n9.0.2 https://github.com/FFmpeg/FFmpeg.git .deps/ffmpeg-source
export CC=clang CXX=clang++
ffmpeg_prefix="$(cygpath -m "$PWD/.deps/ffmpeg-install")"
mkdir -p .deps/ffmpeg-build
cd .deps/ffmpeg-build
../ffmpeg-source/configure --prefix="$ffmpeg_prefix" --arch=x86_64 --target-os=mingw32 \
    --cc=clang --cxx=clang++ --ld=clang --ar=llvm-ar --nm=llvm-nm \
    --ranlib=llvm-ranlib --strip=llvm-strip --extra-ldflags=-fuse-ld=lld \
    --enable-shared --disable-static --disable-programs --disable-doc \
    --disable-debug --disable-autodetect \
    --enable-w32threads --enable-schannel --enable-d3d11va --enable-dxva2 \
    --enable-zlib --enable-libdav1d --enable-libzimg \
    --enable-libass --enable-libfreetype --enable-libfribidi --enable-libharfbuzz \
    --enable-vulkan --enable-libplacebo
make -j12
make install

cd ../mpv-source
export PKG_CONFIG_PATH="$ffmpeg_prefix/lib/pkgconfig"
meson setup build --buildtype=release -Dlibmpv=true -Dcplayer=false \
    -Ddefault_library=shared -Dd3d11=enabled -Dwasapi=enabled \
    -Dlua=disabled -Djavascript=disabled -Dcplugins=disabled
meson compile -C build
```

检查 Meson 配置实际启用 gpu-next/libplacebo、D3D11 和 WASAPI。用 `llvm-readobj --coff-imports` 从 `libmpv-2.dll` 递归检查导入表，从 FFmpeg 安装目录及 CLANG64 `bin/` 收集非系统 DLL。记录 `meson configure build`、工具链版本、依赖版本和构建日志。交付检查使用独立解压目录和系统 PATH，核对实际加载路径。媒体依赖或加载方式变更后复验代表性样片。

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

staging 在 x64 Windows PowerShell 中检查所有 DLL 的 AMD64 PE 标记、所需导出、实际 C ABI 主版本 2/次版本至少 6、`mpv_create`/`mpv_initialize`，以及 gpu-next、D3D11 composition 与尺寸选项是否接受。禁用脚本运行时的构建会省略 `load-scripts`、`osc`，探针和主程序在选项存在时将其设为 `no`。另要求 `mpv_iima_acquire_d3d11_swapchain_v1` 导出存在并检查空闲状态的返回契约。DLL 搜索只允许其自身目录和 System32，不依赖 PATH。通过后写入 `.deps/libmpv/` 并保存二进制哈希，构建前复核。已有 SDK 不自动覆盖，先将其移走再重新 staging，避免残留旧 DLL。

探针覆盖 DLL 加载、初始化和选项接受。视频呈现、声音、HDR 和硬件解码按 [VALIDATION.md](VALIDATION.md) 在 Windows 上分别验证。

## 私有交换链扩展

安全所有权和同步调用链见 [补丁说明](../build/patches/README.md)。上游原始 `display-swapchain` 是借用地址，私有 C API 在 VO 线程 AddRef 后转移所有权。staging 要求 DLL 导出该函数。原生运行结果见 [VALIDATION.md](VALIDATION.md)。
