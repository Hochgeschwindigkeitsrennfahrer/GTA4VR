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
- [004 — Select an OpenXR graphics bridge](open/004-select-openxr-bridge.md)
- [005 — Add opt-in OpenXR initialization](open/005-openxr-initialization.md)
- [006 — Route two eye views with safe visibility and targets](open/006-eye-visibility-and-targets.md)
- [007 — Attribute UI and post-processing draw order](open/007-ui-composition-order.md)
- [008 — Persist reverse-engineering findings in Ghidra (closed)](closed/008-ghidra-project-workflow.md)
