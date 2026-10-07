# SMG physical reload adapter

First SMG coverage under #024 targets the MP5-style weapon, native type13 and
model `w_mp5`. The Micro-SMG remains separate work. Installed weapon data gives
a30-round capacity,66ms shot interval, base damage20 and60m range. Holding the
right trigger requests automatic fire at the native cadence.

Uses the same accepted magazine ejection, shoulder supply, guided insertion,
charging-handle rack, authored support grip and physical arm recoil system.
Its saved chamber/magazine state and profile are independent of Glock/M4/Deagle.
The external Animated Weapons model/animation assets remain required and are
not distributed here. Firing requests its authored `weapon_fire` clip from
`gun@mp5k`.

Tune `plugins/VRWeapons/smg.ini` with `reload-weapon-config` while equipped.
Live type13 readback confirms lowercase `mag` bone2 at
0.20694/0.017843/0.005284m and `Gun_Cock1` bone7 at
0.291862/0.0174169/0.11512m. The initial insertion point uses the seated magazine
pivot. The user accepted the complete interaction on 2026-10-07; their tuned
defaults use magazine roll140degrees, insertion travel0.03m and rack grip
Y offset0.10m. `Gun_Breach` is a separate bolt bone6, not the charging handle.
Initial recoil is3degrees per shot,0.15 supported scale,0.18s recovery and35
degrees maximum. Right grip ejects; left shoulder grip supplies; push the
magazine into its well, then pull/release the charging handle if empty.

`smg-test` queues native equipment without teleporting or writing a save.
It retains owned ammo or supplies60rounds if absent. Read `melee-test-status`
for completion and `reload-status` for type13, profile/config validity and
animation/reload counters. Command/counter acceptance alone does not prove
correct geometry or headset behavior.
