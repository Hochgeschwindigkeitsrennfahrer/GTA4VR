# 015 — OpenXR controller support with Vive Wands

### Closed — user accepted — 2026-10-06

The user accepts current Vive Wand support and requests closure. Tracked poses, semantic SteamVR bindings, locomotion, gameplay/car buttons, finger inputs and menu pointing were implemented and accepted during #014/#016/#020/#022 work. Haptics and formal tracking-loss/reconnect/combined-route checks were not completed; the user does not require those original gates to keep this issue open. No new compatibility claim for those unchecked items.

Historical scope and observations follow; pending statements describe the earlier issue state.

## Goal

Accepted foundation (2026-10-06): independent semantic OpenXR actions for SteamVR rebinding, separate finger inputs, and pause-menu point/select/back with a visible ray. Remaining work is haptics and explicit tracking-recovery/combined-movement validation.

Add tracked OpenXR controller input and controller-driven locomotion, initially verified with the user's Vive Wands on SteamVR. Depends on #005; integrate with the room-scale movement implemented in #014. Roadmap stage 4. This issue does not block #014's headset-only acceptance.

## Tasks

- [x] Define semantic OpenXR pose, hand, gameplay, driving and menu actions; record the actual Vive Wand binding layout.
- [ ] Add and verify haptic actions/feedback.
- [x] Attach/synchronize action sets, validate pose validity and convert controller poses through the same tracking origin and world scale as the headset.
- [x] Provide visible hands/controller proxies, left/right ownership and usable menu/locomotion bindings without conflicting game input.
- [x] Define controller locomotion's directional reference and body-facing policy, keeping stationary head rotation independent of body facing.
- [ ] Verify combined controller and room-scale movement without double-counting physical translation.
- [ ] Verify both wands, tracking loss/recovery, disconnect/reconnect and VR-disabled keyboard/gamepad behavior.

## Done when

Both Vive Wands report correct tracked poses and usable bound inputs/haptics in the recorded runtime. Locomotion/menu actions work and invalid tracking produces controlled behavior. Movement-direction/body-facing policy is documented, and combined controller and room-scale movement passes without double-counting physical translation.

## Notes / evidence

### 2026-10-06 — semantic actions and menu pointing

- User accepted pause opening, tracked pointing/selection/drag/back and the named action groups in SteamVR's binding editor. This completes the current semantic-action/menu request. Haptics and the issue's broader loss/reconnect/combined-route gates remain open; no new performance claim.

- Split OpenXR actions into Hand animation and tracking, On foot, Driving, Menus and Pause menu sets. Gameplay now consumes Move, Sprint, Jump, Fire, Support weapon, weapon selection, steering/pedals and interaction actions, independently of finger animation. Menu context replaces the on-foot/driving set; pose sampling continues. Vive defaults preserved; independent Next/Previous weapon actions are available without default bindings. [Bindings and limits](../../docs/vr-controller-actions.md).
- Win32 Release/x64 compiler host passed (`issue020-pointer-release.log`, existing dependency PDB warnings). Ray-plane/front-face/UV and quaternion reconstruction tests passed. Installed ASI/INI backed up at ignored `build/issue020-before-pointer-20261006-140246`; installed INI unchanged. Deployed SHA256 `CD6AD95267AA63817FE362BC1C936DBD3FA1A81D3E008D1DCFEEE78AD947DA66`.
- CE1.2.0.59 / SteamVR2.17.10 / DXVK-Vulkan PID24104: all action creations, suggested bindings and five-set attachment succeeded. FOCUSED, ready/fresh controller snapshots and semantic Move XY observed. World-space loading screen returned to continuous stereo, without reported runtime failure. Pointer use and SteamVR binding-editor acceptance requested; these runtime observations alone do not establish native menu selection or every gameplay mapping.

### Current state — 2026-10-06

#016 is closed for accepted tracked hands/standard-pistol work. Vive touch-only locomotion, backward/strafe, edge jogging, held-click sprint, weapon cycling, right-menu vehicle entry/exit, running jump, basic steering/throttle/brake and handbrake have been implemented and accepted. Controller poses/finger actions are live. Remaining #015 work: haptic actions/output, explicit tracking-loss/disconnect/reconnect and combined room-scale acceptance, and final VR-disabled controls verification (coordinate #024). Historical notes below retain their original pending states; this summary supersedes them for accepted controls.


- 2026-10-05 #016 adds per-hand grip pose spaces and squeeze/click action alongside the existing aim/trackpad observations. `input-status` exposes grip validity/button/position. Traversal bindings/haptics and this issue's broader acceptance remain pending; #016 intentionally starts with arm tracking.
- 2026-10-02 directional-reference decision: the user wants forward input to follow the headset's horizontal facing direction, with backward/strafe relative to that direction. #014 now covers adapting existing keyboard/gamepad locomotion to this reference; use the same reference for Vive Wand input here. Physical room-scale translation remains distinct from stick/key movement, and looking up/down must not cause vertical locomotion. Body-facing policy is still unverified.

