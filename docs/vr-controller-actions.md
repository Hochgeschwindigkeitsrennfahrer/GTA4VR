# VR controller actions and menu pointer

Accepted in the headset and SteamVR binding editor by the user on 2026-10-06,
using CE1.2.0.59, SteamVR2.17.10, Vive Wands and DXVK/Vulkan. Win32 Release and
focused ray/quaternion tests passed. Core #020 UI is closed; broader controller validation remains #015 and UI coverage/lifecycle is #031.

OpenXR now exposes operations rather than coupling gameplay to finger inputs.
SteamVR receives the named action sets when the VR session initializes. Select
the running GTA IV application in SteamVR's controller bindings and edit its
actions. Existing custom bindings for the old physical-input action names may
need to be reset to the new defaults before editing; we do not overwrite custom
SteamVR binding files.

## Vive Wand defaults

| Context | Action | Default |
| --- | --- | --- |
| Always | Open / close pause menu | Left Menu |
| On foot | Move / enable movement | Left trackpad XY / touch |
| On foot | Sprint | Hold left trackpad click; outer-edge movement still jogs |
| On foot | Jump | Right trackpad center click |
| On foot | Enter vehicle / interact | Right Menu (currently vehicle entry) |
| On foot | Fire equipped weapon | Right trigger value; existing pistol firing path |
| On foot | Support weapon | Left grip near the weapon; hold to retain grip |
| On foot | Activate weapon selection / direction | Right trackpad click / XY; horizontal sides cycle |
| On foot | Next weapon / previous weapon | Unbound independent actions available for rebinding |
| Driving | Steer / enable steering | Left trackpad XY / touch |
| Driving | Accelerate | Right trigger |
| Driving | Brake / reverse | Left trigger |
| Driving | Handbrake | Right trackpad click |
| Driving | Exit vehicle | Right Menu |
| Menus | Select / drag | Point with the right Wand and press/hold its trigger |
| Menus | Back | Right Menu |

The right pointing pose is preferred; the left pose is a fallback when the
right pose is unavailable. Finger curl, free-hand closing, thumb contact and
thumb pose direction are separate actions in **Hand animation and tracking**.
Rebinding Fire does not rebind index-finger animation. Hand poses remain active
in menu context; on-foot and driving input sets are inactive there.

For the default shared right trackpad click, weapon-selection XY distinguishes
center Jump from side selection. A Jump binding independent of weapon-selection
activation bypasses that region test. Unbinding movement/steering activation
makes the corresponding analog action sufficient by itself (for thumbsticks).

## Pointer implementation and limits

The cyan ray starts at the OpenXR pointing pose and intersects the same anchored
2.4m-wide quad used for the native pause screen. UVs feed native mouse x/y through
the game's own scale; trigger holds feed current/previous left-button bits and
Back feeds the native right-button bit. No desktop cursor movement or injected
Windows keys are needed. A small cursor is drawn on the captured menu image;
the ray is a separate world-space quad sharing a four-row color atlas strip.
That strip is excluded from the menu's displayed image.

Pointer selection is disabled on loading screens, without focus, and without
fresh tracking. Gameplay inputs likewise require fresh focused samples. Native
mouse/gamepad controls remain available when VR is disabled. Episode-specific
menus, map dragging and all slider/modal variants still require gameplay checks.

Native CE1.2.0.59 evidence: frontend query `008B4B30`; tab processing `005AAA60`;
mouse record `018B7A80` (x, previous buttons, current buttons, y). Native back
also reads bit8; native tab click reads bit1. Runtime addresses are relocated;
the implementation uses existing FusionFix signatures and menu wrappers.
Ghidra bookmark `Issue020MenuPointer` was read back and the program saved.

References: [OpenXR action sets](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrActionSetCreateInfo.html),
[OpenXR action synchronization](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrSyncActions.html),
[Valve OpenXR controller bindings](https://partner.steamgames.com/doc/features/steamvr/settings?language=english),
[IV-SDK control enumeration](https://github.com/Zolika1351/iv-sdk/blob/master/include/ePadControls.h).
