# Room-scale collision investigation (#014)

## Accepted route — 2026-10-04

#014 is closed after the user's combined first-person/room movement/collision/crouch/jump/vault/ejection/car/headset-aiming acceptance and separate final flat-screen check. The conservative torso sweep and immediate collision-corrected camera are accepted for the tested route; full crouched collision, penetration/step coverage and measured scale/pose behavior remain limitations. Extra vehicle routes remain #019, scopes/special weapons #016, independent desktop visibility #028 and height/settings calibration #030. Cutscenes retain the existing camera path; other special transitions are deferred. The historical pending statements below record earlier experiments and are superseded by this acceptance only for the tested route.

Baseline: Steam Complete Edition 1.2.0.59, x86. Addresses below are Ghidra virtual addresses with image base `0x00400000`; relocate from the actual live module base. Findings were saved and read back in the GTAIV Ghidra project on 2026-10-02.

## Intended movement

### Gameplay camera anchors and physical crouch iteration

`GameplayAnchor` retains the body-root/floor camera during on-foot crouching, jumps, falling and getting up, including animated head offsets below the root. Positive animated head rise relative to the standing baseline is added to root travel for skeletal jump motion; that rise is not learned as a new standing height. It excludes dead players and active cutscenes until those states have an explicit camera policy. Vehicle entry/exit reset the tracking reference. Room-scale body writes and walking-heading overrides are stopped in vehicle views.

The 2026-10-02 vehicle iteration continuously read the animated world head and used only vehicle yaw. The user rejected its speed-dependent backward displacement, jerks and low view on hills. The 2026-10-04 correction calibrates a local head point once the ped is seated, then transforms that fixed point with the current vehicle matrix. Vehicle right/up/back basis now follows pitch and roll as well as yaw. `VehicleEyeHeightOffset` raises the calibrated seat point by0.075m by default (clamped -0.3..0.5). Fresh headset translation remains immediate and seat-relative. Entry/exit animation continuity, seated calibration, bikes, aircraft and vehicle motion comfort require separate runtime checks; no smoothing or velocity-based camera pullback is added.

`ExperimentalPhysicalCrouch=1` adds a simulation-owned native crouch request from fresh focused floor-relative height. `PlayerHeight=1.75` meters supplies a fixed configured reference. Enter at65% of that height (1.1375m), leave at75% (1.3125m); live poses never recalibrate this reference. Pause-menu settings and explicit standing/seated height calibration are planned in #030; pause/tracking gaps do not request state changes. The path clears only crouch it requested and preserves an already-active ordinary crouch. For owned physical crouch the camera ignores native head lowering and compensates any native root lowering: only the physical headset height lowers the eye. This replaces the earlier partial compensation, which the user reported still double-lowered the view. Ordinary button crouch retains animation compensation and was generally accepted. This iteration does not change the standing torso collision approximation to crouched bounds, and OpenXR focus does not independently prove the headset is worn. Crouch math tests passed; corrected headset route remains unverified.

#### Camera-state test matrix

The crouch cancellation latch respects a native duck cancellation after the game has entered duck, whether it was requested physically or manually. It stops automatic retriggering until the headset crosses75% of `PlayerHeight` again; subsequent descent through65% can then trigger a new crouch. This uses the game's duck state, so it follows ordinary keyboard/gamepad bindings and also respects scripted cancellations. It does not consume the button or prevent ordinary manual crouch. Pause and invalid poses do not rearm the latch. Ghidra `Issue014PhysicalCrouchCancel` at9F2270 records tentative controller-clear semantics, without installing a new hook or claiming its ABI is verified.

Vault/climb, get-up and BlendFromNM task states use native head translation instead of the script root. Ragdoll takes precedence over a lingering vehicle association, including a fatal ragdoll while the ped remains present. The existing NaturalMotion neck matrix is preferred with an approximate0.12m world-up head offset, falling back to native bone1205. Camera orientation remains controlled by the headset, without forced body roll. Room-scale body writes and ordinary walking-heading overrides are suspended during these states; already-consumed tracking is subtracted from the animated anchor. Native animated state selection and eye placement still need headset route acceptance. Ordinary non-ragdoll death and cutscenes remain separate pending policies.

