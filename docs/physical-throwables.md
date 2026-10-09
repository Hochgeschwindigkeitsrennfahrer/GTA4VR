# Physical grenade and Molotov throws

Issue #024, GTA IV Complete Edition 1.2.0.59, native grenade type4 and
Molotov type5. Select the weapon normally, hold the right Fire action
(Wand trigger), move the hand to throw, then release. A stationary release
drops it with negligible launch speed. Tracking/focus loss, pause, phone,
death, vehicle entry and weapon changes cancel preparation; releasing the
trigger after recovery is required before another throw can be prepared.

The native B5FE20 weapon dispatch performs its projectile branch B60870
and creator9BE040. Native ownership, ammo, fuse, impact fire and explosion
behavior are retained. A unique-signature midhook at Ghidra9BEA35 replaces
the world-linear velocity vector passed to B25F10 only during our own
simulation-thread throw call. The subsequent angular-velocity setter is
unchanged. Release origin is the currently held model's position; velocity
is estimated from recent tracked world-hand positions using the existing
60ms throw-motion window. No camera-directed throw or aiming stance is
requested. Grenade cooking is not implemented in this first pass.

Native WeaponInfo specifies grenade FUSE5000ms and Molotov IMPACT.
After a successful native throw, the next clip is prepared only if native
reserve ammunition remains. No ammo is added by the gameplay adapter.
Flat-screen and other shooters retain native dispatch.

Development commands `throwable-test grenade` and `throwable-test molotov`
select the corresponding weapon in living unpaused on-foot free roam.
They preserve owned ammo, or supply8 if absent. They do not move the
player or write saves. `hand-status` reports throwReady, throwAttempts,
throws, throwVelocityOverrides and throwSpeed for diagnosing release.

Final Release4D1C61A5 built and deployed with ASI/INI backups and existing
weapon profiles retained. Ghidra9BEA35,9BF3B0 and9C1940 findings previewed,
read back and saved. User accepts throws and the final self-collision fix
on2026-10-09 and requests closing #024; TLAD/TBoGT remains unverified.

Throw distance tuning: [VR] ThrowableVelocityScale=4.0 multiplies tracked grenade/Molotov release velocity. [VR] HandThrowVelocityScale=4.0 sets the requested release speed for grabbed props, with existing mass/force-limited transfer retained. Both settings accept finite numeric values without arbitrary range clamps and currently require restart. Prop desired release velocity is no longer capped at10m/s; stationary releases do not receive a forward launch.

Tracked throws register the native projectile instance (ECX at9BEA35). Contact pairs exclude its whole throwing player, held weapon model and owned limb proxies until native9BF3B0 removes the projectile. Molotov impact update9C1940 separately raycasts through A536B0 after Weapon::Fire returns; it runs inside the same owner/query exclusion scope. This replaces the insufficient500ms grace. World contacts and area explosion damage remain native. hand-status.hands.physics.excludedThrowPairs counts contact exclusions; protectedThrows reports live registered projectiles. Only our tracked throws receive these exclusions; the user accepted this correction.
