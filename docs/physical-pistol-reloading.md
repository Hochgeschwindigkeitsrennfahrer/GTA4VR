# Physical firearm reloads — #024

## Current scope

The first adapter is the Complete Edition standard pistol (native weapon type 7).
`[VR] ExperimentalPhysicalReload=0` remains the distributed default. It requires
the tracked hands/pistol trigger features and the Animated Weapons pistol model.
Other firearms retain their existing behavior; they are not physically reloadable yet.
GTA IV 1.2.0.59 / SteamVR 2.17.10 / Vive Wands / DXVK-Vulkan is the local test configuration.
On2026-10-07 the user accepted pistol reload placement, magazine/slide hand poses,
slide audio and shot strength. Their installed magazine-hand tuning is now the
shipped template and missing-key defaults: rotation100/180/180deg and offset
0.075/0.05/0.04m. Remaining firearm classes and episode/fallback checks stay open.

## Model dependency and attribution

Install the model-only Fusion Overloader `update/01.AnimatedWeaponsVanilla/weapons.img`
from the user-supplied `KxE4Yj3c_AnimatedWeaponsVanilla.7z`. Keep a backup of any
existing directory before replacing it. Do not install the bundled animation ASI
alongside this adapter: the VR code owns magazine/slide poses and reload completion.
The archive, extracted assets, installed game files and backups stay out of Git.

