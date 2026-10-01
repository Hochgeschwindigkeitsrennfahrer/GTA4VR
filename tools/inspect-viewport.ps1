param(
    [int]$ProcessId = 0,
    [ValidateRange(1, 1000)] [int]$Samples = 12,
    [ValidateRange(10, 10000)] [int]$IntervalMs = 100,
    [ValidateRange(0, 600)] [int]$WaitForViewportSeconds = 30,
    [ValidateRange(0, 60)] [int]$PhaseProbeSeconds = 0,
    [string[]]$InspectPhasePointers = @(),
    [string[]]$InspectParentPointers = @(),
    [switch]$PhaseTransitions
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class GTAIVMemory
{
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(uint access, bool inheritHandle, int processId);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ReadProcessMemory(IntPtr process, IntPtr address,
        [Out] byte[] buffer, UIntPtr length, out UIntPtr bytesRead);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

function Read-RemoteBytes([IntPtr]$Process, [UInt32]$Address, [int]$Length) {
    $buffer = [byte[]]::new($Length)
    $read = [UIntPtr]::Zero
    $ok = [GTAIVMemory]::ReadProcessMemory($Process, [IntPtr]::new([long]$Address),
        $buffer, [UIntPtr]::new([uint32]$Length), [ref]$read)
    if (-not $ok -or $read.ToUInt64() -ne [uint64]$Length) {
        throw ('ReadProcessMemory failed at 0x{0:X8} (Win32 {1})' -f $Address,
            [Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
    return ,$buffer
}

$game = if ($ProcessId) { Get-Process -Id $ProcessId } else { Get-Process GTAIV | Select-Object -First 1 }
if (-not $game) { throw 'GTAIV.exe is not running' }
$module = $game.Modules | Where-Object ModuleName -eq 'GTAIV.exe' | Select-Object -First 1
if (-not $module) { throw 'Could not locate GTAIV.exe module' }
$image = [IO.File]::ReadAllBytes($module.FileName)
$pe = [BitConverter]::ToInt32($image, 0x3C)
$sectionCount = [BitConverter]::ToUInt16($image, $pe + 6)
$sectionTable = $pe + 24 + [BitConverter]::ToUInt16($image, $pe + 20)
$codeRva = $null
$codeSize = $null
for ($s = 0; $s -lt $sectionCount; $s++) {
    $entry = $sectionTable + 40 * $s
    $sectionName = [Text.Encoding]::ASCII.GetString($image, $entry, 8).Trim([char]0)
    if ($sectionName -eq '.text') {
        $codeRva = [BitConverter]::ToUInt32($image, $entry + 12)
        $codeSize = [BitConverter]::ToUInt32($image, $entry + 8)
        break
    }
}
if ($null -eq $codeRva) { throw 'GTAIV.exe has no .text section' }

$process = [GTAIVMemory]::OpenProcess(0x410, $false, $game.Id)
if ($process -eq [IntPtr]::Zero) { throw 'OpenProcess failed; run from a process allowed to inspect GTAIV.exe' }
try {
    # The on-disk bytes differ from the code FusionFix scans at runtime, so
    # match the same signature against live .text pages. No process writes occur.
    $candidates = [Collections.Generic.List[uint32]]::new()
    $phaseCandidates = [Collections.Generic.List[uint32]]::new()
    for ($offset = 0; $offset -lt $codeSize; $offset += 0x1000) {
        $pageAddress = [uint32]($module.BaseAddress.ToInt64() + $codeRva + $offset)
        $length = [int][Math]::Min(0x1000, $codeSize - $offset)
        try { $page = Read-RemoteBytes $process $pageAddress $length }
        catch { continue }
        for ($i = 0; $i -lt $page.Length - 13; $i++) {
            if ($page[$i] -eq 0x8B -and
                ($page[$i + 1] -eq 0x35 -or $page[$i + 1] -eq 0x3D) -and
                $page[$i + 6] -eq 0x75 -and $page[$i + 7] -eq 0x14 -and
                ($page[$i + 1] -eq 0x35 -or ($page[$i + 8] -eq 0x6A -and $page[$i + 9] -eq 0x00))) {
                $candidates.Add([uint32]($pageAddress + $i))
            }
            if ($page[$i] -eq 0x89 -and $page[$i + 1] -eq 0x0D -and
                $page[$i + 6] -eq 0x8B -and $page[$i + 7] -eq 0x01 -and
                (($page[$i + 8] -eq 0xFF -and $page[$i + 9] -eq 0x50 -and
                  $page[$i + 11] -eq 0xC7 -and $page[$i + 12] -eq 0x05) -or
                 ($page[$i + 8] -eq 0x8B -and $page[$i + 9] -eq 0x50))) {
                $phaseCandidates.Add([uint32]($pageAddress + $i))
            }
        }
    }
    if ($candidates.Count -ne 1) {
        throw "Expected one live viewport pattern; found $($candidates.Count)"
    }
    $instructionAddress = $candidates[0]
    $instruction = Read-RemoteBytes $process $instructionAddress 8
    $globalAddress = [BitConverter]::ToUInt32($instruction, 2)
    Write-Host ('Pattern at 0x{0:X8}; instruction {1}; viewport global at 0x{2:X8}' -f
        $instructionAddress, [BitConverter]::ToString($instruction), $globalAddress)
    $phaseGlobalAddress = 0
    if ($phaseCandidates.Count -eq 1) {
        $phaseInstruction = Read-RemoteBytes $process $phaseCandidates[0] 10
        $phaseGlobalAddress = [BitConverter]::ToUInt32($phaseInstruction, 2)
        Write-Host ('Current render phase global at 0x{0:X8}' -f $phaseGlobalAddress)
    }
    else { Write-Host "Render phase pattern candidates: $($phaseCandidates.Count)" }
    if ($InspectParentPointers.Count -gt 0) {
        foreach ($pointer in $InspectParentPointers) {
            if ($pointer -notmatch '^0x[0-9A-Fa-f]{8}$') { throw "Invalid parent pointer: $pointer" }
            $parentAddress = [Convert]::ToUInt32($pointer.Substring(2), 16)
            try {
                # Both measured phase setup methods copy the parent viewport at +0x10.
                $parentViewport = Read-RemoteBytes $process ([uint32]($parentAddress + 0x10)) 0x330
                [pscustomobject]@{
                    parent_pointer = ('0x{0:X8}' -f $parentAddress)
                    camera = '{0:F2},{1:F2},{2:F2}' -f
                        [BitConverter]::ToSingle($parentViewport, 0x70),
                        [BitConverter]::ToSingle($parentViewport, 0x74),
                        [BitConverter]::ToSingle($parentViewport, 0x78)
                    fov = [BitConverter]::ToSingle($parentViewport, 0x2B8)
                    aspect = [BitConverter]::ToSingle($parentViewport, 0x2BC)
                    near = [BitConverter]::ToSingle($parentViewport, 0x2C0)
                    far = [BitConverter]::ToSingle($parentViewport, 0x2C4)
                    projection_x = [BitConverter]::ToSingle($parentViewport, 0x1C0)
                    projection_y = [BitConverter]::ToSingle($parentViewport, 0x1D4)
                    plane0_x = [BitConverter]::ToSingle($parentViewport, 0x300)
                    plane0_y = [BitConverter]::ToSingle($parentViewport, 0x304)
                    plane0_z = [BitConverter]::ToSingle($parentViewport, 0x308)
                    plane0_w = [BitConverter]::ToSingle($parentViewport, 0x30C)
                }
            }
            catch { Write-Warning "Could not read parent viewport at $pointer`: $_" }
        }
        return
    }
    if ($PhaseProbeSeconds -gt 0) {
        if ($phaseGlobalAddress -eq 0) { throw 'Render phase pointer pattern was not unique' }
        $phaseReads = 0
        $nonzeroReads = 0
        $phasePointers = @{}
        $transitions = [Collections.Generic.List[object]]::new()
        $lastPhaseKey = ''
        $probeEnd = (Get-Date).AddSeconds($PhaseProbeSeconds)
        while ((Get-Date) -lt $probeEnd) {
            $phasePointer = [BitConverter]::ToUInt32((Read-RemoteBytes $process $phaseGlobalAddress 4), 0)
            $phaseReads++
            if ($phasePointer -ne 0) {
                $nonzeroReads++
                $key = ('0x{0:X8}' -f $phasePointer)
                if (-not $phasePointers.ContainsKey($key)) { $phasePointers[$key] = 0 }
                $phasePointers[$key]++
                if ($key -ne $lastPhaseKey) {
                    $transitions.Add([pscustomobject]@{tick_ms=[Environment]::TickCount64; phase_pointer=$key})
                    $lastPhaseKey = $key
                }
            }
        }
        Write-Output "Read $phaseReads times; $nonzeroReads nonzero phase reads; $($phasePointers.Count) unique pointers."
        foreach ($pointer in $InspectPhasePointers) {
            if ($pointer -notmatch '^0x[0-9A-Fa-f]{8}$') { throw "Invalid phase pointer: $pointer" }
            $key = $pointer.ToUpperInvariant().Replace('0X', '0x')
            if (-not $phasePointers.ContainsKey($key)) { $phasePointers[$key] = 0 }
        }
        $phaseTypes = @{}
        $summary = [Collections.Generic.List[object]]::new()
        foreach ($entry in ($phasePointers.GetEnumerator() | Sort-Object Value -Descending)) {
            $phaseType = ''
            $vtable = 0
            $phaseCamera = ''
            $phaseFov = $null
            $phaseNear = $null
            $phaseFar = $null
            $phaseCamera2 = ''
            $phaseFov2 = $null
            $visibilityMode = $null
            try {
                $address = [Convert]::ToUInt32($entry.Key.Substring(2), 16)
                $vtable = [BitConverter]::ToUInt32((Read-RemoteBytes $process $address 4), 0)
                $locator = [BitConverter]::ToUInt32((Read-RemoteBytes $process ([uint32]($vtable - 4)) 4), 0)
                $typeDescriptor = [BitConverter]::ToUInt32((Read-RemoteBytes $process ([uint32]($locator + 12)) 4), 0)
                $typeBytes = Read-RemoteBytes $process ([uint32]($typeDescriptor + 8)) 100
                $phaseType = [Text.Encoding]::ASCII.GetString($typeBytes).Split([char]0)[0]
            }
            catch { $phaseType = '' }
            try {
                # Existing FusionFix code reads phase + 0x128 as the camera Z;
                # grcViewport::mCameraMatrix[3][2] is at viewport + 0x78.
                # This suggests an embedded viewport at phase + 0xB0.
                $phaseViewport = Read-RemoteBytes $process ([uint32]($address + 0xB0)) 0x2C8
                $phaseCamera = '{0:F1},{1:F1},{2:F1}' -f
                    [BitConverter]::ToSingle($phaseViewport, 0x70),
                    [BitConverter]::ToSingle($phaseViewport, 0x74),
                    [BitConverter]::ToSingle($phaseViewport, 0x78)
                $phaseFov = [BitConverter]::ToSingle($phaseViewport, 0x2B8)
                $phaseNear = [BitConverter]::ToSingle($phaseViewport, 0x2C0)
                $phaseFar = [BitConverter]::ToSingle($phaseViewport, 0x2C4)
            }
            catch { $phaseCamera = '' }
            try {
                $phaseViewport2 = Read-RemoteBytes $process ([uint32]($address + 0x4A0)) 0x2C8
                $phaseCamera2 = '{0:F1},{1:F1},{2:F1}' -f
                    [BitConverter]::ToSingle($phaseViewport2, 0x70),
                    [BitConverter]::ToSingle($phaseViewport2, 0x74),
                    [BitConverter]::ToSingle($phaseViewport2, 0x78)
                $phaseFov2 = [BitConverter]::ToSingle($phaseViewport2, 0x2B8)
            }
            catch { $phaseCamera2 = '' }
            try { $visibilityMode = [BitConverter]::ToInt32((Read-RemoteBytes $process ([uint32]($address + 0x3C)) 4), 0) }
            catch { $visibilityMode = $null }
            $phaseType = $phaseType -replace '^\.\?AV', '' -replace '@@$', ''
            $phaseTypes[$entry.Key] = $phaseType
            $summary.Add([pscustomobject]@{
                phase_pointer = $entry.Key
                count = $entry.Value
                vtable = ('0x{0:X8}' -f $vtable)
                phase_type = $phaseType
                camera = $phaseCamera
                fov = $phaseFov
                near = $phaseNear
                far = $phaseFar
                camera2 = $phaseCamera2
                fov2 = $phaseFov2
                visibility_mode = $visibilityMode
            })
        }
        if ($PhaseTransitions) {
            $transitions | Select-Object -First 180 |
                ForEach-Object { [pscustomobject]@{tick_ms=$_.tick_ms; phase_type=$phaseTypes[$_.phase_pointer]} }
        }
        else { $summary }
        return
    }
    for ($n = 0; $n -lt $Samples; $n++) {
        $deadline = (Get-Date).AddSeconds($WaitForViewportSeconds)
        do {
            $current = Read-RemoteBytes $process $globalAddress 4
            $viewportAddress = [BitConverter]::ToUInt32($current, 0)
            if ($viewportAddress -eq 0 -and (Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 2
            }
        } while ($viewportAddress -eq 0 -and (Get-Date) -lt $deadline)
        if ($viewportAddress -eq 0) { throw 'Current viewport remained null during rendering window' }
        $viewport = Read-RemoteBytes $process $viewportAddress 0x310
        $phaseType = ''
        $phasePointer = 0
        $vtable = 0
        if ($phaseGlobalAddress -ne 0) {
            try {
                $phasePointer = [BitConverter]::ToUInt32((Read-RemoteBytes $process $phaseGlobalAddress 4), 0)
                if ($phasePointer -ne 0) {
                    $vtable = [BitConverter]::ToUInt32((Read-RemoteBytes $process $phasePointer 4), 0)
                    $locator = [BitConverter]::ToUInt32((Read-RemoteBytes $process ([uint32]($vtable - 4)) 4), 0)
                    $typeDescriptor = [BitConverter]::ToUInt32((Read-RemoteBytes $process ([uint32]($locator + 12)) 4), 0)
                    $typeBytes = Read-RemoteBytes $process ([uint32]($typeDescriptor + 8)) 100
                    $phaseType = [Text.Encoding]::ASCII.GetString($typeBytes).Split([char]0)[0]
                }
            }
            catch { $phaseType = '' }
        }
        [pscustomobject]@{
            sampled_at = (Get-Date).ToString('o')
            process_id = $game.Id
            viewport = ('0x{0:X8}' -f $viewportAddress)
            phase_pointer = ('0x{0:X8}' -f $phasePointer)
            phase_vtable = ('0x{0:X8}' -f $vtable)
            phase_type = $phaseType
            cam_x = [BitConverter]::ToSingle($viewport, 0x70)
            cam_y = [BitConverter]::ToSingle($viewport, 0x74)
            cam_z = [BitConverter]::ToSingle($viewport, 0x78)
            fov = [BitConverter]::ToSingle($viewport, 0x2B8)
            aspect = [BitConverter]::ToSingle($viewport, 0x2BC)
            near = [BitConverter]::ToSingle($viewport, 0x2C0)
            far = [BitConverter]::ToSingle($viewport, 0x2C4)
            proj_x = [BitConverter]::ToSingle($viewport, 0x1C0)
            proj_y = [BitConverter]::ToSingle($viewport, 0x1D4)
            view_m00 = [BitConverter]::ToSingle($viewport, 0x180)
            view_m30 = [BitConverter]::ToSingle($viewport, 0x1B0)
            wvp_m00 = [BitConverter]::ToSingle($viewport, 0x100)
            plane0_x = [BitConverter]::ToSingle($viewport, 0x300)
            plane0_y = [BitConverter]::ToSingle($viewport, 0x304)
            plane0_z = [BitConverter]::ToSingle($viewport, 0x308)
            plane0_w = [BitConverter]::ToSingle($viewport, 0x30C)
        }
        if ($n + 1 -lt $Samples) { Start-Sleep -Milliseconds $IntervalMs }
    }
}
finally {
    [void][GTAIVMemory]::CloseHandle($process)
}
