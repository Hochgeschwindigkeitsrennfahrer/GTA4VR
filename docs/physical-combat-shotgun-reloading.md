# Combat shotgun physical reload adapter

Under #024, this adapter targets GTA IV Complete Edition native weapon type11,
the combat shotgun. Its model is named `w_pumpshot` (hash DDB2B5AD); type10
`w_shotgun` is the pump shotgun. Do not infer the gameplay class from these
asset names. The user confirmed the distinction during live equipment tests.
Local baseline: GTA IV1.2.0.59, SteamVR2.17.10, Vive Wands, DXVK-Vulkan.

Right trigger requests one native shot per squeeze from the tracked muzzle.
Native B5FE20 -> B60430 retains pellet firing, cadence and damage. The existing
support grip and physical arm recoil apply. Pump-shotgun support is separate.

Use left grip over the shoulder to take one shell, align it with the loading
gate under the receiver, and push it forward into the tube. Release grip and
repeat to top up. An empty chamber requires pulling and releasing the bolt
with left grip before firing. Right grip does not eject an entire tube of
shells. Dropping a supplied shell loses that round; switching weapons returns
a still-held shell to its original reserve and preserves the loaded state.

The external Animated Weapons assets remain required and are not distributed
here. Live type11 rig: Gun_Shell1 bone4, Gun_Shell2 bone5, Gun_Breach bone7,
Gun_Gate bone3. Loose shell rendering uses only Gun_Shell1; the equipped gun
hides the reload-animation shell meshes. Physical bolt interaction moves
Gun_Breach. Firing requests `weapon_fire` from `gun@baretta`.
Shell, gate and breach are children of `gun_stuff`; its translation
(0.344,0.007,-0.013)m must be included when deriving weapon-relative targets.
The corrected gate is(0.132867,-0.00079973,0.0330923)m.

Tune `plugins/VRWeapons/combat-shotgun.ini`, then run
`tools/game-command.ps1 reload-weapon-config` while equipped. Magazine-named
keys control the held shell; ShellPortX/Y/Z locate the tube entrance in weapon
coordinates. InsertionRotationX/Y/Z set the snapped shell's rotation in degrees
relative to the weapon, applied in X/Y/Z order. X rolls around the weapon's
forward axis; Y and Z tilt it. These do not change InsertionAxisX/Y/Z, which
controls the physical push direction. The configured MagazineTip stays on the
port as the rotation changes. MagazineHandRotationX/Y/Z affect only the shell
held freely in the hand before capture. Acquisition alignment follows the
configured insertion rotation. Apply edits with reload-weapon-config; this
releases an active guide so the next approach snaps with the new tuning.
RackGripOffsetX/Y/Z adjust the bolt-grab point. Support hand and
recoil tuning use the same keys as the other firearm profiles. Values are
checked for valid finite numbers; the insertion axis must be nonzero.

`shotgun-test combat` selects type11, preserving owned ammunition or giving24
shells if absent. It does not teleport or write a save. `melee-test-status`
reports equipment completion; `reload-status` reports the active adapter.

The user accepted this combat-shotgun pass on2026-10-09 after live tuning.
The exact installed profile is stored as the distributed default, with matching
source fallback values: hand offset(0.075,0.02,0)m, insertion axis(1,0,0.5)
(normalized when used), insertion rotation(0,-60,0)degrees, and bolt grip
offset(0,-0.05,0.02)m. Release compilation and deterministic ammo/rotated-tip
anchor tests passed. Broader episode/fallback coverage and remaining weapons
continue under #024; this acceptance does not close the entire firearm issue.
