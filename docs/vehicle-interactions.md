# VR car interactions (#019)

The first car implementation is under development on Complete Edition 1.2.0.59,
SteamVR 2.17.10 and Vive Wands with the DXVK-Vulkan bridge. Free seated hands
and wheel grip/visible rotation are user accepted across multiple tested cars. Bikes,
aircraft, passengers and episode-specific routes are not covered by this result.

## Seated hands

Driving exposes its own **Open phone (hold)** action in SteamVR bindings, with
left Menu as the Vive default. It feeds the same native phone-opening path as
the system action: hold opens the phone, while a short shared Menu tap pauses.
Custom bindings can use SteamVR's long-press activation. The existing Phone
action set supplies navigation/accept/back once open; opening does not exit the
vehicle. Existing custom bindings may need the new Driving action assigned.
Manual seated opening also requests the native held-phone task; our task is
released when the phone closes. Incoming calls keep their native task lifecycle.
The native phone occupies the equipped-object manager as type46. Holster render
filtering is limited to drive-by firearm types, so it retains this task prop.

Stable first-person car seating enables the existing tracked arm IK. Entry,
exit and ragdoll/action transitions retain their native pose gates. The published
tracking transform is rebased through the old/current vehicle root matrices on
the simulation callback, rather than translating it with a lagging animated ped.
On-foot body lowering/neutral stance and physical reload samples are suspended
while seated. Arm collision proxies remain disabled in the player's car.

## Wheel grip

The first prototype used the native wrist midpoint immediately on entry and
did not snap the hand; the user reported reaching far into the wheel to acquire
it. The correction waits 500ms for the seated pose, measures palm positions from
the four non-thumb finger bases, and captures their native midpoint in vehicle
space. It uses a tilted circular rim with an adjustable offset. This is an
animation-derived approximation, not a verified mesh-derived wheel frame.

Holding either tracked grip near the rim acquires it. The visible palm snaps
onto the rim; a rigid wheel-local attachment carries wrist orientation and the
native gripping fingers. Each physical palm is measured against its fixed
acquisition ray and wheel angle; the two absolute estimates fit one common
wheel rotation. Palm offsets are fixed while held. Steering stops do not consume
controller motion or redefine the reference, so returning a Wand returns its
original wheel angle. Releasing/regripping captures a new reference.
Releasing all grips restores the existing touchpad steering path. Phone use
releases wheel grips. Native pad steering still owns the car's driving physics.

### Native wheel mesh — experimental

#### Component isolation correction (2026-10-14, issue #3)

The Admiral revealed that fitting all vertices in one material geometry could
select a dashboard circle and rotate parts of the instrument cluster. Its CE
model84 skeleton has no separate `steeringwheel` bone. The replacement fits
triangle-connected components, welding duplicated positions at mesh seams.
Candidates must fit entirely inside the rim's local disc; nearby dashboard
components are excluded as a whole. Coplanar hub/spoke components are included,
while the shaft and stalks remain fixed.

The captured Admiral selects 173 rim/hub vertices in geometry4, with 63 rim
samples, instead of the former 258-vertex spatial selection. The fitted centre
also changes to the actual wheel. A deterministic fixture checks adjacent
dashboard exclusion, stalk exclusion, hub inclusion and invalid indices. The
x86 Release build passes; headset verification and other models remain pending.
Native geometry inspection confirmed its index buffer at geometry `+1C`, count
at buffer `+4`, and CPU backing indices at `+10`. Captures remain outside Git.
The binary importer failed because of a Java class-version mismatch; these new
findings have not been added to the Ghidra project.

The Ambulance model86 uses a higher driver-seat origin: the old 20cm lower
search cutoff removed the bottom of the wheel and prevented a complete rim
fit. A generic 5cm cutoff preserves the complete rim. Its connected-component
fit selects 84 wheel vertices in geometry3; no vehicle-specific offset is used.
The deterministic fixture also covers this lower wheel-to-seat relationship.
Headset verification remains pending.

The subsequent centre correction grows from the rim into nearby components
inside a tapered envelope, with additional depth toward the wheel axis. This
includes dished spokes and horn pads without widening the rim plane into the
stalks. Captured selections increase from136 to256 vertices for the Blista,
and84 to128 for the Ambulance; the Admiral retains173. The regression fixture
checks a separate dished spoke/centre badge and detached stalk. These captures
and math checks do not establish headset acceptance for every model.

