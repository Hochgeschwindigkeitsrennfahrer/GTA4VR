# 006 — Route two eye views with safe visibility and targets

## Goal

Turn the two-view command boundary measured in #003 into two complete, separately targeted world images for the first OpenXR headset frame. Roadmap stage: 2. Depends on #003 and the graphics bridge decision in #004.

## Tasks

- [ ] Generate two nearby eye `grcViewport` states from runtime pose/FOV with coherent camera, projection, frustum, and dependent constants.
- [ ] Provide a conservative visibility set covering both eyes, or separate per-eye traversal state. Account for the entity/phase frame stamps identified in #003.
- [ ] Route each G-buffer, lighting, and DrawScene command stream to a distinct eye render target; prove depth and color isolation at execution and in captured pixels.
- [ ] Verify whether GTA IV's live D3D9 device and scene target can render or copy into an Ex shareable texture. If not, investigate an Ex-compatible device path or measured transfer fallback before integrating the #004 bridge.
- [ ] Copy each completed eye target through the selected D3D11 bridge into an acquired OpenXR image, with explicit D3D9/D3D11 synchronization and resource recreation on device reset.
- [ ] Test shadows, reflections, deferred-lighting visibility, post-processing, and temporal resources for eye-dependent state. Disable or replace unsafe passes for the first image.
- [ ] Keep one game-process update and a working flat-screen path with VR disabled.

## Done when

Both eye targets contain different, pose-correct world images after one game-process update, without obvious missing geometry from culling. Record the headset/runtime, graphics bridge, target and depth formats, captures, launch/build results, and explicit limitations.

## Notes / evidence

- #003 demonstrated two distinct `grcViewport` command snapshots and render-thread executions for G-buffer and DrawScene, with two lighting list callbacks, after one observed game-process callback. Its second camera was a radar view used as a stress input, and both passes still shared the original visibility work and render-target routing. See [`docs/rendering-pipeline.md`](../../docs/rendering-pipeline.md).
- Static `FUN_00a10140` has an entity/phase frame stamp check; repeating a list callback alone does not rerun the sampled main-world visibility path. A union-frustum or independent-traversal strategy must be verified here.
- #004's [standalone bridge design](../../docs/openxr-graphics-bridge.md) proves a D3D9Ex → D3D11 → OpenXR swapchain pixel path on the user's SteamVR setup. It does not prove that the game's existing D3D9 device or targets are shareable, nor that a copied image is a correct stereo scene.
