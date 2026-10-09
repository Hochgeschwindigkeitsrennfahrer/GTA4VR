# Pump shotgun physical reload adapter

#024 targets GTA IV native type10 (`w_shotgun`), distinct from the accepted
combat shotgun/type11. Initial baseline: GTA IV1.2.0.59, SteamVR2.17.10,
Vive Wands, DXVK-Vulkan, external Animated Weapons installed separately.

Right trigger fires one native shot per squeeze from the tracked muzzle.
After every shot the chamber is empty; the tube does not feed automatically.
Hold left grip on the fore-end, pull back, then push forward to load the next
shell. You can keep holding that grip and shoot/pump repeatedly. Releasing
grip after pulling back alone does not chamber a round. The gun cannot fire
while the pump is open. A held fore-end reduces recoil like a support grip.

Shell loading uses left-shoulder grip and guided insertion through the loading
gate. Top-up is supported. Ejecting a detachable magazine does not apply to
this tube-fed weapon. Ammo and pump readiness persist independently across
weapon switches; dropped supplied shells are lost.

Tune `plugins/VRWeapons/pump-shotgun.ini` and run `reload-weapon-config` while
equipped. RackTravel limits visible hand travel; RackThreshold is the required
back stroke and RackReturnThreshold is the forward-return tolerance, in meters.
RackGripOffsetX/Y/Z adjusts the fore-end acquisition point. Shell hand pose,
port, rotation, insertion axis/travel, snap and recoil remain independently
configurable. Finite values are accepted without arbitrary range bounds.

Prior live type10 rig has shell bones6/7, gate8 and fore-end grip1. It does not
provide a separate skinned pump/bolt bone: this pass moves the gripping hand
and enforces chambering, without claiming separate pump mesh animation.
Object firing animation uses `gun@shotgun` / `weapon_fire`.

`shotgun-test pump` equips type10 (retain owned ammo, give24rounds if absent),
without teleport/save changes. `melee-test-status` confirms selection;
`reload-status` reports chamber, clip and rack counters.

Optimized ammo tests verify each shot requires a rack and that chamber/tube
counts are conserved through exhaustion, reload and selection resume. Release
build passed. Live pump/shell-loading/support/visual acceptance is pending.

User confirmed firing, required repeated pump cycles and shell loading on 2026-10-09. Accepted installed tuning imported: InsertionRotationY=-20 degrees. No separate animated pump mesh is available in this model.
