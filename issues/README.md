# Local issue workflow

Issues are Markdown files tracked in Git. `open/` contains active or planned work; `closed/` contains completed or deliberately canceled work. The filename is the stable issue ID and does not change when the file moves.

## Create and work an issue

1. Pick the next unused number and name the file `NNN-short-description.md` in `open/`. Keep one independently verifiable outcome per issue. Link prerequisites with their issue IDs.
2. Use the sections **Goal**, **Tasks**, **Done when**, and **Notes / evidence**. Add context or links only when useful. The checklist is a work plan; the **Done when** section is the acceptance gate.
3. As work proceeds, check off completed tasks and record commands, build results, game/runtime versions, decisions, and reproducible observations under **Notes / evidence**. Link code changes or commits when available. Do not paste large logs or private machine data.
4. When every acceptance condition is met, move the same file to `closed/` and add a short completion note with date and evidence. If closing as canceled or superseded, state why and link the replacement. Do not mark unverified work complete.
5. If a task grows, create a new numbered issue and link it from the original. Update `ROADMAP.md` when sequencing or milestone exit criteria change.

Use `rg --files issues/open` to list pending work and `rg -n "pattern" issues` to search both states. The directory is the status; avoid duplicate status fields that can drift. Existing GitHub issue templates are separate from this local workflow.

## Initial queue

- [001 — Reproduce the x86 build (closed)](closed/001-reproduce-x86-build.md)
- [002 — Record the flat-screen game baseline (closed)](closed/002-flat-screen-baseline.md)
- [003 — Map camera and rendering hooks (closed)](closed/003-map-rendering-hooks.md)
- [004 — Select an OpenXR graphics bridge (closed)](closed/004-select-openxr-bridge.md)
- [005 — Add opt-in OpenXR initialization (closed)](closed/005-openxr-initialization.md)
- [006 — Route two eye views with safe visibility and targets (closed)](closed/006-eye-visibility-and-targets.md)
- [007 — Attribute UI and post-processing draw order (superseded)](closed/007-ui-composition-order.md)
- [008 — Persist reverse-engineering findings in Ghidra (closed)](closed/008-ghidra-project-workflow.md)
- [009 — Local development command bridge (closed)](closed/009-local-development-command-bridge.md)
- [010 — Traffic-light lamps missing in the right eye](open/010-traffic-light-right-eye.md). Isolated follow-up; the user does not require it to block #006.
- [011 — Validate camera scale, pose timing and auxiliary views (superseded)](closed/011-camera-scale-and-auxiliary-views.md). Outdated broad validation; remaining defects have individual issues.
- [012 — Diagnose intermittent early startup exits (closed, external)](closed/012-intermittent-early-startup-exit.md). Rockstar Launcher/Windows issues, per user.
- [013 — Profile and improve the native-refresh frame budget](open/013-profile-native-refresh-frame-budget.md). Explain the remaining scene-dependent submission drops.
- [014 — First-person headset camera and room-scale movement (closed)](closed/014-first-person-headset-camera.md)
- [015 — OpenXR controller support with Vive Wands (closed)](closed/015-vive-wand-controller-support.md)
- [016 — 6DOF hands, firearm aiming and shooting](closed/016-six-dof-hands-and-firearms.md)
- [017 — 6DOF hand and melee-weapon combat (closed)](closed/017-six-dof-melee-combat.md)
- [018 — Grab and manipulate physics objects with 6DOF hands (closed)](closed/018-six-dof-physics-object-grabbing.md)
- [019 — VR vehicle interaction and driving](open/019-vr-vehicles.md)
- [020 — VR minimap, subtitles and gameplay overlays (closed)](closed/020-vr-overlays-and-subtitles.md)
- [021 — VR weapon wheel (canceled)](closed/021-vr-weapon-wheel.md)
- [022 — Physical interactive in-world phone (closed)](closed/022-physical-interactive-phone.md)
- [023 — Fix world objective markers visible only in the left eye](open/023-objective-markers-both-eyes.md)
- [024 — Firearm coverage and physical weapon reloading (closed)](closed/024-physical-weapon-reloading.md)
- [025 — Physics-based money and weapon drops](open/025-physical-money-and-weapon-drops.md)
- [026 — Remove cinematic depth and focus blur in VR (closed)](closed/026-disable-cinematic-depth-blur.md)
- [027 — Fix stereo water shimmer and reflections](open/027-stereo-water-shading.md)
- [028 — Independent third-person desktop view with VR-only character visibility](open/028-independent-desktop-view.md)
- [029 — Diagnose crashes during first-person free roam (closed)](closed/029-diagnose-free-roam-crashes.md)

- [030 — Pause-menu VR settings and player-height calibration](open/030-pause-menu-vr-settings-and-height-calibration.md)

- [031 — Expand VR UI coverage and validate lifecycle](open/031-ui-coverage-and-lifecycle.md)

- [032 — Attribute VR physical interactions to the player](open/032-vr-interaction-player-attribution.md)
- [033 — Melee feedback and compatibility follow-up](open/033-melee-feedback-and-compatibility.md)
- [034 — Correct spatial audio direction in VR (closed)](closed/034-vr-spatial-audio-listener.md)
- [035 — Locomotion and tracked-hand stability (closed)](closed/035-locomotion-and-tracked-hand-stability.md)
