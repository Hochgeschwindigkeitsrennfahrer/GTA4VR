# Diegetic firearm ammo-counter experiment

## Multi-weapon displays and editor

All eleven implemented on-foot firearm adapters now use the display (pistols,
micro-SMG/SMG, AK-47/M4, both shotguns, both snipers and RPG). SMG/rifle defaults
use `Stacked=1`: reserve digits above two rows of loaded-round pins, supporting
the full native clip capacity up to 64. Other defaults retain the side-by-side
layout. Initial placements beyond the accepted Glock are estimates to tune.

Each weapon profile has `[AmmoCounter] Enabled`, `Stacked`, `EditWithOffHand`,
`GrabRadius`, offsets, rotations and dimensions. Editing is off in distributed
defaults. Enable it and use `reload-weapon-config`:

- Hold off-hand Grab near the display to move/rotate it relative to the gun.
- While gripping, hold the off-hand trigger and move toward/away from the gun
  origin to resize uniformly. Release trigger to resume moving/rotating.
- Stop adjusting for 400 ms or release grip: the eight placement/size values
  are saved to that weapon's INI. Other settings remain intact. Unsaved changes
  flush before switching weapons. `reload-status` exposes `counterEditing`,
  `counterSavePending`, and `counterSaveOk`.

Display editing cancels/blocks rack acquisition and takes priority over support
grip. It does not transfer a grabbed magazine. Tracking/focus loss, phone/pause,
vehicle entry and weapon changes cancel editing. Current first pass is on-foot;
drive-by displays and broader all-weapon placement coverage remain pending.
The user accepted interactive editing and save/reload behavior.
Geometry checks verify a 50-round two-row layout and rotation extraction.

Historical Glock iterations and native rack correction evidence follow.

The user accepted the editor/layout check and requested editing disabled in all
installed profiles. Their saved placement is preserved. Glock/Deagle displays
now apply the actual slide-bone animation delta relative to the resting bone,
covering manual rack and shot animation without rebasing configured offsets.

Issue #24 fixes last-magazine exhaustion: native inventory total now includes
the held magazine (Owned()), while display storage and loaded counts remain
reserve/Clip(). No sentinel ammo is created. Reconciliation uses that same owned
total, preventing magazine transfer from looking like ammo loss or pickup.
Focused last-magazine/conservation tests pass; live slide/last-magazine checks
remain pending in build
`138AEB7E3068AABE7E199BA676967FECEE2451C6B33E133211825F49A1E3348A`.
That build reached focused continuous VR in PID 12992, config valid, passive
diagnostics off. Readback confirms EditWithOffHand=0 in all eleven installed
profiles. User slide-following and last-magazine checks requested; pending at
that point. The user subsequently confirms both work and authorizes committing.
Saved AmmoCounter values from installed profiles are incorporated into repository
defaults; EditWithOffHand remains 0. Broader episode/flat-screen/lifecycle testing
is not claimed. UI issue #9 remains open for its unrelated coverage.

Multi-weapon/editor build `F8DD89CA5A7735D723EDCFD0AA4630781734D70D12D35CE6C1AB6623404F0D27`
passed x86 Release and focused geometry tests, and was deployed with plugin/INI/
all-weapon-profile backups. Existing tuning preserved, missing counter sections
added, installed edit toggles enabled for testing. PID 28144 reached focused
continuous stereo; pump-shotgun profile is valid, clip 8/reserve 27, editor/save
status available. Interactive acquisition, save/reload and other-weapon headset
checks remain pending; no commit/push or performance claim.