Use `movement-status` to distinguish first-person selection, vehicle, ducking and airborne states; check the headset image as well. A native flag alone does not verify camera comfort or placement. The user accepted crouch/jump motion in Release `6D63B367`; configured-height/default-offset revision is Release `92BAE887`. Baseline: CE1.2.0.59, SteamVR2.17.10, system D3D9Ex. Record per-class results in #014 before marking these accepted.

| State / transition | Intended behavior | Current evidence / remaining check |
| --- | --- | --- |
| Standing / stairs / room movement | Body-root floor anchor, immediate tracking and native horizontal collision | Previously accepted; new build starts first-person. Recheck after vehicle exit. |
| Ordinary crouch / stand | Native animation lowering, first-person retained | User accepts crouching on2026-10-04. |
| Physical crouch / stand | Native duck request, no doubled tracked lowering | User accepts corrected motion; requested new65%/75% thresholds with configured1.75m height. New thresholds pass math checks, runtime feel pending. |
| Jump / fall / get-up | Follow root travel plus skeletal jump rise without selecting chase camera | User accepts jumping on2026-10-04; falls/get-up still need separate coverage. |
| Car entry / driving / exit | Seat-specific head anchor and vehicle basis, animated head during exit, then return to floor tracking | User accepts driving height and normal exit on2026-10-04. Other seats/classes and drag/jump-out variants remain unverified. |
| Bike / helicopter / passenger | Per-seat head anchor, free headset look | Separate routes pending. Pitch/roll following now implemented; comfort unverified. |
| Aim / cover / scripted gameplay | First-person retained, camera and gameplay aim coherent | Aiming/auto-aim and special modes remain open. |
| Vault / windscreen ejection / ragdoll get-up | Follow animated head translation with headset rotation; stop room-scale body writes | User accepts these action paths and crouch cancellation/rearm on2026-10-04. |
| Cutscene / death | Explicit transition policy | Currently excluded from gameplay anchoring; unresolved. |
| Pause / tracking loss / recenter | Suspend movement requests, recover without a height jump | Existing gates retained; final combined route pending. |

Replace the rejected accumulated motor/displacement-share prototype with direct, collision-resolved physical movement. Blocked displacement must correct both body and camera. Normal headset tracking remains immediate. Keyboard/gamepad movement continues through the ordinary motor and physics pipeline. Do not implement leg animation as part of this replacement.

## Direct-position prototype

World-camera preparation publishes the latest tracking sample. The simulation's existing `onGameProcessEvent` consumes the horizontal displacement between that absolute sample and its last processed sample, before ordinary game processing. It queries eleven spheres covering the measured standing torso capsule, conservatively inflated from radius `0.25` to `0.256` to cover the spaces between samples. The earliest returned fraction limits the displacement with a two-millimeter contact skin. This uses the verified engine query and ignores the local ped; it is a conservative torso approximation, not a full composite sweep. It stops at contact rather than sliding along walls. Foot/support/vertical travel remain with ordinary physics; crouched bounds, steps and initial penetrations require validation.

For an accepted displacement, signature-checked `00B25FF0(position, skipPedSync=1, flags=0)` updates the root, then `00B25810(flag=1)` synchronizes current/previous collider transforms and broadphase. Assembly confirms `ret 0C` and `ret 4` respectively. This avoids the full ped relocation's contact/motor/IK-history clears. State preservation and update ordering still need runtime acceptance.

