# 020 — VR minimap, subtitles and gameplay overlays

## Goal

Make gameplay overlays readable and usable in the headset, including minimap/radar, subtitles, health/ammo and notifications. Depends on pixel/draw attribution in #007. Roadmap stage 4.

## Tasks

- [x] Capture native radar/health/armor on the left forearm with corrected size, orientation and back-face visibility.
- [x] Present radar/wanted on a stable generic car dashboard mount with vehicle-relative heading and configurable offsets; return to wrist on foot/bikes.
- [x] Show native wanted count/flashing above radar, hidden at zero.
- [x] Submit loading/pause/map on a world-space screen before eye targets are ready and return to stereo gameplay.
- [x] Add tracked pause-menu ray/select/drag/back and semantic SteamVR actions (shared with #015).
- [x] Capture resolved text/popup drawing without advancing HUD timing twice; compose on a readable shared binocular plane.
- [x] Implement native unread-SMS routing to radar; active/expiry acceptance transferred to #031.
- [x] Record source/draw ownership in docs/Ghidra; full phone/post-processing attribution remains #007.
- [x] Verify final VR-disabled launch and ordinary desktop composition, with user confirmation.
- Broader mission/dialogue inventory, lifecycle/reset/fallback routes, active SMS acceptance and episode matrix are transferred to [#031](../open/031-ui-coverage-and-lifecycle.md).

## Done when

The core radar/wanted, loading/pause/menu pointer and native text/popup HUD are visible and readable in both eyes with user acceptance, while the final VR-disabled launch retains normal desktop composition. Remaining breadth/lifecycle coverage is explicitly tracked in #031 rather than inferred complete.

## Notes / evidence

- 2026-10-06 car radar iteration: generic panel uses the stable vehicle-local seat eye anchor, snapshotted per eye. Radar/health/armor and wanted stars switch from wrist to car panel while seated in a car; bikes retain wrist. Source INI exposes size, forward/down/right offsets and tilt. This is approximate placement, with model-specific overrides deferred and no scene-depth occlusion. See `docs/wrist-radar.md`.
- Win32 Release/x64 compiler host succeeded (existing libmodupdater PDB warnings only), diff check passed. ASI SHA256 `5453A4F503BD8CC7689E26E9DD6A84365319F8C75C95BFE6D1DCF9924919B0C9`; installed ASI/INI backed up in ignored `build/issue020-before-dashboard-20261006-141728`, clean bridge exit verified, installed INI preserved. CE1.2.0.59 / SteamVR2.17.10 / DXVK-Vulkan PID20224 reached FOCUSED continuous stereo,3073 layers, no command-buffer overflow. Fresh native image shows a rendered street scene; SteamVR reports one base station not detected. In-car placement/orientation/driving and return-to-wrist acceptance pending; current scene is on foot.

### 2026-10-06 — accepted screen presentation and pointer iteration

- User confirms the deployed ray/select/drag/back controls and named SteamVR action sets all work (“everything seems to be working perfectly”). The pointer subtask is accepted; broader overlay/mission coverage remains open. The game is left running with the accepted Release and unchanged installed settings.

- User confirmed the corrected watch UVs and then accepted wanted/loading/pause presentation (“Sweet, that's working!”). Broader subtitle/notification and mission acceptance remains open.
- Added a tracked OpenXR aim ray, UV cursor and menu button snapshots using existing FusionFix menu wrappers. Native CE frontend query008B4B30 and ProcessTabs005AAA60 confirm mouse record018B7A80, left-button bit1 and back bit8; normalized x/y use native scale. Findings persisted/read back in Ghidra bookmark Issue020MenuPointer and GTAIV.exe saved. No game assets or project database copied into Git.
- Win32 Release and focused geometry tests passed. Deployed CD6AD95267AA63817FE362BC1C936DBD3FA1A81D3E008D1DCFEEE78AD947DA66 with installed INI preserved. Live session initializes all semantic sets and renders; manual pointer hover/select/drag/back acceptance is pending. See #015 and [implementation/controls](../../docs/vr-controller-actions.md).

- User requests overlays such as minimap and subtitles. #007 supplies render attribution; this issue implements and verifies their VR presentation. Physical phone behavior is #022.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.


- User confirms watch is visible and works. Feedback: too large, lateral placement, visible from back. Next Release reduces diameter16 to10cm, moves to wrist X/Z plane/Y mount normal, and suppresses the back face per eye. Placement recheck pending.

- Adjustment Release SHA256 `360B1D819ADFB152591AC4DBEEC71FC515D550AA392DC26239BBE437254F92D5`, Win32 target with x64 compiler host. A test run exited after reaching VR; no matching recent Application Error1000 was found, cause unestablished. Retried through Steam; active #020 bridge and captures confirmed. Updated mount acceptance pending.

- User flags new unexplained game exits as possible HUD regressions and reports motion jitter. These exits occurred before requested bridge quit, so they were not intentional closures. Recent Windows error evidence covers an earlier startup CRYPT32.dll AV, not the latest exits. Current Release `4278912E564F312FB5E188050F4B36EFC6C2A824BBE2F109172AD4DB2C0760B6` corrects mount to -Y and snapshots the wrist with each eye command stream rather than reading a newer simulation pose at publication. Runtime stability and jitter acceptance remain pending.

- 2026-10-06 crash investigation: passive debugger observed PID25836 exit C0000005 without an exception stack. Windows System event2004 recorded low virtual memory at12:57:34 (same PID) and13:02:40 (retry PID29464). A tool failed with paging-space error0x800705AF; only about49MiB commit headroom remained while GTA IV was running. SteamVR Home also faulted. This is evidence of system-wide commit exhaustion, not proof of a HUD fault. After the user's restart, commit is15.0GiB of31.2GiB. Resume the unchanged Release without PNG readback/debugger attachment; final mount/jitter/stability still pending.

- Post-restart runtime PID13296 reproduced the watch-visible crash despite ample commit headroom. Passive monitor captured first/unhandled AV at NVIDIA nvoglv32+0xBC3953, on the render callback through DxvkVulkan::Bridge::Copy (command recording near line128) and OpenXR OnEndScene. This supersedes memory exhaustion as a sufficient explanation for this reproduction. Local DXVK3.1.1 source confirms GetVulkanImageInfo returns the current relocatable DxvkImage handle without pinning it; bridge previously cached handles at initial publication. Added per-copy flush/current-handle resolution as a focused interop experiment; runtime outcome pending. It is not yet proven to explain the driver fault.

- Interop-refresh Release SHA25635D24461C2834F666FB221F0B79616201BD1372D9E4C9C5F8EC8F8E9957E8632 built Win32 Release with x64 compiler host (existing libmodupdater PDB warnings only). Installed INI unchanged. PID16708 has3264 radar copies/5292 watch eye draws, zero reported HUD failures and no monitor exceptions at this checkpoint. Fresh native window image shows a readable10cm radar on top of the left forearm, with health ring and native markers. These are initial positive runtime observations; movement/jitter/underside acceptance and longer stability remain pending. No claim that handle relocation alone caused the driver fault yet.

- Focused180-second monitor window completed without captured AVs; PID16708 remained responsive with12520 radar copies/7568 watch draws and zero reported HUD failures. Explicit debugger detach returned access denied(error5); monitor exited with kill-on-exit disabled and bridge/game responsiveness was verified afterward. This limits the debugger lifecycle evidence; no clean-detach claim. Headset placement/underside/jitter feedback remains pending; game left running.

- User confirms watch works after the interop correction, but its texture is mirrored. Corrected only horizontal UV orientation (u ->1-u) on the -Y mount face; geometry/mount/pose and installed INI unchanged. Win32 Release built successfully; clean bridge quit verified before backup/deployment. Installed SHA256B11BF8B3A113715DFEE2C8EF741E8067094DB62BBB1A5CA7A104A7225F216D8B; final unmirrored headset acceptance pending.

- User accepts the unmirrored watch on2026-10-06. New scope: native wanted count above radar, and an early world-space loading panel reusable for pause-menu presentation. Other HUD remains deferred.

- Stars/loading Release SHA25654673E309B05E291118358A58531F46097676EA001130247A1D9A0B60017AC7B: Win32 Release/x64 compiler host succeeded (existing libmodupdater missing-PDB warnings). Installed ASI/INI backed up, normal bridge exit verified, installed INI preserved. CE1.2.0.59 / SteamVR2.17.10 / DXVK-Vulkan PID19136 reported panelActive=true/panelFrames=2 while eyeImages=false/stereo.ready=false, establishing early screen submission before world targets. Later panelFrames=555 with panelActive=false and3458 stereo layers establishes transition back to normal gameplay without runtime failure. hud-status samples wanted=0, watch draw failures=0. Nonzero wanted-star appearance and headset loading/pause presentation still await visual acceptance.

- Dashboard follow-up: user reports stable panel but continuously spinning map contents; requests1m right shift. Default right offset now1.15m (range +/-2m). Traced native radar angle store005DBE33 in005DBC90, map/player blip consumption in005DC800, camera yaw helperC3E430. Signature-guarded car-only focused-VR correction writes simulation-sampled car yaw to XMM0 before native angle store; on-foot/pause behavior retained. Ghidra bookmark Issue020CarRadarHeading saved/read back; visual acceptance pending.

- Car-heading Release SHA256 `A0B2EDD63FC487F2D6F565F3C93AC6300E32E65A13D4199E64A8E2A06F1BB6B5` built Win32/x64 compiler host successfully (existing libmodupdater PDB warnings only). Restricted builds failed C1902, including a clean-intermediate retry; running MSBuild outside the command sandbox succeeded. Installed ASI/INI backed up in ignored `build/issue020-before-car-heading-20261006-143144`; clean bridge quit/process exit verified before deployment, INI unchanged. `hud-status` now reports heading-hook readiness, override count and native/car angles in radians. Runtime/headset acceptance pending.

- User requests0.6m left correction after the1m right shift. Source/distributed default now DashboardRadarRight=0.55m. Added ViewHud=1 drawing-only capture for native text plus HUD sprite/window commands, composed centrally into both eyes; loading/pause and wrist/dashboard paths retained. No HUD record/fade update or command-stream reader is replayed. Ghidra00923200 bookmark saved/read back. Win32 Release/x64 compiler host built successfully outside sandbox (existing libmodupdater PDB warnings). Deployed SHA256E8035DFE5FE5B4632ECE529520B9E225936E1CFBA8CE5C4DDB6A11084F2E03E2, installed INI unchanged; backup build/issue020-before-view-hud-20261006-144606. Bridge quit/process exit verified. Runtime/visual acceptance pending.

- Runtime PID29508 reached focused continuous DXVK-Vulkan stereo:1754 stereo layers, zero command-buffer overflows, no OpenXR failure. All three text/sprite/window signatures resolved;2887 resolved-draw captures and2178 text eye draws, zero HUD failures at checkpoint. Fresh native window capture shows the live apartment scene without a full-frame black overlay. No subtitle/help message was visible in this particular view, so capture counters do not prove text readability; user check requested. Car heading override was observed5362 times in the preceding runtime, establishing hook execution but not correct visual rotation. CE1.2.0.59 / SteamVR2.17.10 / Vive Wands; TLAD/TBoGT not tested. Issue stays open.

- User reports general HUD too peripheral and asks unread-SMS envelope on wrist radar; also requests another0.1m left car shift. Added ViewHudWidth=0.44 centred, aspect-preserving view layout; DashboardRadarRight default0.45m. Native HUD_TEXT_MESSAGE_ICON texture slot is resolved by unique initialization signature and matched in resolved sprite payloads: separate transparent icon capture, omitted from view HUD and displayed below wrist/car radar. Native alpha/flashing retained, expired captures hidden after250ms. Ghidra #020 SMS bookmark saved/read back. Build/runtime/headset acceptance pending.

- Centered HUD / SMS Release SHA2568A7B1AF086EF129D89A6997FA89FEB7049058BCEEBB6629AE2520A7F3278AF7D built Win32/x64 compiler host (existing libmodupdater PDB warnings only). Backup build/issue020-before-centered-hud-20261006-145517; installed INI unchanged (no overrides for new placement keys). Normal bridge quit completed before deployment; startup took longer but GTAIV PID20212 appeared responsive. All HUD hooks and SMS texture-slot signature resolved; hudWidth0.44 read back. CE1.2.0.59/SteamVR2.17.10/DXVK-Vulkan focused early loading panel active. Gameplay and headset readability/SMS acceptance pending.

- Gameplay checkpoint PID20212:2347 radar captures/1172 radar eye draws,2897 native HUD draws/2186 overlay eye draws, zero HUD failures. Fresh game image shows the native phone-profile help text and cash label drawn well inward from the screen edges over the live scene. SMS slot resolves but smsCaptures=0 in the loaded save at this checkpoint; unread-SMS visual/expiry acceptance still required when an SMS indicator is active. Headset readability/car placement still need user feedback. No flat-screen code-path change was made to the original draw (executed once); broader flat-screen check remains an issue closure gate.

- User reports doubled unreadable HUD despite visible monocular screenshots. Corrected the underlying identical-pixel eye drawing: shared midpoint-head-relative plane projected through each completed eye camera/FOV. Default2m distance,1.28m-wide plane (ViewHudWidth0.32 half-width/distance), native aspect retained; further centering requested. No scene occlusion; pause/loading unchanged. Focused asymmetric stereo ray/plane convergence test passed (x86). Win32 Release/x64 compiler host succeeded (existing libmodupdater PDB warnings). Deployed SHA256829226CAFCD86BD54974190AA300431BE738A45D66D5BCF51BEA641106565D0C after verified bridge quit/process exit; backup build/issue020-before-stereo-hud-20261006-150129, installed INI preserved. CE1.2.0.59/SteamVR2.17.10/DXVK-Vulkan. Live/headset acceptance pending; prior monocular checks do not establish binocular comfort/readability.

- Live corrected runtime PID25936 reached gameplay responsive:1098 radar captures,2178 HUD eye draws, zero HUD failures. Fresh native image shows help text/cash over the live scene with the narrower projected placement. Binocular fusion and readable font size cannot be accepted from a single-eye desktop image; user headset check remains pending. SMS was not active in this save (zero SMS captures).

- User confirms the shared stereo plane fixes HUD fusion, but layout/font is too small. Expand ViewHudWidth default0.32 ->0.64: twice the physical width/height (2.56m wide at2m distance), keeping the accepted shared-plane projection and native aspect. Depth, radar placement, capture and timing unchanged. Larger-size visual acceptance pending.

- Expanded HUD Release SHA2566499D57BD8709DD9EA3F89E674A3F33080F1DEA4FF81420F82D0FF1126506F13 built Win32/x64 compiler host (existing libmodupdater PDB warnings only). Backup build/issue020-before-expanded-hud-20261006-150546; bridge quit/process exit verified, installed INI preserved. Source/distributed default0.64; game has no explicit override. Binocular projection math unchanged; no redundant math/baseline run. Runtime and expanded-size visual acceptance pending.
- Expanded build runtime PID27040 responsive; hud-status confirms hudWidth0.64 and resolved hooks, zero HUD failures at initial gameplay transition. Final size/readability acceptance remains with the user.

## Closure — 2026-10-06

User accepts expanded binocular HUD (fantastic) and requests closure/commit. Earlier wrist/wanted/loading/menu acceptance is recorded above. Final VR-disabled Release PID14976 was responsive, OpenXR disabled and zero layers; fresh native image confirmed ordinary third-person street scene, help text, radar/health ring, SMS envelope, wanted stars and pistol/ammo UI. User separately confirms flat screen still works fine. This is launch/composition evidence, not an entire mission route. Installed Release SHA2566499D57BD8709DD9EA3F89E674A3F33080F1DEA4FF81420F82D0FF1126506F13; CE1.2.0.59 / SteamVR2.17.10 / Vive Wands / DXVK-Vulkan. Exact original INI restored after flat-screen check (backup/installed SHA256DA1C2D14F3F2BB6130B30BD46A314DE11AE97BB4A57BC23E3DC623DE54ED446C).

Core milestone complete. Broader checks transferred to #031; #007/#015/#019/#022/#030 stay open for their own scope. User distributed INI ViewHudWidth0.5 edit preserved; deployed missing-key fallback remains0.64. No all-episode or complete-mission compatibility claim.
- After final flat-screen check, original INI restoration hash matched; VR relaunch PID7148 reached FOCUSED DXVK-Vulkan continuous stereo (386 layers, zero command-buffer overflows). Game left running on accepted build; no source rebuild or calibration changes during closure.
