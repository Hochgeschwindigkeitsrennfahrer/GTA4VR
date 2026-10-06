# Left-arm radar watch

Issue #020 starts HUD presentation with the native radar, health and armor ring on the left forearm. #007 records source attribution; other overlays remain separate future work.

## Implementation

`vrhud` hooks CE 1.2.0.59 native radar health/armor executor `00903060` to record its screen viewport. At eye publication, after native UI draws, it copies that region from the ordinary back buffer to a 512-square texture. This retains native map, markers, health and armor logic without repeating UI or simulation and without CPU pixel readback per frame. Copying directly after the health draw was too early: its diagnostic image contained only the ring. Legacy candidate `00D76360` had zero calls in two live CE runs and is unused.

The panel uses a solved left wrist snapshot captured with each eye command stream, after native pose finishing and body scaling. Each eye projects its world vertices using inverse eye camera times projection. Explicit target/depth writes and diagnostic blits bypass VRStereo's ordinary outside-eye surface redirect to the desktop buffer. A round bezel surrounds opaque sampled content; transparent native gaps can contain the underlying desktop scene. This is not an isolated transparent HUD target.

Diameter is configurable. The updated mount uses wrist X/Z plane and -Y normal to move from the lateral side to the back of the forearm. Per-eye facing checks hide the underside. Near-plane intersection, pause, absent focus, stale wrist or stale native radar suppress the panel. It draws without depth testing for readable essential feedback. Default-pool resources release on reset/stereo shutdown.

World occlusion, a physical watch mesh/strap, vehicle/action arm availability, other rigs, episode coverage, health/armor changes, navigation and reset/fallback coverage remain unverified. Full #020 acceptance includes other overlays and mission routes.

## Configuration

Settings are under `[VR]`; restart after changes.

| Setting | Default | Meaning |
| --- | --- | --- |
| `WristRadar` | `0` | Opt into watch; active stereo and tracked left rig required. |
| `WristRadarSize` | `0.10` | Diameter in metres (0.06-0.30), scaled with wrist basis. |
| `WristRadarBackOffset` | `0.075` | Wrist toward elbow offset (0-0.25m). |
| `WristRadarLift` | `0.04` | Mount normal offset (0.01-0.15m). |

`tools/game-command.ps1 hud-status | ConvertTo-Json -Depth 5` reports native hook calls, GPU capture/draw counts, failures and source/projected bounds. Successful draws alone do not establish visible/readable pixels.

`tools/game-command.ps1 hud-capture` requests one source radar and post-draw eye PNG on the next drawable watch frame: local `plugins/GTAIV.VR.wrist-radar.png` and `GTAIV.VR.wrist-eye.png`. This diagnostic readback can hitch; do not use it for timing and keep images out of Git.

## Evidence (2026-10-06)

- x86 Release builds passed with existing libmodupdater missing-PDB warnings. MSVC x86 host later hit C1060; `/p:PreferredToolArchitecture=x64` builds the same Win32 Release target successfully. Source diff checks passed.
- Installed ASI/INI backed up in ignored `build/issue020-backup-20261006-121539/`. Local watch enabled; hand calibration and user settings, including350N arm drive, preserved.
- Ghidra Issue020WristRadar bookmarks distinguish legacy and active paths; previewed, read back and saved on `/GTAIV.exe`, x86 image base00400000.
- Initial captures/draws succeeded but user saw no panel. Pure camera projection and explicit texture coordinates alone did not resolve it. Surface routing was the confirmed destination error: eye targets mapped back to desktop outside eye phases.
- Corrected routing Release SHA256 `03D9435972B9B704C87AC19DF054E2882A6AAF19DB8CE949BF6248589DAF33D4`: fresh source PNG shows player/home/north markers and health ring; fresh eye PNG shows radar on left forearm. More than7,700 copies, zero reported copy/draw failures. User confirms visible working watch.
- User requests smaller size, top-of-arm placement and underside hiding. Default/local diameter changed0.16 to0.10m, mount moved from lateral Z to -Y normal, per-eye facing suppression added. Updated placement acceptance pending.

