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

The DXVK discovery probe below has separate success criteria.

```bat
build\bridge-probe\d3d9ex_d3d11.exe
build\bridge-probe\openxr_win32.exe
```

Both should exit 0 and print `PASS`. The first checks shared D3D9Ex → D3D11 color pixels. The second additionally creates an OpenXR D3D11 session, acquires a swapchain image, copies the pixels into it, reads the image back, and releases it. It does not submit a frame to the headset. See [`docs/openxr-graphics-bridge.md`](../../docs/openxr-graphics-bridge.md) for the recorded environment, decision, and limitations.

## DXVK Vulkan discovery (#013)

Build using the same SDK setup and x86 Native Tools prompt:

```bat
cl /nologo /EHsc /std:c++17 /W4 /MD /Ibuild\openxr-sdk\include /Fo:build\bridge-probe\dxvk-discovery.obj /Fe:build\bridge-probe\dxvk_openxr_discovery.exe tools\bridge-probe\dxvk_openxr_discovery.cpp build\openxr-sdk-win32\src\loader\Release\openxr_loader.lib advapi32.lib
```

With GTA IV closed and SteamVR/headset available, run in PowerShell using existing installed x86 DLLs:

```powershell
$env:DXVK_LOG_PATH = (Resolve-Path build/bridge-probe).Path
& .\build\bridge-probe\dxvk_openxr_discovery.exe `
  'C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto IV\GTAIV\vulkan.dll' `
  'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win32\openvr_api.dll'
$LASTEXITCODE
```

The optional second DLL enables DXVK's existing OpenVR extension bootstrap. Omit it to compare plain initialization; that variant failed discovery on this installation. No DLLs are copied into the game; backend settings remain unchanged. Success covers DXVK image/device discovery, matching runtime GPU, session and swapchain creation. It does **not** test copies, barriers, headset submission or performance. ABI declarations are pinned to DXVK v3.1.1 and x86. See [the feasibility decision](../../docs/vulkan-openxr-feasibility.md).

## Repeated Vulkan pixel copies

Update submodules first: `git submodule update --init --recursive`. Vulkan headers are public Khronos source pinned to v1.3.231; no Vulkan import library is needed. Build the matching Debug loader with `cmake --build build/openxr-sdk-win32 --config Debug --target openxr_loader`.

In the x86 Native Tools prompt:

```bat
cl /nologo /EHsc /std:c++17 /W4 /MDd /Od /Zi /Ibuild\openxr-sdk\include /Iexternal\vulkan-headers\include /Fo:build\bridge-probe\dxvk-copy-debug.obj /Fd:build\bridge-probe\dxvk-copy-debug.pdb /Fe:build\bridge-probe\dxvk_openxr_copy_debug.exe tools\bridge-probe\dxvk_openxr_copy.cpp build\openxr-sdk-win32\src\loader\Debug\openxr_loaderd.lib advapi32.lib
cl /nologo /EHsc /std:c++17 /W4 /MD /O2 /Ibuild\openxr-sdk\include /Iexternal\vulkan-headers\include /Fo:build\bridge-probe\dxvk-copy.obj /Fe:build\bridge-probe\dxvk_openxr_copy.exe tools\bridge-probe\dxvk_openxr_copy.cpp build\openxr-sdk-win32\src\loader\Release\openxr_loader.lib advapi32.lib
```

Run each executable with the same two absolute DLL paths and `DXVK_LOG_PATH` as discovery. The copy probe uses the game's actual `DxvkVulkan::Bridge` implementation. It checks every pixel of 32 changing-color copies, alternates the two source slots and rotates through runtime-owned swapchain images, restores image layouts and verifies teardown. Its extra GPU-to-host readback is diagnostic only. This does not submit headset frames or benchmark game performance.

`GTA4VR_COPY_PRODUCER_READBACK=1` optionally checks the first source through D3D9 readback for failure diagnosis; leave it unset for acceptance. The final copier passed without this additional synchronization. No x86 Khronos validation layer was registered on the test machine, so successful pixels are not a validation-layer clean bill.

## Standalone shader / billboard instancing probe (#013)

`billboard-instancing.cpp` and `billboard-instancing.hlsl` exercise GPU expansion
of a shared unit quad using compact center/half-axis/color instance records.
They create their own hidden window/device/128x128 render target. No game hooks,
installed-file edits or OpenXR session. Run from the repository root so the HLSL
path resolves. This is a feasibility/correctness test, not a frame-time benchmark
or a compatible GTA IV corona implementation.

From an x86 Native Tools command prompt:

```bat
cl /nologo /EHsc /std:c++20 /W4 /O2 tools\bridge-probe\billboard-instancing.cpp /Febuild\bridge-probe\billboard-instancing.exe /Fobuild\bridge-probe\billboard-instancing.obj d3d9.lib d3dcompiler.lib user32.lib
build\bridge-probe\billboard-instancing.exe "C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto IV\GTAIV\vulkan.dll"
```

An optional absolute backend DLL selects the installed x86 DXVK; omission uses
system D3D9. The test compiles Shader Model3 vertex/pixel programs, renders16
instances, reuses the instance buffer with two eye matrices, and compares every
pixel against CPU-expanded geometry. Nonempty output and zero mismatches are
required. Readback exists only for this correctness test; a production draw
consumes GPU geometry directly. Native lighting/depth/occlusion/transparency,
resource reset and game-shader routing require separate validation. Keep DXVK's
generated log and all executables in ignored build directories. Do not run this
or other GPU workloads during performance captures.

### Preserve the native billboard shader

