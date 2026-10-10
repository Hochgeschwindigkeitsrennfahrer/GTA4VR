# 019 — VR vehicle interaction and driving

## Goal

Provide usable VR vehicle cameras and controls for cars, bikes and available aircraft. Depends on #014/#015; coordinate tracked hands with closed #016 and weapon coverage with #024. Roadmap stage 4.

## Tasks

- [x] Keep both Vive Wand hands freely tracked in cars, with native phone presentation/controls available (tested car route accepted; broader mission/episode lifecycle remains pending).
- [x] Acquire/release the steering wheel with either grip, steer physically, and rotate the visible wheel where the vehicle asset supports it (user accepts multiple tested cars; untested models/episodes remain pending).
- [ ] Equip native drive-by-compatible weapons in the left hand and fire from their tracked muzzle while driving.

- [ ] Inventory actual vehicle classes in GTA IV, TLAD and TBoGT separately. Assess the user's planes request against available aircraft; document any requirement for additional content.
- [ ] Define seated vehicle tracking origins, entry/exit, recenter and head/body collision; remove forced headset rotation where possible.
- [ ] Implement VR driving/riding/flying controls and necessary cockpit/dashboard feedback; define physical hand interactions and weapon use per supported vehicle class.
- [ ] Test entering/exiting, driving/riding/flying, collisions, passenger views and camera transitions; record comfort/performance and flat-screen results.

## Done when

A documented route passes for each supported vehicle class with usable controls and coherent headset camera/entry/exit. Aircraft coverage and any unavailable plane gameplay are stated explicitly.

## Notes / evidence

- 2026-10-10: User confirms the corrected manually opened phone works in the car
  and requests committing this work. Final PID29448 reached FOCUSED continuous
  DXVK/Vulkan stereo, ready eye images, zero command-buffer overflows. Phone
  action/presentation accepted; entry weapon preservation is implemented but has
  no separate explicit user acceptance yet. #019 remains open for its remaining
  vehicle classes and combined gameplay/lifecycle routes. Historical pending
  phone statements below are superseded by this result.

- Phone visibility follow-up: user sees the native holding pose but no phone.
  Read-only live PID1696/base005C0000 while seated/phone active found weapon-manager
  slot9, entity1054C0A0, CWeapon13EA8090 type46, model2175. The phone is therefore
  instantiated, not missing from the task. HideDrivingWeapon suppressed any
  equipped entity while drivingHolstered, including that native phone. Restricted
  this render omission to DriveByWeapon types7/9/12/13, preserving task props.
  Ghidra Issue019PhoneEquipment bookmark at B64E30 previewed, applied, read back
  and saved. No native patch/prototype changes. x86 Release passed (existing
  dependency PDB warnings); deployed D405A9A806D18E5C085E3C88AAD82F797C61C60F3D37DD2ABE7EEEEA56E75FE6
  after verified normal exit and ASI/INI backup under ignored
  build/issue019-phone-visibility-*. INI preserved; headset acceptance pending.

- Follow-up: user reports manual phone controls work but no held model appears,
  whereas incoming calls previously showed it. Manual seated opening now starts
  the native TASK_USE_MOBILE_PHONE task once, stopping our owned task on closure;
  incoming calls are left to their native lifecycle. This is a behavior hypothesis
  pending headset validation, not a verified native disassembly finding.
- User also reports automatic SMG selection on every entry. Cache the equipped
  inventory weapon before native entry starts and restore it once stable seated
  tracking becomes available. Unsupported drive-by weapons enter holstered;
  deliberate seated cycling remains available. x86 Release passed (existing
  dependency PDB warnings); diff check passed. Deployed SHA256
  CB034C284B066FCB5EFA739E59D1729C335552DC272FF24502B2F63C598C3F96 after
  verified normal exit, ASI/INI backup in ignored build/issue019-phone-selection-*
  and preserved installed INI. PID1696 initialized FOCUSED continuous
  DXVK/Vulkan stereo with eye images ready and zero command-buffer overflows.
  Seated headset validation pending.