Adjustment ASI SHA256 `360B1D819ADFB152591AC4DBEEC71FC515D550AA392DC26239BBE437254F92D5`. A run exited after reaching VR (cause unestablished; no matching recent Application Error1000 found); retry is active.

A fresh adjusted eye capture established that +Y mounts underneath the left arm. Final correction uses -Y mount offset and matching underside rejection.

Frame matching: watch pose is copied into each EyeMarker during scene construction and published alongside that frame camera, avoiding a newer global wrist pose being paired with an older rendered scene. Current Release SHA256 `4278912E564F312FB5E188050F4B36EFC6C2A824BBE2F109172AD4DB2C0760B6`; user-reported jitter and unexplained exits remain under investigation.

- 2026-10-06 crash investigation: passive debugger observed PID25836 exit C0000005 without an exception stack. Windows System event2004 recorded low virtual memory at12:57:34 (same PID) and13:02:40 (retry PID29464). A tool failed with paging-space error0x800705AF; only about49MiB commit headroom remained while GTA IV was running. SteamVR Home also faulted. This is evidence of system-wide commit exhaustion, not proof of a HUD fault. After the user's restart, commit is15.0GiB of31.2GiB. Resume the unchanged Release without PNG readback/debugger attachment; final mount/jitter/stability still pending.

- Post-restart runtime PID13296 reproduced the watch-visible crash despite ample commit headroom. Passive monitor captured first/unhandled AV at NVIDIA nvoglv32+0xBC3953, on the render callback through DxvkVulkan::Bridge::Copy (command recording near line128) and OpenXR OnEndScene. This supersedes memory exhaustion as a sufficient explanation for this reproduction. Local DXVK3.1.1 source confirms GetVulkanImageInfo returns the current relocatable DxvkImage handle without pinning it; bridge previously cached handles at initial publication. Added per-copy flush/current-handle resolution as a focused interop experiment; runtime outcome pending. It is not yet proven to explain the driver fault.

- Interop-refresh Release SHA25635D24461C2834F666FB221F0B79616201BD1372D9E4C9C5F8EC8F8E9957E8632 built Win32 Release with x64 compiler host (existing libmodupdater PDB warnings only). Installed INI unchanged. PID16708 has3264 radar copies/5292 watch eye draws, zero reported HUD failures and no monitor exceptions at this checkpoint. Fresh native window image shows a readable10cm radar on top of the left forearm, with health ring and native markers. These are initial positive runtime observations; movement/jitter/underside acceptance and longer stability remain pending. No claim that handle relocation alone caused the driver fault yet.

- Focused180-second monitor window completed without captured AVs; PID16708 remained responsive with12520 radar copies/7568 watch draws and zero reported HUD failures. Explicit debugger detach returned access denied(error5); monitor exited with kill-on-exit disabled and bridge/game responsiveness was verified afterward. This limits the debugger lifecycle evidence; no clean-detach claim. Headset placement/underside/jitter feedback remains pending; game left running.

- User confirms watch works after the interop correction, but its texture is mirrored. Corrected only horizontal UV orientation (u ->1-u) on the -Y mount face; geometry/mount/pose and installed INI unchanged. Win32 Release built successfully; clean bridge quit verified before backup/deployment. Installed SHA256B11BF8B3A113715DFEE2C8EF741E8067094DB62BBB1A5CA7A104A7225F216D8B; final unmirrored headset acceptance pending.

## Wanted stars and world-space screens

The radar shader adds a centered five-point star row above the circular watch when wanted count is nonzero. Count and flashing state come from read-only STORE_WANTED_LEVEL / WANTED_STARS_ARE_FLASHING calls on the game-process callback. No wanted level is changed by this feature.

