# VR pause-menu tab

Work: [GitHub #8](https://github.com/bordplate/GTA4VR/issues/8), roadmap milestones 4 and 5.

The **VR** tab is registered after **Graphics** using the native menu extension API. It owns its rows and its vehicle submenu; the existing tabs and preference IDs remain intact. Native pointer, keyboard and gamepad navigation use the same path. `vrsettings.ixx` registers presentation, while `vrconfig.ixx` owns persisted configuration snapshots.

## Settings

| Page | Controls | Application |
| --- | --- | --- |
| VR | Recenter, standing eye height/calibration, physical crouch, visible body/body placement | Simulation/camera/rig owners after resume |
| VR | HUD width/distance, watch size and offsets | Render owner |
| VR | Diegetic ammo counters | Live global visibility; individual weapon profiles retained |
| Vehicle camera and radar | Seated eye offset, dashboard size/position/tilt | Camera/render owners |
| VR | Eye resolution scale and per-eye dimension preview | Explicit Apply in the active paused session |
| VR | Enable VR on next launch | Next game launch |

Sliders have live numeric values to the right; scale also shows the resulting per-eye dimensions below. Empty native rows separate groups. Instructions, calibration outcome and option descriptions use the right-hand help area, not selectable status rows.

Each changed value writes its existing `[VR]` INI entry. Other tuning, controller bindings and weapon profiles are retained. Menu slider ranges are presentation ranges; opening the tab never clamps or overwrites a finite INI value outside them. Dimensions and standing height must be positive. Offsets/angles accept finite signed values.

Gameplay text HUD size is independent of distance: its full width is `4 * ViewHudWidth` metres, preserving the original sizing at the default 2 metre distance. Moving it farther away makes it appear smaller. These controls affect gameplay subtitles/prompts, not the separate loading/pause panel. `[VR] DiegeticAmmoCounters=0` hides weapon displays and disables their edit grabs without changing weapon ammunition or profile placement.

## Standing calibration

Stand upright with the headset worn normally, using a runtime with a calibrated floor. Select **Calibrate standing eye height**. The saved reference is the mean eye height above OpenXR's STAGE floor, not total height to the top of the head. The page shows that reference and the derived 65% crouch-entry / 75% standing thresholds.

Calibration is explicit. It does not run at startup, on recenter, or automatically while crouching. Do not calibrate while seated; vehicle eye offset is a separate control. A LOCAL fallback, untracked position, lost focus or pose older than 250 ms leaves the existing height unchanged and displays a failure message. Manual height entry remains available without floor tracking.

## Resolution Apply

The render owner handles a queued scale change between frames while the pause menu is active. It completes outstanding bridge copies, releases old eye swapchains, retires owned stereo/scope targets and updates the prepared dimensions. Fresh eye swapchains are created when rebuilt textures arrive. The OpenXR session, controller actions, menu panel and desktop video mode are retained. Swapchain/interop failures retain the existing graceful flat-screen failure path. Live resizing acceptance is tracked under #8.

## Native rendering boundary

CE analysis `008BD530` draws slider bars; `008BDB1C` dispatches native value styles. Live disassembly in PID 16944, module base `00020000`, confirmed the row and coordinate layout. `00921DA0` draws native text; `009241F0` selects color using the thread's font context (`0091C150`). The 72-byte font parameters at `0119BF18 + context*0x48` are saved/restored around custom values and wrapped descriptions. The native rows' labels are fill-time caches, so their strings cannot supply live numeric feedback. These findings were bookmarked, read back and saved in Ghidra under #8. Addresses are Ghidra image addresses based at `00400000`; hooks resolve live patterns.

## Validation

Focused optimized x86 checks cover floor/tracking/freshness rejection, invalid numeric values and acceptance of finite INI values outside menu ranges. The x86 Release build passes. In GTA IV Complete Edition 1.2.0.59 with SteamVR 2.17.10, VIVE Pro/Wands and DXVK 3.1.1, Apply changed the eye targets and submitted images from 2468 x 2740 (scale 1.0) to 1974 x 2192 (scale 0.8) in the same process and focused OpenXR session. Fresh captures from both eyes show the scene rendering after the change. Headset checks confirmed the revised menu layout, live slider values, descriptions and live resolution Apply. The fixed-size HUD distance behavior and global ammo toggle passed headset checks. Flat-screen regression, D3D11 and TLAD/TBoGT coverage remain pending.

The development bridge's `vr-settings-status` reports registration, calibration result and configured height/scale. It reads snapshots and adds no continuous observer.
