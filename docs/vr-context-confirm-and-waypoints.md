# Context confirmation and map waypoints

Work: [GitHub #9](https://github.com/bordplate/GTA4VR/issues/9), roadmap stage 4/6.

## Controls

- **On foot → Confirm / accept** and **Driving → Confirm / accept** expose a boolean contextual action (`confirm`). They have no suggested button, so custom SteamVR bindings can assign a suitable control without overwriting movement, shooting or phone inputs.
- **Menus → Set / remove map waypoint** (`set_waypoint`) defaults to the right Wand's touchpad click. Aim the menu ray at a map position and click. The trigger retains native map dragging. Clicking again removes the existing waypoint according to native behavior.
- The existing Menus Confirm and Phone Accept actions retain their separate contexts.

## Native routing

Confirmation edges cross the render/simulation boundary using the existing bounded pulse transport. Outside pause, cutscenes and the phone, the explicit action supplies `INPUT_FRONTEND_ACCEPT` (77), `INPUT_CONTEXT` (23) and raw gamepad A (16) queries. Native script prices, purchases and progression remain authoritative. The inspected FusionFix `gunlockup.sco` uses `IS_CONTROL_JUST_PRESSED(2,23)` for its purchase branch. Script previews synchronize their gameplay set alongside the higher-priority Menus set so a contextual confirmation binding remains available; Menus keeps priority on overlapping bindings.

CE native waypoint handling is `005B2800`: frontend query `008B4B30(8,...)` or rising mouse bit 2 toggles a marker. The cursor branch at `005B2875` tests `CPad+328D`; screen coordinates at `018B7A80/018B7A8C`, scaled by `017ACCE8/017ACCF0`, go through `009011E0` before `008BF7A0` creates the waypoint. VR supplies that mouse bit only on the Map page. A scoped branch override selects cursor placement while the ray hits the panel, including after gamepad input, without changing the global input-device mode. Native coordinate conversion, snapping, marker removal and route creation remain intact.

Addresses are Ghidra image addresses at base `00400000`. Read-only inspection in PID 25920, actual module base `00020000`, confirmed the instruction boundaries and unique cursor-gate pattern. The waypoint findings were bookmarked, read back and saved in Ghidra.

## Validation

Initial target: GTA IV Complete Edition 1.2.0.59, SteamVR 2.17.10, VIVE Pro/Wands, DXVK 3.1.1/Vulkan bridge. The x86 Release build and whitespace check pass. The deployed game initializes a focused OpenXR session with all passive diagnostics disabled. Headset testing passed map waypoint placement/removal, gun-store ammo purchases and vehicle-context confirmations. Broader mission, episode and flat-screen regression coverage remains pending. No mission, episode or flat-screen coverage is inferred from compilation.