OpenXR already initializes at the first usable EndScene/device callback. WorldSpaceScreens=1 submits the native full back buffer through a separate XrCompositionLayerQuad while stereo world resources are unavailable or the user/script pause flag is set. This bypasses the world-target warmup; it does not duplicate game simulation. The screen is2.4m wide,2m in front of the initial horizontal head direction, fixed in the tracking reference space for the duration of the screen. On STAGE space its center is at least1.2m above the floor. Each pause/loading entry reanchors; the screen is not head-locked. Gameplay restores the ordinary stereo layer. The same source/quad path now supports the pause-menu pointer iteration described in vr-controller-actions.md; VR settings remain #030.

The panel has its own swapchain and Vulkan copy bridge (or shared D3D9/D3D11 texture). GPU copies finish before release, queue calls use DXVK interop locking, and reset/shutdown release panel resources on the render callback. RPC status exposes panelActive/panelFrames; hud-status exposes wanted. The user accepted the wanted/loading/pause presentation on 2026-10-06. Controller pointer interaction is now being added; see [controller actions and pointer](vr-controller-actions.md).

Reference: [OpenXR quad composition](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerQuad.html).

## Car dashboard radar

With `WristRadar=1`, `DashboardRadar=1` moves the same native radar/health/armor texture and wanted-star row to a car-fixed panel while seated in a car. On foot and on bikes, the wrist presentation remains. The mount uses the existing stable vehicle-local seat eye anchor, snapshotted with each eye command stream; it follows the car rather than the headset or an animated head bone. Exiting the car restores the wrist presentation.

Placement is generic, not an identified dashboard surface. `DashboardRadarSize` is the diameter in metres (default0.16); `DashboardRadarForward`, `DashboardRadarDown` and `DashboardRadarRight` offset from the seat eye anchor (defaults0.55,0.32,0.45m). `DashboardRadarTilt` tilts the face upward toward the driver (default12 degrees). These INI settings are read at launch. Missing settings use those defaults, so existing local configurations need not be replaced. Per-model placement and bike mounts are deferred. Like the current watch, the panel has no scene-depth occlusion; unusual interiors may need offset tuning.

`hud-status` exposes `dashboardEnabled` and `dashboardEyeDraws`. Initial dashboard Release SHA256 `5453A4F503BD8CC7689E26E9DD6A84365319F8C75C95BFE6D1DCF9924919B0C9` built Win32 with the x64 compiler host (existing libmodupdater missing-PDB warnings). Installed ASI/INI backed up in ignored `build/issue020-before-dashboard-20261006-141728`; normal bridge quit verified, installed INI preserved. Headset placement, orientation and driving acceptance pending.

- Stars/loading Release SHA25654673E309B05E291118358A58531F46097676EA001130247A1D9A0B60017AC7B: Win32 Release/x64 compiler host succeeded (existing libmodupdater missing-PDB warnings). Installed ASI/INI backed up, normal bridge exit verified, installed INI preserved. CE1.2.0.59 / SteamVR2.17.10 / DXVK-Vulkan PID19136 reported panelActive=true/panelFrames=2 while eyeImages=false/stereo.ready=false, establishing early screen submission before world targets. Later panelFrames=555 with panelActive=false and3458 stereo layers establishes transition back to normal gameplay without runtime failure. hud-status samples wanted=0, watch draw failures=0. Nonzero wanted-star appearance and headset loading/pause presentation still await visual acceptance.

- Car follow-up: user confirms panel mount stays in place but its map texture rotates endlessly. Moved default right offset from0.15 to1.15m as requested; widened configurable range to +/-2m. CE radar update005DBC90 stores camera yaw at005DBE33 into0118F4B4. The scoped VR/car hook substitutes simulation-sampled vehicle heading in radians before native blip/map use, leaving camera transforms untouched. Signature matches once in the installed CE binary. Ghidra Issue020CarRadarHeading bookmark previewed, applied, read back and saved. Headset outcome pending.



