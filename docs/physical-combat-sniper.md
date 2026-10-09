# Combat sniper / PSG-1

First pass under #024 targets GTA IV Complete Edition 1.2.0.59's native type16,
`w_psg1`, not the bolt-action rifle or episode weapons. It has a 10-round
magazine, 400ms cadence, 1500m range and native base20/fps75 damage data.

The adapter provides tracked semiautomatic firing, authored two-hand support,
arm recoil and physical reload using `VRWeapons/psg1.ini`. It has an independent
chamber/magazine cache. The external Animated Weapons assets remain required;
no proprietary assets are distributed here. Its firing clip is `weapon_fire`
in `gun@rifle`. Local model inspection identifies `PSG1_Mag`,
`PSG1_Gun_Cock1` and a separate `PSG1_Breach`. Live type16 readback confirms
magazine bone8 pivot -0.0139846/0.00878203/-0.0202853m, handle bone5 pivot
0.215676/0.00689075/0.0828681m, muzzle bone1 and support grip bone2. The initial
insertion point uses the seated magazine pivot. Reload, support grip, firing,
scope depth and quick-motion attachment are accepted by the user. An initial
type17 probe selected the bolt-action rifle
and was corrected before asking for this weapon's acceptance.

`sniper-test psg1` equips it through the bounded simulation callback, preserving
owned ammo or supplying20 rounds if absent. It does not teleport or write saves.
Use `melee-test-status`, `reload-status` and `hand-status` for readback.

## Native delayed bullets

Ghidra addresses below use image base00400000; runtime module base is read
fresh. `B5FE20` dispatches firetype2 to `B60BD0`. That calls the stdcall
spread function `B5CCC0` (RET0C), then `D9B430` to queue a native bullet with
1000m/s speed. The adapter removes hip-fire spread only for the active VR
type16 shooter, retaining physical aiming error and recoil.

The later update `D9B9D0` queries `A545A0` at `D9BF7C`:13 cdecl arguments,
caller ADD ESP34, fourth argument points to the retained shooter. A wrapper
applies the existing owned player/weapon/arm ray exclusions during that query.
The firing-time exclusion alone would expire before delayed collision occurs.
Travel, native collision results, impact effects, attribution and damage
remain native. Other shooters retain the original query and spread behavior.
Bookmarks `Issue024SniperDelayedFire` and `Issue024DelayedVrShotFilter` were
previewed, applied, read back and saved in Ghidra on2026-10-07.

## Scope rendering — initial live adapter

The first implementation builds one rifle-aligned narrow-FOV world camera
when either tracked eye enters the eyepiece region. It clones one set of the
native world targets, dispatches only the three world phases after the two
eye streams, and samples that result on a circular lens with a reticle.
Simulation, shared shadow/reflection updates, HUD and OpenXR submissions
are not repeated. Each eye projects the lens through its own world camera
and tests it against its own depth surface. Freshness is checked against the
completed eye frame; an old scope image is never composed into a newer pair.
The local player is excluded from scope construction.

The equipped rifle and owned loose magazine also require exclusion from this
camera: its origin is inside the optic's mesh. CE's CObject virtual `+0x8C`
resolves to Ghidra `0x00B12E80` (four stack arguments, `RET 0x10`). A bounded
read of the live type16 object and disassembly establish this draw-command
builder. The filter checks the exact owned objects only during scope world
construction. Other entities, normal headset views and shared phases retain
their native calls. `scopeHiddenObjects` records actual filter coverage.
The Ghidra bookmark `Issue024ScopeObjectRender` was saved on 2026-10-09.

The scope uses a separate resource context on the native command executor,
with all bindings returned to their original identities at context changes.
It retains the engine's existing target dimensions in this first pass to keep
fullscreen/deferred screen-size constants consistent. This adds memory and
an extra world build while aiming. Reduced target dimensions, cost measurements,
optical placement, eye-box behavior and shot/reticle alignment remain pending;
no negligible performance penalty is claimed. The scope targets and shader
are released alongside stereo on reset/shutdown. This is a new world view,
not a magnified crop of a headset image.

Tune `[Scope]` in installed `plugins/VRWeapons/psg1.ini`, then run
`tools/game-command.ps1 reload-weapon-config`. `EyepieceX/Y/Z` are weapon-local
meters (+X muzzle-forward, +Z up). `LensRadius`, `EyeRelief`, `EyeBoxRadius` and
`VerticalFovDegrees` control the aperture and activation/magnification.
`Enabled=0` disables the scope without changing firing/reloading. Initial
eyepiece (-0.16/0/0.125m) and radius22mm are provisional, not measured mesh
landmarks. `status` reports scope build/render/lens draw/failure counters;
counters alone do not establish visual correctness. The PSG support seat is
6cm behind and3.5cm below its authored `gun_grip2` pivot in this test.

The support adjustment is accepted in headset. The initial lens remains
invisible according to the user; this is not an accepted scope implementation.
The lens now uses the equipped object's rendered world transform and the
G-buffer's actual eye depth attachment. `DebugLens=1` shows a green aperture;
`DepthTest=0` temporarily bypasses occlusion for placement diagnosis. Both
diagnostics default off/on respectively and should be restored after testing.
`scope-probe` builds 30 scope views and saves one source image to the installed
game's `plugins/GTAIV.VR.scope.bmp`. It does not force the lens into the eye box.
This readback is only requested for diagnosis, never part of normal rendering.