The camera base's horizontal position is the current root minus the absolute processed tracking sample. Composing the current eyes therefore displays fresh unprocessed head movement immediately; the next simulation update resolves that displacement and corrects body/view at contact. Accumulated `consumed` and `achieved` distances are diagnostics only; they cannot drive view compensation. Tracking gaps, owner changes and jumps over `0.3` units rebase the next displacement without a catch-up queue. No room-scale velocity or animation-target injection remains. Contact correction can occur one simulation update after the newest rendered pose; head-height collision is not yet independently modeled.

The first direct-position Release (`5D9050E2…`) performed writes during camera preparation. The user accepted physical centering/return and wall blocking/camera correction, and walked outside the apartment. PID27396 later became unresponsive; RPC timed out and the process was force-closed. The cause is unknown and must not be attributed to camera preparation without evidence. The simulation-ownership revision changes update ordering and requires its own runtime check.

## Native entry points

| Address | Evidence and limits |
| --- | --- |
| `00A535E0` | Assembly confirms a cdecl wrapper with nine stack arguments: start vector, end vector, radius, ignored entity, result pointer, collision mask, material mask, mode, optional start-sphere test. It resolves the ignored ped's instance through `+7B0`/`+7B4`, then invokes `00738A80` with one result. Exact mode and hit-field semantics still require runtime evidence. |
| `00738A80` / `00738CD0` | Native radius/segment and sphere queries. Their constructors store endpoints, radius and ignored instance; they invoke the engine's physics query dispatcher. This establishes the native collision pipeline, not a verified full-body sweep. |
| `00C9C670` | Native caller initializes sixteen `0x60`-byte results, combines a downward ray with a radius query, and selects a contact. Observed mask `0x8E`; do not assume that it includes every obstacle needed by VR. |
| `00B25FF0` | Lower-level position setter copies entity translation and can call virtual `+B4` for physics synchronization. It does not perform a sweep. Calling convention, synchronization flags and safe update ordering remain to be verified before movement writes. |
| `009F0E20` / `00B25810` | Ped synchronization and physics matrix/broadphase update. Nonzero reset flag clears support/motion history. Preserve these contracts when choosing a displacement operation. |
| `009F33D0` | Full ped relocation resets grounded/contact/support state and synchronizes transforms. The script coordinate setter reaches this path; unsuitable as an assumed per-frame collision mover. |

## Actual player bound

Live PID 26172, module base `00AA0000`: physics archetype `+0C` points to `rage::phBoundComposite`. RTTI for vtable `00FE5FEC` confirms the type. Virtual `+38` at `00748A30` walks child pointers at `+80`, local matrices at `+84`, and the ushort count at `+92`.

The sampled Niko bound contains four parts: two `phBoundCurvedGeometry` parts and two `phBoundCapsule` parts. Capsule local AABBs indicate radii `0.1` and `0.25`, with local Y extents `±0.425` and `±0.735`; their local matrices rotate Y into vertical Z and translate by `-0.875` and `+0.085`. These are sampled dimensions, not proof that each child is active in ordinary walking. Child collision masks and curved geometry contents remain unknown. The composite AABB must not be treated as one capsule.

`tools/read-player-transform.ps1 -GameProcessId <pid> -CollisionShape` reads these layouts with bounds checks and RTTI gating. It makes no process writes or native calls.

## Bounded native query probe

Under the existing development `ProbeFirstPersonAnchor` option, a signature-checked diagnostic runs on the verified local pedestrian motor callback. It samples eight horizontal directions over time, comparing both native query modes with radius `0.25` and mask `0x8E`, excluding the local ped. It logs raw one-result records and a trailing-buffer guard to `FusionFix.vr-collision-probe.log`. No body positions are changed. The diagnostic tests query ABI/result behavior; it is not the room-scale collision implementation or a complete body-volume test.

## Query runtime evidence