Wheel completion now operates across all supported material geometries rather
than only the geometry containing the strongest rim fit. A combined 32-bit
triangle index space and model-space seam welding let hub/spoke components in
another material join the same tapered assembly. The native adapter validates
rigid chassis weights and acquires all affected vertex buffers before writing,
then rebinds every selected material part to the same wheel transform.
The captured Airtug adds90 centre vertices in geometry2 to208 rim vertices in
geometry3. Admiral173, Ambulance128 and Blista256 selections remain unchanged.
No model-specific wheel rules are used. Unsupported native vertex/palette
layouts still retain static geometry; universal vehicle coverage is not claimed.

The Benson exposed a remaining adapter restriction: material geometries with
eight or more palette entries were excluded before assembly selection. The
adapter now admits the native supported palette range when the checked spare
entry is present, retaining the rigid-weight/buffer checks. Its spokes/hub use
two13-entry palettes. Dish depth now scales with fitted radius rather than a
fixed8cm allowance. The captured Benson selects298 vertices across geometries
2,3 and6, including the90 centre vertices. Airtug298, Admiral173 and
Ambulance128 remain unchanged; Blista includes an additional12 centre-collar
vertices. The long steering shaft and detached stalks remain excluded by
component extent/contact tests. Runtime model coverage remains in progress.

The Bus revealed that a full native palette has no append space at all. Wheel
rebinding now uses a module-owned expanded palette for each affected geometry,
without inspecting or writing allocation padding. Before native geometry
destruction, its original palette pointer/count are restored and the module's
storage/cache records are released. This lets full palettes participate in the
same multi-material wheel assembly; the captured Bus selects292 vertices,
including48 spoke vertices in its formerly excluded8-entry material.

The destructor's allocation ownership was traced from a bounded decrypted
runtime capture in Ghidra: native `0045F540` frees geometry `+38` through its TLS
allocator. The deleting wrapper `0045F470` calls it at `+3`. Installation checks
both signatures through the live geometry vtable. Findings are saved in the
`wheel-native.bin` capture program under `VRWheelPaletteOwnershipIssue3`;
capture offset `540` maps to native `0045F540`. The imported executable's bytes
in this region are encrypted/misdecoded, so that stored static extent was not
repaired or reused. Captures remain ignored and are not distributed.

The Forklift uses shared19-entry palettes listing every vehicle bone, including
the driver seat, even though the high-detail vertices never use that bone.
The adapter now checks actual nonzero skin weights across all three scene LODs
instead of rejecting palette membership. A discarded CPU backing copy uses a
read-only native GPU-buffer lock for this one-time model preparation check.
Unknown layouts or an actually weighted seat bone retain static geometry.
Its captured high-detail assembly contains128 wheel vertices in geometry5;
headset verification remains pending.

The first shared-palette build still rejected the Forklift's unuploaded second
LOD, where vertex buffer `+8` and GPU interface `+1C` are null. The native
backing store at `+20` remains populated. Read-only inspection of all scene
LODs found8641/4781/522 vertices and no driver-seat weights. Ownership checks
now use that native backing store before attempting a GPU-buffer read.

The new build searches the high-detail car geometry for a complete circular rim
near the authored driver seat. A deterministic geometric fit supplies the centre,
plane and radius for both hand snapping and visible rotation. Native seated palms
remain the fallback if a model cannot be fitted safely. This corrects the user's
reported leftward placement without another guessed universal offset.

CE model112 has no separate steering-wheel bone. Its geometry6 contains the rim
and spokes; a local capture identified 198 candidate wheel vertices at indices
1060–1295, excluding the adjacent dashboard. The current C++ fitter selected194
of these vertices, with98 rim support samples. The native driver-seat bone32 is
unused by existing high-detail geometry. The experiment adds that bone to
apparent palette padding and rebinds only rigid chassis-weighted wheel vertices.
Original model-space positions, textures, materials and native inverse binds
remain intact. The seat's rendered pose rotates around the fitted wheel frame
only while building the player's car commands, then its original pose is restored.
Other instances retain their ordinary seat pose. No game asset files are changed.

The first car-entry test **crashed**. This path is now disabled by default,
behind developer-only `[DEVELOPMENT] ExperimentalWheelMesh=1`. Neither the
original native VB+20 COM-interface assumption nor the palette allocation's writable capacity was
verified; padding bytes alone do not prove capacity. The crash's exact fault
site is unknown. Ordinary builds keep the fixed-reference steering calculation
without installing this render hook or modifying geometry/seat matrices.

