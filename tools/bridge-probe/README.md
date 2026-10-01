# Standalone x86 graphics bridge probes

These programs validate the candidate graphics bridge outside GTA IV. They create their own 64×64 devices/resources; they never install an ASI or change the game. Run on Windows with a 32-bit OpenXR runtime and an HMD connected for the OpenXR probe.

## Build

The checked configuration uses Visual Studio 2026 C++ build tools, Windows SDK, CMake, and Git. From the repository root in PowerShell:

```powershell
git clone https://github.com/KhronosGroup/OpenXR-SDK-Source.git build/openxr-sdk
git -C build/openxr-sdk checkout f2448a8797c85814aa892efc1ab8707900fbcc78
cmake -S build/openxr-sdk -B build/openxr-sdk-win32 -G "Visual Studio 18 2026" -A Win32 -DDYNAMIC_LOADER=OFF -DBUILD_TESTS=OFF
cmake --build build/openxr-sdk-win32 --config Release --target openxr_loader
```

Open an **x86 Native Tools Command Prompt for VS 2026**, change to the repository root, and run:

```bat
mkdir build\bridge-probe
cl /nologo /EHsc /std:c++17 /W4 /Fo:build\bridge-probe\ /Fe:build\bridge-probe\d3d9ex_d3d11.exe tools\bridge-probe\d3d9ex_d3d11.cpp
cl /nologo /EHsc /std:c++17 /W4 /MD /Ibuild\openxr-sdk\include /Fo:build\bridge-probe\ /Fe:build\bridge-probe\openxr_win32.exe tools\bridge-probe\openxr_win32.cpp build\openxr-sdk-win32\src\loader\Release\openxr_loader.lib advapi32.lib
```

The OpenXR probe uses the **static x86 loader** built above, so no `openxr_loader.dll` is copied into the game. Keep SDK source, loader libraries, and executables in ignored `build/`; never commit runtime DLLs.

## Run and interpret

```bat
build\bridge-probe\d3d9ex_d3d11.exe
build\bridge-probe\openxr_win32.exe
```

Both should exit 0 and print `PASS`. The first checks shared D3D9Ex → D3D11 color pixels. The second additionally creates an OpenXR D3D11 session, acquires a swapchain image, copies the pixels into it, reads the image back, and releases it. It does not submit a frame to the headset. See [`docs/openxr-graphics-bridge.md`](../../docs/openxr-graphics-bridge.md) for the recorded environment, decision, and limitations.
