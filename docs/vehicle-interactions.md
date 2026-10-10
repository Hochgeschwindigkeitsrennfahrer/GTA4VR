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

## Analysis record (native wheel)

Ghidra bookmark `Issue019WheelRender` at A3D980 records the verified runtime
CVehicle +8C entry (four stack arguments, RET10) and native mesh layouts.
PID10124 module base005C0000 placed that entry at00BFD980. The imported program
still contains packed bytes in this code region, so its static pseudocode is
not evidence for the implementation. The bookmark was previewed, read back
and saved; local native vertex captures remain ignored under `build/`.
