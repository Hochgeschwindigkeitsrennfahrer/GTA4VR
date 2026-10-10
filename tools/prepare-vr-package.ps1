# Stage only redistributable dependency files. Run from the repository root.
$ErrorActionPreference = 'Stop'
$requiredFiles = @('data/dinput8.dll', 'data/d3d9.dll', 'data/vulkan.dll',
    'data/plugins/GTAIV.EFLC.FusionFix.asi', 'data/plugins/openxr_loader.dll',
    'third_party/animated-weapons/weapons.img')
foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) { throw "Missing package dependency: $requiredFile" }
}
$weaponDirectory = 'data/update/01.AnimatedWeaponsVanilla'
New-Item -ItemType Directory -Force $weaponDirectory, 'data/GTA4VR-notices' | Out-Null
Copy-Item -LiteralPath 'third_party/animated-weapons/weapons.img' -Destination $weaponDirectory -Force
Copy-Item -LiteralPath 'third_party/animated-weapons/original-readme.txt' -Destination 'data/GTA4VR-notices/AnimatedWeapons-original-readme.txt' -Force
Copy-Item -LiteralPath 'third_party/animated-weapons/NOTICE.md' -Destination 'data/GTA4VR-notices/AnimatedWeapons-NOTICE.md' -Force
Copy-Item -LiteralPath 'license' -Destination 'data/GTA4VR-notices/GPL-3.0.txt' -Force
Copy-Item -LiteralPath 'readme.md','readme.fusionfix.md' -Destination 'data/GTA4VR-notices' -Force
Copy-Item -LiteralPath 'external/openxr-sdk/LICENSE' -Destination 'data/GTA4VR-notices/OpenXR-LICENSE.txt' -Force
Copy-Item -LiteralPath 'third_party/dxvk/LICENSE' -Destination 'data/GTA4VR-notices/DXVK-LICENSE.txt' -Force
Copy-Item -LiteralPath 'third_party/NOTICE.md' -Destination 'data/GTA4VR-notices/DEPENDENCIES.md' -Force
# The graphics proxy reads this before creating the game device.
Set-Content -LiteralPath 'data/d3d9.cfg' -Encoding ascii -Value "[MAIN]`r`nAPI=1"
# Never ship the external animation ASI: VR owns weapon transforms and reloads.
if (Get-ChildItem data -Recurse -Filter 'WeaponAnimations*.asi') { throw 'Conflicting external animation ASI in package' }
Write-Output 'VR package dependencies and notices staged.'