Release SHA256 `F37BC021B4E4402EC7B2E456BDF917B61FC65CF014F6D10FA6941E1CCA5F2E90`, PID34172: all eight paired samples completed with intact trailing guards. Directions toward nearby geometry returned hits; other directions returned none. In mode 1, a two-unit negative-Y sweep with radius `0.25` reported fraction `0.7014797` at result `+40`, normal approximately `(-0.001134, 0.999999, 0)` at `+20`, and wall contact Y `-501.45798` at `+10`. Starting Y was `-499.805023`: `2 * fraction + radius` matches that wall distance. Result `+44` was `0.5970405`, matching `2 * (1 - fraction)`. This supports mode 1 as a swept sphere and those result-field interpretations for the sampled wall. Initial overlap, nearest-hit ordering and full-body support remain unverified.

The native window capture failed creating a D3D11 device with `0x8007000E`; bridge-triggered fresh eye BMPs provided an alternative image check. They show apartment wall/floor geometry at `1920x1080` per eye in this run. This is launch/render output evidence only, not acceptance of collision, centering or vertical visibility.

## Leg animation ownership, deferred

`SET_PED_ENABLE_LEG_IK` reaches `00BAD830`, toggling ped `+264` bit `0x20000000`. `SET_USE_LEG_IK` reaches `00BB51F0`, toggling disable bit 4 in ped `+BE0`, within the inline `CIkManager` at `+BB0`. The walking move-blend owner at `+A80` is separate. These switches expose IK control but do not establish a full leg animation solution. No rig/animation writes were added.

## Remaining gates

- Prove native query mode, result position/normal/fraction semantics and self-exclusion.
- Resolve actual body volume, support/floor behavior and safe transform synchronization.
- Replace accumulated motion attribution; test simultaneous keyboard and physical movement, door alignment, walls, stairs and recentering.
- Fix the separately reported vertical culling/LOD defect. The sphere near-distance correction did not pass the user's retest.
- Physical crouch and vehicle first-person remain open acceptance work in #014.

### Vehicle transform helpers (2026-10-04)

CE registration hash7F8D3DD9 selects Ghidra BC60C0 (`InvokeVehicleLocalToWorldOffset`) and373B213C selects BC6080 (`InvokeVehicleWorldToLocalOffset`). Their wrappers pass seven cdecl arguments: vehicle, three floats, three float output pointers. BCB3D0 multiplies local XYZ by the vehicle entity+20 matrix rows and adds translation; BCB310 subtracts translation and dots the basis. These helpers do not read a chase camera. Native local axes map X=right, Y=forward, Z=up; the VR camera basis maps right/up/back to X/Z/-Y. Decompiler core prototypes incorrectly omit the vehicle argument; the wrapper assembly establishes the call boundary. B9ED70 (`InvokePedWorldCoordinates`, hash2B5C06E6) calls BA6740, whose coordinate source selects entity+20 or associated object+20 under its native flag. This means coordinate/animated-head/root ownership must be measured rather than inferred from a script-native name. `Issue014VehicleSeatAnchor` bookmark and wrapper names were read back and saved in Ghidra. The fixed-seat correction removes repeated animated world-head reads during driving, but headset acceptance is still pending.

Action-state selection is sampled on the existing simulation callback. The renderer accepts that snapshot only for the same player handle/entity and within250ms, rather than walking a potentially changing task list during world-camera preparation. On the simulation thread, body/crouch guards recheck current action state before writing. This separation does not establish measured pose latency; #011 retains timing validation.

### Vehicle exit handoff

Exit-task selection covers `TaskComplexNewExitVehicle`, get-out, jump-out and being-dragged-out tasks using existing repository IDs. While a native vehicle association still exists, the exit view uses the animated head plus the accepted vehicle eye-height offset instead of the fixed local seat point. Vehicle-relative headset tracking and basis remain until attachment ends; afterward the animated on-foot path continues until the task finishes, then normal body-root/floor tracking resumes. Room-scale writes stay suspended during the exit task, and ragdoll still takes priority over any vehicle association. `movement-status.vehicleExiting` identifies this branch. Runtime continuity and per-vehicle exit coverage remain pending.
