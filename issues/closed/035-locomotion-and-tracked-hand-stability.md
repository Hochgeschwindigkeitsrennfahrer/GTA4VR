# 035 — Locomotion and tracked-hand stability

## Goal

Keep headset translation, tracked hands and held reload objects consistent during idle and traversal. Add grip-button release from native ledge hanging. Follow-up to closed #014/#016/#018/#024, roadmap stages4–6.

## Tasks

- [x] Bind grip to native hang release without interfering with ordinary grabs/reloads.
- [x] Remove head-rotation-induced camera translation while retaining the accepted body/camera placement.
- [x] Keep hand tracking immediate during running without losing collision blocking or native leg animation.
- [x] Align held ammunition and hands during moving shoulder acquisition.
- [x] Prevent idle/breathing animation from displacing tracked arms/weapon aiming.
- [x] Build x86 Release and record scoped headset acceptance and verification limits.

## Done when

Grip releases a native hang; head-only rotation does not shift the tracking origin relative to the world; running/idle do not displace hands or held magazines from tracked targets. Arm contacts still block against world geometry. Record supported game/runtime and remaining limits.

## Notes / evidence

### Completion — 2026-10-09

The user confirmed that everything else in #035 is working and explicitly requested closure after accepting the placement/rack follow-up and confirming jogging/sprinting work. This accepts the combined scoped headset behavior, including hang release and arm contact. Release build and optimized math tests passed; installed ASI/settings are retained. No additional flat-screen launch was performed for this closure; previous flat-screen acceptance remains historical evidence, not a fresh test of #035. Broader episode/controller compatibility remains outside this issue. No performance claim.

- Follow-up user report2026-10-09: camera/rack correction is good. Jog/sprint initially appeared unavailable, then user confirmed they work again and requested no further investigation. No gait source changes or deployment made for this transient report. Read-only native observation found sprint input/accumulator active; CBFD80 clamps directional blend axes to[-2,2], ordinary forward to[0,3]. This alone does not establish the transient cause. Ghidra bookmark Issue035GaitObservation atCBFD80 was read back and saved.

- Start2026-10-09: clean tracked working tree at9f639c4. CE1.2.0.59 / SteamVR2.17.10 / Vive Wands / DXVK-Vulkan. User reports head-turn world shift, running hand lag, moving shoulder-mag offsets and idle breathing affecting aim.
- Source inspection: eye camera currently adds FirstPersonForwardOffset along instantaneous headset yaw. Physics feedback always replaces requested wrists with the prior rigid-body position, even without contact; requested magazines use unmodified wrist targets. These are candidate causes, not yet accepted fixes.

### First correction — 2026-10-09

- Forward eye placement now follows the fixed upright tracking reference instead of instantaneous head yaw. It cannot orbit the eye position on head-only rotation; the configured placement distance remains.
- Native locomotion between render publication and skeleton update rebases the controller transform to the current body. Room-scale consumption rebases body displacement and consumed tracking displacement together, preventing double counting. Focus/owner/stale guards remain.
- Native arm IK retains elbow bending. The final wrist XYZ basis/position now follows the requested or collision-resolved pose even when the native reach clamp/animation differs. Metadata W lanes, finger carry, body scaling and native support-bone finishing remain.
- Free-space proxy motor latency no longer replaces the wrist target. Actual native CObject collision feedback and prop/door/ragdoll grip constraints still resolve the hand. Removed the proxy target-velocity5m/s clamp; total motor force and recovery remain bounded by existing settings.
- Reload props use final rendered wrists. Shoulder acquisition uses the authored gun_grip inverse instead of dividing an older attached weapon pose by a newer tracked wrist; user per-weapon hand/insert offsets remain.
- A fresh grip press on either Wand injects native INPUT_ENTER only for native climb states2/3/4 (hang/shimmy). A held grip before hanging does not auto-drop; pause/focus loss requires release before rearming. `locomotion-status.controllers.hangDrops` counts injected presses.
- Native evidence (Ghidra VAs, image base00400000): GET_PED_CLIMB_STATE hash391822A7 registrationBC4666 -> BB8BC0 -> BBBD10 -> CF47F0/CF47A0; CTaskSimpleClimb RTTI/vtableEDF154 identifies processCF6580. CF57C0 hanging stage calls CF4210 mode0, reading CPad+26C8 (INPUT_ENTER rising edge), then CF4B50 selects hang_drop animation9/6A and completion stage8. HAS_OBJECT_COLLIDED_WITH_ANYTHING hash106811E4 registrationA095EF -> A01200 -> A052F0 reads object+150. Bookmarks Issue035HangInput and Issue035HandContact were previewed, written, read back and saved in GTAIV.exe; no game-binary edits.
- x86 Release build passed with existing updater-library missing-PDB LNK4099 warnings. Focused optimized x86 VR math tests passed, including ordinary running, consumed room movement, collision-rejected room movement and stable repeated origin rebasing. No Debug build.
- Deployed ASI SHA256 `05E766ACFAA09175FE0A93EE83464E60A94190EDEC2863185CF36336C6377F7D`; local ASI/INI backup `build/issue035-20261009-182521`. Runtime headset/hand/hang acceptance pending. No performance claim.

### User check and follow-up

- User accepted stable world/hands and moving magazine placement in the first build. Reported camera behind the body and regressed rack gestures; these are not accepted/closed.
- The first fixed-reference eye offset retained world stability but could land behind a turned body. Follow-up moves the visible rig backward along native body forward using existing `FirstPersonForwardOffset`, instead of offsetting the eyes. Native camera/controller origin and entity physics remain unchanged; the wrist solve removes this body-only placement before final scale/translation so hands stay tracked.
- Separated raw requested wrists (reload gestures/shoulder acquisition) and final rendered wrists (loose ammo visual attachment). A slide-snapped left hand no longer feeds its own constrained position back into the rack-distance calculation; pump cycles use the same raw movement path.
- Enable native `SET_OBJECT_RECORDS_COLLISIONS` on each owned arm proxy so `HAS_OBJECT_COLLIDED_WITH_ANYTHING` is meaningful. RegistrationA08EF6 -> A01EE0 -> A06FA0 sets object+118 bit0x10. Existing force budget/owner exclusions remain. The intermediate contact-only build was not deployed separately.
- First runtime PID8512 reached FOCUSED continuous DXVK-Vulkan stereo, with native IK/finger/physics callbacks advancing, no physics failure, and successful magazine insertions. A fresh window capture showed the apartment image. Those observations are not hang/collision acceptance.
- Follow-up x86 Release passed (same existing LNK4099 warnings), deployed SHA256 `FDE56A11AA1ABD250AA392D20FE8A895A18ACF794DC23EEBEC441F9F05D26079`; local ASI/INI backup `build/issue035-placement-rack-20261009-183424`. Installed settings/profile values retained. Ghidra contact bookmark updated with the recording setter evidence, read back and saved. Viewpoint/rack/contact/hang checks pending; issue remains open.
