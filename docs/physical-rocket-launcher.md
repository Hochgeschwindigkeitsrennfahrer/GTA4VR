# Rocket launcher / RPG

Work in progress under #024 for GTA IV Complete Edition 1.2.0.59, native
weapon type18. First pass enables tracked right-trigger firing through the
existing native projectile path. The physical adapter now owns a single loaded
rocket and blocks native automatic reloads while active. The user accepted
physical reloading, support orientation and built-in rocket suppression. Loaded
rocket fast-motion attachment is also user-accepted.

`launcher-test rpg` selects the launcher through the simulation callback,
preserving owned ammunition or supplying four rounds if absent. Read completion
with `melee-test-status`. This command does not teleport or write saves.

## Native firing

Ghidra image base00400000: `B5FE20` dispatches firetype3 to `B60870`.
The latter is thiscall with six stack arguments (RET18); its native projectile
constructor/launch call `9BE040` is cdecl with eight arguments (ADD ESP20).
The VR trigger supplies the equipped gun frame, authored muzzle and gun +X
target to the outer fire function. Projectile travel, explosions, ammunition,
cadence, effects and damage remain native. Rockets do not use the VR bullet
headshot eligibility override.

`B60870` obtains the shooter's ignore list via vtable+B8 and checks the path
from the body to the launch origin through `A53770`. Its later projectile
collision handling still needs an owned-arm obstruction check: firing-time
query exclusions alone do not prove exclusions after launch. Bookmark
`VR024_RPG` at B60870 was applied, read back and saved on2026-10-09.

Installed weapon data and live type18 readback agree: one-round clip, eight
maximum rounds, 800ms cadence, 2066ms reload and 100m weapon range. Live
weaponInfo+A8 is projectile type6; offset B0 is(0.54,0,0.055) and rotation
C0 is(0,0,-1.571). Aim pitch offset+60 is zero. Keep native handling of these
fields; do not apply the offsets a second time in VR without tracing the
projectile constructor.

## Live model and physical reload direction

Read-only equipped-model capture at module base00660000 found nine bones:

| Bone | Name | Local translation (m) |
| --- | --- | --- |
| 0 | RPG | 0,0,0 |
| 1 | gun_grip2 | 0.125244,0.013331,-0.021044 |
| 2 | Mag | 0.126240,0.012999,0.008447 |
| 3 | gun_ejac | -0.659085,0.015069,0.101812 |
| 4 | gun_grip | -0.108372,0.023822,0.020115 |
| 5 | Gun_Trigger | 0.161066,-0.000977,0.025712 |
| 6 | Gun_Cock1 | 0.110170,-0.001016,0.032829 |
| 7 | gun_muzzle | 0.318807,0.015501,0.062882 |
| 8 | gun_rocket | -0.110000,0.001000,-100.000000 |

The rocket bone is parked far below the model at rest. Earlier attempts to
hide bone8 in skeletons, queued palettes and geometry draws did not remove the
permanent rocket visible in the headset; those experimental hooks are removed.
Live inspection instead found a separate native CObject at the player's weapon
manager (`ped+0x2B0+0x1C`), alongside the launcher at manager+0x14.
Its model info identifies weapon type6, its parent+0x1BC is the player ped,
its CWeapon pointer+0x25C is null, and it has no skeleton. This matches the
native projectile attachment described above.

The current fix records that owned secondary object while the guarded type18
VR reload adapter is active and suppresses its general CObject render callback
(`B12E80`, virtual+0x8C). It preserves the object's native lifetime and does not
modify shared models or palettes. `reload-status` exposes `nativeRocketVisual`
and `nativeRocketSkips` to verify identification and render-hook coverage.
The separate physical-round visual owns loaded/empty appearance. The user accepted this correction in the headset. The Mag and Gun_Cock1 bones do not
imply a pistol magazine/slide sequence.

The adapter owns one loaded rocket plus reserve and an optional held rocket.
Taking a rocket transfers reserve ownership; loading seats it along the
launcher's front opening; firing consumes the loaded rocket. There is no
separate chambered bullet or pistol-style slide rack. Loaded/empty state is
retained across weapon switches. Dropping the held round discards it; switching
away returns a still-held reserve round to its weapon, matching the existing
reload cancellation policy.