- 2026-10-10: Baseline c8ac66e. Added an independent Driving/open_phone boolean
  action, labeled Open phone (hold), default left Menu for Vive. Its state joins
  the existing system phone action only while driving outside menus, preserving
  native phone opening and existing hold/tap handling and Phone navigation.
  x86 Release passed with existing dependency PDB warnings; deployed ASI SHA256
  ADFA3FAAE9292C705E671B80295111D19B6FEC8EC1E51861A35E49882D721171 after
  normal bridge quit/verified exit and ASI/INI backup under ignored
  build/issue019-driving-phone-*. Installed INI preserved. After launcher retries,
  PID3792 initialized FOCUSED DXVK/Vulkan with the new action set attached.
  Seated headset acceptance pending. No new
  native hook or teleport; diagnostics remain off.

- 2026-10-10: User accepts the authored-left-grip correction as perfect and requests committing the current vehicle state. #019 remains open by explicit request: broader vehicles, phone, drive-by fire/ammunition transitions, holster/horn and combined routes still require acceptance. Earlier offset failures below are historical experiments, superseded by this accepted attachment.

- Authored-left-grip iteration: x86 Release and diff check passed (existing libmodupdater LNK4099 warnings). PID27820 normal bridge exit verified; ASI/INI backed up under ignored build/issue019-left-authored-grip-*, installed INI retained. Deployed SHA256 EC168171A85393C17FBE2D340B738CE3FE180827EFB3771CCF8EED03B330DCC5. Direct launcher exited during startup; Steam fallback launched responsive PID26252. FOCUSED continuous DXVK-Vulkan eye images ready with zero command overflows; fresh window capture shows normal apartment spawn. No teleport; car alignment remains pending user check.

- Parked SMG capture PID27820/base005C0000: left wrist (903.17084,-536.3995,14.128049), gun_grip (903.1645,-536.4329,14.13396), about3.4cm apart. Asset root translation is below0.1mm, ruling out its unaccumulated parent translation as the cm offset here. The authored gun_grip2 has the left anatomical basis (opposite Y/Z to gun_grip). Replaced calibrated-right/palm-centre approximation with direct inverse authored left grip times final left wrist; kept on-foot support tuning separate. Alignment acceptance pending. Runtime capture stays ignored; no new native function interpretation requiring a Ghidra annotation.


### Car interaction implementation — 2026-10-10

