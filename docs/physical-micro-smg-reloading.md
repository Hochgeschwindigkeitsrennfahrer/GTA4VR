# Micro-SMG physical reload adapter

First Micro-SMG coverage under #024 targets native weapon type12, expected
`w_uzi`, separately from the accepted MP5/type13 adapter. External Animated
Weapons assets remain a local dependency and are not distributed here.

Holding right trigger requests automatic native fire at the weapon's cadence.
The tracked muzzle, native damage, held support grip, physical arm recoil and
native aiming reactions reuse the existing firearm paths. The weapon's shot
animation uses `weapon_fire` in `gun@uzi`.

Right grip ejects the magazine and discards its remaining rounds. Left grip
over the shoulder supplies a magazine. Push it into the handle, then pull and
release the top charging handle if empty. Ammo state is saved independently
of other weapon types across selection changes. Physical reloading is opt-in
through the existing ExperimentalPhysicalReload setting.

Reference model has lowercase `mag` bone4, Gun_Cock1 bone2 and child
Gun_Breach bone3. Both charging parts move during a manual rack; native firing
animation owns them between shots. Reference magazine pivot including the
root is(-0.03807348,-0.00078788,-0.14679066)m. Initial insertion travel0.06m,
rack travel0.045m and rack grip Z offset0.025m remain subject to live tuning.

Tune `plugins/VRWeapons/micro-smg.ini` and run `reload-weapon-config` while
equipped. It has independent hand, magazine insertion, charging handle,
support grip and recoil keys. Initial recoil3degrees per shot,0.15 supported
scale,0.18s recovery and35degrees maximum. Numeric tuning accepts finite
values without arbitrary range limits; the insertion axis must be nonzero.

`smg-test micro` requests type12, retains owned ammunition or gives60rounds
if absent. No teleport/save changes. `melee-test-status` reports equipment
readback and `reload-status` reports the selected profile/rig/ammo.

The user accepted firing, support grip and physical reloading on2026-10-09.
Their exact installed profile is stored as the default: held magazine roll145
degrees and snap radius0.06m, with matching source fallback values.
Release compilation passed before the fallback-default synchronization.
GTA IV1.2.0.59 /
SteamVR2.17.10 / Vive Wands / DXVK-Vulkan is the initial target; episodes and
remaining firearm/fallback coverage stay under #024.