Loose and loaded round visuals use the game's native projectile model returned
for type6, with weaponInfo's -90-degree Z rotation. They are visual objects,
not live explosives. The separate native rocket suppression is user-accepted. Initial use of
weaponInfo's projectile origin as the reload guide was rejected by the user:
there were duplicate rockets and no detectable insertion snap. The correction
uses the native projectile model bounds to identify its tail (local minimumY,
rotated to launcherX). The tail enters at the authored gun_muzzle pivot and
travels15cm rearward. Loaded model origin is derived from that final tail seat.
Roll is unrestricted for the cylindrical rocket; axial alignment remains.
The user accepted the tail-based reload interaction.

`plugins/VRWeapons/rpg.ini` exposes the usual hand pose, insertion point,
axis/travel and snap controls. RocketEntranceX/Y/Z sets the tube opening in
launcher-local meters (initial0.318807/0.015501/0.062882); MagazineTip is ignored
for RPG reloads. Existing RPG profiles without the new entrance keys receive
these defaults, preserving other tuning. SupportHandRotationX/Y/Z rotates the support
palm around its authored grip without moving its seat. The user rejected X180
and accepted Z180 (X0/Y0/Z180). Use
`reload-weapon-config` after edits; `reload-status` reports acceptance.

After firing, hold left grip over the left shoulder to take one reserve rocket.
Bring it to the front opening, align with the tube and push it rearward while
holding grip. Seating loads the round immediately; right trigger fires it.
Right grip does not eject a loaded rocket. Native timed reloads and reload
tasks are guarded only for this local VR weapon; other shooters retain them.

## Current evidence

Release x86 build13B6F0B3378567F970872F7F1C388EAA6CFE48865BC35135C0F453B7E4F1C2AC
compiled with existing modupdater missing-PDB warnings. Backed-up deployment
reached PID28652, completed native type18 equipment readback and continuous
FOCUSED DXVK-Vulkan rendering on SteamVR2.17.10. A fresh window capture showed
normal textured apartment geometry. No teleport or save changes. Trigger
direction, support attachment and projectile obstruction remain pending a
headset firing check at that checkpoint. The user subsequently accepted firing
direction and support-grip position, but reported the support palm faces the
wrong way. Physical reload build2241520D7B2E6559634AB47294903862E73A86B5AC6380A72EA737B391974F38
compiled Release-only. Optimized x86 accounting tests passed loaded/empty
rocket, acquire/insert/fire, deliberate drop, cancellation and switching,
alongside existing firearm accounting/guide tests. Runtime reload/palm checks
remain pending; this is not complete RPG acceptance. Correction build
1C0D9FF4482F610CA79E3B3D9ED78FED6E8768571BEBEE12716159B4091C0468
compiled Release-only and was deployed with ASI/INI/profile backup
build/issue024-rpg-tail-20261009-140412. Existing installed profile was preserved.

Final-palette candidate Release C78A9B090393BC21457C1F91C5C7E8F79A5DFE7179B6FA6DC115390154145252
compiled with existing modupdater missing-PDB warnings. Backup:
build/issue024-rpg-final-palette-20261009-142741. Installed RPG tuning, including
accepted support rotation0/0/180, was preserved. Ghidra bookmark
VR024_RPG_PALETTE at4593E0 records the verified live calling convention and
palette layout; it was previewed, applied, read back and saved. Runtime counter
coverage is verified in PID12924 (rocketPaletteUpdates840), with active type18
adapter and fresh textured apartment image. SteamVR2.17.10/DXVK-Vulkan session
is FOCUSED and continuous. The user rejected post-shot visibility in this build.
No teleport/save changes.

Geometry filtering replaces the failed palette collapse. The463540 thiscall
has one palette argument and RET04; it remaps palette entries to vertex
constants64 and invokes virtual+8 for the actual draw. Ghidra bookmark
VR024_RPG_GEOMETRY was previewed/applied/read back/saved at463540. Runtime
rocketGeometryChecks/rocketGeometrySkips distinguish ownership coverage from
actual rocket draw removal. Geometry-filter visibility acceptance is pending.

Geometry-filter Release79EDA396FD2E9860B200A9FDD7410DCF287F04EC4E9772AD8C13C58C8337AD70
deployed with backup build/issue024-rpg-geometry-20261009-143444. PID19576 has
active valid RPG reload,2095 owned palettes,8454 geometry checks and4227 actual
rocket draw skips. Continuous FOCUSED DXVK-Vulkan on SteamVR2.17.10. A fresh
window screenshot was dark with a bright effect and did not show a useful
launcher view. User rejected this build too: permanent rocket unchanged.

