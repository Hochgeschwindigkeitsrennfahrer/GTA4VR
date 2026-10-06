# 031 — Expand VR UI coverage and validate lifecycle

## Goal

Extend and validate the accepted UI foundation from [#020](../closed/020-vr-overlays-and-subtitles.md) across representative missions, transitions and episodes. Keep the working watch/dashboard, stereo HUD and world-space menus.

## Tasks

- [ ] Inventory uncaptured or poorly placed notifications, mission timers/counters, ammo/weapon feedback and phone/call UI. Attribute unmatched pixels/draw paths and transition ordering, recording camera/target ownership and reproducible evidence. Reuse closed #007 findings; physical phone draw/interaction mapping belongs to #022.
- [ ] Verify subtitles/help/mission feedback during dialogue and representative missions, including driving, death/cutscenes, loading and pause transitions. Record actual coverage rather than assuming all font/sprite calls are sufficient.
- [ ] Verify unread-SMS envelope appearance, flashing and disappearance after reading; distinguish SMS from missed-call/phone notifications. The native texture signature resolves, but an active SMS capture was not observed in the final saved-game runs.
- [ ] Test UI resource recreation on a real resolution/device reset, runtime loss/recovery and headset/controller tracking recovery; verify normal desktop UI after fallback.
- [ ] Record GTA IV, TLAD and TBoGT routes separately; do not infer episode compatibility from CE GTA IV free roam.
- [ ] Review HUD font size, depth and placement with representative long subtitles/help text. Expose tuning through #030; per-car placement and other vehicle classes remain #019.

## Done when

The recorded UI inventory and route matrix are readable in both eyes, preserve native timing/mission feedback, and recover from the tested lifecycle transitions. Unimplemented phone/weapon/vehicle behavior remains explicitly owned by its existing issue.

## Notes / evidence

- 2026-10-06: follow-up split from #020 at the user's request to close the accepted UI work. User accepted wrist radar/health/armor, wanted stars, loading/pause screen, menu ray and semantic actions, and finally the expanded binocular HUD. SMS routing is implemented but its active/expiry acceptance remains here. #020's evidence/history remains in its closed file.
- Latest deployed ASI is Win32 Release SHA2566499D57BD8709DD9EA3F89E674A3F33080F1DEA4FF81420F82D0FF1126506F13. CE1.2.0.59 / SteamVR2.17.10 / Vive Wands / DXVK-Vulkan. Source missing-key HUD width fallback0.64; the user's distributed INI width0.5 edit is preserved. Existing installed INI has no explicit width override.
