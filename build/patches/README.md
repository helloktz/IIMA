# IIMA private mpv extension: owned composition swapchain v1

Apply `mpv-iima-owned-swapchain-v1.patch` **only** to an independent checkout of mpv `bcde6b93cca4d128d84e1a60d1a6db2cd81c889e`, using `scripts/prepare-mpv-source.ps1`. Never apply it to the reference snapshot. This is an IIMA-maintained modification, not an upstream API or an already validated upstream release.

## Why a private C function, not an owning property

The pinned `display-swapchain` property calls `vo_get_display_swapchain`, which simply returns `vo->display_swapchain`. It does not transfer ownership. Reading that address and later calling `AddRef` on another thread can race teardown. `ra_d3d11_ctx_get_swapchain` does AddRef, but that is a different internal path and does not establish ownership for the property. An owning property would also create references during observation, conversion and cached reads, without a reliable matching Release. The extension therefore has a direct, synchronous, one-request/one-reference contract.

```c
int mpv_iima_acquire_d3d11_swapchain_v1(mpv_handle *ctx, void **out);
```

- Success `0`: `*out` is an `IDXGISwapChain*` with **one owned COM reference**. Consume with `ComPtr::Attach` / `winrt::com_ptr::attach`, not a second AddRef. Release once when no longer needed.
- Error: `*out == NULL`, with `MPV_ERROR_INVALID_PARAMETER`, `MPV_ERROR_UNINITIALIZED`, or `MPV_ERROR_PROPERTY_UNAVAILABLE`. The extension is available as an export on unsupported platforms, but cannot return a chain there.
- Caller must serialize against handle destruction and run this potentially blocking function off the UI thread. A queued callback must retain the owning COM wrapper; dropped callbacks then release safely.
- Ownership prevents use-after-free; it does **not** keep an obsolete chain active. Reacquire after video reconfiguration. Unbind `SwapChainPanel` and release old references on end-file, replacement and shutdown. Presenter must not hold a raw pointer outside an owning wrapper.
- The DLL must stay loaded until all application-held COM references originating from it have been released and the mpv worker has finished destruction.

## Synchronization proof against the fixed source

1. `player/client.c::run_locked` takes `MPContext.dispatch` via `mp_dispatch_lock`. The new request locates `mpctx->video_out` under this same lock used by the existing synchronous property API. The playback core cannot replace/destroy that VO until the callback returns.
2. `video/out/vo.c::vo_control` invokes `mp_dispatch_run(vo->in->dispatch, run_control, ...)`. `misc/dispatch.c::mp_dispatch_run` enqueues the callback and waits for `item.completed`; `run_control` executes `vo->driver->control` on the VO thread.
3. `video/out/vo_gpu_next.c::control` forwards unknown requests to `p->ra_ctx->fns->control`. The D3D11 context installs `d3d11_control` there.
4. The added D3D11 case checks composition mode and non-null current `p->swapchain`, performs `IDXGISwapChain_AddRef`, and returns the address before leaving the VO thread. Chain creation/resize/teardown in this context cannot interleave that operation. The core lock remains held until the VO call returns.
5. Other contexts may return success for unknown controls, so the client wrapper requires both `VO_TRUE` **and** non-null output. No borrow-to-own conversion happens after returning.

No new frame loop or Render API path is introduced; output remains gpu-next + D3D11 composition. The standard mpv ABI and standard headers are unchanged outside the additive private declaration. IIMA can dynamically resolve the named extension without changing its pinned reference headers.

## Validation and license

`git apply --check` passes against the exact reference snapshot in the Linux development environment. Applying to independent copies, checking reverse applicability, and parsing the patched public header with GCC C11 and G++ C++23 also pass. These are patch/header checks, not a full mpv compilation. No Windows compilation, hardware execution, rapid-replacement stress test or race detector has run. These remain required acceptance steps. The proof concerns reference acquisition, not all GPU lifecycle behavior or rendering correctness.

The patch retains each modified upstream file's copyright and license. Modified client API code follows the existing permissive notice; video files retain their LGPL notices. Distribute this patch and the exact corresponding modified source/build materials with the binary in accordance with all applicable licenses. This document does not assert that a licenses directory alone completes distribution obligations.

## Export path

The declaration uses `MPV_EXPORT`, which is `__declspec(dllexport)` on Windows. `player/client.c` sees this declaration through `player/client.h` → `mpv/stream_cb.h` → `mpv/client.h` before the function definition. The fixed Meson `library` target has no export `.def`/symbol whitelist; hidden ELF visibility is overridden by `MPV_EXPORT` on the declaration. Staging verifies the exact named export with `GetProcAddress`; that Windows check has not yet run.