- User reports the revised left-hand gun still floats to the left of the arm; palm-centre placement is not accepted. Preparing native attachment capture. `ResolveSupportGrip` reads definition-local `gun_grip` directly, whereas the physical reload adapter accumulates parent transforms before computing its primary grip inverse; this difference is a candidate, not yet established as the remaining offset. Requested a parked car with pistol/SMG selected; current live probe has no held weapon object, so no attachment measurement yet. Horn/holster acceptance remains unreported.
- User accepts vehicle weapon selection after the correction, but reports a few-cm rightward gun/grip mismatch and inability to unequip; requests a horn action. PID9220 counters show43 attempts,15 swaps, last stage4/candidate0: native unarmed selection did not match immediately. Added explicit vehicle holster ownership with native visibility plus guarded local object render suppression and firing disabled; cycling resumes from unarmed and exits restore visibility. Corrected mirrored left attachment translation using both actual palm centres instead of wrist origins. Added semantic Driving `horn`, default upper right-pad click, routed through pad control54; native horn behavior and revised grip/holster await runtime acceptance.
- Grip/holster/horn x86 Release passed with existing dependency PDB warnings and diff check passed. Verified PID9220 exit through the bridge, backed up installed ASI/INI under ignored build/issue019-driveby-holster-*, deployed SHA256 `8E1C4A023AC10987D1671474C4F8A255C6F2778719D1E0A9B4A596709D0DF4EE`, preserving the installed INI, and relaunched PlayGTAIV without teleport. Awaiting scoped car validation; no new Ghidra annotation this source-only iteration.
- Relaunch PID27820 reached continuous DXVK-Vulkan stereo without command overflow; fresh window capture showed normal apartment rendering after the loading panel. New horn/holster status fields parse. Session transitioned FOCUSED to VISIBLE with the resting headset; car grip, cycling to unarmed and horn headset check requested.
- First drive-by test failed to select any weapon despite user-configured bindings. Read-only PID24392/base005C0000 inventory: current slot4/type11 (combat shotgun), held object null, owned pistol9 and SMG13 present. This establishes inventory availability and absence of the current held object, not which rejection caused the failed clicks. Selection now falls back to the verified CPed+2B0 inventory slot/type when GET_CURRENT_CHAR_WEAPON fails, and waits for a valid side direction before consuming the click. Added input-status.driving and hand-status weaponSwapAttempts/stage/candidate for the next live check. Ghidra instance/program confirmed; no new binary role was established or project annotation changed this iteration.
- Selection correction x86 Release passed (existing dependency PDB warnings), diff check passed. Backed up installed ASI/INI under ignored build/issue019-driveby-selection-*, verified PID24392 exit, deployed SHA256 `B2934AC5B793415D7869F3255DBF2EEF97CBE28672D4A404EB9CBCE5A4D14CE7`, and relaunched PlayGTAIV without teleport or INI overwrite. Runtime selection acceptance pending.
- Corrected run PID9220 reached FOCUSED continuous stereo with zero command-buffer overflows; fresh window capture shows normal apartment rendering after loading. Both new status payloads parse through the bridge. Requested car selection retry with the car left occupied if selection fails, so the recorded stage can distinguish action receipt, inventory lookup and native equip rejection.
- First left-hand drive-by pass implemented: Driving semantic selection/fire actions, pistol/SMG inventory filtering, calibrated left-Wand equipped-object pose refreshed before native rendering/firing, and native in-car ammunition/reload ownership. Armed controls use left trigger fire and left pad click brake/reverse; right pad side clicks select weapons and centre remains handbrake. Phone/menu gates suspend weapon inputs. Physical reload remains on foot; drive-by visual/fire/ammo/phone acceptance is pending. See docs/vehicle-interactions.md.
- x86 Release passed with existing dependency PDB warnings; diff check passed. Backed up installed ASI/INI under ignored build/issue019-driveby-*, verified PID7256 exited through the bridge, and deployed SHA256 `D4BA73A08E6CBB2521006A145440BBAA8CC40A5EEC28C7212D3F6A35F51CF736` while retaining installed wheel flag/tuning. PlayGTAIV relaunch PID24392 reached FOCUSED continuous DXVK-Vulkan stereo without command overflow. Fresh native window capture showed the normal apartment spawn; no teleport. Scoped left-hand drive-by and in-car phone test requested. GTA IV CE1.2.0.59 / SteamVR2.17.10 / Vive Wands; episodes remain untested.
- User accepts corrected car entry, visible wheel rotation, hand snapping and one/two-hand return behavior across multiple cars. The bounded exception monitor detached normally with no recorded fault; its observation window is not claimed to cover every user test. Mesh palette/asset-lifetime assumptions remain limitations for untested models. Next: semantic Driving weapon selection/fire, left-hand equipped firearm attachment and tracked-muzzle shooting. Preserve accepted wheel route; native phone in-car check remains pending.
- Isolation build car entry succeeded (user parked model164, Schafter). Read-only PID17972/base005C0000 inspection proves native VB+20 contains raw vertex positions/weights, not a COM interface. VB+1C points to DXVK's D3D9 vertex-buffer interface: vtable slot10 returns type6; slot13 copies the descriptor, matching the installed DXVK implementation. Corrected +20 to +1C and checks descriptor type. This is a concrete invalid-call bug; no original fault stack exists, so broader crash/lifecycle acceptance remains pending.
- Model164's geometry6 fit yields68 rim support samples,198 wheel candidates1559..1770, centre(-.435667,.330287,.339279), radius.168368; authored driver seat43(-.448975,-.024929,.003559). Ghidra Issue019WheelResource A3D980 previewed/applied/read back/saved. Local vertex/interface captures stay ignored. Release passed; deployed SHA256 `3668FC8EF5105B7F3B9FF1C7203454FE74DF8106238DC98ADA7302E244BB3EE8` with the installed developer mesh flag enabled for the next monitored experiment. Backups build/issue019-wheel-resource-*, other INI tuning retained, previous process17972 quit verified before relaunch. No teleport.
- Relaunch PID7256 reached FOCUSED continuous DXVK-Vulkan stereo with no command overflow. Fresh native window capture shows normal apartment rendering; `hand-status` confirms visualHook=true (inactive on foot). Existing read-only exception monitor PID8268 attached after initialization for its bounded180s observation; log build/issue019-wheel-resource-crash.log. Requested car entry, visible rotation, hand snapping and one/two-hand return test. #019 remains open.

