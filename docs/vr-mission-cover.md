# Concrete Jungle cover tutorial

Tracked in [#12](https://github.com/bordplate/GTA4VR/issues/12), roadmap stage 6.

## Evidence (2026-10-11)

Baseline: repository `3debae2`, GTA IV Complete Edition 1.2.0.59,
SteamVR 2.17.10, DXVK/Vulkan, Vive headset and Wands.

The [published jacob1 mission decompilation](https://github.com/0x52617665/GTAIVMissionScripts/blob/master/IV/jacob1.txt)
identifies Concrete Jungle's cover tutorial. Its main phase 11 presents
`JA1_H4` when the player reaches the required wall marker. It waits for
`IS_CONTROL_PRESSED(2, 28)` (or a developer keyboard path), disables player
control and assigns `TASK_SEEK_COVER_TO_COVER_POINT`. The next phase proceeds
when the player is covered **or after two seconds**. Phase 12 runs the door
opening and aiming tutorial, including scripted cameras, temporary freezing,
position confinement and repeated cover-task assignment. Cleanup restores
control and unfreezes the player. Later player cover queries govern tutorial
hints; Jacob's cover queries/tasks are separate NPC behavior.

Independently decoded the installed `script.img` entry `jacob1.sco`:
SHA-256 `5BE16E41C935128B15AF25FF3ABB94290F6F8EE1E58F0230A61F16FE6AEF85A8`.
The code contains `JA1_H4` at offsets `0xD750`, `0xD766`, `0xD809`, the
pad-2/control-28 query and following player-control/cover-task natives.
Offsets here refer to the SCO code section, not executable addresses.
Decoded bytes and published script material remain under ignored `build/`.

Read-only native inspection in PID 26328 (module base `0x00D00000`) confirmed
`IS_PED_IN_COVER` hash `0x5C825D83` registration at Ghidra `0x00BB139D`,
with wrapper `0x00BA0150`. That wrapper has not been decompiled/typed in
Ghidra; no internal predicate semantics are inferred. Saved and read back an
evidence bookmark at the registration address in `/GTAIV.exe`.

## Implementation

The user explicitly chose to remove the mandatory cover-input gate and player
cover assignments in VR. The first experiment synthesized control 28, which
the user confirmed entered cover but made exiting fiddly. A subsequent
automatic task-cancellation approach was rejected before deployment. Both
approaches have been removed from source.

The replacement intercepts two native wrappers, only for the executing
`jacob1` script while an OpenXR session is running:

- Its sole `IS_CONTROL_PRESSED(2,28)` query returns true directly, removing
  the cover-input condition without sending input to the player motor.
- `TASK_SEEK_COVER_TO_COVER_POINT` calls targeting the actual local player
  are omitted. NPC calls and sequence-placeholder ped 0 calls remain native.

The mission still owns the wall marker and story sequence. Its existing
two-second fallback handles entry without a cover task; its existing
task-status-finished path handles the aiming tutorial without a cover task.
The user confirmed this sequence progresses without forced cover. Temporary
story-camera freezing/positioning remains; there should be no player cover
task to escape afterward. No synthesized cover input or ClearCharTasks call
remains. Session lifetime, rather than focus, gates the adapter so opening the
SteamVR dashboard does not reenable scripted player cover.

CE native registration patterns were unique in runtime PID 23444/base
`0x00D00000`: control wrapper `0x00B984B0`, registration `0x00B995E2`;
cover-task wrapper `0x00BBA5B0`, registration `0x00BC4B97`.
Runtime disassembly of the task wrapper verifies a cdecl native context at
stack +4, argument buffer at context +8, and six 32-bit arguments. Both hooks
must install before either adaptation activates. Explicit locomotion status
exposes `missionCoverAdapterReady`; this is installation evidence only.

The global cover predicate, NPC tasks, mission variables, installed mission
archive and saves are unchanged. With VR disabled or no running session,
both wrappers retain native behavior. Episode missions and other cover gates are not
claimed supported by this mission-specific rule.

## Validation

Win32 Release build passed (existing modupdater missing-PDB linker warnings).
Backed up the installed ASI/INI, verified bridge quit and process exit, then
deployed matching build/installed SHA-256
`57C31EEF3C1802F3AA7A5916B5FE5873F110C9CDEF9CA7436F2F9A38E6EA95CC`.
The normal relaunch reached the command bridge with OpenXR initialized.
The user confirmed automatic cover entry but found exiting it fiddly; that
feedback motivated the subsequently rejected cleanup experiment. This build
is superseded by the replacement below. No performance claim or save modification.

The replacement adapter also passed Win32 Release. After the user confirmed
their mission run had ended, backed up the installed ASI/INI, verified process
exit, and deployed matching SHA-256
`021881F0761AF6E74831F29880729CD046447B7F7678CB7AB348BC9C0EA11651`.
The cover input/cancellation experiments are superseded. Replacement mission
progression and absence of player cover were confirmed by the user.

Replacement startup verified: GTAIV PID 26460 responsive, OpenXR FOCUSED with continuous DXVK-Vulkan stereo, missionCoverAdapterReady=true. Fresh native window capture shows the mission car scene. Hook installation is confirmed.


Manual acceptance (2026-10-11): the user confirmed the wall/door sequence
proceeds without forced cover and they can continue afterward ("Yup, that
works, you can commit it."). Flat-screen regression for these new hooks remains
unverified; #12 remains open for that check. No performance claim.