The [Animated Weapons page](https://www.nexusmods.com/gta4/mods/641) credits
ImpossibleEchoes and Spartan112, with AstoSolen for the animation fixer and KryzS
for alternate models. Its published permissions allow credited reuse/modification
but prohibit selling its assets and require permission for Donation Points.
The supplied archive's README specifically credits shvab (ImpossibleEchoes) for
the script, Sierra112 for models/animations, and Rockstar for original assets.
Its filename is AnimatedWeaponsVanilla; do not assume it is byte-identical to the
currently listed 3.0 Fusion Overloader download. We retain it as a separately
installed dependency rather than redistributing vanilla-derived assets.

The [reference animation source](https://github.com/ImpossibleEchoes/WeaponAnimations)
declares a public-domain dedication in its source headers (Yulian Shvab, 2025).
No implementation from that source has been copied into this fork. Native ABI,
bone ownership and interaction behavior are independently verified here.

## Interaction and ammo policy

- Right Grab/grip press ejects the installed magazine. Its remaining rounds are
  immediately discarded from the player's total, per the user's explicit request.
  Repeated presses without a magazine cannot discard ammo again.
- A chambered round survives magazine ejection and may still be fired.
- Left Grab behind/over the shoulder retrieves a magazine from reserve. It debits
  reserve once, not each frame. While held, the left hand uses a partly closed
  magazine grip for all fingers, independent of trigger/trackpad input; releasing
  or inserting restores free-hand controls. Align it at the bottom of the grip; it snaps to
  an insertion guide, then requires an8.5cm push along the configured grip axis to
  seat. Pulling away cancels the guide. An empty chamber remains empty after insertion.
- Left Grab near the slide, pull backward, then release to feed a round. Racking
  an already-loaded chamber discards that chambered round and feeds the next.
  The palm snaps onto the slide near the starting longitudinal grab point, with
  a partial pinch/curl pose. The visual hand follows the slide and current solved
  pistol wrist, while the original unsnapped Wand still determines pull distance.
  Releasing grip or losing tracking/focus frees the hand. Slide surface placement
  uses the current Glock model's rear pivot and bounded approximate surface;
  this slide attachment was accepted by the user on2026-10-07.
  Crossing the full-pull threshold plays `HANDGUN_RELOAD_COCK_PULL` once;
  releasing that pull plays `HANDGUN_RELOAD_COCK_RELEASE` once at the pistol.
  Holding the slide cannot loop these events. `reload-status` exposes
  `slidePullSounds` and `slideReleaseSounds` request counters; the user accepted
  audibility. These audio requests do not invoke native reload tasks.
- A loose loaded magazine released from the left hand also loses its rounds.
  Weapon switching returns an interrupted held reserve magazine to reserve.
- Native clip capacity remains the cap, including the chamber. No additional
  +1 round is added beyond the game's configured clip capacity.

The deterministic state in `source/includes/vrreload.hpp` owns chamber, inserted
magazine, reserve and a held magazine. Native clip = chamber + inserted rounds;
native total excludes a magazine temporarily in the left hand. The test at
`tools/tests/vrreload.cpp` checks tactical/empty reloads, repeated operations and
ammo conservation over 4096 short operation sequences.

## Runtime weapon profiles

The standard pistol reads `plugins/VRWeapons/pistol.ini` beside the installed ASI.
The repository template is `data/plugins/VRWeapons/pistol.ini`. Edit the **installed**
copy for live tuning. Each future weapon adapter will have its own profile; adding
an INI alone does not enable an unsupported weapon.

From the project PowerShell prompt, apply edits without restarting:

```powershell
& .\tools\game-command.ps1 reload-weapon-config
& .\tools\game-command.ps1 reload-status
```

The first command queues a reload on the simulation callback. Check that
`configRevision` increases and `configValid` is true. An invalid numeric value,
zero/near-zero insertion axis, or missing file sets `configValid=false`, leaves
the revision unchanged, and retains the preceding profile. Missing keys use the
documented defaults. Save the file before issuing the command. A held magazine
updates too; an insertion in progress is canceled without changing its ammo.

| Keys in `[Reload]` | Meaning |
| --- | --- |
| `MagazineHandRotationX/Y/Z` | Degrees about the magazine/weapon local X/Y/Z axes, composed in that order. This changes the loose magazine, not the calibrated hand or pistol. Start with small changes to Y for its outward tilt, then X for roll. |
| `MagazineHandOffsetX/Y/Z` | Meters from the left wrist to the magazine bone origin, in hand axes: X along fingers, Y left, Z up. Increasing X moves it farther forward in the hand. |
| `InsertionAxisX/Y/Z` | Weapon-local direction from the bottom of the grip into the pistol. X is barrel-forward, Y left, Z up; normalized on load. For a pitch angle `a` from vertical use X=`sin(a)`, Y=0, Z=`cos(a)`. |
| `MagazineTipX/Y/Z` | Leading tip in the seated weapon model's coordinates, in meters. This determines where the magazine meets the well opening. |
| `InsertionTravel` | Meters to slide from the opening to its seated position. Default 0.085. |
| `SnapRadius` | Acquisition radius around that opening, in meters. Default 0.045, reduced from the earlier 0.14 center-based gate. |
| `AlignmentCosine` | Minimum dot product of the magazine/channel directions before snap. Default 0.5 permits up to 60 degrees; increase toward 1 to require closer alignment. |

The guide now acquires the magazine's **leading end**, aligns it to the seated
orientation and translates only along the channel axis. The initial 15-degree
channel pitch and tip position are tuning estimates: the model's Mag bone has
identity rotation and does not encode the slanted physical channel. They remain
subject to the user's headset check and can be changed live.

An empty trigger squeeze requests a positional native weapon-bank click
(`GENERAL_GUNS_AK47_DRY_CLICK`, present in this install's `sounds.dat15`) once per
squeeze. It does not invoke Fire, debit ammo, or start a reload animation. The
`emptyClicks` counter proves requests, not audible playback; sound acceptance
remains a headset test.

## Native findings

All addresses below are Ghidra image-base `00400000` addresses for CE1.2.0.59;
resolve the live module base every run. The model probe on 2026-10-07 used base
`00350000` and confirmed the installed model's eight bones:
`W_GLOCK`, `Gun_Trigger`, `Gun_Cock1`, `Mag`, `gun_grip`, `gun_muzzle`,
`gun_ejac`, `gun_grip2`.

- `B5D100`: reload completion, thiscall with one stack argument / RET4.
  Refills clip with min(total, capacity); sets state0, or state4 when empty.
- `B5D180`: weapon state update, thiscall with ped argument. State1 owns shot
  cadence; states2/3 initiate/complete timed reloading. The opt-in local-pistol
  guard retains cadence but blocks automatic reload completion.
- Weapon offsets: type18, state1C, cadence/reload deadline20, clip5C, total60;
  weapon-info86 is the native signed-short clip capacity. Ammo changes use natives.
- `B10780`: allocates an instance skeleton for a drawable with bones. Entity100
  owns that skeleton; skeleton4 is the definition, 10 the bone count, C local
  matrices, 14 world matrices. Shared drawable/rest matrices are never modified.
- `B126E0`: native dynamic-entity pose finishing. The adapter follows the native
  update and edits only the local pistol and its magazine visual instance palette.
- `466BD0`: native skeleton world calculation, resolved through the unpacked
  `B126E0+8D` call with a validated live entry. A loose frozen magazine can have
  its world pose recalculated after `B126E0`. The adapter therefore also applies
  the magazine-only palette after this calculation and after moving its object.
  All hidden bones collapse to the same point; their distinct bind positions
  must not leave unwanted triangles. Instance-local matrices at skeletonC use
  80-byte records; world matrices at14 use64. No shared model data is changed.
- `CFFDD0`: gun-task action eligibility (thiscall, five stack arguments, RET14).
  Action7 requests a reload animation from empty clip plus available reserve,
  independently of weapon state4. The local-pistol guard blocks this request;
  This guard alone did not stop the player's reload loop: its callback counter
  remained zero during the user's failed sound test.
- `DCD3F0`: TaskSimpleReloadGun process (thiscall task, ped / RET4). State0
  calls DCD1A0 to start an animation and sets weapon state2; later states repeat
  the animation. The adapter completes only the local physical-pistol task
  before it can start. This guard stays active during Steam overlay focus loss.
- `DCD610`: task cleanup (thiscall task, ped / RET4). Detaches task48's active
  animation and clears it; may call the separately guarded B5D100 refill.
  Called only if the intercepted local task already owns an animation.
- Loose magazines use the player's native interior room key through
  GetKeyForCharInRoom/AddObjectToInteriorRoomByKey; ClearRoomForObject restores
  exterior ownership. The user confirmed the material is lit outside but
  differed from the gun indoors. The user now confirms the room inheritance
  fixes the lighting.
- `988CC0`: audWeaponAudioEntity constructor, called on the CWeapon object by
  B5C120. Sets vtableE8EC90 and zeros8/C/10; native weapon audio is embedded in
  CWeapon. VR024 bookmark was previewed, read back and saved. This constructor
  alone does not establish an empty-trigger event selector.
- `B60430 -> A545A0 -> 738F10 -> 73A370`: native pistol ray path. Existing arm
  sweep exclusion at73B7D0 did not cover it. A tracked-shot scope now excludes
  owned arm proxies, shooter and held gun before ray hit writes, retaining other
  NPCs, scenery, spread, impact and damage processing.

Ghidra bookmarks VR024 at73A370 andB5D100 were previewed, read back and saved.
The Release shot-filter build resolved the hook live. The user reports that shots
no longer encounter the invisible obstruction. The later native aimed-headshot
correction resolved perceived weak shots per the user's2026-10-07 acceptance.

## Acceptance still pending

The user accepted the physical sequence, magazine-only visual/lighting, tuned
placement/insertion, slide-hand snap/audio and corrected shot strength.
The empty trigger click still lacks explicit audio acceptance. Dropped magazine
visuals are temporary and have no physical collider or recovery interaction in
this first adapter. No haptic or magazine sound feedback,
left-handed ownership, other firearm adapters, episode coverage, save persistence,
or completed VR-disabled reload regression is claimed. The whole #024 remains open.

The first deployed reload build (`EFCCECA0...`) let the user rack the slide, but
showed a full black gun for each loose magazine and did not accept insertion.
It also repeatedly played the native reload behavior, particularly in the Steam
overlay. Readback of a held clone confirmed eight bones with uncollapsed world
bases, despite the initial pose override. The follow-up adds the final world-pose
guard and wider insertion gate fixed magazine isolation and basic reload per
the user's test. Action7 alone did not stop the reload loop. The follow-up
guards the player reload task, inherits interior room assignment and adds a
two-stage insertion. A stopped OpenXR session returns held reserve ammunition
and ordinary reload ownership; overlay focus loss keeps physical reload ownership.

### Headshot investigation

`CA4530` is native lethal-headshot eligibility. It maps the collision part through
`A76700` to head/neck bone IDs4B5/4B4, after victim and weapon gates. Its ordinary
player-pistol branch checks an aiming-state bit or the native gun-target query
`A71D60`; absent either, it uses a random1-in16 lethal flag. Direct tracked muzzle
firing bypasses the ordinary aiming task, so that eligibility branch is a specific
candidate for the user's weak headshots. The adapter now changes only that
aiming-bit branch's condition at `CA46E3` for type7 damage events from the local,
freshly tracked player in focused VR. This selects the same native lethal-headshot
eligibility as vanilla aiming; it does not multiply base/body-shot damage or start
an aiming animation. Initial victim immunity, weapon-class and hit-region gates
still run before this branch. `pistol-status` exposes `aimedHeadshotReady`,
`aimedHeadshotChecks` and `aimedHeadshotOverrides`; the latter counts cases where
native aiming was absent and VR supplied eligibility. The accepted run recorded
six checks/six overrides, and the user confirmed proper shot strength.
`CA2F20` processes armor/head modifiers; `CA4780` applies the final native health
response, including the lethal flag. Preserve their victim immunity/mission rules.

## Native aiming reactions

The opt-in standard-pistol aim adapter sends the native `CEventGunAimedAt` to
NPCs selected by the game's own aim/line-of-sight query. It uses the rendered
pistol muzzle and barrel direction, including the native vehicle-occupant pass.
The right Wand must be above current headset height minus
`[VR] PistolAimRaiseBelowHead=0.40` meters (bounded0.15–1.0; restart to change).
This follows seated/standing/crouched tracking height. Lowering the gun,
tracking/focus loss, menus/phone, weapon changes and entering a vehicle clear
the target dwell. No trigger or vanilla aim button is required.

The native player-gun dwell formula is retained: about0.2 seconds at5m,
increasing to2 seconds at20m; targets beyond20m are excluded. Native NPC
mission/protection/decision-maker rules remain in event handling. No forced
flee/attack task, animation, player aim flag or global targeting field is written.
This supplies NPC threat reactions; it does not yet replace every mission
script's `IS_PLAYER_TARGETTING_CHAR` predicate. The user accepted the pistol
aim-reaction build on 2026-10-07; coverage of other firearms remains open.

CE addresses (Ghidra base00400000): A6EF90 `CTaskComplexPlayerGun` control,
A73AD0 native cdecl aim target selection (five stack arguments), B452F0 gun-aim
event constructor (thiscall, one shooter argument/RET4), B506A0 event-group add,
B45D90 destructor. A6F437's native producer targets ped intelligence+224+84.
Constructor/dtor maintain the shooter reference; event-group add clones the
stack event. These findings are saved in Ghidra VR024 bookmarks.
`pistol-status` exposes `aimThreatReady`, `aimRaised`, `aimHasTarget`, query/event
counts and accepted vehicle-occupant event count. Counters do not prove a visible
NPC reaction.