OpenXR composition layers can display an application-rendered texture; they
do not render the magnified game scene for us. Prefer a world-depth lens draw
in the existing eye pass to an always-on-top compositor panel.

### Scope-state regression (2026-10-09)

The user reported different shadows between the eyes after using the scope,
and a disappearing lens while tuning its INI. The first correction restored
a complete D3D9 state block, but the user then reproduced persistent severe
world corruption on scope activation. That approach was rejected: native
commands update RAGE's software cache too, and rewinding only device state
can make the cache disagree with the device. The revised executor retains
native final state, uses the existing native-identity resource/register
rebind, and restores only our camera/ray/cascade selection and first-eye
mip-filter bookkeeping. HUD/lens shader calls bypass routing bookkeeping
while drawing under their own state blocks (these draws do not execute native
commands). Scope activation/deactivation visual acceptance remains pending.

The user subsequently confirmed the persistent renderer corruption is fixed.
The remaining obstruction is the ordinary rifle drawing over the lens, not
the rifle appearing inside the magnified source image. Lens projection now
runs in a vertex shader with normal homogeneous GPU clipping instead of
rejecting the whole quad when one projected corner crossed the near plane.
The lens uses the main geometry depth viewport's MinZ/MaxZ rather than assuming
0..1. These lens visibility changes require headset confirmation; the current
eyepiece position is user tuning and has not been replaced.

The GPU projection change did not resolve the user's obstruction. Disabling
depth testing made the lens usable, but re-enabling it at the final aligned
position still hid it. The lens now prefers the actual scene-depth target
`_DEFERRED_GBUFFER_3_` (INTZ, also used by the existing depth readback), rather
than the depth buffer bound after the scene phase. Its usage/dimensions and
depth attachment call are checked; early scope logs compare both pointers.
This correction's headset acceptance remains pending.

Scope settings reload as a transaction: an invalid number retains all previous
accepted settings. Rejections now identify the key and allowed range in
`plugins/GTAIV.VR.stereo.log`. `EyeBoxRadius` accepts 0.01–1.5m (previously
0.01–0.15m); large values are for placement diagnosis, not realistic optics.
The user's 1.10m setting previously rejected the entire update. Eyepiece
position controls the optical camera. `LensSurfaceOffset` moves the aperture
toward the eye from that origin (meters, default 0.001) without changing the
magnified view. It reloads with the other scope settings. The lens explicitly
clears native constant/slope depth bias and keeps normal scene depth testing.

The held depth capture logged lens NDC 0.03307738 versus scene 0.02537155 /
0.02518529 at clip W 0.05166626m. The initial interpretation as 0.4mm surface
overlap was WRONG: FusionShaders writes logarithmic depth, so perspective
NDC cannot be compared with that buffer. The user confirmed a 2mm offset
change did not help. Named GBuffer3 and phase-end pointers were identical,
ruling out the earlier attachment hypothesis in that run.

### Match FusionShaders depth encoding (2026-10-09)

`source/shaders.ixx` supplies PS c209 = (1/near, 1/log2(far/near),
far/near, near). FusionShaders' perspective `LogDepth Write` computes
`log2(clipW / near) / log2(far / near)` in the fragment shader. Live bounded
shader captures confirmed c209=(20, 0.0672373995, 30000.002, 0.05).
The lens had relied on ordinary interpolated perspective depth instead.
At the captured lens distance, correct logarithmic depth is about 0.00318,
which is IN FRONT of scene depth 0.0252; at ordinary viewing distances the
incorrect perspective depth becomes much larger and rejects the aperture.

The lens now carries clip W through a perspective-correct interpolator and
writes DEPTH using the same logarithmic formula. Main-eye near/far parameters
are retained with each queued view, so the additional optical pass cannot
replace them. Homogeneous near-plane clipping and ZWRITE=0 remain; normal
scene occlusion is retained. The unnecessary 3mm default offset is restored
to the original 1mm. Unit regression checks encoding endpoints, ordering,
and the previously misinterpreted captured values. The user confirmed the
corrected scope works in headset, then refined the eyepiece to
(-0.25, 0.007, 0.122)m and aperture radius to 0.019m. These positions are now
the shipped defaults; the diagnostic-wide live eye box is left untouched.
This follows the project's FusionShaders pipeline; a replacement shader pack
using conventional depth would require matching its encoding separately.

### Keep the lens on the rendered rifle

The user reports slight lag relative to the weapon during rapid movement.
SceneSetup sampled the equipped root before ordinary draw-list construction.
The CObject render callback now records the owned rifle's transform after
building its first-eye geometry commands. Before constructing the optical
pass, refresh the scope camera and eye-box test from that snapshot. Both the
aperture and optical camera retain this same pose through execution. A bounded
eight-sample diagnostic compares it with the early setup pose. No controller
prediction or smoothing is added. The user confirmed quick-motion attachment
now works. Source defaults preserve the user's charging-grip offsets
Y=0.07/Z=0.025m, eyepiece and aperture tuning. Episode compatibility and #024's
broader weapon/fallback gates remain open.
