# 020 — VR minimap, subtitles and gameplay overlays

## Goal

Make gameplay overlays readable and usable in the headset, including minimap/radar, subtitles, health/ammo and notifications. Depends on pixel/draw attribution in #007. Roadmap stage 4.

## Tasks

- [ ] Inventory HUD/notification/subtitle elements and their source targets/draw calls; distinguish gameplay overlays from world objective markers (#023).
- [ ] Choose readable placement, scale and depth for each overlay, with adjustable placement where necessary and correct visibility in both eyes.
- [ ] Preserve subtitle timing, minimap navigation and mission feedback during walking, driving, phone use, pause and loading transitions.
- [ ] Test representative missions/dialogue, navigation and menus in the headset, plus ordinary flat-screen composition.

## Done when

The recorded overlay inventory is visible and readable in both eyes through the test route, with correct timing and no duplicate/occluded essential feedback.

## Notes / evidence

- User requests overlays such as minimap and subtitles. #007 supplies render attribution; this issue implements and verifies their VR presentation. Physical phone behavior is #022.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