- 2026-10-02 scope clarification: controller locomotion belongs here, not in #014. Controller runtime checks are deferred while #014 completes headset-only room-scale movement. The existing read-only input prototype remains unfinished #015 work.

- Started the input foundation needed by #014 on 2026-10-02: opt-in `ExperimentalControllerInput` creates/attaches one gameplay action set with per-hand trackpad vector, click and aim pose, suggested HTC Vive controller bindings, and action spaces owned by the existing OpenXR module. Sampling runs on the OpenXR callback thread and publishes a bounded snapshot; invalid tracking/focus and stale samples cannot provide movement intent. `input-status` exposes read-only diagnostic state. Builds/runtime checks and game locomotion injection are pending; this does not close the broader controller/haptic/hand-proxy acceptance gate.

- User hardware target: Vive Wands with SteamVR. No controller implementation or headset/controller route acceptance is claimed yet. This provides input infrastructure for #016–#022 and #024–#025.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.

- #016 now also samples Vive trigger/value and trackpad/touch for independent index/thumb poses. These are observations only; they do not inject firing or traversal input. Values are exposed by input-status alongside the existing squeeze/click and pad XY.

### 2026-10-06 — left touchpad on-foot traversal

- User requests contact/position movement without pressing the pad; use existing headset-horizontal directional reference. Added opt-in ExperimentalControllerLocomotion, requiring controller input and head-relative movement. Left pad touch/XY has radial15% deadzone, proportional magnitude and unit-circle diagonal clamp. Thumb release, stale/inactive/lost tracking or focus retain native input; no latched movement fields.
- Native CE signed axis decoder008EF950 (cdecl value*, int result) is substituted only at four reads inside on-foot motors00CD4B00 and00CD4E60. Calls00CD4B4F/00CD4EB3 read X;00CD4B6E/00CD4ED2 read Y, which motors negate. Other camera/menu/vehicle reads remain original. First-person, fresh valid heading/left grip, focus and non-menu/non-vehicle/non-animation/non-ragdoll gates apply. Native motor, movement animations, stairs and collision remain in use. Room-scale translation remains separate. Body backward/strafe animation policy is still native and remains future #016 work.
- Release build passed (existing dependency PDB warnings); deterministic pad center/forward/back/diagonal/invalid-input checks and existing vrmath suite passed. Deployment backed up ASI/INI as before-touchpad-locomotion.*. Runtime touch-only direction/release and combined room-scale acceptance pending. No issue closure or FPS claim.
- Ghidra Issue015TouchpadMovement008EF950 previewed/applied/read back/saved. Bridge locomotion-status reports configuration, installed hook and substituted axis-read count.
- First traversal deployment had installed=false: integer/float native axis decoders share the short prologue, so matching conservatively refused multiple results. Extended signature includes signed normalization and integer result conversion. No movement substitution ran in that rejected install. Diagnostic temporary ASI/INI retained locally; no claim of traversal acceptance from that run.
- Corrected Release SHA2565C624EE1A1C72920B305778591A25866D2424268F7D5751DD2DD62ED0FC13977 deployed after verified normal quit. Extended signature matches exactly one signed integer decoder in installed CE executable. Release/maths/diff checks passed. Direct PlayGTAIV launch exited before GTAIV.exe appeared; retried through the established Steam -applaunch12210 route. Runtime hook/traversal acceptance remains pending until loaded.
- Corrected runtime PID29372: locomotion-status installed=true, FOCUSED DXVK-Vulkan/OpenXR, continuous stereo ready with no command-buffer overflow. Fresh live game-window image inspected. User requested touch-only directional/release/head-turn gameplay check; axisReads coverage and visual movement acceptance pending. All #015 broader menu/haptic/reconnect coverage remains open.

### 2026-10-06 — ordinary input path and directional leg blends

