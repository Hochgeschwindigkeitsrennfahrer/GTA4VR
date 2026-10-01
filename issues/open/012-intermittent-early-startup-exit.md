# 012 — Diagnose intermittent early startup exits

## Goal

Make launch testing reproducible by identifying intermittent exits before FusionFix initialization. Do not attribute them to the VR renderer without a supported call stack and controlled comparison.

## Tasks

- [ ] Record whether each failed launch occurs before ASI load, during initialization or after world rendering, with exact executable/backend/build versions.
- [ ] Capture unhandled exceptions using the bounded external observer; compare observed and unobserved launches because debugger attachment can affect startup.
- [ ] Analyze the relevant game/loader boundary with Ghidra and runtime evidence; keep annotations and repository notes consistent.
- [ ] Compare the known baseline ASI and current VR-disabled build with preserved configs. Avoid changing saves or authentication state.
- [ ] Implement a scoped fix if the cause belongs to this fork; otherwise document a reproducible external dependency failure and tested recovery.

## Done when

The launch failure has an evidence-supported cause and either a verified fix or a reproducible external limitation with recovery steps. Record repeated launch results; one successful retry is not proof of reliable startup.

## Notes / evidence

- During #006 final VR-off testing, the external x86 observer captured PID 14640's `C0000005` at runtime `0x76671A46`, allocation base `0x76530000`, symbolized as `combase!Ordinal100+0x87B6`. EDX pointed to loaded `MTLX.dll` at `0x66670000`; the frame chain ended through Kernel32/ntdll. The observer logged dinput8 loading but no FusionFix ASI load before this exception. This does not prove MTLX caused the fault; symbols/stack and the effect of observing need confirmation.
- A later PID 22860 ran the same Release ASI (`CED41E3039DC0F4DEF56ECAAB8EED580DFD5C135B803FE749FB2BE04561009F1`) with system D3D9 (`API=0`), OpenXR/stereo/Ex overrides disabled and the user's 120 FPS preset. RPC was responsive and a fresh native capture showed ordinary apartment free roam with HUD. Earlier pre-ASI/unmapped startup exits are recorded in #006's chronology. Keep local exception logs under ignored `build/`.
- This is separate from the DXVK `vulkan.dll` crash captured in PID 3508 with graphics API=1; the first stereo bridge uses API=0.