- User's first car-entry test of SHA5566537E crashed. No fresh dump or matching Windows Application fault was found; exact fault site remains unknown. Restored preceding SHA7A5C27E9 while isolating the regression. The mesh experiment's VB+20 COM-interface cast and palette allocation capacity are unverified; CDCD padding is not evidence of writable capacity. Native mesh rotation is **not accepted**.
- Render-hook/mesh mutation is now opt-in through developer-only `ExperimentalWheelMesh` (default off). The fixed-reference steering solver remains active independently; no wheel geometry or seat matrices are modified in ordinary runs. x86 Release passed with existing dependency PDB warnings. Deployed SHA256 `FC264C58A303D147D9BBAC9F6F259A0B63444827E74634E6D441BB1F66AE76E6`, preserving installed INI and backing up both files under ignored build/issue019-wheel-isolation-*. Relaunched without teleport. Car-entry and steering acceptance still pending; disabling the experiment does not establish its root cause.

- Latest user feedback: acquisition/snap improved, but anchor is too far left in several cars; steering feels difficult and hands drift after a closed controller movement. User prioritizes visible native wheel rotation. Replaced incremental averaged deltas with fixed per-grab ray/angle references and cached palm offsets. Deterministic repeated-return and steering-stop tests pass.
- Inspected the active model112 at runtime PID10124/base005C0000. Native steering wheel resides in geometry6, not an independent bone. Local mesh fit identifies its rim near(-.409,.041,.195), radius.161, tilt20deg; selection covers198 vertices1060..1295 without adjacent dashboard. Driver-seat bone32 is absent from existing high-detail palettes. New guarded render path rebinds wheel vertices to that bone and substitutes a temporary wheel-pivot rotation during CVehicle +8C command construction. Actual fit drives hand snapping. Unknown layouts retain native geometry; headset/render-pose acceptance pending. Ghidra Issue019WheelRender A3D980 previewed/applied/read back/saved. Imported bytes remain packed; runtime evidence, not nonsensical pseudocode, supports the layouts.
- x86 Release and focused grip/rim tests pass; no Debug build. Installed ASI SHA2565566537E8D7A93045CDE35DE75E35E5EC86888B454348D82AE90EC2EB70B9EFF; previous ASI/INI backed up under ignored build/issue019-native-wheel-*. Installed INI retained. PID18624 is FOCUSED with continuous DXVK-Vulkan stereo, no command overflow, and a fresh native window capture shows normal apartment rendering. Requested car test of visible rotation, snap placement and one/two-hand return; on-foot success does not establish wheel acceptance. No teleport.

