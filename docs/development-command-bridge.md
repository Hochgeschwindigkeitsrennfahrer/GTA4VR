# Local game command bridge

Set `[DEVELOPMENT] EnableCommandBridge=1` in the installed FusionFix INI for development tests. The distributed default is `0`. A running game exposes `\\.\pipe\GTA4VR.Dev.<process-id>`. The pipe rejects remote clients and grants access only to the current Windows user and local System. No TCP port, arbitrary script, memory-write command, or game-input command is exposed.

From the repository's PowerShell prompt:

```powershell
.\tools\game-command.ps1 status
.\tools\game-command.ps1 quit
.\tools\game-command.ps1 stereo-test
# Specify the process if discovery is ambiguous:
.\tools\game-command.ps1 status -GameProcessId 12345
```

`status` reports protocol version, game PID, and OpenXR enabled/running/failure/session/frame state. `quit` releases OpenXR on the EndScene callback thread and posts `WM_CLOSE` to the game-owned window, requesting its usual close path. The response acknowledges command acceptance; confirm actual exit from the process list. Neither command saves the game or modifies player saves.

Issue 006 adds stereo readiness, built-frame/budget, continuous mode and submitted-layer counts to `status`. Set `[VR] ExperimentalStereoFrames=0` for continuous development rendering; this is the user's preferred mode for headset checks. Values 1–600 retain the bounded diagnostic mode and hold the last pair afterward. `stereo-test` resets the counter and requests a fresh capture on the next game-process callback when resources and valid OpenXR views are ready. It does not repeat simulation, launch another process, save or inject input. Check status/logs and actual images; command acceptance alone is not rendering evidence.

`openxr.displayRefreshHz` is the current hardware rate returned by the optional OpenXR display-refresh extension (zero if unavailable). `openxr.submittedHz` is a five-second projection-layer submission average; it does not count compositor-generated frames or prove the headset displayed every submitted frame. `[VR] MatchHeadsetRefresh=1` skips the ordinary gameplay limiter while OpenXR requests rendered eye images, letting `xrWaitFrame` supply timing. Loading/cutscene/minigame safety limits still apply, and ordinary gameplay caps resume when VR stops or fails. This pacing works without the optional refresh-rate query.

If a restricted command sandbox denies pipe access, run the same client in the normal Windows user context through the approved tool path. Keep the pipe ACL restricted; do not broaden it to work around the sandbox.

## Protocol and ownership

The server uses one overlapped message-mode pipe and bounded buffers, polled without blocking from EndScene. Commands are UTF-8 messages of at most 128 bytes. The response is one JSON message of at most 512 bytes. The client reads the response, sends the three-byte message `ack`, and disconnects. The server executes the quit side effect only after the acknowledgement; a stalled connection is canceled after five seconds. Unknown commands return an error. A paused game can respond as long as it continues rendering; startup/loading or a frozen render thread can make the client time out.

The bridge and OpenXR do not create a control worker thread. Explicit OpenXR shutdown occurs after the current zero-layer frame has completed, before the normal window-close request. DLL detach only closes IPC/log handles; it does not call OpenXR or unload its loader under the Windows loader lock.

Track live test evidence in [issue 009](../issues/closed/009-local-development-command-bridge.md) and [issue 005](../issues/closed/005-openxr-initialization.md). Compiled binaries, installed configuration, and runtime logs remain outside Git.
