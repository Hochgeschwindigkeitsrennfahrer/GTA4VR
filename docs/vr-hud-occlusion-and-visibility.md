# Dashboard occlusion and gameplay HUD visibility

Work: [vehicle interaction #3](https://github.com/bordplate/GTA4VR/issues/3) and [UI coverage #9](https://github.com/bordplate/GTA4VR/issues/9).

## Dashboard depth

The car radar, wanted-star row and SMS icon compare against the matching eye's opaque scene depth. The stereo owner passes `_DEFERRED_GBUFFER_3_` (with the recorded geometry-pass surface as fallback), its viewport depth range and completed logarithmic depth parameters to `VRHud::Draw`. The radar shader encodes interpolated clip W with the same logarithmic formula as scope lenses and weapon displays. Testing uses LESS_EQUAL, no depth writes and no inherited material depth bias. Wrist radar and gameplay text retain their existing presentation.

No new scene rendering or depth readback is needed. Targets, viewport and graphics state are restored after compositing. A missing/incompatible car depth surface leaves the panel undrawn rather than reverting to an overlay through geometry. Transparent surfaces that do not write native scene depth do not occlude it.

## Native HUD records

During focused VR gameplay, `HUD_WANTED_FRONT` and `HUD_WANTED_BACK` are omitted from the native text HUD; wanted stars remain on the wrist/dashboard. `HUD_AMMO` and `HUD_WEAPON_ICON` are omitted while the global Diegetic ammo counters setting is on. With it off, native weapon/ammo presentation returns. Other notifications and native flat-screen rendering remain unchanged.

Record IDs resolve from the native initialization labels, rather than assumed screen coordinates or fixed row numbers. The filtered records' native fade state still advances. CE text formatter `008FF3D0` and sprite formatter `008FF000` both use cdecl `(record ID, mode)`; fade advancement is `008FD0C0`. Wanted and ammo record initialization occurs at `005CB41B`, `005CB472`, `005CB531`, with the weapon icon at `005CB588`.

The VR reload owner requests an ammo-HUD refresh when authoritative clip/reserve values change or the player fires, including direct drive-by shots. The native weapon-HUD updater `005C6F10` consumes that request through `DISPLAY_AMMO(true)` before its ordinary update, preserving the normal 400 ms fade and 5000 ms hold. Its native reveal flag is `011DB239`; direct VR firing previously omitted this signal. The findings were bookmarked, read back and saved in Ghidra. Addresses use analysis image base `00400000`; live read-only verification used PID 16244, module base `00020000`.

## Validation

x86 Release build and whitespace check pass. Headset checks passed dashboard occlusion in both eyes, wanted-star filtering, conditional weapon/ammo presentation and ammo-use refresh without switching weapons. Initial target: GTA IV CE 1.2.0.59, SteamVR 2.17.10, VIVE Pro/Wands, DXVK 3.1.1/Vulkan bridge. Flat-screen, D3D11 and episode coverage remain pending.
