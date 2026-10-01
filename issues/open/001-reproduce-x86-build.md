# 001 — Reproduce the x86 FusionFix build

## Goal

Establish a repeatable local Debug and Release build before changing rendering code. Roadmap stage: 0.

## Tasks

- [ ] Record the current Git commit, Windows/Visual Studio toolchain, and `GTAIV.exe` version.
- [ ] Inspect `.gitmodules` and verify all required submodules. Diagnose the local `git submodule` Cygwin helper failure without changing tracked source to hide it.
- [ ] Run `premake5.bat` and build the FusionFix ASI for Win32 Debug and Release.
- [ ] Record the exact commands, outputs, and any prerequisites not present on this machine.

## Done when

Both x86 configurations build from a documented checkout, or a specific external blocker is recorded with a reproducible diagnostic and follow-up issue. Do not close this issue merely because CI builds elsewhere.

## Notes / evidence

- At roadmap creation, `git submodule status` failed in a Cygwin helper. This has not yet been diagnosed.
