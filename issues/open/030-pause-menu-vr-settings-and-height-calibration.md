# Pause-menu VR settings and player-height calibration

## Goal

Make VR settings accessible from GTA IV's pause menu, including an explicit player-height calibration workflow. Replace the temporary INI-only height setup with a usable in-game control while preserving opt-in VR and ordinary menu behavior.

## Tasks

- [ ] Inspect FusionFix pause-menu settings integration and select a VR settings location.
- [ ] Inventory existing VR options and define the initial settings exposed to players, including player height, physical crouch and vehicle eye-height offset.
- [ ] Add explicit height entry and calibration from a valid floor-relative headset pose. Explain the standing/seated reference; do not silently recalibrate during crouching or from headset startup placement.
- [ ] Persist settings in the ASI INI and apply changes safely with the correct simulation/render ownership; document options that require restart.
- [ ] Show configured height and derived crouch thresholds (65% entry / 75% exit), with clear units and invalid/tracking-unavailable handling.
- [ ] Verify pause/resume, headset pose recovery, settings persistence and ordinary flat-screen menu behavior. Track GTA IV/TLAD/TBoGT coverage separately.

## Done when

Players can open VR settings in the pause menu, set or explicitly calibrate their height, see the resulting crouch thresholds, and retain those settings after restart. Calibration rejects unavailable/invalid floor tracking without changing the previous value. Menu and gameplay transitions remain usable, with supported options and remaining compatibility limits documented.

## Notes / evidence

- 2026-10-04: Requested by the user while testing #014. Interim `[VR] PlayerHeight=1.75` meters is stored in the ASI INI; physical crouch enters at1.1375m (65%) and exits at1.3125m (75%). Height calibration UI is deferred to this issue. UI presentation choices for gameplay overlays in #020 remain separate.

- 2026-10-06: include ViewHudWidth/ViewHudDistance, wrist radar size/offsets and generic car dashboard placement in the settings inventory. #020 supplies accepted stereo HUD and tracked pause-menu pointer; #031 owns broader UI coverage. Preserve the user's distributed ViewHudWidth0.5 preference when designing the settings UI.
