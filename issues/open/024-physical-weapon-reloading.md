# 024 — Firearm coverage and physical weapon reloading

## Goal

Complete the remaining firearm work from closed #016 alongside physical ammunition insertion and weapon charging/cocking, inspired by the interaction requested from Half-Life: Alyx. Depends on #015/#016 and grab interactions in #018. Roadmap stage 6.

## Tasks

- [ ] Record a weapon/episode matrix and implement each class's grip, muzzle, firing/projectile path and support-hand attachment; include shotguns, rifles, automatic weapons, sniper/scoped weapons and special weapons explicitly.
- [ ] Add configurable dominant-hand ownership, mirrored attachment/calibration and opposite-hand support; verify switching and tracking recovery.
- [ ] Verify muzzle obstruction and aim-assistance policy, damage, ammo, cadence, recoil, weapon switching and tracking loss for supported classes.
- [ ] Record a final VR-disabled hands/controls/shooting regression; older flat-screen acceptance predates these hand changes.
- [ ] Inventory weapon reload mechanisms and define per-class magazine, loose-round and charging states with correct ammo accounting.
- [ ] Support eject/remove, grab ammunition, insert/load and physically operate the appropriate slide/bolt/charging mechanism; validate spatial interaction thresholds.
- [ ] Integrate readiness, chamber state, firing, haptics and weapon switching; handle partial reloads, interrupted interactions and tracking loss.
- [ ] Test supported weapon classes from empty/partial states and prevent ammo duplication/loss or firing before the weapon is ready. Verify flat-screen reloads.

## Done when

Supported weapons require their documented physical reload/charging sequence, then fire with correct magazine/chamber/reserve counts. Interrupted and invalid sequences remain recoverable. The weapon/episode matrix records supported 6DOF grip/firing and hand ownership, native damage/ammo/cadence/recoil and obstruction behavior, and a working VR-disabled fallback; unsupported classes stay explicitly listed.

## Notes / evidence

### Scope update - 2026-10-06

At the user's request, this existing issue owns all remaining firearm work from closed #016. Both tracked hands, standard-pistol right-trigger muzzle firing and held left support grip were accepted; other weapon classes, left-handed weapon ownership and explicit gameplay/obstruction/fallback validation remain open. No additional numbered issues are needed. Physical reloading itself remains unimplemented.

- User explicitly requests physically putting ammunition into the weapon and cocking it. Each weapon class needs a suitable mechanism rather than one universal animation. Any unsupported class remains listed.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
