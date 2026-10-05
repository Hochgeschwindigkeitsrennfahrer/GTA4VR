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

- 2026-10-04, #013 instrumentation deployment: direct PlayGTAIV launch left a responsive launcher without GTAIV.exe; Steam retry exposed Rockstar's “Grand Theft Auto IV exited unexpectedly” dialog. The OpenXR log had not advanced from the prior session's shutdown, so ASI initialization was not observed on that attempt (no exception trace captured). Clicking the observed Retry button started PID32748, which reached the command bridge/OpenXR. Profiling ASI SHA256 `6A4B83A56C8E42AB5A127F86DF055FF50BC0CF83564AE20D657F49A3A2375E4D`, system D3D9Ex. This is launch recovery evidence, not a cause/fix or reliability claim.
- During #006 final VR-off testing, the external x86 observer captured PID 14640's `C0000005` at runtime `0x76671A46`, allocation base `0x76530000`, symbolized as `combase!Ordinal100+0x87B6`. EDX pointed to loaded `MTLX.dll` at `0x66670000`; the frame chain ended through Kernel32/ntdll. The observer logged dinput8 loading but no FusionFix ASI load before this exception. This does not prove MTLX caused the fault; symbols/stack and the effect of observing need confirmation.
- A later PID 22860 ran the same Release ASI (`CED41E3039DC0F4DEF56ECAAB8EED580DFD5C135B803FE749FB2BE04561009F1`) with system D3D9 (`API=0`), OpenXR/stereo/Ex overrides disabled and the user's 120 FPS preset. RPC was responsive and a fresh native capture showed ordinary apartment free roam with HUD. Earlier pre-ASI/unmapped startup exits are recorded in #006's chronology. Keep local exception logs under ignored `build/`.
- This is separate from the DXVK `vulkan.dll` crash captured in PID 3508 with graphics API=1; the first stereo bridge uses API=0.
- 2026-10-02 bounded startup monitor observed PID14396 C0000005 at76CE1A46 in an unmapped region after dinput8.dll loaded; EDX66CE0000 identifies MTLX.DLL and stack includes that module. FusionFix load not observed in this captured process. ExitC0000005 confirmed; launcher subsequently started PID4748, which reached RPC/OpenXRFOCUSED. Fault cause unproven; do not conflate this pre-ASI failure with PID23004 Steam-overlay fault after stereo frame240. Local stack evidence: ignored build/issue014-bounded-fine-startup-monitor.log.
- First-chance startup observation PID28700 recorded eight repeated MTLX+7314 AV stacks, then1,457,889 handled AV events before exit code1; no FusionFix load observed. Debugger attachment may interfere with protected startup, so this is diagnostic interference evidence, not a VR regression. Do not repeat pre-ASI attach for camera debugging. Added --runtime observer discovery gate waiting for FusionFix module before attach; x86 tool build passed.

- 2026-10-05 post-Windows-restart profiling: some PlayGTAIV attempts left PlayGTAIV/Rockstar Launcher pending without GTAIV.exe after40 seconds. After checking no game process existed, clearing only PlayGTAIV/Launcher and retrying succeeded in the lock-order enabled run. Similar retry needed after restoring original GNU backend. These are launch observations, not proof of backend or OpenXR failure; no source fix or exception trace claimed.

2026-10-05 18:02 local: after clean bridge exit of PID21876 and deploying Release7B46212..., Rockstar log reports GTAIV exited before creating a window with0xC000001D at18:02:05.967. No new scratch diagnostic ran. Cleared failed PlayGTAIV/Launcher processes after confirming no GTAIV, then retried. This repeats the existing startup failure; no evidence attributes it to mesh depth/order code.

2026-10-05 18:33 local: first launch of Release04B2E8ED... exited before window with0xC0000005. Windows event1000 names SHELL32.dll10.0.26100.9278 offset00163E26, GTAIV1.2.0.59. Stereo log had not advanced past18:32:46 preceding shutdown, so new ASI initialization not observed. No diagnostic requested. Cleared failed PlayGTAIV/Launcher after confirming no GTAIV, retried through steam.exe -applaunch12210. Fault module does not establish root cause; preserve separation from renderer tests.
