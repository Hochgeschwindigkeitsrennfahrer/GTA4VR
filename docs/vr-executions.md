# Native execution interactions

Tracking: [GitHub issue #20](https://github.com/bordplate/GTA4VR/issues/20), roadmap stage 6.

## Evidence (2026-10-11)

GTA IV Complete Edition 1.2.0.59, Ghidra x86 image base `00400000`.
Uncle Vlad is `roman7.sco`, not a numbered Vlad script. Offline inspection of
the installed script archive finds its target in script local 1788. The script
marks this actor ready for execution (`SET_CHAR_READY_TO_BE_EXECUTED`), restores
player control, and selects pistol type 7. Its helper at script offset `11538`
accepts pistols 7/9, requires both actors to exist and be uninjured, and accepts
`GET_CHAR_MELEE_ACTION_FLAG0` or the keyboard free-aim/control-4 path. Success
clears execution readiness and the combat flag, then returns true. The call at
`1186C` advances into the native finish sequence beginning at `11A7A`, which
owns player freezing, cameras, animations and subsequent cleanup.

Verified native wrappers and disassembly:

| Native | Ghidra address | Implementation |
| --- | --- | --- |
| Set ready for execution | `BA1990` | `BAC3F0(handle,bool)` writes CPed `+270`, bit `10` |
| Get ready for execution | `B9EFF0` | `BA6C60` reads the same bit |
| Get combat action flag 0 | `B9EEF0` | `BA6B30` reads CPed `+270`, bit `20` |
| Set combat action flag 0 | `BA16E0` | `BABFA0(handle,bool)` writes bit `20` |
| Free aim at character | `BB2430` | `BB4000` requires native aim-task state |
| Target character | `BB25F0` | `BB4520` compares the native player target |

The setter decompiler prototypes currently omit their second argument;
disassembly and native wrappers establish the two-argument contract. Bookmarks
at `BA1990` and `B9EEF0` were read back and saved under `GTA4VR #20`.
Extracted scripts and the game executable remain outside Git.

## Adapter

- Observe the native ready setter, retaining at most 16 generation-checked ped
  handles. Revalidate native readiness and existence on simulation updates.
- While a ready actor is within 8 game meters and native player control is on,
  classify the interaction as gameplay rather than a scripted service menu.
  Keep tracked hands and first-person view. Actual cutscenes remain native.
- Supplement the two native character-aim queries only for these actors, using
  the existing raised-weapon, obstruction-tested muzzle ray. Ordinary target
  and aim results remain native.
- A pistol trigger squeeze while physically aiming at the ready actor signals
  combat flag 0 through its native setter. The mission consumes that flag and
  owns the execution animation and progression. Avoid shooting/killing the
  actor ahead of that sequence, or starting the vanilla aiming stance.
- Lowering the weapon and moving away remain normal gameplay; no spare outcome
  is invented for missions whose scripts do not provide one. Execution-reticle
  flashing is not reproduced.

## Validation and limits

x86 Release builds successfully; only existing updater missing-PDB warnings.
README cleanup committed separately as `911febd`. No performance claim.
Runtime startup passed on SteamVR 2.17.10 / Vive Pro + Wands with DXVK-Vulkan.
The bridge reports `executionReady=true`, a focused OpenXR session and continuous
1974×2192 eye rendering. A fresh explicit eye capture shows the live apartment
scene. All four passive diagnostic controls remain off. The deployed ASI SHA256
is `125524C853E812A2AD4CE7A88B768710D55ECCC3AA9D35DEE966B721C8181FA9`.
The user subsequently confirmed the execution interaction works in gameplay
and authorized committing/pushing it. Issue #20 records that acceptance.
Other execution missions, TLAD/TBoGT, and a flat-screen regression are pending.
The generic readiness/aim adapter is reusable, but other scripts may require a
different native finish signal. No mission bytecode, completion flag or save is
patched.