Read-only inspection after successful entry in the isolation build identified
the error: native VB+20 is raw mapped vertex data, while +1C is the D3D9
interface. Model164's +1C vtable GetType returns VERTEXBUFFER and GetDesc matches
DXVK's descriptor-copy code. The corrected experiment uses +1C and checks the
descriptor type. The user accepts car entry, wheel rotation, snapping and
one/two-hand return across multiple cars. It remains enabled locally. Palette
ownership/capacity, command retention and asset lifetime still need verification.

Actual command
pose retention, model/LOD coverage, transitions and shared flat-screen behavior
still need checking. Unknown formats, an already-used seat bone, insufficient
palette padding, unsupported skin weights or failed buffer access retain the
ordinary geometry. Visible rotation is not claimed for such models.

The circular-control precedent is Valve's
[SteamVR CircularDrive sample](https://github.com/ValveSoftware/steamvr_unity_plugin/blob/master/Assets/SteamVR/InteractionSystem/Core/Scripts/CircularDrive.cs),
which projects tracked hands onto a rotation plane. Our two-hand reference fit
is project code; it is not copied from that sample or claimed as a universal
VR steering standard.

## Tuning and diagnostics

The ASI's `[VR]` section accepts the following settings; restart to apply:

| Setting | Default | Meaning |
| --- | --- | --- |
| `WheelGripRadius` | `0.18` | Palm-to-rim acquisition tolerance, metres |
| `WheelRadius` | `0.17` | Fallback rim radius, metres; a fitted mesh supplies its own radius |
| `WheelTiltDegrees` | `20.0` | Fallback wheel plane tilt from vertical |
| `WheelCenterRight` | `0.0` | Vehicle-local right offset from fitted centre or fallback palm midpoint |
| `WheelCenterForward` | `0.0` | Forward offset; negative moves toward driver |
| `WheelCenterUp` | `0.0` | Vehicle-local up offset, metres |
| `WheelSteeringDegrees` | `180.0` | Rotation from centre to full steering |

Values must be finite; rim radius and steering range must be positive for valid
circle/division operations. Other tuning is not limited to guessed ranges.
`tools/game-command.ps1 hand-status` reports `hands.wheel.ready`, held hands,
palm-to-rim distances, world centre, normalized steering, mesh model/vertex count,
rim support, render-hook availability and visual callback count. These establish
which path ran; they do not establish that the anchor fits the visible model.

Manual car phone activation/presentation is user accepted on 2026-10-10;
broader call/mission lifecycle and drive-by fire/ammo routes remain in #019.
The initial wheel grip currently uses the raw tracked grip state;
independent semantic driving bindings remain to be added.

## Drive-by weapons — first pass

Entry preserves the last on-foot equipped weapon if it supports drive-by fire;
otherwise it starts holstered instead of accepting the native automatic SMG
choice. Weapon cycling while seated still changes that selection normally.

Driving now exposes separate semantic weapon selection and left-hand fire
actions. Right pad side clicks cycle the native inventory through unarmed,
pistols (7/9) and SMGs (12/13); other classes are excluded in this GTA IV pass.
Right trigger remains acceleration. With a firearm selected, left trigger fires
and left pad click brakes/reverses; unarmed retains left-trigger brake/reverse.
Right pad centre remains handbrake. All new actions appear in Driving and can
be rebound; custom SteamVR bindings may require adding them explicitly.

The Driving `horn` action defaults to an upper right-pad click. Centre remains
handbrake and side clicks select weapons. Cycling through unarmed holsters the
local equipped gun, suppresses its render callback and disables tracked fire,
even when the native seated weapon selection refuses type zero. Selecting a
firearm unhides it; leaving the car clears this vehicle-only holster state.
Left attachment uses the asset's authored gun_grip2 frame against the
completed left wrist, independently of on-foot support-position tuning.

Selection reads the current inventory slot even if the seated weapon has no
held model. It consumes a side click only after its direction is valid.
`input-status.driving` exposes the selected action-set inputs;
`hand-status` exposes selection attempts, last stage and candidate. Stages
1/2/3/4/5 mean input received, player validated, inventory read, native selection
requested and immediate readback matched. These are diagnostics, not evidence
that the gun is visible or the selection remains active on subsequent ticks.

The authored left grip is attached to the calibrated left wrist. Only the
current local equipped object is moved; its animated world bones receive the
same rigid transform. The pose is refreshed before rendering and firing and
rebased through the current car frame. Left weapon ownership excludes left
wheel grip. Phone/menu activity suspends firing and weapon selection.

This first pass returns clip management/reloading to the native vehicle route,
transferring the existing partial clip and total ammunition on entry. Car-used
weapons resume from native ammunition on exit. Physical magazine/rack gestures
remain on foot. User accepted left-hand drive-by attachment and manual car phone
use on 2026-10-10. Firing/ammo transitions and broader phone lifecycle remain
pending; bikes/passengers/episodes are not inferred covered.

## Motorcycle handlebars and worn helmet

Tracked under [vehicle interactions #3](https://github.com/bordplate/GTA4VR/issues/3).
Bike hand targets use the authored `hbgrip_l` / `hbgrip_r` bones and `forks_u`
pivot, with parent-composed bind transforms. Either grip acquires the matching
handle within WheelGripRadius; both hands fit one absolute steering angle about
the fork axis. Hand attachments follow the native animated grip bones, including
fork and suspension motion, rather than a separately rotated bind pose.
Acquired palm offsets keep the wrist aligned without accumulating drift.
Releasing returns steering to the touchpad. Phone, panel editing and the
drive-by hand retain priority. Steering travel comes from each bike's native
handling data (+DC8 pointer, +84 radians), rather than a fixed 35-degree scale.
CE `00CECBE0` smooths and signed-squares pad steering before writing +1088.
The local tracked driver's held-handle adapter overrides the resulting +1080
normalized input and +1088 angle after that handler, retaining throttle/brake
and native control when no hand is held, tracking is stale or VR loses focus.
Native bike physics controls lean and balance;
this does not introduce body-lean steering or a physical throttle twist.

The first-person helmet filter operates on copied native prop draw records,
without removing the helmet or changing its protection/state. CE 1.2.0.59
`009E9560` checks the worn flag at CPed+264 bit8000. `008DB1F0` constructs
CDrawPedPropsDC from two 0x40 records; `00AB9B10` rejects drawable index -1.
`00AB9BC0` distinguishes default attachment (+14==-1) from an animated bone
override. The eye-only constructor filter omits the local worn head prop on
the default attachment, retaining the putting-on/removed animation attachment,
other characters, scripted third-person views and shared shadow builds. Findings
are saved in Ghidra under `VRBikeHelmetIssue3`. Runtime headset validation and
coverage of the remaining bike models are pending. The initial indirect pad
steering produced nonlinear response and hand/handle separation; direct steering
and animated handle attachment are the current correction under validation.

Native arm solver `00C9F720` calls `00C95F90` to choose an elbow from the
animation's old shoulder/elbow/wrist plane. Near straight-arm poses this plane
can become unstable. Tracked bike solves now scope a body-relative outward/back
elbow pole around that helper, retaining native upper/forearm rotation and final
twist-bone processing. It applies to tracked bike arms under either touchpad or
held-handle steering; other native solves retain their own elbow selection.
The helper ABI was checked live (nine stack arguments, RET24, output pointer
in EAX). Geometry fixtures cover steering-sweep continuity, segment lengths,
unreachable targets and a collinear pole. The corrected full-lock arm behavior
passed the Bobber headset check with touchpad and grip steering; the current
base-game bike sequence is complete. Other episodes remain untested.

## Helicopter flight — initial adapter

Helicopters now enter the freely tracked vehicle-hand and dashboard-panel paths.
The existing Driving accelerator/brake actions supply native lift up/down
(right/left trigger in the Wand defaults). With no cyclic grip, the left
touchpad supplies pitch/roll. Grip the cyclic with the right hand to control
pitch/roll physically; left/right on the left touchpad then supplies yaw.
Release returns to touchpad cyclic control. Phone/menu activity releases the
cyclic and suppresses injected flight controls.

CE 1.2.0.59 `00B6AA00` is the pilot input handler, virtual+158, one stack
argument. It reads lift at pad+2A08/2A18 (55/56), unlike car controls40/41.
The local-driver adapter retains native lift/hover processing and replaces
the pitch/roll outputs +1EB8/+1EBC and yaw +1EB4. Physics forces, rotor startup,
flight assistance and landing remain native. Other drivers and unfocused VR
retain their native input handler.

The tested Annihilator skeleton has no authored cyclic bone. Its initial grip
position is learned from the native seated pilot's right palm before tracked
IK when no mesh fit is available. A recognized stick supplies its bottom-end pivot and grip centre; `HelicopterCyclicShaftLength` supplies the fallback pivot below the native grip;
`HelicopterCyclicTravelDegrees` controls centre-to-full-input tilt. Acquisition
captures one reference; subsequent tilt uses absolute vehicle-relative angles.
The hand and isolated stock stick geometry follow the same virtual pivot.
The mesh adapter selects complete welded slender shaft/grip components near
an authored pilot seat, joins grip components only near the shaft upper endpoint, validates rigid chassis weights, and borrows an unused
seat bone through the existing owned-palette lifetime adapter. The skeleton
matrix is overridden only during the local vehicle draw and restored afterward.
Touchpad pitch/roll also tilts the visible cyclic. Unsupported meshes remain
static rather than rotating neighbouring cockpit geometry.
Flight controls have been checked in the Annihilator. Visible cyclic alignment
and other helicopter models/episodes remain pending. Ghidra bookmark: `VRHelicopterCyclicIssue3`.

## Analysis record (native wheel)

Ghidra bookmark `Issue019WheelRender` at A3D980 records the verified runtime
CVehicle +8C entry (four stack arguments, RET10) and native mesh layouts.
PID10124 module base005C0000 placed that entry at00BFD980. The imported program
still contains packed bytes in this code region, so its static pseudocode is
not evidence for the implementation. The bookmark was previewed, read back
and saved; local native vertex captures remain ignored under `build/`.

## Boats

Boat classification now enables seated tracked hands and vehicle panels through
native `IS_THIS_MODEL_A_BOAT`, independently of the car/bike/helicopter gates.
Boats share the physical wheel solver, mesh fitting, grip priorities and release
to touchpad steering with cars. Native throttle/reverse and rudder simulation
remain responsible for movement; no boat forces are synthesized. Where wheel
geometry cannot be safely fitted, grip placement falls back to the settled
native driver's hand pose and the visual wheel remains static.
Base-game boat validation starts with the Dinghy; other models and episodes
remain pending.

Large helm wheels use a wider geometric search only when the car-sized search
fails. This retains the original rim/connected-component validation and avoids
per-model wheel positions. The Marquis capture fits a 0.485 m radius wheel.
Seated camera anchors accept finite native vehicle-local offsets rather than
rejecting seats beyond five metres from a large vehicle's origin. Radar heading
follows any valid seated vehicle instead of being limited to car/bike models.
Rigid wheel parts weighted to a separate `bodyshell` bone are accepted only when its bind frame is identity; other articulated joints remain excluded.
Rim fitting permits front/back samples from a thick steering-wheel rim to
contribute to the same circle. The Reefer's separated depth faces otherwise
leave too few inliers per face. Whole-component depth/radius checks still
exclude neighbouring geometry; the captured Reefer wheel now passes.
Sparse full-circle rim components can seed wheel selection with 24 vertices,
while retaining seven-of-eight angular coverage and complete-component checks.
This supports the tugboat's rim faces split across material geometry; the full
assembly still passes rigid-weight validation before any buffer is modified.

## Rail test setup (CE 1.2.0.59)

Ghidra bookmark `VRRailTestingIssue3` records live disassembly at native
`BC54A0` -> `BC9A20` -> `C39570` (analysis addresses; observed process base
00C00000). Native consists are 16-int rows at analysis016D0D58; type9 is cable
model208, type12 is a singleton subway model209. Route counts and pointers at
016D1198/016D11C8 describe six routes with 12-byte nodes: signed shorts XYZ,
scaled .125/.125/.03125. Native C3A300 performs nearest-node selection.
The bridge uses route4 for cable and route0 for subway tests. A scoped type12
model substitution exercises subway_hi/model210 and restores the consist
before returning. Native creation readback must match the requested hash.
Train speed, boarding and cleanup use native functions on the simulation
callback. No ambient trains are removed or given test input. Runtime validation
of all three rail entries remains pending.
The stock cablecars script checks wanted level zero before offering boarding. Rail tests temporarily suppress wanted levels and restore the maximum on safe exit/cleanup. Native train occupancy complements car occupancy in VR camera/action routing.
A one-time maximum-wanted change is insufficient against locked-island scripts. Rail tests lazily install scoped ALTER_WANTED_LEVEL/NO_DROP filters once the native table is ready, maintain police-ignore/invincibility during the test and restore normal behavior on exit. Explicit rail status reports health, wanted and actual boarding state; protection is not inferred from command acceptance.
Cable-car passengers retain native seated travel and freely tracked hands, with radar on the wrist. The on-foot cabin experiment was withdrawn: touchpad and room-scale movement expelled the player from the cabin. Walkable interiors are out of scope; the exact collision shape was not established.
