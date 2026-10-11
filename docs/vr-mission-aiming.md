# Physical aiming recognized by mission scripts

Tracking: [GitHub issue #21](https://github.com/bordplate/GTA4VR/issues/21), roadmap stage 6.

## Native evidence

GTA IV CE 1.2.0.59, baseline `36e0640`, Ghidra image base `00400000`.
Do You Have Protection? uses `faustin2.sco`, SHA256
`088CC2BCD4337B141B1A2555B17FFC1000C3300E3DD69DA4235F476368AEC09B`.
Offline inspection of the installed script finds the intimidation helper at
`857A`, called with target local 502 at `8570`. It tests
`IS_PLAYER_TARGETTING_CHAR` (`858C`) or `IS_PLAYER_FREE_AIMING_AT_CHAR`
(`859B`), then `IS_CHAR_ARMED(playerPed,4)` (`85AE`). Its own diagnostic strings
distinguish aiming, not aiming, and aiming without ammunition. Success at
`8658` clears the help and target blip and advances the mission's native logic.

The native free-aim wrapper `BB2430` reaches `BB4000`: disassembly searches
player tasks 6/416 and compares their target with the pool-resolved character.
The targeting wrapper `BB25F0` reaches `BB4520`, comparing a native player
target reference. Both accept character handle zero as any character target.
This explains why sending native threat events produces ordinary NPC reactions
without satisfying script aiming checks. The previous adapter only supplemented
these checks for execution-ready actors.

Ghidra bookmark `GTA4VR #21` at `BB4000` was read back and saved.
Extracted bytecode and game binaries remain outside Git.

## Correction

Both script queries now recognize the local player's physically raised firearm
and obstruction-tested native muzzle ray without requiring execution readiness.
They compare the requested generation-checked ped handle against the fresh ray
candidate; handle zero accepts any valid candidate. A focused headset and a
sample no older than 100 ms are required. Existing native positive results and
queries for other players remain intact. Execution trigger interception and
its first-person camera exception still require native execution readiness.

No mission bytecode/progression flags are patched, and no vanilla aiming task
is started. The mission's weapon/ammunition and other prerequisites remain
authoritative. Ordinary threat-event behavior is unchanged.

## Validation

x86 Release builds successfully, with existing updater missing-PDB warnings;
`git diff --check` passes. ASI SHA256:
`0AFF7C7B19CE8047E19D8260C8999BB9032374164B96F6EA1477F5444C27EFD2`.
Deployed after the user-authorized restart, with ASI/INI backup and installed
INI preserved. Runtime PID 4608 reports the aiming/execution hooks installed,
focused continuous DXVK-Vulkan stereo at 1974×2192, and all four passive
diagnostic controls off. A fresh eye capture confirms the live driving scene.
Test setup: SteamVR 2.17.10, Vive Pro + Wands. No teleport, Computer Use or save
modification. The user confirms intimidation now works in Do You Have
Protection? Other missions, episode
variants, execution regression and flat-screen behavior are not yet verified
for this change. No performance claim.
