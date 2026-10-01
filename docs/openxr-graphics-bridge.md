# OpenXR graphics bridge decision (issue 004)

**First implementation path:** Windows x86 D3D9Ex shared render texture → x86 D3D11 device on the same adapter → copy into an acquired `XR_KHR_D3D11_enable` swapchain image. Keep it opt-in and start with the system D3D9 backend. This is a bridge decision and a standalone transfer result, not a claim that GTA IV can yet render into the shared texture or that a frame has appeared in the headset.

## Observed on 2026-10-01

- Runtime: SteamVR/OpenXR 2.17.10, `SteamVR/OpenXR : lighthouse` HMD system. The machine registers `steamxr_win32.json` for 32-bit OpenXR applications; the manifest points to SteamVR's x86 `vrclient.dll`. The x86 Khronos static OpenXR loader built from SDK commit `f2448a8797c85814aa892efc1ab8707900fbcc78` loaded it. The runtime accepted OpenXR API 1.0; a request for 1.1 returned `XR_ERROR_API_VERSION_UNSUPPORTED` (`-4`).
- GPU: NVIDIA GeForce RTX 4070 Ti. SteamVR's D3D11 requirement specified adapter LUID `00000000:00015996` and minimum feature level `0xB000` (11.0). The x86 D3D11 device and a separate x86 D3D9Ex device matched that LUID.
- `XR_KHR_D3D11_enable` was available. A D3D11 session and 64×64 `DXGI_FORMAT_B8G8R8A8_UNORM_SRGB` swapchain succeeded. The runtime listed nine formats, including BGRA8 sRGB (`91`), but not BGRA8 UNORM (`87`). The acquired D3D11 image reported typeless BGRA8 (`90`).
- The D3D9Ex probe created a shared `D3DFMT_A8R8G8B8` render texture, cleared it to ARGB `FF3366CC`, waited for a D3D9 event query, opened the shared resource in D3D11, and copied it into the acquired OpenXR image. A staging readback **from that OpenXR image** returned BGRA `CC 66 33 FF`; the image was released and all OpenXR objects destroyed. The standalone executable exited 0.
- With `XR_RUNTIME_JSON` set only for the probe process to a nonexistent manifest, extension enumeration returned `XR_ERROR_RUNTIME_UNAVAILABLE` (`-51`) and the executable exited 1 without creating a session.

The readback was deliberately used as a correctness check, not as the proposed production transfer path. This test did not submit a frame, display an image in the headset, use the game's D3D9 device, prove stereo rendering, or measure copy cost/frame latency. The probe's one-shot event wait does not establish safe repeated cross-device ownership; production code needs an explicit synchronization and reset strategy. Microsoft documents that D3D9Ex/D3D11 shared surfaces require application-managed synchronization ([surface sharing guidance](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/surface-sharing-between-windows-graphics-apis)).

## Options and decision

| Path | Fit for this fork | Cost and risk to test next |
| --- | --- | --- |
| System D3D9Ex → D3D11 → OpenXR | OpenXR's D3D11 binding and shared surface copy passed on the available x86 runtime/GPU. FusionFix's system D3D9 path can remain the initial graphics mode. | At least one GPU copy per eye; latency and synchronization are unmeasured. GTA IV's existing device may be ordinary D3D9, and its render target may not be shareable. Reset/recreation must release both devices' resources in order. |
| FusionFix DXVK → Vulkan → OpenXR | Potentially avoids the D3D9Ex/D3D11 route if a compatible Vulkan image/device can be reached. FusionFix can select `vulkan.dll` instead of system `d3d9.dll`; DXVK implements D3D9 over Vulkan ([DXVK](https://github.com/doitsujin/dxvk)). | No x86 Vulkan OpenXR session or DXVK image export was tested. Mapping DXVK's internal device/resources and synchronization into an OpenXR Vulkan binding could tie this fork to DXVK internals; latency and reset behavior are unknown. |

Choose D3D9Ex/D3D11 for the first headset image because its required x86 OpenXR binding and complete sample-pixel path were observed here. Do not claim a performance advantage until both paths are measured. OpenXR's D3D11 graphics binding supplies the session device and D3D11 swapchain images ([OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.1-khr/html/xrspec.html)).

## Integration gates

1. In #005, load the x86 OpenXR loader only when VR is enabled. If no loader, runtime, HMD, D3D11 extension, compatible LUID, or required format exists, log the specific failure, release partial objects, and continue flat-screen. Do not create a second D3D11 device on the wrong adapter. Request API 1.0 initially and negotiate newer versions separately.
2. Before #006 transfers a game image, identify whether GTA IV's actual D3D9 device is Ex and whether the scene color target can be rendered or copied into a shareable D3D9Ex target. If it cannot, investigate an Ex-compatible game-device path or a measured fallback; do not silently assume the standalone probe's device is equivalent. Verify color-space conversion when copying UNORM source into the runtime's sRGB/typeless target.
3. For #006, tie D3D9 completion and D3D11 copy/submission to explicit per-image ownership. Test both eyes' color and depth isolation, device reset, swapchain recreation, runtime loss, alt-tab, and flat-screen operation. Measure copy and synchronization time under an actual game frame before making performance claims. A copied flat frame alone is not stereo VR.

## Reproduction

The tracked, standalone sources and build instructions are in [`tools/bridge-probe/`](../tools/bridge-probe/README.md). They do not load or modify the game. The SDK checkout, compiled loader, executables, and logs stay under ignored `build/`.