`instance-shader-prefix.cpp` reads a local native VS disassembly and adds GPU
corner expansion using `VRBillboardShader::AddInstanceExpansion`. It keeps the
original shader body, including depth visibility and fog. Unsupported inputs or
occupied scratch registers are rejected. Captured shader text is never embedded
in the repository. The standalone runner is separate from the default-off
in-game `billboard-instancing-test` experiment.

```bat
cl /nologo /EHsc /std:c++20 /W4 /O2 /Isource\dxsdk tools\bridge-probe\instance-shader-prefix.cpp /Febuild\bridge-probe\instance-shader-prefix.exe /Fobuild\bridge-probe\instance-shader-prefix.obj source\dxsdk\lib\x86\d3dx9.lib d3d9.lib user32.lib
build\bridge-probe\instance-shader-prefix.exe "C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto IV\GTAIV\vulkan.dll" build\profiles\native\corona-native-vs.asm build\profiles\native\corona-native-ps.asm build\profiles\native\corona-native-constants.txt
```

With only the backend/VS arguments, it checks shader assembly and driver creation.
The optional PS/constants arguments compare CPU-expanded and GPU-instanced pixels
for both eyes with synthetic white atlas/depth textures, including fully visible
and occluded cases. The constants text contains eye0 then eye1, each with256 VS
and224 PS float4 rows. Extract these from a completed valid local corona capture;
keep all native inputs/logs out of Git. Decimal dumps preserve float values but
not NaN payload bits. This checks the captured shader variant with synthetic
geometry, not all native lights, fades, atlas contents, draw ordering or reset.
Readback is test-only; there is no performance or in-game compatibility claim.

Append `compact` after the constants argument to test native32-byte records:
center/radius/brightness/distance push/packed RGB. The prefix reconstructs corners,
shifted center, atlas UV, brightness and packed color before the original shader
body. This mode uses a colored2x2 atlas, native distance-shift fixtures and each
eye's constants, and checks visible/occluded pixel parity. Native scratch register
and input validation can reject unsupported variants. Compact runner parity does
not replace real-game graphics, lifecycle or performance checks.

### Rigid mesh matrix instancing

`rigid-instance-prefix.cpp` uses `VRRigidInstanceShader::AddInstanceMatrices` to
retain a locally captured rigid SM3 vertex shader while replacing its world/clip
matrix constants with eight per-instance float4 attributes. Unsupported layout,
relative constants, matrix definitions or occupied scratch registers are rejected.
The local native pixel shader is kept intact. No native shaders are distributed.

```bat
cl /nologo /EHsc /std:c++20 /W4 /O2 /Isource\dxsdk tools\bridge-probe\rigid-instance-prefix.cpp /Febuild\bridge-probe\rigid-instance-prefix.exe /Fobuild\bridge-probe\rigid-instance-prefix.obj source\dxsdk\lib\x86\d3dx9.lib d3d9.lib user32.lib
build\bridge-probe\rigid-instance-prefix.exe "C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto IV\GTAIV\vulkan.dll" build\profiles\native\mesh-lanes-native-VS.asm build\profiles\native\mesh-lanes-native-PS.asm
```

Runs in a hidden standalone x86 D3D9Ex window. Sixteen independently transformed
synthetic quads use authored atlas/material fixtures; compare16 native draws with
one instanced draw in two eye projections. Check all four GBuffer color targets,
nonempty visible output and empty cutout output. A D24S8 depth surface and a later
magenta plane test native shader-written depth: nearer instances remain visible,
farther ones are covered, and all four color targets must match between paths.
This observes depth indirectly; it does not compare every depth-buffer bit.
No game mesh replacement, benchmark, native occlusion-query/order/resource-reset
or broader material compatibility claim. Keep native inputs/results/DXVK logs
out of Git; do not run the test during performance captures.

Append `--tangent` to test the verified five-input variant (position/color/UV/
normal/tangent in v0..4). It preserves tangent v4 and moves instance matrices to
v5..12, still using free r16..23. Authored vertices are52 bytes; native input
declarations determine whether the tangent element is enabled. The fixture binds
defined authored material textures and an albedo multiplier; tangent fade clipping
uses the native pixel shader's c39.x texkill path rather than assuming texture
alpha alone clips. Both the original four-input control and city tangent native
VS/PS passed all four targets, two eyes and the indirect depth-plane check.
This is representation evidence, not permission to combine different material
states or reorder native scene/query commands.

The runner also exercises the production `VRRigidInstance::Renderer` as a third
mode:16 separate one-instance draws of the same authored geometry. Each draw
checks restoration of shader/declaration/stream1/frequencies; compare its four
targets against native output using `compatibilityMismatches`. Both four-input
and tangent fixtures passed with32 replacements per eye, zero mismatches and
zero draw/restoration failures (including depth-plane and fade/cutout cases).
The in-game `rigid-instancing-test` command uses this same renderer. Its bounded
shader/declaration cache and dynamic instance buffer retain native resources;
unsupported declarations/streams/shaders fall back. Neither this mode nor the
single-instance game stage is a draw-count optimization.

The production emitter also accepts an explicit span of world/clip matrices
for a caller-verified same-state packet. A fourth fixture mode submits16 objects
with one production indexed draw and compares `batchMismatches` against native
output. An8190-instance offscreen upload before the visible packet forces early
ring-buffer wrap; both shader variants passed color/depth/fade checks with zero
mismatches and restored bindings. The game still supplies one instance per draw.
There is no native scene grouping or measured speedup from this fixture. An
upstream packet builder must preserve material/geometry/state/query order and
resource lifetime; the emitter does not establish that compatibility.
