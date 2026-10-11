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
| On foot | Enter vehicle | Right Menu (currently vehicle entry) |
| On foot | Interact with shops / vendors | Left trigger click; native pickup/context input |
| Menus (scripted interactions) | Navigate / confirm | Right trackpad click: four directions, center confirms |
| Menus (scripted interactions) | Back / leave interaction | Right Menu |
| Menus (scripted interactions) | Navigate up/down/left/right; Confirm | Independent actions for custom D-pad bindings |
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

## Cutscene and contextual interaction routing

During a loaded, unfinished cutscene, **Pause / skip cutscene (tap)** sends a brief native frontend-confirm input instead of opening pause or the phone. The game retains skip eligibility and mission bookkeeping. The existing Pause binding is unchanged.

SteamVR 2.17.10's tested editor displays internal OpenXR names rather than the
friendly labels supplied by the application. Bind **`on_foot` →
`context_interact`** to start native contextual interactions (default left trigger
click). **`on_foot` → `interact`** is vehicle entry; it does not activate shops or
vendors. **`menus` → `confirm`** only confirms an interaction already in progress.
Custom SteamVR bindings need the new actions added manually. The vehicle-entry
action remains separate.

For right-touchpad directional browsing, bind Position to `navigate_direction`
and Click to `navigate_click` under `menus`. A direct `confirm` binding always
confirms; it does not interpret the click position. Alternatively use SteamVR's
D-pad mode with `navigate_up/down/left/right` and center `confirm`. Both `back`
and `interaction_back` exit scripted interactions. Save the personal binding
after editing so a later session uses the updated controls.

A valid propagating script draw camera activates the existing **Menus** action set and retains the native third-person character, including the head. Navigation supplements native frontend controls and raw gamepad queries, independent of keyboard/gamepad selection. Sampled short presses survive between render and simulation ticks. Pause pointer actions are read only during pause; scripted navigation is read only during scripted interactions. Phone routing takes precedence.

The contextual input does not require a first-person camera. Native scripts retain proximity, availability, prices and mission progression. Camera-free menus and service variants still need gameplay acceptance; see GitHub #13 and #15 and `docs/contextual-interactions.md`.

## Ladder controls

Near a native ladder, forward/back on the left movement touchpad requests
native ladder entry. While attached, forward climbs up and back climbs down;
releasing the touchpad stops climbing. Native alignment, ladder animation and
endpoint exits remain in charge. Physical hand-over-hand climbing is not
implemented. Gameplay acceptance and remaining coverage are tracked in
[issue #19](https://github.com/bordplate/GTA4VR/issues/19).

On CE 1.2.0.59, A71F50 gates the CFB330 ladder scan on the jump action.
The existing FusionFix AutoLadder hook can also bypass that input gate when
VR movement requests entry, without changing the saved AutoClimbLadders
setting. Native task CF9F30 reads shared vertical controls 14/15 through
8F00F0 and 8EFF80. These controls were suppressed whenever animatedView was
active, including ladder climbing. The VR pad update now permits them for
ladder tasks, while native body ownership includes both ladder approach and
climbing. Ghidra bookmark at CF9F30 is saved under GTA4VR #19.

Win32 Release D4FB4A6D7E52B98DE32D27ABF7B4B8248EA5BA0FEB7316FAA17A4642CDA63382
built and deployed with ASI/INI backup; existing updater PDB warnings only.
Baseline CE 1.2.0.59 / SteamVR 2.17.10 / Vive Pro and Wands / DXVK-Vulkan.
Runtime ladder and flat-screen acceptance remain pending. The user authorized
this restart and returning to the ladder; a read-only entity transform captured
the location because the development bridge refuses capture during missions.
Location restoration does not restore mission progress. No performance claim.

Runtime PID25488 reports focused continuous stereo with all four passive
diagnostic controls off. Native restore completed and read back the saved
position within the bridge tolerance; a fresh eye capture confirms ordinary
world rendering at the restored location. User ladder gameplay check requested.

The user confirms native entry, climbing up/down, stopping and endpoint exit
work in this build and authorizes committing it (2026-10-11). This accepts the
tested VR ladder control path. Mission replay, broader ladder/episode coverage
and flat-screen regression remain unverified; issue #19 retains those checks.
