# Local game command bridge

Set `[DEVELOPMENT] EnableCommandBridge=1` in the installed FusionFix INI for development tests. The distributed default is `0`. A running game exposes `\\.\pipe\GTA4VR.Dev.<process-id>`. The pipe rejects remote clients and grants access only to the current Windows user and local System. No TCP port, arbitrary script, memory-write command, or game-input command is exposed.

From the repository's PowerShell prompt:

```powershell
.\tools\game-command.ps1 status
.\tools\game-command.ps1 quit
# Specify the process if discovery is ambiguous:
.\tools\game-command.ps1 status -GameProcessId 12345
```

`status` reports protocol version, game PID, and OpenXR enabled/running/failure/session/frame state. `quit` releases OpenXR on the EndScene callback thread and posts `WM_CLOSE` to the game-owned window, requesting its usual close path. The response acknowledges command acceptance; confirm actual exit from the process list. Neither command saves the game or modifies player saves.

If a restricted command sandbox denies pipe access, run the same client in the normal Windows user context through the approved tool path. Keep the pipe ACL restricted; do not broaden it to work around the sandbox.

## Protocol and ownership

The server uses one overlapped message-mode pipe and bounded buffers, polled without blocking from EndScene. Commands are UTF-8 messages of at most 128 bytes. The response is one JSON message of at most 512 bytes. The client reads the response, sends the three-byte message `ack`, and disconnects. The server executes the quit side effect only after the acknowledgement; a stalled connection is canceled after five seconds. Unknown commands return an error. A paused game can respond as long as it continues rendering; startup/loading or a frozen render thread can make the client time out.

The bridge and OpenXR do not create a control worker thread. Explicit OpenXR shutdown occurs after the current zero-layer frame has completed, before the normal window-close request. DLL detach only closes IPC/log handles; it does not call OpenXR or unload its loader under the Windows loader lock.

Track live test evidence in [issue 009](../issues/closed/009-local-development-command-bridge.md) and [issue 005](../issues/closed/005-openxr-initialization.md). Compiled binaries, installed configuration, and runtime logs remain outside Git.
