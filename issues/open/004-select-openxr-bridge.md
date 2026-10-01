# 004 — Select an OpenXR graphics bridge

## Goal

Prove a viable path from the 32-bit D3D9 game to OpenXR swapchain images. Roadmap stage: 1. Can proceed alongside #003 after #001.

## Tasks

- [ ] Record available OpenXR runtime/headset and verify whether the required x86 loader and graphics binding are usable.
- [ ] Compare realistic D3D9-to-D3D11 interop and DXVK/Vulkan paths against latency, synchronization, device reset, and packaging constraints.
- [ ] Prototype the smallest texture transfer or shared-resource operation needed to establish feasibility; record actual format and synchronization behavior.
- [ ] Choose one first implementation path and document why, including fallback behavior when no runtime is present.

## Done when

A design note records a tested graphics path, architecture and bitness requirements, a minimal observed result, and remaining risks. If the available headset/runtime cannot support x86, document that blocker and create a follow-up issue for the chosen alternative.

## Notes / evidence

- FusionFix hooks D3D9 and exposes a DXVK graphics option. OpenXR does not take a D3D9 render target as a native swapchain image.
