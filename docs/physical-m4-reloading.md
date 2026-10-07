# M4 physical reload adapter

Initial coverage under #024: native type15, 30-round clip, native firing cadence
and damage. Hold the right trigger for automatic fire. The user accepted firing
and authored two-handed grip in the first M4 build on 2026-10-07.

The installed Animated Weapons dependency exposes `M4_Mag` and `M4_Gun_Cock1`.
The latter is the rear charging handle: pull it rearward along weapon-local -X
and release to chamber. Right grip ejects; left grip over the shoulder supplies
a reserve magazine. Insert upward through the well, then charge if empty.
Unused `AK_stuff`/`Mag` bones in the same model are not the M4 magazine.
Loose-magazine assets remain a separate dependency and are not distributed here.

`plugins/VRWeapons/m4.ini` supports the pistol profile's magazine hand rotation,
offset, insertion axis/tip/travel, snap radius and alignment fields. The initial
M4 reload sequence is accepted, with user-tuned insertion travel0.05m adopted as
the default. The charging-hand rearward correction still needs acceptance. It also exposes
`RackTravel`, `RackThreshold` and `RackGrabRadius` in meters. `RackGripOffsetX/Y/Z`
move the acquisition point and snapped palm relative to the charging-handle bone
in weapon-local meters; negative X moves rearward toward the stock. The default
is -0.06/0/0, retaining height and lateral placement. This offset changes only the
interaction, not the charging-handle mesh or its travel. Runtime config reload
cancels a partial rack without chambering or changing ammunition. Invalid values retain
the preceding valid profile. Equip the M4 and run:

```powershell
& tools/game-command.ps1 reload-weapon-config
& tools/game-command.ps1 reload-status
```

Check `configFile`, `weaponType`, `configValid` and `configRevision`. Pistol and
M4 configuration/ammunition ownership are independent. Switching returns only a
held reserve magazine to its original weapon and preserves an absent magazine
or empty chamber even when vanilla recreates a full clip on weapon selection.
Native total ammo updates reserve; a cached state is rejected if capacity changes
or the total cannot cover its chamber/magazine. Ejected rounds stay discarded.
Death resets cached state; native pickups can add reserve ammunition.

`rifle-test m4` queues native equipment in living unpaused on-foot free roam.
It retains owned ammunition or grants60 rounds if the rifle is absent;
`melee-test-status` reports completion. No teleport or save operation is involved.
Legacy `pistol-status` counters cover both currently supported firearms.

The user accepted corrected charging-handle placement and weapon-switch ammo
retention. M4 recoil now rotates/lifts the right wrist target before native arm
IK, carrying the visible rifle and its actual muzzle with it. Each successful
local M4 shot adds `RecoilKickDegrees` (default6); the attached support grip scales
new kicks by `RecoilSupportScale` (0.15). Exponential return uses
`RecoilRecoverySeconds` (0.18), with accumulated pitch bounded by
`RecoilMaxDegrees` (user-tuned75, accepted range0–90). All four fields reload through `reload-weapon-config`.
The virtual forearm pivot is0.35m behind the wrist along the barrel; this raises
the hand as well as changing its orientation. Weapon/owner changes and inactive
VR clear the kick. Native firing cadence, spread and damage still run normally.
`pistol-status` reports `recoilShots` and current `recoilDegrees` for this adapter.

The user accepted physical M4 recoil. The tuned profile is included as the default.
Other weapons, episodes and the final shared-controls/fallback check remain open.
