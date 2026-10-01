# 004 — Select an OpenXR graphics bridge

## Goal

Prove a viable path from the 32-bit D3D9 game to OpenXR swapchain images. Roadmap stage: 1. Can proceed alongside #003 after #001.

## Tasks

- [x] Record available OpenXR runtime/headset and verify whether the required x86 loader and graphics binding are usable.
- [x] Compare realistic D3D9-to-D3D11 interop and DXVK/Vulkan paths against latency, synchronization, device reset, and packaging constraints.
- [x] Prototype the smallest texture transfer or shared-resource operation needed to establish feasibility; record actual format and synchronization behavior.
- [x] Choose one first implementation path and document why, including fallback behavior when no runtime is present.

## Done when

A design note records a tested graphics path, architecture and bitness requirements, a minimal observed result, and remaining risks. If the available headset/runtime cannot support x86, document that blocker and create a follow-up issue for the chosen alternative.

## Notes / evidence

- FusionFix hooks D3D9 and exposes a DXVK graphics option. OpenXR does not take a D3D9 render target as a native swapchain image.
- On 2026-10-01, the x86 SteamVR/OpenXR 2.17.10 runtime exposed `XR_KHR_D3D11_enable`; a D3D11 session and BGRA8 sRGB swapchain succeeded with the user's SteamVR headset. The x86 loader was built from Khronos OpenXR SDK commit `f2448a8797c85814aa892efc1ab8707900fbcc78`.
- A standalone x86 probe on the NVIDIA GeForce RTX 4070 Ti matched the OpenXR/D3D11 and D3D9Ex adapter LUIDs, cleared a shared D3D9Ex render texture, waited for a D3D9 event query, opened it in D3D11, copied into an acquired OpenXR image, and read BGRA `CC 66 33 FF` back from that image. Exit code 0. The game device, headset display, and frame latency remain untested.
- A scoped invalid `XR_RUNTIME_JSON` caused `XR_ERROR_RUNTIME_UNAVAILABLE` (`-51`) and probe exit code 1, with no session created. This establishes a detectable failure for #005's flat-screen fallback; the game fallback itself remains to be implemented.
- [Design note](../../docs/openxr-graphics-bridge.md) compares D3D9Ex/D3D11 and DXVK/Vulkan, records limitations, and chooses the tested x86 D3D9Ex/D3D11 path. [Probe sources and build steps](../../tools/bridge-probe/README.md) reproduce the observation.
- Closed 2026-10-01. The required acceptance evidence is in the design note. Game-device sharing and reset are explicit #006 integration gates, not results claimed by this probe.
