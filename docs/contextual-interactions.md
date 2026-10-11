# Native contextual interaction tracing

Active work: [clothing purchases #13](https://github.com/bordplate/GTA4VR/issues/13)
and [NPC services #15](https://github.com/bordplate/GTA4VR/issues/15).
This is analysis evidence, not runtime acceptance of VR purchases.

## Baseline and evidence

2026-10-11: GTA IV Complete Edition executable version 1.2.0.59; repository
HEAD `4a0dcb77978a506ee2686b2ab8a7906ba0da300b`, plus the uncommitted
Menu/cutscene-input change. The game was not running during this investigation.
Existing changes to controller routing, action labels and README were preserved.

Read the published [GTA IV script decompilation](https://github.com/0x52617665/GTAIVMissionScripts/tree/master/IV)
for control flow, then independently decoded the repository's current FusionFix
SCO files to check native input hashes and immediate arguments. Instruction
lengths and the existing SCO AES decoding convention were checked against
[SCO ToolBox's decoder](https://github.com/Flitskikker/SCO-ToolBox/blob/master/clsSCO.cs).
Control names come from [IV-SDK ePadControls](https://github.com/Zolika1351/iv-sdk/blob/master/include/ePadControls.h).

Local decoded/decompiled material and the temporary read-only inspector are in
ignored `build/interaction-trace/`; game script source/bytes are not included
in this note. Published scripts are leads for behavior, not a guarantee that
every instruction matches CE/FusionFix. The current Binco bytecode, for example,
contains extra raw keyboard just-pressed navigation tests.

After Ghidra was started, connected to project `GTAIV` and confirmed the open
`/GTAIV.exe` program: Windows x86, image base `0x00400000`. Native handlers were
verified through registration hashes, disassembly and decompilation. Findings,
names, verified prototypes and bookmarks were read back and saved in the project.
No live runtime call was captured. Offsets in the script input table below are
**SCO code-section offsets, not Ghidra or relocated process addresses**.
TLAD and TBoGT remain untested.

## Native input handlers

These are Ghidra addresses for CE 1.2.0.59; obtain the live module base before
using them in a running process.

| Native | Hash | Registration PUSH | Wrapper | Implementation |
| --- | --- | --- | --- | --- |
| IS_CONTROL_PRESSED | `0x0E635761` | `0x00B995E7` | `0x00B984B0` QueryScriptControlPressed | `0x00B98BE0` CheckScriptControlPressed |
| IS_CONTROL_JUST_PRESSED | `0x4CB729F1` | `0x00B99767` | `0x00B98490` QueryScriptControlJustPressed | `0x00B98BA0` CheckScriptControlRisingEdge |
| IS_BUTTON_PRESSED | `0x7FF21081` | `0x00B994D3` | `0x00B98470` QueryScriptButtonPressed | `0x00B98B60` then `0x00B99220` |
| IS_BUTTON_JUST_PRESSED | `0x016C37CD` | `0x00B9973A` | `0x00B98450` QueryScriptButtonJustPressed | `0x00B98B20` then `0x00B992F0` |

`GetScriptControlPad` at `0x00B991C0` routes pads 0/1 through `0x008F69E0`
(player/entity-selected table), and other indices, including pad 2, through
`0x008F69D0`, which returns the fixed local table at `0x0117E700`. This is the
table already updated by our VR button merge.

For control 23, the entry is at `pad + 0x2698 + 16 * control`: key byte at +4,
current value at +6, previous value at +7. Held input tests
`(current XOR key) > 127`; rising edge additionally requires
`(previous XOR key) <= 127`. Control 22 has separate release/history handling;
do not generalize that special case to contextual control 23.

Raw gamepad queries use a separate state path: D-pad button IDs 10/11 select
dword slots at offsets 44/48 in `0x00B99220`; the just-pressed implementation
uses current/prior raw state and special D-pad routines. Wrappers also apply
gating through `0x00B901F0` and `0x00482790`, whose runtime role remains unverified.
This confirms why merging frontend left/right into the control table alone
does not cover all clothing navigation branches.

Created the four small registered wrappers, named the three verified control
helpers, and applied their `__cdecl` prototypes. Bookmarks at `0x00B98BE0`,
`0x00B98BA0`, `0x00B991C0` and `0x00B98470` use category
`VR contextual interactions #13 #15`, with evidence and runtime limitations.

## Shared entry path

Hot-dog vendors, restaurant food service and the clothing shops share the same
context-button helper structure. It reads `IS_CONTROL_PRESSED(2, 23)`;
control 23 is `INPUT_PICKUP`, also used for contextual interactions.
It is separate from vehicle entry (`INPUT_ENTER`, control 3).

The helper arbitrates ambient interaction ownership using script globals
`g_U9172` (priority), `g_U9173` (state), `g_U9174` (prompt) and `g_U9178`
(owning thread). These are decompiler labels, not verified x86 memory addresses.
The script checks eligibility before consuming the button: living player,
screen faded in, player control/ambient-task availability, competing activity
ownership and whether the player is already carrying a native object. Individual
services add range, vendor availability and money checks.

The vendor script's entry route checks proximity to the cart, outdoor/on-foot
state, loaded service animations and vendor condition. After the context button
passes, its own state machine handles the purchase, service animations and
deducting money. VR should feed the expected input and let that state machine
run, rather than directly awarding food, changing clothes or editing mission
globals.

## Input map

| Interaction | Native input | Evidence / distinction |
| --- | --- | --- |
| Hot-dog purchase | Held control 23, pad 2 | `vendor`: context helper `sub_7148` in published decompilation; current bytecode calls at `0x1CA4`, `0x1CEC` |
| Restaurant food purchase | Held control 23, pad 2 | `foodserver`: current bytecode `0x1A2D`, `0x1A7B` |
| Start/leave clothing try-on | Held control 23, pad 2 | Binco helper `sub_20445`, direct query `sub_29662`; current calls `0x50B9`, `0x5101`, `0x5122`, `0x746C`. The exit path can use context again, not always frontend Cancel |
| Confirm clothing purchase | Held control 77, pad 0 or 2 | `INPUT_FRONTEND_ACCEPT`; Binco purchase helper `sub_38249`, current calls `0x9605`, `0x9610` |
| Browse clothing | Raw gamepad left/right 10/11; keyboard navigation; analogue input | Current Binco has raw just-pressed calls at `0xAF81`, `0xAFB0`, raw held calls at `0xCC73`, `0xCCA8`, and keyboard alternatives. Frontend left/right alone does not cover all branches |
| Begin bowling service | Rising edge of control 23, pad 2 | Published `bowling_lane` front-desk route checks availability/funds, shows `TEN_PIN_46`, then advances on control 23. This script is not among the inspected FusionFix replacement files |
| Confirm/cancel bowling service | Control 77 to confirm; control 23 to back out in the front-desk prompt | Published `bowling_lane`, helper `sub_5641` and the `TEN_PIN_45` state; preserve the script's prompt/state ownership |
| Bowling gameplay | Controls 23, 77, 78 and other device-specific inputs | Current `tenpinbowl` bytecode confirms these queries, but this is a separate state machine from the front desk and does not establish VR minigame support |

Modo and Perseus independently confirm the same entry and purchase inputs:
Modo context queries `0x566B`/`0x56B3`, purchase `0x84B0`/`0x84BB`;
Perseus context `0x58ED`/`0x5935`, purchase `0x8D64`/`0x8D6F`.

Current script SHA-256 identifiers used for the bytecode checks:

| Script | SHA-256 |
| --- | --- |
| vendor | `1AAA81D445416DC5ACD8962A4348E9491C4EAC54C60C2CC28BB497613F335838` |
| binco_brook_s | `4173A0DCA950DE0152EAC49A0AB45AC27C80429512E43A8AEDB06497322C4331` |
| modo_manhat_5 | `3372E45CFF21E2CABF36CAE6AB8B4E0168C9D1DBC6FB860D81A1CCC9B861D553` |
| perseus_manhat_8 | `E1D29B6456A637006BC9DAEBD0012F1D84E5273E6452243C25CF85832BD0692F` |
| foodserver | `123A839C5F2D41EAA0203219C7A387AE68630053031CB195D78FA8D81051BA15` |
| tenpinbowl | `9B1107E77082B115B956F136C3AC123F19C18F89287D080A83C79C691A6BF835` |

## Routing gaps identified before implementation

`VRCamera::UpdateControllerPad` currently maps on-foot Interact only to control
3, and vehicle Exit to control 43. It supplies no context-control-23 value.
The existing raw gamepad query hooks supplement phone directions only while
the phone is visible; they do not currently supply shop navigation.

The gameplay button block also requires `firstPersonActive`, a tracked right
grip pose, and no native pause/script-pause. A scripted purchase camera can
leave that block inactive. Native pause menus are identified through
`CMenuManager::m_MenuActive`; a clothing try-on is not automatically that menu.
OpenXR's Menus action set and pause-pointer handling therefore do not by
themselves cover clothing purchases. These are source-level limitations;
the exact runtime shop camera/action state still needs a capture.

## Implementation and remaining checks

2026-10-11: added a distinct Contextual interact action (suggested left trigger
click) to control 23, independent of vehicle entry. Added a shared Scripted
interactions action set with four-direction navigation, confirm and back, using
right touchpad direction/click and right Menu defaults. Separate directional
actions support custom SteamVR D-pad bindings. Edge counters and held pulses use
the existing phone input transport to survive render/simulation sampling.

The simulation callback detects a valid propagating script draw camera through
the native camera manager's null-safe root and selected-draw accessors, activates script input
and suspends the first-person camera/body/head filter. Navigation supplements both
frontend controls and the raw gamepad queries used by clothing scripts. Phone,
pause and cutscenes retain their own routing. Prices and mission state are untouched.

Native camera evidence saved in Ghidra: GET_SCRIPT_DRAW_CAM registration PUSH
`0x00B8A84B`, wrapper `0x00B86CF0`, forwarding `0x00B88690` to `0x009B75D0`;
IS_CAM_PROPAGATING registration `0x00B89FE1`, wrapper `0x00B86E30`, forwarding
`0x00B88810` to `0x009B7900` (camera flag bit 3 at +0x13C). The returned camera
root is checked before enumerating its active/propagating children. Camera-free menus need separate
ownership evidence; this camera gate is not acceptance of every scripted menu.

Use a shared semantic contextual-interaction action routed to native control
23. Add scripted-service browsing/confirm/back routing that remains available
when the native script owns the player/camera, separate from locomotion and
from the pause-menu pointer. Carry rising edges between OpenXR/render sampling
and simulation, as phone routing already does.

Do not blindly send both context and vehicle-entry input for every press:
near a vehicle/service they can start competing native tasks. Keep native
interaction eligibility and ownership, and choose bindings/context based on
observed prompt/state evidence. Likewise, do not globally turn trigger presses
into purchase confirmation or remap all raw D-pad queries outside an owned
interaction.

Next runtime evidence needed: actual service prompt ownership, native input
query results and device switching; try-on start, browse, buy, back out and
insufficient-funds behavior; vendor purchase and bowling-front-desk mission
recognition. Verify the installed `bowling_lane` script separately. No purchase
or mission acceptance is claimed by the bytecode trace alone.

## Build and deployment

Win32 Release passed on 2026-10-11, with existing modupdater missing-PDB linker
warnings. Restricted builds encountered C1902 because the Visual Studio PDB
manager could not operate there; the x86 developer build outside the sandbox
succeeded. ASI deployed after backing up the installed ASI and INI; INI unchanged.
Built/deployed SHA-256:
`5EC4642ECA20065CC4ED7A8379C2F09DA61851561DC6011CA9C9253F125C9524`.
`git diff --check` passed. Runtime service acceptance remains pending.
Steam and direct PlayGTAIV launches exited before a targetable game window or
command bridge appeared. Windows Application events at 11:30 on 2026-10-11 report
USER32.dll access violations (offsets 0x001216AD and 0x000D1A4B). The faulting
module alone does not establish the cause; no shop/runtime acceptance was obtained.

### Startup crash correction

The preceding ASI `EE9D6EE0...` launched successfully. The new ASI reproduced a
native null dereference under `game-crash-monitor --runtime`: PID 13200, GTAIV
base `0x00D00000`, fault `0x0137C368` = Ghidra `0x00A7C368`, ECX=0. Stack resolves
to `UpdateControllerPad` line 654 calling GET_SCRIPT_DRAW_CAM. Its implementation
at `0x009B75D0` passes the result of `0x00A86BD0` unconditionally to `0x00A7C970`;
without a script root this is null, and child enumeration at `0x00A7C360` faults.
The earlier USER32 reports did not identify this original fault.

Corrected detection resolves the two native `__thiscall` accessors from the
verified GET_SCRIPT_DRAW_CAM implementation signature. Check root for null before
the selected-draw accessor; it in turn returns null if no active/propagating child
exists. Do not call the unsafe native or camera-handle conversion. Unknown code
signatures leave scripted-menu detection inactive. Ghidra evidence was corrected,
read back and saved. Win32 Release passed and corrected ASI deployed with SHA-256
`7F7E9076C22EEA3B518AFA01719809F32DD9EC45768A51C0B55E21045E27AF27`.

Normal startup subsequently reached the apartment (PID 12892); a fresh window
capture showed live gameplay and the command bridge responded. CE 1.2.0.59 with
SteamVR 2.17.10 and DXVK/Vulkan reported OpenXR running, stereo ready and continuous
rendering, with no command-buffer overflow. The resting headset was SYNCHRONIZED
and both Wands inactive, so this does not establish headset interaction acceptance.
The user also confirmed that the game runs. Clothing and service interaction
checks remain pending; GitHub #13 and #15 remain open.

### Binding organization correction

The user could find Confirm but not the contextual action, and could not purchase
a hot dog. Confirm is control 77; vendor entry requires control 23. Removed the
separate `script_menus` action set: its navigation actions now belong to `menus`.
Moved `context_interact` from `system` to `on_foot`, labeled **Interact with shops /
vendors** (default left trigger click). Vehicle entry remains a distinct action.
The shared Menus set activates for pause or scripted cameras; each consumer uses
its own context. Custom bindings must add the moved actions. This fixes action
discovery, but vendor purchase acceptance still needs a bound control-23 test.

### Failed interaction: wrong action binding

On 2026-10-11 the user reported both apartment Change Clothes and hot-dog entry
failing. Read-only inspection with the matching Release PDB found zero retained
`contextEdges` presses in PID 26468. SteamVR's current custom binding and fresh
editor showed left trigger assigned to `on_foot/interact` (vehicle input), with
`context_interact` listed unbound. The editor displays internal names, not the
application's localized **Interact with shops / vendors** label. Earlier saved
bindings also retained `script_menus/confirm`, which SteamVR explicitly skipped.
Corrected only the left-trigger assignment through the SteamVR editor to
`on_foot/context_interact`; readback confirmed that path in the saved binding.
Right Menu retained vehicle entry. No native purchase shortcut or additional
runtime build was needed. Gameplay acceptance is pending the corrected binding.

### Scripted camera resolver corrected

The user confirmed hot-dog purchase and apartment wardrobe entry, but wardrobe
navigation/exit stayed inactive and the view remained first-person. Live PID 26468
(module base `0x00D00000`) showed all three resolved script-camera pointers null.
The outer native signature has two matches: Ghidra `0x009B7060` selects camera
root type 39 via `0x00A86AE0`; `0x009B75D0` selects root type 6 via `0x00A86BD0`.
The previous unique-outer-match requirement silently rejected both. The resolver
now checks each candidate's root prefix including `PUSH 0; PUSH 6` and the
selected-draw accessor, requiring one verified script-camera candidate. The
null-root guard remains. This uses native camera ownership rather than a
wardrobe/script-name exception. Ghidra bookmark at `0x009B75D0` was read back
and saved. Both Menus `back` and `interaction_back` feed the shared scripted
exit path; explicit status commands now expose resolver state and input edges.
Release build passed; corrected wardrobe navigation/view acceptance pending.

Runtime follow-up: corrected Release ASI SHA-256  9AD826E5454C268C52843BC798AB2AFC6934CD8CF552BBD2A1DC3E563E12D43 deployed with matching installed hash. PID 26328 reports scriptCameraResolved=true and scriptCameraActive=true during interaction; retained Menus presses include directional, confirm and back inputs. This confirms action-set activation and sampled input, not yet successful native wardrobe browsing/exit. Manual outcome remains pending. SteamVR custom bindings must use navigate_direction plus navigate_click, or the individual directional actions; confirm alone does not browse.

### Accepted runtime behavior — 2026-10-11

The user confirmed the corrected build's apartment Change Clothes flow works
reasonably well: the native third-person character preview, touchpad browsing,
and right-Menu exit all work. Hot-dog purchase was also confirmed earlier.
Test configuration: GTA IV Complete Edition 1.2.0.59, SteamVR 2.17.10,
DXVK/Vulkan and Vive headset/Wands; Win32 Release build passed. The active
script-camera resolver and retained menu presses were confirmed through the
command bridge. This acceptance covers the wardrobe and vendor routes only;
clothing-store purchase/mission progression, bowling staff, episode variants,
and flat-screen interaction regression remain to be checked. Issues #13 and
#15 remain open for that coverage. Menu-to-cutscene skip routing is included
in this changeset but its manual acceptance has not been recorded.