- User rejected first traversal test: neither pad moved the character. Static tracing found ordinary movement A741D0 reads the normalized float decoder008EFA20, absent from the initial integer-only interception. Added scoped float caller returns A741FC/A74212 and integer directional returns A74701/A7471E.
- User requests torso/head/arm ownership and native backward/strafe legs. In opt-in hand IK + head-relative first-person mode, ordinary task movement A741D0 redirects to native directional movement A746D0 (same thiscall task,ped / ret4 and task fields). It projects movement onto body axes and sends signed forward/lateral blends to B27E30, retaining native collision/speed handling. Native SetCharHeading/SetCharDesiredHeading align local body to fresh headset yaw on the simulation callback, without injecting an aim stance. Vehicles, airborne/action/ragdoll, pause, inactive/stale tracking retain original walking. Hand-based torso contribution is deferred; headset yaw is the initial policy.
- All four hook signatures match exactly one location in installed CE1.2.0.59. Release build passed with existing dependency PDB warnings. Ghidra directional movement and missing float path bookmarks saved. Runtime coverage and animation appearance remain pending; no closure or performance claim.
- Deployment Release SHA256 F560986F0E50755FC477159B3C89C088C7787CAD91CD20533790ED47F738E9AA, installed ASI backed up as before-directional-walking.asi; installed INI/calibrations unchanged. Runtime PID19204 FOCUSED SteamVR2.17.10 DXVK-Vulkan continuous stereo. Both axis hooks and directional hook installed; observed axisReads2262, directionalCalls1273 and fresh movement heading overrides. Room-scale still consumed physical translation; no command-buffer overflow. Fresh initial game image captured (headset resting low, character clipping); subsequent live state has floorEyeHeight1.57m. These counters prove callback coverage only; touch-only/backward/strafe appearance acceptance requested from user.

### 2026-10-06 — pad jogging/sprinting

- User accepts touch-only locomotion and native backward/strafe/body behavior. Requests jogging at the pad edge and sprinting while pad click is held, without repeated physical taps.
- New scoped native speed query A2A180 / run-action getter8F0080 interception: left pad raw radius>=0.85 requests held jog; click while nonzero movement supplies native fresh-press observations for sprint accumulation. Center remains proportional walking; native stamina/exhaustion and speed constraints remain intact. Only a private TLS action copy is read inside the local player's eligible speed query; shared keyboard/gamepad action bytes are not modified. Tracking/focus/menu/vehicle/action gates remain. Runtime gait/stamina acceptance pending.
- Release builds passed, only existing dependency PDB warnings. Speed/run signatures uniquely match installed CE1.2.0.59. Ghidra Issue015PadGait previewed/applied/read back via annotations and saved. No performance claim or issue closure.
- Deployed Release SHA2567CF4358D008D53BFD0B50023E065710454D1C90796BD58B16EE7BD210F2E5A32 after verified bridge exit and ASI backup before-pad-gaits-arm-finish.asi; installed settings/calibrations unchanged. Steam startup exited before game; direct PlayGTAIV retry reached PID25040. FOCUSED SteamVR2.17.10 DXVK-Vulkan continuous stereo, fresh street image inspected. gaitInstalled=true; jogReads1753/sprintReads441, axisReads2246/directionalCalls1317 demonstrate native callback coverage. No command-buffer overflow. Visual gait/stamina and arm-deformation acceptance requested; no issue closure.
- 2026-10-06 user accepts the running jog/sprint and arm finishing-order build together. #015 broader menu/haptic/reconnect acceptance remains open.

### 2026-10-06 — right touchpad weapon cycling

- User requests left/right clicks on the right Wand touchpad for weapon swapping. Opt-in ExperimentalWeaponSwap (distributed0, requires tracked-hand input) interprets right pad X<-0.25 as previous and X>0.25 as next, once per fresh click. Center clicks do nothing; held click does not repeat. Invalid tracking/focus inhibits until released.
- Simulation callback uses existing native GetCurrentCharWeapon/GetWeapontypeSlot/GetCharWeaponInSlot/HasCharGotWeapon/SetCurrentCharWeapon to cycle owned native inventory slots0..10, skipping empty slots, wrapping and including unarmed. No granted weapons, ammo edits or keyboard injection. On-foot fresh active hand mode only; pause/dead/in-vehicle guards. Weapon change releases support attachment and inhibits a held trigger until release. hand-status exposes weaponSwapEnabled/weaponSwaps, with counter incremented only on native selection readback.
- #016 support sliding fix accepted by user before this change. Weapon cycling build/runtime and headset acceptance pending.
- x86 Release build/diff check passed (existing updater LNK4099 and line-ending warnings only). Deployed SHA2561BB4E7D241773773B3C2BAFD9210BF35A9379CD74292AEB496517DDCA0B813F0 with before-weapon-swap ASI/INI backups and accepted calibrations/settings preserved. PID18828 CE1.2.0.59/SteamVR2.17.10/DXVK-Vulkan FOCUSED continuous stereo ready,2555 tracked updates, weaponSwapEnabled=true and3 native selection readback-confirmed swaps during user's live input. Fresh image shows equipped shotgun and tracked hands. Bidirectional/wrap/hold behavior still needs user acceptance; no new performance claim, #015 remains open.

### 2026-10-06 — basic traversal and car buttons

