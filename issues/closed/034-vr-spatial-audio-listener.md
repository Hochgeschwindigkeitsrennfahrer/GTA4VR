# 034 — Correct spatial audio direction in VR

## Goal

Make positional sounds, including NPC voices, arrive from the correct direction relative to the headset.

## Tasks

- [x] Reproduce the listener mismatch while turning the headset independently of the ordinary camera.
- [x] Trace the native listener update and its weighted camera listeners.
- [x] Bind the gameplay listener to the VR head center and orientation without duplicating audio per eye.
- [x] Obtain user positional-audio acceptance and check VR-disabled native routing.

## Done when

Positional sources remain attached to their world locations as the headset turns/moves, with correct left/right direction, and normal flat-screen audio remains intact.

## Notes / evidence

- 2026-10-07: User reports NPC speech and other 3D sounds come from incorrect directions. The old third-person camera may still control the listener; this is a hypothesis awaiting native analysis. Current setup: GTA IV Complete Edition 1.2.0.59, SteamVR 2.17.10, Vive Wands, DXVK/Vulkan VR.

- 2026-10-10: Started after vehicle commit34869a7. Confirmed Ghidra GTAIV project /GTAIV.exe x86 base00400000. Native GTA audio worker8C13B0 calls8C28E0, which calls listener updater96D490. Updater reads native camera128E310 and submits frames via8A8BE0 (spatial inverse) /8A8F50 (distance/world frame), both thiscall(matrix*,index,gain), RET0C. Read-only PID26252/base005C0000 entry signatures match Ghidra; live camera remains an ordinary game camera. Listener role supported by camera/listener call chain; callback coverage and listening are pending.
- Implemented once-per-native-audio-update headset snapshot, replacing both setter inputs only within that thread's GTA listener update scope. Eye midpoint position and right/forward/up orientation are converted from VR viewport right/up/back. Native listener slot weights, double buffering and mixing remain native; all active slots use the same head pose, avoiding vehicle camera blend. Inactive/stale (>250ms) VR falls back to original input; no per-eye audio processing. CE entry signatures gate installation. audio-status reports readiness, callback counts, pose age and native/head position/forward vectors.
- Ghidra Issue034AudioListener bookmark at96D490 previewed, applied, read back and save_program succeeded. x86 Release passed with existing libmodupdater LNK4099 warnings; diff check passed. Runtime/listening, cutscene/menu transitions and VR-disabled acceptance remain pending.

## Completion — 2026-10-10

- GTA IV CE 1.2.0.59, SteamVR 2.17.10, Vive Wands, DXVK/Vulkan. PID2976 reported 2,164 native listener updates, 10,365 spatial overrides and 12,438 distance overrides; the native and headset forward vectors differed by approximately 83 degrees. Fresh capture confirmed normal continuous VR rendering.
- User accepted the nearby-voices/traffic headset-turn check: “Yeah that works. You can close the issue and commit.” Closed on that acceptance and explicit request.
- VR-disabled fallback PID28976 loaded normal third-person gameplay (fresh window capture). OpenXR was disabled; 1,023 native listener updates ran with zero overrides in either setter. Original installed INI was restored byte-for-byte afterward. This verifies original listener routing, not a separate audible flat-screen comparison.
- Ghidra bookmark updated with callback coverage and user acceptance, read back, and saved. Release build and diff check passed; no performance improvement is claimed.
- Remaining coverage limits: vehicle-specific listening, cutscenes, menu/focus transitions, TLAD and TBoGT were not separately accepted. Native camera blend weights and fallback inputs are preserved. Vehicle issue #019 remains open.
