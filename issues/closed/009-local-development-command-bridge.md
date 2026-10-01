# 009 — Local development command bridge

## Goal

Control development tests through an opt-in local IPC connection, including a clean `quit` command, without repeated manual launch/quit actions or UI automation. Supports #005.

## Tasks

- [x] Add a disabled-by-default named pipe restricted to the current user/local System, with bounded commands and no arbitrary code execution.
- [x] Provide `status` and `quit`; process game/OpenXR operations on the existing EndScene callback thread.
- [x] Release OpenXR before requesting the game's normal window close path; confirm the process exits and can relaunch.
- [x] Add a repository client script and document protocol, configuration, and limitations.

## Done when

The client can query the live game and request quit, OpenXR teardown succeeds before quit, the game process exits, and a subsequent launch succeeds. The bridge is off by default and rejects unsupported commands.

## Notes / evidence

- Requested by the user on 2026-10-01 while closing out #005. A direct temporary `ExitProcess` test previously left an exit-state process requiring reboot; this bridge will request `WM_CLOSE` after explicit OpenXR cleanup.
- Implemented in [`source/devcontrol.ixx`](../../source/devcontrol.ixx); [`tools/game-command.ps1`](../../tools/game-command.ps1) is the client. [Protocol documentation](../../docs/development-command-bridge.md) covers ACL, bounded messages, acknowledgement, and timeout behavior.
- x86 Debug and Release ASIs built successfully. Live Debug `status` returned protocol 1, the game PID, and running OpenXR state. An unsupported command returned an error. A connected client that sent no command was canceled after five seconds; a subsequent status request succeeded.
- Live `quit` acknowledged acceptance, then logged successful `xrDestroySession` and `xrDestroyInstance` plus explicit shutdown. GTAIV.exe exited within the eight-second observation window. Subsequent missing-runtime and VR-off launches succeeded; the missing-runtime game also exited through RPC. No UI automation, forced termination, or save modification was used for these RPC exits.
- Closed 2026-10-01. All acceptance conditions passed in the live x86 game. The distributed bridge default is off; local development testing enables it explicitly.