- Requested mapping: right trigger accelerate; left Wand pad touch X steer; right menu enter car; right pad center click jump. User then adds center click handbrake in vehicle. Added left trigger brake/reverse and same menu button exit, as essentials for basic driving. Reload and physical wheel/handle steering deferred.
- ExperimentalControllerButtons (distributed0) adds Vive OpenXR menu/click action and a local-pad-only native post-update hook at8F4D00. Native values table+2698 stride16, XOR byte4 key/current6/prior7; ID2 jump,3 enter,30/31/111 steer,40 throttle,41 brake,43 exit,44/45 handbrake. Cross-checked existing native decoder/Ghidra and IV-SDK CPad/ePadControls (https://github.com/Zolika1351/iv-sdk/blob/master/include/ePadControls.h). Native physics and jump/entry tasks retained. Mode/focus/pause/pose freshness gates; discrete buttons need release after unavailable input. Center radius<0.35 and absX<0.25 avoids overlap with weapon-swap clicks. Continuous handbrake on hold; jump/entry native current/prior press semantics. Fresh VR steering/triggers override those car values; other native controls remain.
- locomotion-status adds button hook/read counters and steering/throttle/brake; input-status adds menu action active/pressed states. Ghidra Issue015GameplayPad preview prepared. Release/runtime and actual control behavior pending.
- Release/diff check passed, deployed4712B50612EA76060FC7276A0B3F1C65EB35231EB547B3CFBA9E388407187CC9 with before-basic-driving ASI/INI backups, accepted settings preserved. PID17096 CE1.2.0.59/SteamVR2.17.10/DXVK-Vulkan: new native buttons hook installed,263 button update callbacks, Vive menu action active on both hands and fresh trigger/pad samples. Jump/entry/driving counters initially0 before test. Ghidra Issue015GameplayPad applied/read back/saved. Actual jump, vehicle entry/exit, steering/throttle/brake/handbrake acceptance pending; hook readiness is not action acceptance.

### 2026-10-06 — vehicle exit history and running-jump input

- User reports right menu brakes the car at low speed but cannot exit at standstill; high-speed bailout works. Right center jump interrupts running and selects an in-place hop. These reject the initial control acceptance.
- Native CE vehicle task B7BDE0 reads exit value pad+2948 and calls 8F0230 at B7BF7A/B7C007 to test held input across a 64x8 history ring. Initial current/prior-byte injection omitted this history. Correct this tick's existing ring value with the merged XOR-encoded current value, preserving native timestamps, index and allocation; do not synthesize a task/teleport.
- Retain injection after 8F4D00: its classifier changes movement XOR keys, so merge against the final keys for the tick. Left touchpad now also populates shared movement IDs12/13/14/15/90/91 plus held run ID1, so jump/task reads beyond the walking-only axis hook can see direction and running state. Compensate the native signed decoder's 25% deadzone after the existing VR radial deadzone. Native jump/physics still own takeoff and momentum; actual running-jump result remains pending.
- Ghidra Issue015ExitHistory at B7BDE0 previewed/applied/read back/saved. CE1.2.0.59 static disassembly and user observations are the evidence; runtime acceptance of this correction remains pending. No performance claim.
- x86 Release/diff checks passed (existing libmodupdater missing-PDB warnings only); initial restricted build hit MSVC C1902, normal toolchain build succeeded. Final correction SHA256 C42D46544007475501AEACB3E6BD335E63E83A65C96AE6BB1A1BEB5693C8AFB6 deployed after bridge quit and verified process exit. before-input-history ASI/INI and intermediate input-history-before-key-order ASI backups retained locally; installed settings/calibrations unchanged. Startup of the intermediate run eventually reached continuous stereo; final control behavior still awaits headset testing.
- Final retry via Rockstar launcher reached PID23104, FOCUSED SteamVR2.17.10 / DXVK-Vulkan, continuous stereo ready with513 built frames and0 command overflows; buttonsInstalled=true and515 fresh button updates. Earlier direct/Steam startup attempts did not reach the bridge and launcher reported unexpected exit; retry succeeded. No claimed exit/jump acceptance from these initialization counters.

- 2026-10-06: user accepts the corrected native history and shared movement/run injection: standstill vehicle exit and running jumps now work ("Yup, that works!"). SHA256 C42D46544007475501AEACB3E6BD335E63E83A65C96AE6BB1A1BEB5693C8AFB6. Broader controller/episode acceptance remains open.

- 2026-10-06: semantic action sets and tracked pause ray/select/drag/back are user-accepted. General pause-menu navigation is implemented; remaining gates are haptics, explicit tracking loss/reconnect and combined controller/room-scale route evidence. UI mission/lifecycle coverage is now #031. Do not repeat accepted menu binding work.