Tracking: [UI issue #9](https://github.com/bordplate/GTA4VR/issues/9), roadmap stages 4/6.

2026-10-12, baseline `8744975`. User mockup: rear-facing amber pin stack and
seven-segment numeric count. Implemented as small colored world-space geometry
on the standard pistol (native type 7), without replacing proprietary textures
or models. The pins show physical reload `Clip()` state: magazine rounds plus a
chambered round. The numeric display shows reserve/storage ammo only, excluding
rounds in the gun and any magazine currently held in the hand. Digits
use pointed six-vertex segments and fit additional decimal places automatically
rather than truncating at 99.

The equipped object render callback captures its actual queued transform and
ammunition state for each eye; the final-scene marker carries the snapshot to
composition. Two small shaders project the geometry with the completed eye
matrix and write the same logarithmic fragment depth as the game/scope. The
counter uses geometry depth, does not write scene depth, hides its back face,
and restores D3D state, targets, depth and viewport after drawing. Shader
resources are released on reset/shutdown. There is no additional scene camera.

## Tuning

Installed `plugins/VRWeapons/pistol.ini` has `[AmmoCounter]`:

- `Enabled=1` (0 disables).
- `OffsetX/Y/Z`: meters in weapon coordinates; +X muzzle-forward, +Y left, +Z up.
- `RotationX/Y/Z`: degrees about weapon axes, composed in X/Y/Z order.
- `Width`, `Height`: physical size in meters.

Defaults: offset `(-0.115,0,0.080)`, zero rotation, 26×24 mm. The initial rear
position is an estimate pending headset placement acceptance. Edit and invoke
`tools/game-command.ps1 reload-weapon-config`; no restart needed for tuning.
Existing profile syntax/finite checks apply; dimensions must be positive to
form valid geometry. No arbitrary tuning bounds. The pins support up to 32
slots, sufficient for the standard pistol.

## Evidence and remaining checks

x86 Release built successfully with existing updater missing-PDB warnings;
`git diff --check` and the focused counter geometry test passed. Test checks
rear-facing orientation, physical placement, rigid translation and filled versus
empty indicator geometry. Deployed ASI SHA256:
`E7FD2E99EB4D5202C9417C123E74F4FD7302D76AD855ED9AD7DF4A072F0CEEEE`.
ASI, INI and installed pistol profile backed up before deployment; existing
reload tuning preserved and only the new counter section appended.

Runtime PID 15820 reports focused continuous DXVK-Vulkan stereo at 1974×2192,
SteamVR 2.17.10/Vive Pro and Wands. Profile runtime reload increments revision
with `configValid=true`. The pistol was not selected during this initial bridge
check, so it does not establish visible counter rendering. Both-eye visibility,
attachment through fast motion, readability, hand/world occlusion, shot/ejection/
insertion/rack updates, reset and flat-screen regression remain pending. This is
a prototype with no performance or compatibility acceptance claim. No Computer
Use, teleport, save modification or commit/push.

### User acceptance and display revision

The user confirms the initial display works, then clarifies the numeric portion
must show total ammunition and requests the usual pointed segment ends. Both
changes are implemented: pins retain loaded-round state; digits use Owned() and
tapered polygons. Focused checks also verify masks for totals 218 and 1000.
x86 Release passed; updated ASI SHA256
`00779F5CD1A2469C22D7DC7B2BE0DCB333FC79CCF908485D70BCAE454C9B909C`
was backed up/deployed with existing installed configuration preserved.
Revised display headset acceptance and broader lifecycle/fallback remain pending.
After launcher retries, PID 21340 reached focused continuous VR; standard pistol
reload state is valid with clip 17, reserve 304 and held rounds 0 (total 321).
All four passive diagnostics are off. This verifies startup/state inputs;
revised segment appearance still awaits headset acceptance.

### Reserve-only display and native rack accounting

The user confirms the pointed-segment display works, then clarifies digits should
show storage only. Numeric source now uses `ammo.reserve` while pins retain
`Clip()`. The earlier Owned() description above is historical for that revision.

The user also reports chambered racking loses two rounds. Traced under
[issue #23](https://github.com/bordplate/GTA4VR/issues/23): native
SET_AMMO_IN_CLIP wrapper `BD1160` → `BD1BA0` saves CWeapon `+5C`, sets the new
clip through `B63F10`, then adds the clip delta to total `+60` through `B5C240`.
SET_CHAR_AMMO `BD1190` → `BD1C20` → `B65F40` → `B63F40` sets total directly.
Writing authoritative total before clip therefore deducted the rack round
twice; the following reconciliation removed the extra from reserve. Both normal
synchronization and weapon-switch saving now write clip first, total last.
Physical Rack() already discards exactly one chambered round and feeds the next.
Ghidra helpers were created only at verified entry points; bookmark `GTA4VR #23`
at `BD1BA0` was read back and saved. CE 1.2.0.59, analysis base `00400000`.

Existing deterministic pistol/rifle/rocket/shotgun/pump accounting tests pass,
including chambered and empty racking; x86 Release passes with updater PDB
warnings. ASI `0B25A0F79D35D830D3261EA2CD28CD27022659F55DA6190F07C19D4E3F69DCC1`
deployed with ASI/INI/pistol-profile backup and installed tuning preserved.
Live chambered-rack/unchanged-reserve acceptance remains pending.
Runtime PID 8500 reached responsive focused continuous DXVK-Vulkan VR; profile
config is valid. Pistol was not equipped in this initial check. User rack/reserve
check requested without forcing an inventory change or location restoration.
