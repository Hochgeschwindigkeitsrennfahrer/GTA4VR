# 028 — Independent third-person desktop view with VR-only character visibility

## Goal

Show the regular third-person game camera and existing HUD on the desktop while the headset receives independent first-person stereo views. Preserve the desktop character model while allowing it to be hidden in VR. Depends on #006/#014; coordinate draw ownership with #007. Roadmap stages 3–4.

## Tasks

- [ ] Preserve the ordinary game camera and its desktop targets separately from headset cameras, eye targets and temporal state. Confirm visibility covers the desktop camera and both eyes before any shared traversal.
- [ ] Schedule the desktop world render and original minimap/subtitle/HUD/menu composition in their correct order after one simulation update. Avoid duplicated script, audio, shadow, weather or UI updates; do not assume appending one draw call restores the HUD.
- [ ] Add per-view local-character visibility: hide the selected head/body meshes in headset rendering while retaining the regular model in the desktop image. Keep collision, AI/script state and appropriate shadow behavior intact. Future tracked hands/weapons (#016) need separate visibility ownership.
- [ ] Measure the extra desktop-view cost in Release with #013. Record any lower-resolution or update-rate option as an explicit tradeoff; preserve correct headset synchronization and pacing.
- [ ] Verify third-person desktop camera/HUD, VR camera/head motion, view-specific character visibility, pause/phone/menu transitions, device reset and ordinary VR-disabled behavior.

## Done when

Simultaneous desktop and headset captures show distinct intended cameras: ordinary third-person with character and existing HUD on desktop, independent first-person stereo with configured local-character visibility in VR. One simulation update, transition/reset/fallback evidence and measured additional cost are documented.

## Notes / evidence

- User asked on 2026-10-02 whether third-person desktop gameplay with minimap/subtitles/overlays and VR-only character hiding can coexist. Both are feasible render-architecture goals; implementation and acceptance remain pending.
- Current #006 scheduler builds two eye world streams and restores saved phase viewports afterward; it does not build a separate normal third-person desktop world image. The desktop currently exposes an eye view. #014's first prototype revealed near-camera mesh occlusion in the left eye, suspected local character head/body geometry.
- A global character-invisibility toggle would also affect desktop rendering and potentially shared state. Use verified per-view command/list/draw ownership rather than treating entity-wide visibility as the final solution.
- This preserves the existing desktop UI presentation. It does not require deciding the future headset overlay layout in #020, which remains deferred.
