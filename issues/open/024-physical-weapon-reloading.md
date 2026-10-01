# 024 — Physical weapon reloading

## Goal

Implement physical ammunition insertion and weapon charging/cocking, inspired by the interaction requested from Half-Life: Alyx. Depends on #015/#016 and grab interactions in #018. Roadmap stage 6.

## Tasks

- [ ] Inventory weapon reload mechanisms and define per-class magazine, loose-round and charging states with correct ammo accounting.
- [ ] Support eject/remove, grab ammunition, insert/load and physically operate the appropriate slide/bolt/charging mechanism; validate spatial interaction thresholds.
- [ ] Integrate readiness, chamber state, firing, haptics and weapon switching; handle partial reloads, interrupted interactions and tracking loss.
- [ ] Test supported weapon classes from empty/partial states and prevent ammo duplication/loss or firing before the weapon is ready. Verify flat-screen reloads.

## Done when

Supported weapons require their documented physical reload/charging sequence, then fire with correct magazine/chamber/reserve counts. Interrupted and invalid sequences remain recoverable.

## Notes / evidence

- User explicitly requests physically putting ammunition into the weapon and cocking it. Each weapon class needs a suitable mechanism rather than one universal animation. Any unsupported class remains listed.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
