# 001 — Reproduce the x86 FusionFix build

## Goal

Establish a repeatable local Debug and Release build before changing rendering code. Roadmap stage: 0.

## Tasks

- [x] Record the current Git commit, Windows/Visual Studio toolchain, and `GTAIV.exe` version.
- [x] Inspect `.gitmodules` and verify all required submodules. Diagnose the local `git submodule` Cygwin helper failure without changing tracked source to hide it.
- [x] Install or locate an MSVC toolchain with Win32 C++ and MSBuild support compatible with the generated `vs2026` solution.
- [x] Build the FusionFix ASI for Win32 Debug and Release.
- [x] Record the commands, outputs, and prerequisites found so far.

## Done when

Both x86 configurations build from a documented checkout. Do not close this issue merely because solution generation or CI builds elsewhere succeed.

## Notes / evidence

- 2026-10-01 baseline: commit `1312215cbd7103abbda1d38500c4426c9f1d70f6`; Windows NT build `26200.9457`; `GTAIV/GTAIV.exe` file/product version `1.2.0.59`; Premake `5.0.0-beta8`. Git for Windows is `2.33.0.windows.2` on the ordinary path. No Visual Studio C++ installation was reported by `vswhere -all -products '*'`, and neither `msbuild` nor `cl.exe` was found. The visible VS 2017/2019 folders contain shared components, not a build toolchain.
- `git submodule status` launched `C:\cygwin64\bin\basename.exe` and `sed.exe` from Git's helper and failed with a Cygwin heap mismatch, then `git-sh-setup: file not found`. Running Git for Windows' Bash with a controlled `PATH` succeeded. Working invocation in PowerShell: `$env:PATH = 'C:\Program Files\Git\usr\bin;C:\Program Files\Git\mingw64\bin;C:\Program Files\Git\cmd;C:\Windows\System32;C:\Windows'; & 'C:\Program Files\Git\usr\bin\bash.exe' -c 'git submodule status --recursive'`. The shell reports all five top-level and seven nested submodules at their pinned commits with no leading `-` or `+`.
- In that shell, `git submodule update --init --recursive` fetched the missing submodules successfully. The first attempt in the normal sandbox could not lock `.git/config`; an approved run with Git metadata access completed. No tracked source was changed by initialization.
- `premake5.bat` completed and generated `build/GTAIV.EFLC.FusionFix.slnx` plus project files. `build/` is ignored by Git. Re-running after submodule initialization also succeeded.
- Before installation, bare `msbuild` was unavailable. After the user installed Visual Studio Community 2026 `18.10.3`, their bare `msbuild` resolved to .NET Framework MSBuild `4.8` and rejected `.slnx` with `MSB4068`. The working MSBuild is `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` (version `18.10.1`). Use this full path or a VS 2026 Developer Command Prompt; verify with `msbuild -version`.
- The first real Debug build reached resource compilation but failed because the snow shader prebuild loop passed an unquoted `%%f` path through `Vetle Hjelle`; `fxc.exe` produced no snow shader outputs and `snow.rc` reported four `RC2135` missing-file errors. `premake5.lua` now quotes `%%f` in both loops. After regenerating with `premake5.bat`, all four shaders compile.
- Both commands succeeded with exit code 0: `& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' -m build\GTAIV.EFLC.FusionFix.vcxproj /p:Configuration=Debug /p:Platform=Win32 /nologo /verbosity:minimal` and the same with `Configuration=Release`. They produced `bin\GTAIV.EFLC.FusionFix.asi`. The direct project build covers the ASI; it does not build the installer or D3D9 helper projects. Warnings include deprecated D3D9 hooks (`C4996`) and missing dependency PDBs (`LNK4099`); neither blocked linking. No game launch has been tested here; that belongs to #002.

## Completion

Closed 2026-10-01. The Win32 Debug and Release ASI builds are reproducible with VS 2026 MSBuild using the full executable path after the shader path fix.