- Start at ce96eb7 with a clean tracked tree. User requests free tracked arms, grip-based steering with a visibly rotating wheel, mission-compatible native phone use while driving, and left-hand drive-by weapons. Cars are the first route; bikes/aircraft remain separately pending within this issue.
- Existing camera publishes tracked-arm ownership only on foot, disabling the accepted IK in cars. Driving activates a separate semantic action set with no wheel-grip or drive-by selection/fire actions. Native arm physics is already removed while seated, avoiding collisions with one's own car. Preserve this separation while adding tracked seated poses.
- Parked runtime inspection of model 164: CPed +0xB30 points to the seated vehicle; native wrist bones (semantic 15/11) lie about 0.33m forward and 0.425m above the driver-seat bone, separated by 0.274m. Inspected vehicle skeletons expose driver/passenger seats, road wheels and bodyshell, but no independently movable steering-wheel bone. Actual wheel geometry rotation still needs analysis; physical steering is implemented first.
- First implementation enables tracked arms for stable car seating only, rebases render-published tracking through the current vehicle matrix, and retains native seated legs instead of applying the on-foot neutral/height pose. Either raw tracked grip can acquire the wheel near the native wrist midpoint. Vehicle-local angular movement feeds existing native steering pad controls; release restores touchpad steering. Physical reload samples remain on foot. Release build and headset acceptance pending.
- Initial x86 Release compile hit sandbox compiler database error C1902; the same Release build outside the restricted sandbox passed. Existing `issue014-math-test.cmd` passed and `git diff --check` passed. Backed up installed ASI/INI in ignored `build/issue019-seated-*`, deployed ASI SHA256 `496FE0154366C53629074803948727566C5235F6ACDE07BB6A34A3BA076C4004`. Launch initially timed out on RPC while loading, then reached normal apartment rendering; fresh window capture and PID16540 status confirmed FOCUSED DXVK-Vulkan stereo with no command-buffer overflow. User car hand/wheel acceptance requested; visible wheel rotation, left-hand weapons, and vehicle phone acceptance remain pending. No player teleport or INI overwrite.
- User accepts free seated hand tracking, but wheel acquisition required reaching far into its model and only worked briefly. First build had no rim snap, captured its anchor at entry, and tested wrist position instead of palm. Correction measures finger-base palm offsets, waits 500ms before capturing settled native palms, shifts default centre 8cm toward driver, uses a tilted wheel plane and 18cm nearest-rim acquisition tolerance, then stores a rigid wheel-local attachment per held hand. Both hands drive one averaged wheel angle; the actual native grip fingers follow snapped wrists. INI exposes centre offsets/radius/tilt and `hand-status.wheel` reports centre, palm distances and grip ownership. Wheel position still derives from native seated animation, not verified mesh vertices; geometry isolation and per-model fit remain pending.
- Corrected x86 Release build passed (only dependency PDB warnings); `git diff --check --ignore-submodules` passed. Installed ASI SHA256 `7A5C27E938F6A1A170C6C0DAE84F6FE4C1946FF2E828A964D0784C0472501385`, previous ASI/INI backed up in ignored `build/issue019-wheel-snap-*`. User tuning INI retained and defaults apply to newly added wheel keys. See [vehicle-interactions](../../docs/vehicle-interactions.md). Snap/placement headset check pending; #019 remains open.
- PlayGTAIV exited during corrected-build startup; Steam `-applaunch 12210` retry succeeded. PID10124 reached FOCUSED continuous DXVK-Vulkan stereo, fresh native window capture showed the normal apartment, and `hand-status.wheel` returned valid diagnostics (inactive on foot). User requested to test one/two-hand rim acquisition and snap placement. No automatic position restoration.

### Current state — 2026-10-06

The #014 car camera is accepted. #015 adds user-accepted basic Vive controls: right trigger throttle, left trigger brake/reverse, left pad touch steering, right menu entry/exit (including standstill exit), and right pad center handbrake. These are ordinary driving controls, not physical wheel/handle gripping. Remaining work includes physical cockpit interactions, bikes/aircraft/passengers and class-specific camera/control/comfort/compatibility routes. Historical statements that vehicle controls are wholly unimplemented are superseded by this summary.


- 2026-10-04 user accepts #014's current car camera coverage for its closure. Bike/aircraft/passenger and unusual vehicle exit camera routes are deferred here; do not infer those classes passed from the accepted car route.

- 2026-10-02: first-person vehicle camera support and entry/exit camera continuity are now required before #014 closes. Coordinate seated/riding anchors there; this issue retains VR vehicle controls, physical interactions and the broader vehicle route.
- User requests cars/bikes/planes. Do not assume the base game has playable planes or silently replace that requirement with helicopters. Split class-specific implementation issues when the inventory establishes scope.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.

- 2026-10-06: #020 implements accepted generic seat-relative car radar/health/armor/wanted panel; car radar yaw uses native vehicle heading. Wrist presentation resumes on foot/bikes. Default car offset is0.45m right,0.55m forward,0.32m down from stable seat eyes; size0.16m. Per-model placement, bikes/aircraft/passengers and physical cockpit interaction remain this issue. Broader UI compatibility is #031.
