# 026 — Remove cinematic depth and focus blur in VR

## Goal

Remove cinematic depth-of-field/focus blur from the VR image while preserving other required post-processing and ordinary flat-screen behavior. Own the required post-processing attribution here, using the findings preserved in closed #007. Roadmap stage 5.

## Tasks

- [ ] Identify cinematic depth-of-field/focus blur passes and distinguish them from motion blur, bloom and unrelated post-processing. Record relevant pass order, camera/target ownership and target/history dependencies, including pause/cutscene transitions, before bypassing them.
- [ ] Bypass or configure those blur passes only for the VR rendering path, including scripted focus changes and cutscene transitions.
- [ ] Verify eye targets/history remain valid through cutscene entry, playback, skip and return to gameplay.
- [ ] Capture representative near/far-focused cinematics in both eyes and verify flat-screen settings still behave normally.

## Done when

Tested VR cinematics remain sharp without the requested depth/focus blur, and transitions do not corrupt stereo or remove unrelated effects. Flat-screen behavior passes.

## Notes / evidence

- User specifically requests removal of depth/focus blur in cinematics. Broader forced camera motion and comfort policies remain separate work.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
