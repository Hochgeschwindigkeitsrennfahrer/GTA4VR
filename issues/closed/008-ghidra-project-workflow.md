# 008 — Persist reverse-engineering findings in Ghidra

## Goal

Verify the Ghidra MCP can read and save annotations in the GTA IV project, then document how future analysis should update that project as findings are established.

## Tasks

- [x] Verify the connected project, open program, architecture, and image base.
- [x] Read a known render function through MCP, make and save a useful annotation, then read it back.
- [x] Add project-writing instructions to `AGENTS.md`.

## Done when

The MCP read/write/save round trip succeeds on `GTAIV.exe`, and `AGENTS.md` tells future contributors when and how to persist verified names, comments, and types while recording evidence and address mappings.

## Notes / evidence

- 2026-10-01: `list_instances` found the connected `GTAIV` project at `C:\Users\Vetle Hjelle\Projects\GTAIV\GTAIV`; `list_open_programs` found `/GTAIV.exe`, `x86:LE:32:default`, image base `0x00400000`.
- MCP decompiled `0x008f7f00`, showing render-phase virtual calls and command queue setup. A `GTA4VR issue 003` bookmark records the tested two-view list boundary and its visibility limitation. The function was renamed `BuildRenderPhaseCommandLists` after a successful dry run, saved, and read back with the new name.
- Closed 2026-10-01: the read/write/save round trip succeeded, and `AGENTS.md` now requires project annotations to be saved and verified as assembly findings are established.
