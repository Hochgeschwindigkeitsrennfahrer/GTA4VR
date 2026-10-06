# 022 — Physical interactive in-world phone

## Goal

Reuse the native held phone and working screen, exclude its duplicate VR HUD capture, and operate it with semantic Vive Wand controls. Depends on #015/#016/#020; phone attribution uses the findings preserved in closed #007. Roadmap stage 6. Broader mission/episode/lifecycle validation is tracked in #031.

## Tasks

- [x] Keep the native held phone and working screen; exclude PHONE_SCREEN rendering from the VR HUD plane without suppressing ordinary HUD/subtitles.
- [x] Add semantic phone actions: hold left Menu to open, right trackpad click for four-way navigation/center accept, right Menu for back. Preserve short left Menu pause and suppress conflicting right-hand gameplay actions while the phone is active.
- [x] Map native screen targets and both keyboard/gamepad input paths; retain native summon/stow, hand attachment and phone scripts.
- [x] Make phone navigation work after switching between mouse and physical gamepad input.
- Broader calls/messages/mission/episode and flat-screen/fallback route coverage transferred to [#031](../open/031-ui-coverage-and-lifecycle.md). Independent grabbing/dropping is outside this native-phone milestone.

## Done when

The native held phone remains readable, its screen is excluded from duplicate HUD capture, and Wand open/navigation/accept/back controls work independently of keyboard/gamepad device mode. Broader mission progression and lifecycle coverage remains in #031.

## Notes / evidence

### Closed 2026-10-06 — user accepted

- User approved closing and committing after the gamepad-mode correction. This closes the native-phone presentation/control milestone; historical pending statements below describe earlier iterations.
- Final x86 Release build passed with existing updater LNK4099 warnings. Focused phone edge/pulse checks passed. CE1.2.0.59, SteamVR2.17.10, Vive Wands, DXVK-Vulkan; final deployed ASI SHA2561237E56A5524701ED14B67450DF8BB6CFF71817B183A5F45B72A1B788506927C. Both raw gamepad query hooks were observed receiving Wand input in the running game.
- No comprehensive calls/messages/mission progression, TLAD/TBoGT or post-change flat-screen route is claimed; those checks are retained in #031. Native phone scripts and original rendering remain in use.

- 2026-10-06: remaining phone model/screen draw order and camera/target ownership investigation transferred here when #007 was closed as superseded. Include that evidence in the native phone mapping before implementing physical presentation.

- User wants the phone to be a physical in-world object. Existing PhoneModel/Script2d observations in #003/#007 are starting evidence, not a complete interaction mapping.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.

- 2026-10-06: unread-SMS envelope is routed to wrist/car radar in #020, with active/expiry coverage in #031. This is notification presentation only; phone model/screen interaction remains unimplemented here.

### 2026-10-06 — native phone first pass

User reports the existing native phone is already held with working UI. Reuse that implementation rather than replace it; the immediate scope is duplicate HUD capture and Wand controls. Shared PHONE_SCREEN target is created/cached in comvars.ixx; native display state is CPhoneMgr::bDisplayMobile. Phone actions must preserve ringing and script behavior through normal control input.

- First pass excludes HUD capture when current RT0 is native cached PHONE_SCREEN, and excludes final sprite payload referencing that target. Original draws still execute once, preserving native screen content and desktop phone. Does not hide all HUD while the phone is active.
- Added Phone semantic OpenXR action set: direction/click/back on right Wand, active only while CPhoneMgr::bDisplayMobile is true outside pause. System exposes Pause (tap) and Open phone (hold), both default left Menu. Holding500ms injects native TAKE_OUT21 briefly; short release injects pause76. Phone uses native frontend65/64/66/67/77/78 and keyboard130/131/132/133/181/182 controls (IV-SDK ePadControls), retaining normal scripts/calls/menus. Right-hand gameplay selection, jump, enter/exit, firing and accelerator are suppressed while phone owns its buttons; left movement/steering remains available. Focus loss clears hold/release state.
- Control reference: https://raw.githubusercontent.com/Zolika1351/iv-sdk/master/include/ePadControls.h . Win32 Release passed, existing updater LNK4099 warnings only. Native phone duplicate-plane and Wand route acceptance pending; existing physical phone model appearance is user observation, not a newly implemented screen.

- User asks about runtime press/hold distinction: OpenXR boolean state carries currentState/change timing; SteamVR may map complex button modes into separate actions. Corrected handling so runtime-separated PhoneOpen executes immediately (no additional500ms delay), and independent Pause executes immediately. Shared default click still needs fallback tap/hold arbitration because xrSuggestInteractionProfileBindings suggests input components, not long-press mode metadata.

- Deployed revised Release SHA256 CF7BD655B01A873C76A93B13FBD2CE84FFAEDFF067088AA2534B4BFF51A0320F; prior accepted ASI/INI preserved under ignored build/issue022-phone timestamp folder. First phone build PID11280 reached FOCUSED continuous DXVK-Vulkan stereo with zero command-buffer overflows. Revised launch had an intermittent early exit and was retried. No installed INI or save changes. User phone screen/navigation acceptance pending.

### Directional rebinding and HUD correction follow-up

- User reports the first pass still duplicates the phone UI on the view HUD and cannot advance to the keypad. Acceptance remains open; the earlier surface match is insufficient.
- Added boolean Phone up / keypad, Phone down, Phone left, Phone right and Phone accept actions (no physical defaults). These can be assigned to SteamVR trackpad D-pad North/South/West/East/Center outputs. The existing direction+click default remains available. Phone Up also injects native TAKE_OUT21 while raised; keypad behavior awaits user confirmation.
- Release build F554C89DC324AE05F3C156E874022C045E9D4345155CB33EB4C71F5A473C3BD1 deployed with backup under ignored build/issue022-dpad timestamp directory. PID30164 reached continuous DXVK-Vulkan stereo. Added phone fields to input-status and bounded per-draw-kind target diagnostics to hud-status. Phone target records remain empty while session is VISIBLE and phone inactive; no phone attribution claim from those samples.
- Ghidra CE1.2.0.59: GET_MOBILE_PHONE_RENDER_ID native5E72C0 returns018B6EF4, initialized by5BF730 with PHONE_SCREEN. SET_TEXT_RENDER_ID handlerB8DA10 -> B91E60 sets011DB234. Text builderB8E760 records that ID in011EE5BC; sprite builderBD5490 records it in01037678. Script UI dispatcher94AD40 selects text records by target ID and queues94B130 with ID for sprite processing. These findings were previewed, bookmarked atB8DA10 as Issue022PhoneRenderId, read back and saved in GTAIV.exe project. Newly discovered native wrappers/builders were defined after preview. Inline script execution is disabled; analysis used MCP decompilation plus read-only executable reference searches.
- A further HUD guard rejects offscreen UI targets whose dimensions differ from the desktop backbuffer, covering phone targets unavailable through the factory cache. Original phone drawing is retained. Runtime phone/subtitle acceptance remains pending.
- Combined offscreen-target guard build D59C20222A9B66B404151FB1884B30CFCD5E745988B8791E4EF3179E95E370D4 passed Win32 Release (existing updater LNK4099 warnings). Prior plugin backed up; INI untouched. Native quit completed a few seconds after acknowledgement; deployment waited for actual process exit. First relaunch exited early, retry started. Phone visual acceptance remains pending.
- Follow-up: launcher initially showed only its splash; Steam retry/delayed initialization reached PID28040, FOCUSED, DXVK-Vulkan, continuous stereo, zero command-buffer overflows. Fresh game window capture shows apartment scene and tracked arms. The initial absence of GTAIV.exe did not itself prove a crash; no fault was captured. Phone/keypad and custom D-pad acceptance still pending.

### Intermittent phone input delivery

- User reports configured semantic D-pad inputs sometimes work and mostly do not. A45s live input-status capture recorded Phone active continuously during a navigation interval and detected Down/Back presses; the follow-up user check worked. User clarified that the successful check was not deliberately slower, so this does not establish press duration as the cause. No proven root-cause claim.
- Corrected a concrete race in the existing bridge: render-thread samples overwrote the only button-state snapshot, so a press/release between simulation input updates could be lost. Publish monotonic rising-edge counts for the six phone buttons alongside their latest state. Simulation consumes bounded pending pulses, holds each for at least120ms and inserts50ms of release between presses; a physically held input remains held. Phone close, pause and stale/focus loss cancel pending actions. Counters remain monotonic across session state clears to avoid replaying old presses on reopening.
- Added input-status.phonePresses and locomotion-status.phoneDelivered counters for received/native-delivered edges. These distinguish binding/sample failure from input-bridge failure on the next repro without a timed manual capture.
- Focused x86 deterministic checks cover a complete tap between simulation updates, two quick taps with a distinct release, held input and cancellation without stale replay. Passed. Runtime acceptance pending; UI/calls/message coverage remains open.
- HUD diagnostics from the phone run recorded font RT0 at512x512 with phone cache match0, corroborating why the original pointer-only guard failed and why the offscreen-dimension guard is needed. Sprite diagnostic last target was2560x1440. This identifies offscreen font content, not complete phone UI visual acceptance.
- Release4276CF215865084980EA0F3C8973928A86E624BD6C502687B01E68369D28934F deployed after verified bridge-driven exit. Prior ASI/INI backed up under ignored build/issue022-phone-latch timestamp folder; installed INI and saves untouched. Build passed with existing updater LNK4099 warnings. Runtime phone delivery acceptance remains pending.
- Live latch build PID2864 settled into FOCUSED continuous DXVK-Vulkan stereo; native input hook installed and updating. Snapshot received phone edge counts[24,12,0,1,4,0] matched native pulse-delivery counts exactly (up/down/left/right/accept/back). This proves delivery to pad injection for that interval, not that every phone script consumed the press. Awaiting user reliability result. Raw input and delivery snapshots retained under ignored build/issue022-phone-latch files.

### Keyboard/gamepad device-mode follow-up

- User supplied a repeatable repro: mouse input makes Wand phone navigation work, physical gamepad movement makes it stop. This is a stronger lead than timing; the earlier successful check was not deliberately slower.
- CE1.2.0.59 live disassembly (PID2864, module base00250000; Ghidra base00400000): IS_BUTTON_PRESSED handlerB98470 -> B98B60 -> B99220; IS_BUTTON_JUST_PRESSED handlerB98450 -> B98B20 -> B992F0. Both read raw gamepad state viaB991F0, separate from the semantic CPad values we injected. Selector010330F8 indexes0118D470 with strideBC; D-pad helpers compare current+3C/40/44/48 to previous+8C/90/94/98. This explains how semantic delivery can succeed while gamepad-mode queries see no Wand press.
- Added unique-signature hooks atB99220/B992F0 that OR native results with the existing simulation phone pulse/edge for buttons8/9/10/11/16/17 (D-pad, A/B; reference https://wiki.gtaconnected.com/Defines/IV). Limited to local pad0/1, visible phone, focused fresh VR input and no pause. No raw state writes or global device-mode forcing. Other buttons and original native results are retained. Added phoneRawInstalled/Queries/Matches diagnostics.
- Findings previewed, bookmarked atB99220 as Issue022PhoneRawButtons, read back and saved in the Ghidra project. Both signatures matched exactly once in the live executable. Release build passed (existing updater LNK4099 warnings); deployed1237E56A5524701ED14B67450DF8BB6CFF71817B183A5F45B72A1B788506927C after verified bridge-driven exit and ASI/INI backup under ignored build/issue022-phone-gamepad timestamp folder. INI and saves unchanged. Relaunch and device-switch acceptance pending.
- Relaunch PID17704 reached FOCUSED continuous SteamVR2.17.10 DXVK-Vulkan stereo. Both raw query hooks installed; live phone snapshot recorded996 eligible raw queries and176 supplied Wand results, with received native pulse delivery active. This confirms the missing gamepad query path is being exercised in gameplay, not just a theoretical code path. User device-switch reliability acceptance remains pending.
