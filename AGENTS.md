# Working instructions for this fork

This repository is a FusionFix-based GTA IV VR project. Read `ROADMAP.md` before planning VR work. Preserve the user's existing changes, including the current `readme.md` edit. Do not edit the installed game as a substitute for making source changes in this repository.

Track work in `issues/open/` and `issues/closed/` using `issues/README.md`. Start from an existing open issue or create a numbered one before implementation. Record evidence as work proceeds; move an issue to `closed/` only when its acceptance conditions are met or it is explicitly canceled/superseded.

## Scope and design

- Target the official GTA IV Complete Edition and single-player use. Track GTA IV, TLAD, and TBoGT behavior separately.
- Keep VR opt-in and retain a working flat-screen launch. A missing OpenXR runtime or headset must fail gracefully.
- The ASI is x86. Check the bitness of every native dependency, including any OpenXR loader or graphics bridge.
- Prefer a small VR module with clear ownership of OpenXR resources over scattering session state across unrelated FusionFix fixes.
- Separate game simulation, per-eye scene rendering, and headset submission. Do not call a duplicated frame VR merely because the headset displays it.
- Validate camera matrices, render order, depth, frame timing, and D3D9-to-OpenXR interop experimentally before committing to an architecture.
- Keep upstream fixes easy to merge. Avoid unrelated refactors and changes to installer behavior until needed by a milestone.

## Repository workflow

1. Inspect `git status` before work and leave unrelated edits untouched. Record baseline versions and exact repro steps for runtime bugs.
2. Use short branches and focused commits. Document observations, unknowns, and acceptance evidence against the relevant `ROADMAP.md` milestone.
3. Generate the Visual Studio solution with `premake5.bat` and build x86 Debug first. Build Release before making performance claims. Check `.github/workflows/msvc_x86.yml` when changing the build.
4. For local deployment, `premake5.lua` reads ignored `.env` with `GTAIV_DIR` pointing to the folder containing `GTAIV.exe`. It copies only the built ASI when the target plugin already exists. Back up that plugin and verify the target path before enabling deployment.
5. Test flat-screen behavior after every rendering or camera change. Add focused automated checks for deterministic math or lifecycle state; verify headset behavior manually with a recorded runtime/headset configuration.
6. Include in each change report: what changed, build/test results, game/runtime versions when relevant, observed frame-time data if performance is claimed, and remaining limitations.

## Safety and distribution

- Never commit game files, copyrighted assets, proprietary SDK binaries, headset logs, `.env`, or local build output.
- Do not modify player saves or change Steam/game files outside an explicit deployment or test step. Keep backups for any overwritten ASI or config.
- Preserve the GPL-3.0 license and required notices. Keep the project single-player; do not add multiplayer or online hooks.
- Avoid presenting unverified VR support, comfort, compatibility, or performance as complete.