- Car-heading Release SHA256 `A0B2EDD63FC487F2D6F565F3C93AC6300E32E65A13D4199E64A8E2A06F1BB6B5` built Win32/x64 compiler host successfully (existing libmodupdater PDB warnings only). Restricted builds failed C1902, including a clean-intermediate retry; running MSBuild outside the command sandbox succeeded. Installed ASI/INI backed up in ignored `build/issue020-before-car-heading-20261006-143144`; clean bridge quit/process exit verified before deployment, INI unchanged. `hud-status` now reports heading-hook readiness, override count and native/car angles in radians. Runtime/headset acceptance pending.

## View-attached text and popup HUD

`ViewHud=1` captures native resolved font drawing and HUD sprite/window commands into a transparent render target and composites it into both eye images. Native text layout, colors and timing remain game-owned. The layer sits on a shared stereo plane2m in front of the midpoint head pose; it follows the view and does not require a tracked wrist. Each eye projects the same plane through its completed scene camera, including its asymmetric FOV. Loading/pause continue to use the separate world-space panel; radar and wanted remain on the wrist/dashboard. Read at launch; VR focus and pause guards prevent flat-screen capture.

CE1.2.0.59 analysis:00923200 consumes a resolved font buffer(begin/end);008DDD50 is its render-command executor and must not be replayed because it consumes the command stream.008FE950 updates HUD records,008FF3D0 formats text and advances fades; these are not replayed either.008DDC90/008DDD00 draw HUD sprite/window payloads directly. Signature-guarded drawing replay uses a recursion guard, restores targets/depth/viewport/device state, and releases overlay resources on reset. Ghidra bookmark Issue020HudTextDrawing saved and read back at00923200. Runtime/headset coverage of subtitle/help/mission notifications remains pending; this does not establish all UI coverage or TLAD/TBoGT acceptance.

Dashboard placement correction: per user feedback, move0.6m left from the prior1.15m default to0.55m right of the seat eye anchor (0.4m right of the original0.15m position).

### Centering and unread SMS follow-up

User feedback: edge HUD messages are outside the useful headset view. `ViewHudWidth=0.44` now brings the entire native layout into a centred44% of each eye image, preserving the source aspect ratio (height limited to60%). Range0.25–0.70; read at launch. Readability needs headset confirmation.

Unread SMS: uniquely resolve the native `HUD_TEXT_MESSAGE_ICON` texture slot from its CE initialization at005CB80A (record index0118EDF8, slot019D3AB4). Match that texture in type2 HUD sprite commands, capture only the native sprite with its color/alpha, and suppress its duplication in the view HUD. Draw it below the wrist/dashboard radar, preserving native visibility/flashing. It is not driven by a guessed unread count. This is SMS only; other phone notifications remain in the general HUD. Ghidra Issue020SmsIcon bookmark previewed/applied/read back/saved. Icon placement/expiry and TLAD/TBoGT coverage remain pending.

Car dashboard default is now0.45m right of the seat anchor, following the additional0.1m left request.

### Binocular HUD correction

The first HUD layout used identical pixel coordinates for both eyes. User feedback established that this was doubled and unreadable in the headset; its earlier monocular screenshot/counter checks were insufficient. Replaced it with one common head-relative plane, using the completed stereo pair's midpoint rigid camera pose and each eye's actual view-projection matrix. This preserves eye offsets/asymmetric FOV and gives both eyes a common2m convergence surface. It follows the head and draws over the scene (no scene occlusion). Loading/pause retain their existing world panel.

`ViewHudDistance=2.0` metres (1–4); `ViewHudWidth=0.64` is half-width divided by distance (0.20–0.70), no longer a screen-pixel fraction. Default physical plane is2.56m wide, native aspect ratio retained, bringing layout further inward. Values read at launch. Existing explicit ViewHudWidth overrides remain honored. Focused math test reconstructs both asymmetric eye rays and confirms that they intersect the same point on the2m plane. Win32 Release and x86 vrmath test passed; headset fusion/readability acceptance pending.

HUD stereo fusion accepted by the user; default ViewHudWidth increased to0.64 for readability (2.56m wide at2m distance), twice the initial stereo plane dimensions. Distance and binocular projection unchanged. Size acceptance pending.
