# 036 — Gate runtime VR diagnostics

## Goal

Keep diagnostic tools available while normal Release gameplay avoids unsolicited
tracing, observation hooks, captures and counter bookkeeping. Make the visible
first-person body a normal setting, enabled by default.

## Tasks

- [x] Add independent diagnostic settings, default off.
- [x] Keep gameplay state, safety checks and explicit profiling commands functional.
- [x] Replace the debug body flag with ShowFirstPersonBody, default on.
- [x] Build x86 Release and check VR rendering, diagnostics off/on and fallback.

## Done when

Normal VR retains the body and gameplay features with diagnostic work disabled;
diagnostic tools remain available by explicit opt-in.

## Notes / evidence

- 2026-10-10: User requested this after #034. Baseline a81863a; GTA IV CE
  1.2.0.59, SteamVR 2.17.10, DXVK/Vulkan, Vive Wands. Installed ASI SHA256
  7787E4AB094787D05B4DA2184DDA1F058D52BEB13E57AA86EA55503BE61FA68F.
  Full profiler inactive; periodic logs and passive observers still active.
- Implemented independent startup toggles for logging, passive counters/snapshots,
  observation-only hooks and automatic image readback. Body visibility defaults
  on under [VR] ShowFirstPersonBody, independent of hand IK. Existing explicit
  probes/profiling/capture commands remain opt-in; scheduling, collision, ammo,
  tracked-pose publication and command-buffer safety state remain functional.
- x86 Release passed (existing deprecated upstream callback and libmodupdater
  missing-PDB warnings). Installed AD96FDFA18E67F04E2FDDCA47A22E57316CFF9ADA74FC43E98B51B6887865EDA
  after backing up installed ASI/INI. No Debug build. With all four diagnostic
  settings on, PID15312 reached FOCUSED continuous DXVK/Vulkan stereo, ready=true,
  zero command-buffer overflows. Bullet observer ready=true; audio counters
  advanced (97 updates, 35/42 setter overrides in the early sample); automatic
  eye BMPs and stereo/OpenXR logs were written. No FPS gain claimed.
- Final Release SHA256 0FD45C42BF8E8D5E6246415EC7F9E95F0AC2AAB8ED1CB8C65CFE72ADF864A442.
  Follow-up review retained raw phone button publication as gameplay state and
  gated the remaining D3D9Ex startup log. A restricted incremental build hit
  C1902; the same Release build outside the sandbox succeeded.
- Diagnostics-off fallback PID10144 reached normal third-person gameplay in a
  fresh window image, OpenXR disabled; counters stayed zero and bullet observer
  absent. Final diagnostics-off VR PID28332 reached FOCUSED continuous stereo,
  ready=true, eyeImages=true and 2388 submitted layers at the sampled readback.
  Fresh game image showed the apartment rendering. Hand hooks and 20 physics
  shapes remained active, passive counters zero, native headshot/threat hooks
  available, bullet observer absent. ShowFirstPersonBody=1 remains installed.
- No automatic BMP or stereo/OpenXR log file changed during disabled runs.
  D3D9Ex log grew by one startup line in the earlier fallback run; its last
  timestamp remained 14:09:25 after the final logging correction and VR launch.
  An explicit CPU capture still produced 594 renderBeforeXRWait samples with
  all toggles off, and was stopped. This validates tool availability, not an
  equivalent-scene performance improvement.
- Earlier launch attempts generated delayed Windows APPCRASH records in
  ucrtbase.dll (0xc0000096, offset 0x00101ac1). Current final run initialized and
  stayed responsive. Cause of those intermittent startup failures is unproven;
  no new startup fix is claimed. No teleport or save modification performed.
- Diagnostics are documented in docs/vr-diagnostics.md and default off in both
  distributed and installed INIs. Gameplay physics/pose/audio/routing and safety
  work remains active; broad gameplay was not revalidated by this scoped check.
