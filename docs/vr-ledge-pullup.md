# Ledge pull-up input

Tracking: [issue #25](https://github.com/bordplate/GTA4VR/issues/25), roadmap stage 4.

## Native trace and correction

GTA IV CE 1.2.0.59, Ghidra image base `00400000`, baseline `661a5bf`.
The native hang task `CF57C0` calls input helper `CF4210` with mode 1 before
checking clearance through `DAB6F0` and selecting climb animation `72`/state 5.
Disassembly at `CF428D` uses pad `+26B8` (`2698 + 16 * INPUT_JUMP(2)`) and the
native rising-edge query `5C43A0`. Mode 0 reads INPUT_ENTER for dropping.
Climb-state mapper `CF47A0` reports hang/shimmy as states 2/3/4.

VR jump merging previously required `!animatedView`, while TaskSimpleClimb
necessarily gives the native animation body ownership. The fix permits jump
when GET_PED_CLIMB_STATE reports 2/3/4. Other animated states keep the existing
gate. The hang-state lookup is shared with grip-drop and runs only when either
input is requested. No native task, clearance check or animation is bypassed.

Ghidra bookmark `GTA4VR #25` at `CF428D` was read back and saved.

## Checks

x86 Release built successfully with existing updater missing-PDB warnings;
diff check passed. ASI and INI backed up before deployment, INI preserved.
No Computer Use, teleport or save writing. Gameplay checks passed: jump pulls
up from a clear ledge, grip still drops, and ordinary jumping remains functional.
Broader ledge/episode and flat-screen coverage is unverified.

Deployed Release SHA256
`97B535DA5575E3C63791BA86E2FFFBAFEABBC13D00D8283C510AD7ED86119894`.
Startup reached focused continuous DXVK-Vulkan stereo at 1974×2192 per eye on
SteamVR 2.17.10 / Vive Pro and Wands.
