param([string]$Path = 'GTA4VR.zip')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Path))
try {
    $entries = @{}
    foreach ($entry in $archive.Entries) { $entries[$entry.FullName.Replace('\','/')] = $entry }
    $required = @('dinput8.dll', 'd3d9.dll', 'vulkan.dll', 'd3d9.cfg',
        'plugins/GTAIV.EFLC.FusionFix.asi', 'plugins/GTAIV.EFLC.FusionFix.ini',
        'plugins/openxr_loader.dll', 'plugins/openvr_api.dll', 'plugins/GTAIV.EFLC.FusionFix.cfg', 'plugins/VRWeapons/pistol.ini',
        'update/01.AnimatedWeaponsVanilla/weapons.img',
        'GTA4VR-notices/DXVK-LICENSE.txt', 'GTA4VR-notices/GPL-3.0.txt',
        'GTA4VR-notices/OpenXR-LICENSE.txt', 'GTA4VR-notices/AnimatedWeapons-NOTICE.md')
    foreach ($name in $required) {
        if (-not $entries.ContainsKey($name) -or $entries[$name].Length -eq 0) { throw "Missing/empty package file: $name" }
    }
    foreach ($name in @('dinput8.dll','d3d9.dll','vulkan.dll','plugins/GTAIV.EFLC.FusionFix.asi','plugins/openxr_loader.dll','plugins/openvr_api.dll')) {
        $stream = $entries[$name].Open()
        $buffer = [System.IO.MemoryStream]::new()
        try {
            $stream.CopyTo($buffer)
            $bytes = $buffer.ToArray()
            $offset = [BitConverter]::ToInt32($bytes, 0x3c)
            if ([BitConverter]::ToUInt32($bytes,$offset) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$offset+4) -ne 0x14c) { throw "Package dependency is not x86 PE: $name" }
        } finally { $stream.Dispose(); $buffer.Dispose() }
    }
    $reader = [System.IO.StreamReader]::new($entries['plugins/GTAIV.EFLC.FusionFix.ini'].Open())
    try { $ini = $reader.ReadToEnd() } finally { $reader.Dispose() }
    foreach ($key in @('DisableOpenXR','EnableCommandBridge','DiagnosticLogging','DiagnosticCounters','DiagnosticObservationHooks','DiagnosticAutomaticCaptures')) {
        if ($ini -notmatch "(?m)^$key\s*=\s*0\s*(?:(?:;|//)[^\r\n]*)?\r?$") { throw "Unexpected distribution default: $key" }
    }
    if ($entries.Keys | Where-Object { $_ -match 'WeaponAnimations.*\.asi$|(^|/)(GTAIV\.exe|\.env)$' }) { throw 'Development or conflicting executable in package' }
    Write-Output "Verified $($entries.Count) package entries, x86 dependencies, notices and distribution defaults."
} finally { $archive.Dispose() }