After the rejected test, clip/chamber/inserted were empty and the separate
reload visual was deleted, while14346 object geometry skips had occurred.
Candidate extends filtering to the tagged local-player body draw (both rigid
and skinned geometry), matching only the exact authored rocket geometry
learned from owned RPG palette draws. Thread-local scope is restored after
each body command; model-definition changes clear the geometry cache.
rocketBodySkips measures this attachment path separately. Acceptance pending.

Attachment candidate BBF42DA243A0DA846922EE1382D3716843C853F963E37BE8D99BF05646C449D9
deployed with backup build/issue024-rpg-body-geometry-20261009-144009.
PID19300 recorded8576 object skips but zero body skips; that does not reach
the suspected additional attachment path. No repeated headset check requested.
The user subsequently drove a car, leaving weapon-manager+14 null. Inspection
needs the RPG equipped on foot. The extracted oldschool RPG is a reference
variant; compare live meshes before treating it as identical to installed
AnimatedWeaponsVanilla IMG content. Permanent rocket remains unresolved.

## Separate native rocket correction — 2026-10-09

After the user equipped the RPG on foot, read-only capture at module base
00350000 found launcher1039B860 and a distinct held rocket1039BB80 owned by
player0FBB3E80. Secondary model9/modelinfo11B263B0 has weapon type6 and hash
5A6525AE; parent is the player, CWeapon and skeleton are null. Evidence is kept
locally in build/issue024-rpg-extra.txt. Ghidra bookmark
VR024_RPG_NATIVE_ROCKET atB12E80 was previewed, applied, read back and saved.
This supersedes the failed palette/geometry/body attachment candidates above.

Release158DD51C971274BB04F29578712E087DB4014BC89AAEC1AB9D29875E13C9ED99
built successfully with existing modupdater missing-PDB warnings. Installed
ASI/INI/RPG settings backed up in build/issue024-rpg-native-object-20261009-145212;
only the ASI was replaced. Accepted support and reload tuning is preserved.
Runtime coverage and headset acceptance remain pending; no teleport/save changes.

- PID14324 reached continuous FOCUSED SteamVR2.17.10 DXVK-Vulkan stereo, responsive bridge and zero command-buffer overflows. Reload adapter active/type18/assets valid, nativeRocketVisual=true and nativeRocketSkips3251: the new filter reaches the actual separately attached rocket. Current ammo empty (clip0/reserve7) with one physical insertion recorded. Fresh window image is very dark with a nearby hand/body surface, so it does not independently establish launcher appearance. Focused headset loaded/empty visibility check requested; acceptance pending.

- User confirms permanent native rocket removal and correct single loaded visual. Remaining report: our loaded rocket lags during fast weapon movement. Candidate refreshes only the loaded rocket matrix from the equipped launcher frame immediately before its own native CObject draw-command build; creation, room membership and ammo remain on the simulation callback. No native script calls added to rendering. rocketRenderUpdates measures coverage; fast-movement headset acceptance pending.

- Loaded-rocket pose Release EECB846753E30D00034019FD4ED57ED93E6D524656645AAA4CF81BAB7BA42865 built after correcting a const/mutable matrix-validation compile error; successful rebuild has only existing modupdater PDB warnings. Backed up installed ASI/INI/profile in build/issue024-rpg-render-pose-20261009-145730, replaced only ASI, and relaunched normally. Settings preserved, no teleport/save changes; render-update coverage and headset fast-motion check pending.

- PID19196: equipment completed type18, clip1/reserve7, loaded visual inherits room2719718318. nativeRocketVisual=true, nativeRocketSkips1410 and rocketRenderUpdates1410 confirm both secondary omission and per-eye loaded visual pose updates. Continuous FOCUSED DXVK-Vulkan stereo on SteamVR2.17.10, zero command-buffer overflows. No performance claim; fast-motion headset acceptance pending.

- Final user headset acceptance: loaded RPG rocket stays attached during quick movement/rotation after the render-pose correction. RPG direction, support orientation, physical front-loading, single loaded visual, empty-launcher appearance and fast-motion attachment are now user-accepted. #024 remains open for remaining firearm classes/coverage. Current accepted Release is EECB846753E30D00034019FD4ED57ED93E6D524656645AAA4CF81BAB7BA42865; no new commit requested in this iteration.

