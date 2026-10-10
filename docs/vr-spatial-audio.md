# VR spatial audio listener

Issue: [#034](../issues/closed/034-vr-spatial-audio-listener.md).

GTA IV CE 1.2.0.59 builds audio listener transforms independently of our per-eye
viewport overrides. The GTA audio worker at Ghidra VA `008C13B0` calls the audio
update `008C28E0`, which calls `0096D490`. This listener update reads the ordinary
camera matrix at `0128E310` and supplies primary and blended listener frames.

The setters at `008A8BE0` and `008A8F50` accept a matrix pointer, listener index
and gain as three stack arguments with `this` in ECX; both return with `RET 0xC`.
The former derives a spatial inverse, including native axis conversion; the
latter stores the world/distance listener frame. Their native TLS-indexed buffer
selection and gains are retained. These interpretations are supported by their
call chain and disassembly; listening acceptance is recorded in the issue.

The VR camera publishes one head-centre transform per generated stereo frame.
Viewport axes (right/up/back) become native axes (right/forward/up), and position
is the mean of the two eye origins. The native listener update snapshots this
transform once. Only setters called within that update's thread-local scope
receive the VR transform, including active secondary listeners used for camera
blends. No audio update is invoked by an eye render or the scope camera.

Installation verifies all three CE entry signatures. When first-person VR is
not enabled, the hooks are not installed. With no focused headset views or a
head snapshot older than 250 ms, setters retain their original input. Unknown
binary entries retain native audio. Cutscenes use the head transform already
published by the existing VR camera route; their audible behavior remains to be
checked, along with menu/focus transitions and vehicle-specific listening.
The user accepted world-anchored positional audio on 2026-10-10. A VR-disabled
launch retained native listener inputs: 1,023 updates with zero overrides in
both setters, and a fresh image confirmed normal third-person gameplay. This
checks fallback routing; no separate flat-screen listening test was performed.

`tools/game-command.ps1 audio-status` reports readiness, native update count,
setter override counts, snapshot age, and the head/native position and forward
vectors. Counters demonstrate routing, not audible correctness. Ghidra bookmark
`Issue034AudioListener` at `0096D490` records analysis, runtime signature
readback, callback coverage and listening acceptance. The live module base
must be read for each process. TLAD and TBoGT have not been tested separately.
