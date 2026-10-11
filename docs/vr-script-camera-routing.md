# Script cameras and VR input ownership

Tracking: [issue #22](https://github.com/bordplate/GTA4VR/issues/22), roadmap stage 6.

## Evidence — 2026-10-11

Do You Have Protection? gun-store tutorial leaves the VR player in third person
with movement suppressed. CE 1.2.0.59, SteamVR 2.17.10, Vive Pro/Wands,
DXVK-Vulkan. Current run preserved during investigation; no teleport or save
modification, no Computer Use.

PID 4608, GTAIV base `00650000`: bridge reports script-camera resolved/active
and Menus scripted interaction active. Read-only native player lookup resolves
player info `24302550`; its control-disable mask at `+4C8` is zero.
Ghidra `BB23F0` is the IS_PLAYER_CONTROL_ON wrapper (`30CD2F1F`), calling
`BB3FA0`. Disassembly and decompilation confirm the single-player path compares
this mask against zero. The separate network path is outside project scope.

FusionFix gunlockup.sco SHA256:
`EFA307AE083429AE49DF00F7E7D6D4454270F4742E099F7BC8BFC1F1CCEDC1AA`.
Its tutorial disables control at `29F2`, activates/interpolates script cameras,
then calls ACTIVATE_SCRIPTED_CAMS(false,false) at `2EB2` and restores native
control through SET_PLAYER_CONTROL_ADVANCED at `2ED8`. Purchase preview disables
control at `3BD1`, enables its cameras at `4806`/`48C2`, and restores control at
`632E`. Inspected clothing-store scripts likewise disable native control for
their previews. Merely finding a propagating child under the script-camera root
does not establish that a menu owns player input after tutorial cleanup.

Ghidra bookmark at `BB3FA0` under `GTA4VR #22` was read back and saved.
Raw script extraction and bounded process captures remain ignored.

## Correction and limits

After resolving a script draw camera, VRCamera now checks native player control
with a valid player and native-handler guard. If control is enabled, it keeps
first-person gameplay and the gameplay action set. If disabled, it keeps the
native preview and Menus input. This generalizes the execution camera exception
without a gun-store or mission-name special case. The native camera state,
player control, purchases and mission progression are not modified.

x86 Release builds with existing updater PDB warnings; diff check passes.
ASI SHA256 `C01CF536DD9C4AC2041BB59839B0B92B848BD05CD6AFCA211832B3836F72510E`.
Deployed after user-authorized restart, with ASI/INI backup and preserved INI.
PID 25748 is responsive and reports focused continuous VR rendering, resolved
script camera with scriptCameraActive=false in ordinary gameplay, and all four
passive diagnostics off. Fresh eye capture confirms the live apartment scene.
The user accepted the camera-routing correction in response to the gun-store
check and authorized committing/pushing it. Broader purchase/mission coverage,
wardrobe regression, other script cameras and flat-screen acceptance remain
unverified. No performance claim.