## Loaded rocket depth tuning

`[Reload] RocketLoadedDepth=0.03` in `plugins/VRWeapons/rpg.ini` seats the
loaded rocket an extra 3cm inward along the insertion axis. Values are meters:
positive moves farther into the tube, negative moves outward, zero restores
the previously accepted seat. Values must be finite numbers. This affects only
the loaded visual, including its late render update; the opening, insertion
travel, loose shoulder rocket and projectile launch direction are unchanged.
Apply edits live with `tools/game-command.ps1 reload-weapon-config` and check
`reload-status` for configValid=true. Initial 3cm visual tuning awaits user review.

- Installed user profile contains InsertionAxisX=-1.5 and InsertionTravel=0.3, both rejected by prior bounds. Preserve these edits: RPG axis component bounds expanded to +/-2 (still normalized), travel up to0.5m. Other weapons retain their bounds. This makes the user profile loadable instead of rejecting the whole file; loaded-depth remains independently adjustable.

- Per user tuning preference, weapon-profile loading no longer imposes arbitrary numeric min/max ranges or rack-threshold/travel ordering. It validates numeric syntax and finite values; the insertion axis must remain nonzero to normalize it. Tuning limits are user-controlled.

- Release908567C9393C1A67BBA1E38A5267B4341B5F4FFE1838797284C79E791ADC7B14 built successfully (existing modupdater PDB warnings), deployed after verified bridge exit with backup build/issue024-rpg-depth-unrestricted-20261009-150857. Added only RocketLoadedDepth=0.03 to the installed RPG profile; existing user values preserved. AGENTS.md records avoiding arbitrary ranges for editable VR tuning. No teleport/save changes; runtime profile acceptance pending.

- Runtime PID5296: native RPG equipment completed, user profile accepted (configValid=true/configRevision3), reload-weapon-config accepted, continuous FOCUSED DXVK-Vulkan stereo with zero command-buffer overflows. Initial 3cm depth has not been user-approved visually; tune live as needed. No additional gameplay route requested for this configuration change.

- User requests RPG support grip 20cm rearward. Shifted authored secondary grip -0.20m in weapon-local X before deriving the right-wrist-relative support frame, preserving accepted support orientation and all other weapon grips. Both acquisition target and final locked IK share this seat. Release build/deployment pending.

- Support-grip ReleaseB038DE5E9D21F97354BC8E05C49AE0CDF70BD345294BB89067B1BF1C7DF4D87A built successfully (existing modupdater PDB warnings), deployed after verified bridge exit. ASI/INI/RPG profile backed up in build/issue024-rpg-support-back-20261009-151841; only ASI replaced. User settings preserved; no teleport/save changes. Relaunch initiated, grip-placement user review pending.

- User rejects fixed -0.20m RPG support shift as too large and requests INI tuning. Removed fixed offset; added SupportHandOffsetX/Y/Z in weapon-local meters, finite numeric values without arbitrary limits, loaded by reload-weapon-config and existing grip-cache revision. RPG initial X=-0.05 (5cm rearward), Y/Z0; zero restores original authored seat. Other weapons default to zero additional offset. Existing rotation and PSG-1 correction preserved. Build/deployment pending.

- Configurable-support Release8CC72572F10725A07A1DFC113850DD49F3C016558895DA1FB52F59B4F7CD7933 built successfully (existing modupdater missing-PDB warnings), backed up/deployed in build/issue024-rpg-support-config-20261009-152132. Only added missing SupportHandOffsetX=-0.05/Y0/Z0 to installed RPG INI; existing rocket depth and hand/reload tuning preserved. Normal relaunch initiated, no teleport/save changes.

- User accepts RPG work and requests storing current profile and committing. Imported exact installed rpg.ini: shoulder rotation0/90/0, offset0.08/0.05/-0.12m, insertion(-1,0,0)/0.15m, loaded depth0.09m, support rotation180/0/180 and offset-0.05/0/0m. Source fallback defaults mirror this tuning. Accepted Release runtime is8CC72572; source fallback synchronization does not alter an installed valid profile. #024 remains open. Next requested class is combat shotgun, explicitly not pump shotgun.
