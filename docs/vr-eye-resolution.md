# Independent headset resolution

Tracked in [rendering issue #2](https://github.com/bordplate/GTA4VR/issues/2).

## Configuration

The desktop resolution remains GTA IV's Graphics setting. In the ASI INI:

```ini
[VR]
UseRecommendedEyeResolution = 1
EyeRenderScale = 1.0
```

OpenXR's stereo view configuration supplies the recommended per-eye size.
The scale multiplies both width and height, so 0.75 uses approximately 56% of
the pixels. Restart to apply. Scale must be a positive finite number; only
runtime/device allocation limits constrain the resulting dimensions.
`UseRecommendedEyeResolution=0` retains the preceding desktop-sized eye path.

The two eyes use one common allocation size (the larger recommendation for
each axis), retaining each eye's separate runtime FOV and projection. The
desktop mirror scales the completed left-eye image into the desktop buffer.
It is not an independent third-person camera.

## Routing

Eye color/depth, deferred G-buffer, AO and screen-sized post-processing targets
resize proportionally to the eye dimensions. Native originals and metadata
remain untouched. Fixed luminance/history textures, phone targets and shared
shadow/reflection resources retain their semantic dimensions. The optional
scope view uses the independent eye raster size too.

CE viewport windows are normalized: the recorded native viewport is
`windows=0,0,1,1` with raster `size=1920,1080`. Only raster dimensions change;
the normalized windows and OpenXR FOV remain intact. Routed viewport/scissor,
explicit copy rectangles and pretransformed fullscreen quads account for the
destination dimensions. Fullscreen pixel-center scaling preserves D3D9's
half-pixel convention.

Named native `globalScreenSize` and `gooDeferredLightScreenSize` shader
parameters are transformed through an optional thread-scoped callback using
copied values. It does not mutate shared shader parameter metadata or global
desktop dimensions. Existing queued CE fullscreen constants also have guarded
dimension/inverse-dimension replacement, preserving unrelated material data
that uses the same register numbers. End markers restore native fullscreen
constants and routed target identity for auxiliary/desktop work.

Reset destroys the eye targets and OpenXR swapchains; the recreated eyes query
the runtime again independently of the new desktop dimensions. Loading/pause
panels and desktop HUD capture keep their native resolution and aspect.

## Initial verification (2026-10-11)

Baseline `ed17911`, GTA IV CE 1.2.0.59, SteamVR 2.17.10, DXVK/Vulkan,
Vive headset/Wands. Win32 Release passed with existing modupdater missing-PDB
warnings. Deterministic resolution tests passed: dimensions, scale, structural
allocation caps, half-sized targets and pixel-center preservation.

Backed up installed ASI/INI and deployed matching SHA256
`543851D64AED6E444FE693FAB24A1892A45CFD97650D5DA83694C7B3A6D605C1`.
Runtime graphics, desktop-resolution reset and flat-screen checks are pending.
No frame-rate improvement is claimed. Adaptive resolution is out of scope.

The first live build produced a stretched, blended second image in both the
headset and desktop mirror. Detaching incompatible native depth/MRT attachments
before switching sizes did not remove it. An explicit `stereo-test` capture on
the following build (`184DA1C981CE76E2638C20203730A0E0E41CDCF36F2EB84121CB6F4A6CF09891`)
showed a clean 2848×3168 diffuse G-buffer and a ghosted final color image,
localizing the error after geometry rendering. Deferred shader sources also
place `gooDeferredLightScreenSize` in PS c77/c79/c84/c86/c72, not just the
previously handled VS c118/c208 and PS c44. Screen-size value routing now checks
the full dimensions/inverse-dimensions signature across constant writes and
cached constants at phase boundaries. This correction is under runtime test.

The bounded lighting draw capture then established the missing encoding:
depth-copy PS c66, directional VS c208 and local-light PS c77 contain
`(0,0,0.000390625006,0.000694444461)` at a 2848×3168 viewport, with eye G-buffer
textures bound. Those inverse dimensions still describe 2560×1440. Directional
lighting's image is clean; the next local-light draw adds the stretched image.
Both `(width,height,1/width,1/height)` and `(0,0,1/width,1/height)` now resize and
restore while preserving the zero-origin meaning. Deterministic tests cover
both encodings and reject unrelated vectors. With `[DEVELOPMENT]
ProbeEyeResolution=1` (restart to apply), `stereo-test` also saves bounded
lighting inputs and intermediate images; the option defaults off. Ordinary
gameplay does not capture. Shader parameter lookup caches clear after reset.

The corrected Release ASI (`D8EAC399B80F32270F6C4A6E097EF44D66561B7476D9DEFAEEA63BB8F4B4ECA4`)
has clean final captures in both eyes. The local-light PS c77 readback is now
`(0,0,0.000351123599,0.000315656565)`, matching 2848×3168. Headset acceptance,
desktop reset and flat-screen validation remain pending.

After changing desktop resolution to 1920×1080, the user reported mismatched
shadows and substantially worse performance. Runtime readback retained
2848×3168 for configured and submitted eyes. A 30-second explicit GPU capture
reported eye medians 7.3165/7.8633 ms (p95 9.7628/10.8575 ms), eye command CPU
medians 1.9223/1.6709 ms, and submission interval median 20.8517 ms. This is
not a third desktop camera: each eye now contains about 2.45 times the pixels
of the preceding 2560×1440 targets. Recommendation scale 1 is not a claim of
90 Hz performance; the fixed scale can reduce this pixel cost independently
of desktop resolution.

The post-reset directional-light draw capture contains the original symmetric
`mul r0.xy, r0, c209`, rather than the stereo ray correction. Shader discovery
previously stopped after the first eight vertex/sixteen pixel shaders. The
correction now classifies each previously unseen lighting shader, stopping
vertex discovery when the required ray shader is found. This avoids depending
on loading/reset shader order. Shader-parameter indices also invalidate on
reset. Runtime acceptance of these reset changes remains pending.

Release build `133DCA9F7E4C9E35566CD096FA94ECA3F8BEED2F81A17B00765C6FB8432B06BF`
deployed with installed test scale 0.64 (repository default remains 1).
The game renders continuously, reporting 1822×2027 submitted eyes, and a fresh
native window capture confirms the desktop is 1920×1080 with a clean mirror.
This scale has approximately the preceding 2560×1440 pixel count per eye.
The user is checking shadows across another Graphics reset; no final reset
acceptance is claimed yet. The explicit lighting probe setting was restored
to 0 for the next launch.

The 30-second scale-0.64 run recorded GPU medians 3.7519/5.0217 ms
(p95 5.2480/6.5106), with mean submission interval 11.1352 ms (~89.8 pairs/s).
These runs span a relaunch and shader correction, so they are not an isolated
matched estimate of scaling alone. They demonstrate a workable pixel budget
in this apartment view, not acceptance of every street scene at 90 Hz.

The user accepted shadow alignment after changing desktop resolution with
the corrected discovery. They still report intermittent hand shimmer or
different lighting/reflections after reset. A new explicit-only body material
probe records up to sixteen local-player draws per eye (shader, textures,
samplers and constants), so diagnosis does not require holding the headset
and controller in a fixed pose. It shares `ProbeEyeResolution=1` and
`stereo-test`; ordinary gameplay keeps its draw hooks removed. No cause or
fix for the remaining shimmer is established yet.

Diagnostic Release `EC420C6A343C487D6E419D52810627088E7FD9DD1488B2D08E6635AF2A2B6505`
captured six body material draws per eye after a fresh launch. Matching draws
use the same pixel shaders/textures and all declared samplers' thirteen states
match. Used pixel constants match except expected per-eye camera position
PS c15. A post-reset comparison is pending; this fresh capture does not prove
the reported intermittent defect absent. The desktop came back at 2560×1440
after this launch while eyes remained 1822×2027. Raw records stay ignored.

After the user's 1280×720 desktop reset, six matched body draws again have
equal used textures, sampler states and material constants (apart from the
expected eye position). The user sees shimmer more clearly in the headset,
especially looking up; still-image VR View does not clearly show it. This
weakens the old body normal-map sampler-state explanation. The next explicit
probe captures up to 64 deferred draws in each eye with all sixteen texture
slots/sampler states and native target names/dimensions, and only four
thumbnail readbacks. The current location was recorded for this specific
fresh-launch/post-reset comparison, rather than returning to an old profiling
location.

The first expanded probe crashed during its explicit capture. Removed raw
iteration of native `RTCache` entries; the diagnostic now queries dimensions
only from AddRef-held, actually bound D3D texture/cube interfaces. Release
`A67B6E7CE2A64B75323703A6FBA9666749820D27FC0D7C9D5E46E9CC7ADF0B77`
completed a fresh explicit capture (21/22 deferred draws) with the crash
monitor attached and continued rendering. Its native readback confirmed the
location restoration; fresh eye graphics show the apartment ceiling. Deferred
sampled texture dimensions and sampler states match on corresponding draws;
light-list indices differ where eye culling selects different local lights.
Post-reset comparison and headset fresh-launch observation are pending.

The user deferred the remaining flicker investigation on 2026-10-11 and accepts
keeping desktop resolution fixed during a session. Three explicit captures
show a dark-to-lit hand change with similar normals and changing G-buffer 2
contents; no cause is established. Flicker is visible in the desktop mirror
as well as the headset. Fresh-launch versus reset-only behavior is not isolated,
and relaunching is not a proven cure. Latest x86 Release
CAAC046643F965806A9BDBD4BFDB38BEC9882944055444BE6F011AE7C78C7195
adds explicit-only vertex constants/shader, MRT and render-state body capture;
it built and is running, but that additional probe was not exercised before
the user stopped the investigation. ProbeEyeResolution is restored to 0 in
the installed INI for the next launch. Independent sizing and accepted shadow
correction remain active; flat-screen regression remains pending. See GitHub
issue #2 for the deferred limitation.

## Persistent shading investigation (2026-10-11)

The user reports the defect throughout gameplay, including the moon and mirror
reflections, without requiring a desktop resolution change. This supersedes
the reset-only interpretation. Investigation resumes under [issue #2](https://github.com/bordplate/GTA4VR/issues/2);
performance work remains deferred.

The explicit bridge command `resolution-capture` records one stereo pair's
shader/constants, viewport, target and sampled texture dimensions across all
three main eye stages. It records up to 64 unique vertex/pixel shader pairs
per stage/eye, includes indexed draws, and reuses the existing bounded body
capture. It does not enable continuous diagnostic logging or counters.
Raw output stays outside Git. No Computer Use or location restore is needed.

Release E2175437C2C564C655F2A0FEEBA7FE7BD5E96534598FF737A84ACF8FEC5E7885
captured 33 geometry, 6 lighting and 43 final-scene records across both eyes
in the normal spawn scene. A final-scene smoothing shader sampled
`FullScreenCopy` at 1974x2192 while PS c76 remained
`(1/2560, 1/1440, 0, 0)`. Its instructions use c76.xy for neighbouring
color/depth samples. The previous screen-size matcher only recognized inverse
dimensions in zw. The correction also routes the verified inverse-xy/zero-zw
encoding and restores it at the native phase boundary. Packed constants
such as c220 `(1/2560, 1/1024, 1.3333, -0.3333)` remain unchanged.

Focused math checks cover forward conversion, restoration and preservation of
unrelated packed constants. This is a verified texel-size mismatch, not proof that it
causes all reported shading, moon or mirror defects. The tracked mirror shader
uses projected coordinates and no screen-size parameter; it may need separate
projection/texture-generation investigation. Headset acceptance is pending.

Corrected Release 3FC2D60E0487A86CE2BBA8193C65163EA0FDF9189B6B238A6E0CE70C84AF7981
built and deployed with ASI/INI backup; installed INI was preserved. PID31564
reported focused OpenXR and continuous stereo. A fresh explicit capture reads
PS c76 `(1/1974, 1/2192, 0, 0)` in both identified final-scene consumers.
Focused forward/restoration/packed-constant preservation tests passed. This confirms the
specific shader input correction in runtime; visible defect acceptance and
flat-screen regression remain pending. No performance improvement is claimed.

The user reports possible partial improvement from inverse-xy correction, with
remaining smooth shading sliding across walls/floors during head movement.
The captured final-scene fog VS still reconstructs depth rays symmetrically:
`mul r0.xy, v0, c77.zwww`, with c77.zw `(1.27877092, 1.42167366)`.
Its fog PS combines that ray with eye depth to reconstruct world height and
atmospheric scattering. Main eye geometry uses the asymmetric runtime FOV,
so these ray slopes omit each eye's projection offset.

The verified six-instruction VS is cloned with only that multiply changed to
`mad r0.xy, v0, c209.xyxy, c209.zwzw`. c209 is unused by the native VS and is
supplied with half tangent spans and half tangent sums for the active eye,
then restored when switching shader/phase. Native clip position, texture UVs
and camera basis remain intact. This extends the deferred-lighting ray
correction to the final-scene fog path; it does not patch arbitrary shaders.
Release 1072BF4B215EDD11BB41A3D93E4CD66D9AB14B28686C2AEB97E0EC192BF1A61A
built successfully (existing updater PDB warning only) and was deployed after
normal bridge shutdown, with ASI/INI backup and no automatic location restore.
Live shader input and visual acceptance checks follow.

Runtime PID26072 reports focused continuous DXVK-Vulkan stereo at 1974x2192.
The explicit post-correction capture confirms the patched MAD and c209 match
expected eye rays in both eyes: left `(1.27877092, 1.42167366,
-0.0680714846, 0.00277125835)`, right `(1.28085423, 1.42296398,
0.0687324405, 0.00235509872)`. Raw records are in ignored
`build/shading-ray-after`. Headset acceptance is requested; moon/mirror and
flat-screen regression remain unverified. No performance claim is made.

User follow-up rejects fog correction as the remaining defect fix: the reported
problem started with independent eye sizing. Treat the fog patch as a separate
projection correction, without claiming acceptance of the target issue.

The earlier "shadow-atlas c220" interpretation was too confident. Captured
directional and forward shaders read only c220.zw in a clamped blend; neither
reads c220.xy containing inverse2560/inverse1024. Thus those numerical matches
do not establish desktop-size shadow sampling. Keep that packed register
unchanged; investigate actual shadow/reflection sampling separately.

The initial all-stage capture missed DrawPrimitiveUP, used by FusionFix's
ambient-occlusion and immediate postfx passes. Explicit resolution capture now
includes that call as well, using the same bounded shader-pair inventory.
Release 5DADE2CBCAE8C6918BCD607A4DA5B55CFF23CC9CB0BBC6C7D34667F291F5683D
built and deployed with backup; installed settings preserved. No behavior
change beyond the explicit capture is claimed for this build.

Immediate-pass capture at PID36572 found AO rendering into 1974x2192 with
projection coefficients derived from desktop2560x1440. ComputeAO PS c6.xy
was `(-0.000999039738,-0.00197454682)`, while the eye projection requires
`(-2*1.27877092/1974,-2*1.42167366/2192)`. Its inverse-size c3 was already
routed, creating an internally inconsistent reconstruction. Minification
preshader c2 retained `(2559,1439)`; horizontal blur c6.x was 2/2560 and
vertical c6.y was 2/1440. These are verified desktop-dependent consumers,
not just numerical resemblance in an unused register.

RenderAmbientOcclusion now derives its raster dimensions from the device's
actual saved viewport rather than RAGE's execution-time mWidth/mHeight.
This supplies consistent full-screen vertices, inverse size, projection
coefficients/scale, mip bounds and blur offsets. Mip dimensions clamp to one
pixel for valid texture levels. It retains the current camera projection,
near/far, AO settings, and native texture identities for existing routing.
Release E73CE72963BA776485F04925E95832DABBE06A203279D069D4CA8A99392143DB
built successfully and was deployed with ASI/INI backup. Runtime readback and
headset acceptance follow; this mismatch can explain depth-dependent sliding
shading but is not yet proven to explain moon/mirror symptoms.

Post-correction explicit capture verifies AO c6.xy now
`(-0.00129561382,-0.00129714748)` in the left eye, equal to its actual
projection slopes divided by1974x2192. Minification bounds are `(1973,2191)`
and horizontal blur c6.x is2/1974. Runtime remains focused/continuous.
Headset acceptance is requested; no visual resolution or performance gain is
claimed until that check. Shared AO implementation also needs flat-screen
regression before issue closure.

User accepts the AO correction: sliding wall/floor shading is now fixed.
The moon remains unresolved, so the graphics issue stays open. The held-sky
capture includes the moon-capable atmos/clouds shader. PS c44 already reads
1974x2192 with correct inverse dimensions; its only screen-size use controls
an aspect-conditioned fog branch, disabled by captured c210.y=0. Moon UVs
come from normalized sky direction and PS c85/86/87, not screen coordinates.
The captured final eye image shows starfield but no visible moon. Asked for
its observed symptom and a visible view before claiming a cause or changing
moon projection. Current settings/location preserved; no Computer Use.

### Moon clipping after independent eye sizing (2026-10-11)

The user reports the moon appearing only at the edge of the headset view and
confirms it worked before independent sizing. Release clipping capture
`66CDB347177532739213DA05FB96DF0542E2C7CF37A185169A8B13FA250C41C2`
on CE1.2.0.59 / SteamVR2.17.10 / DXVK-Vulkan / Vive Pro records the actual
moon-capable atmosphere draw with scissor testing enabled. Left-eye viewport
and target are1974x2192, but its scissor is `(1212,793)-(1510,951)`.
The right eye similarly retains `(1076,793)-(1373,950)`. Depth range is0-1,
ZFUNC is LESSEQUAL, and user clip planes are disabled.

Projecting captured PS MoonPosition through the sky VS matrices places its
left-eye center near `(1052,1322)`. Using desktop2560x1440 instead gives
approximately `(1365,868)`, inside the captured rectangle. This identifies
a desktop-coordinate scissor, rather than a wrong moon screen-size constant.
The previous SetScissor hook resized only complete fullscreen rectangles.

SetScissor now scales partial native rectangles to the bound eye target as
well, rounding lower bounds down and upper bounds up. Existing full eye
rectangles and compositor calls are preserved. Rebind also converts the
existing device scissor when switching between differently sized native/eye
targets, keeping restored native state consistent. Focused deterministic
tests cover the captured rectangle, fullscreen bounds, and outward rounding.
They pass; runtime capture and headset acceptance follow. No moon fix or
performance gain is claimed before those checks. No Ghidra project changes
were required: evidence is shader bytecode, constants and actual D3D state.

Release `53773E66C1D642E7D1687177F13AF3D577C745B9682F1ED09BBC7C00B1522C5B`
built successfully (existing updater PDB warnings only), passed the focused
eye-resolution math checks, and was deployed with ASI/INI backup and normal
bridge shutdown. Installed tuning is preserved. Launcher startup was retried
through Steam when PlayGTAIV exited without a live GTAIV process; no player
location restore is performed. Runtime acceptance remains pending.

After-capture at PID11408 confirms partial eye scissors now scale with the
eye raster: left `(934,1207)-(1164,1448)`, right
`(828,1205)-(1058,1447)` on1974x2192 targets. Slight differences from the
earlier bounds reflect the current view. Raw records remain ignored under
`build/moon-scissor-after`. The user confirms the moon now works normally
when turning the head. AO and moon regressions are accepted fixed; mirror
behavior and shared flat-screen regression remain unverified. Issue#2 stays
open for its broader outstanding rendering/performance work.
