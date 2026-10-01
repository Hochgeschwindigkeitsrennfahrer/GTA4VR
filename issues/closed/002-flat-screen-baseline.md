# 002 — Record the flat-screen game baseline

## Goal

Know what works before installing or testing a VR build. Roadmap stage: 0. Depends on #001 for testing a locally built ASI.

## Tasks

- [x] Record the installed Complete Edition executable version and whether FusionFix is already installed in the game's `plugins` folder.
- [x] Launch GTA IV flat-screen without VR changes; record startup behavior. The free-roam route could not run because vanilla showed a blank rectangle.
- [x] After #001, deploy the local FusionFix build and repeat the route. No installed ASI existed to back up.
- [x] Record a small GTA IV / TLAD / TBoGT smoke-test matrix and pre-existing defects.

## Done when

A flat-screen launch and free-roam result for the local build is recorded with exact versions, settings, install path, and any observed failures. The episode matrix may mark untested cases explicitly.

## Notes / evidence

- 2026-10-01 install: `C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto IV\GTAIV\GTAIV.exe`, file/product version `1.2.0.59`. Steam app `12210` reports Complete Edition, installed build ID `14009960`. Both `TLAD` and `TBoGT` content directories exist.
- The game directory was unmodded when inspected: no `plugins` directory, `dinput8.dll`, `vulkan.dll`, `d3d9.dll`, `commandline.txt`, or FusionFix ASI/config. There is no existing ASI to back up. The locally built ASI is in this repository's ignored `bin/` and has not been deployed.
- Steam launch requests (`-applaunch 12210` and `steam://rungameid/12210`) did not produce a `GTAIV.exe` process. Starting `PlayGTAIV.exe` launched Rockstar `Launcher.exe` and left `PlayGTAIV.exe` running; no `GTAIV.exe` process or game window was observable from this session. The user saw an "already running" launcher message after the duplicate attempts. The exact test-created `PlayGTAIV.exe` and Rockstar Launcher processes were closed; no game process remained. Awaiting a single launch by the user from Steam and a report on the result. No game files or saves have been changed.
- Graphics settings and the on-foot route remain unverified for vanilla because startup did not reach the menu.
- The user completed Rockstar login verification and launched vanilla GTA IV through Steam. `GTAIV.exe` started at 11:37:47 local time and loaded the system `C:\Windows\System32\d3d9.dll` and `DINPUT8.dll`; no game-local graphics/ASI DLL was present. The user reported only a frozen blank rectangle, so the menu and route were not reached. The process was stopped after the failure; this is a pre-existing vanilla startup defect, not evidence about the local FusionFix build.
- For the modded comparison, downloaded the official [FusionFix v5.1.0 release ZIP](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases/tag/v5.1.0) (SHA-256 `9B4195CD87FA1E29450590CC37711CFCF64D4163A3841315EC7FB723D73D92A4`). The archive contained 156 files and had zero collisions with existing game files. Extracted it to the game directory, then replaced `plugins/GTAIV.EFLC.FusionFix.asi` with the locally built Win32 Release ASI. The installed ASI SHA-256 matches `bin/GTAIV.EFLC.FusionFix.asi`. Awaiting the user's modded launch result.
- With FusionFix installed, GTA IV launched and loaded the game-local `d3d9.dll`, `DINPUT8.dll`, and locally built `plugins/GTAIV.EFLC.FusionFix.asi`; FusionFix generated a config with `GraphicsAPI = 0` (D3D9) and `Windowed = 0`. The user reported the game started but froze when alt-tabbing before reaching gameplay. The process was stopped. The generated config was copied to `GTAIV.EFLC.FusionFix.cfg.baseline`, then changed to `Windowed = 1` and `BorderlessWindowed = 1` for the next test. No save files were modified by this work; the user reports their prior Steam save was corrupted and is starting a new game.
- On the borderless rerun, the user reported gameplay worked and they progressed far enough to save, providing a repeatable free-roam starting point. A GTA IV save file was updated at 11:48:49 local time by the game. The current FusionFix config remains `GraphicsAPI = 0`, `Windowed = 1`, `BorderlessWindowed = 1`.
- The user confirmed walking, driving, pause/resume, and alt-tab away/back all worked well in the borderless rerun, without reported visual problems. Display resolution and other menu settings were not captured; keep them as unknown instead of assuming defaults.

## Smoke-test matrix

| Mode | Vanilla | Local FusionFix ASI | Notes |
| --- | --- | --- | --- |
| GTA IV | Startup reached only a frozen blank rectangle | Pass: free roam, walk, drive, pause/resume, alt-tab in borderless D3D9 | Existing save was reported corrupted; the user made a new save. |
| TLAD | Untested | Untested | Installed content present. |
| TBoGT | Untested | Untested | Installed content present. |

## Completion

Closed 2026-10-01. The local Win32 Release ASI loads and the repeatable GTA IV flat-screen route passes with borderless D3D9. Fullscreen alt-tab froze during the first modded launch, so borderless mode is the working baseline. Episode behavior and exact display resolution remain to be measured in later compatibility work.
