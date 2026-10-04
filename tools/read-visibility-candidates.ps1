param(
    [Parameter(Mandatory=$true)][int]$GameProcessId,
    [Parameter(Mandatory=$true)][float]$X,
    [Parameter(Mandatory=$true)][float]$Y,
    [Parameter(Mandatory=$true)][float]$Z,
    [ValidateRange(1,300)][float]$Radius=150,
    [switch]$FineGrid,
    [ValidateRange(0,31)][int]$QueuePhase=0,
    [switch]$Queues
)
$ErrorActionPreference='Stop'
# CE 1.2.0.59 only. ReadProcessMemory only; no writes, calls or suspension.
# AE9400/AE5CA0 use a 64 KiB array of {entity*, distance} candidates.
# The game keeps running: individual records can belong to adjacent frames.
if (-not ('GtaVrVisibilityRead' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GtaVrVisibilityRead {
 [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint rights, bool inherit, int pid);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr count, out UIntPtr read);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
}
$gameProcess=Get-Process -Id $GameProcessId
if ($gameProcess.ProcessName -ne 'GTAIV') { throw 'Expected GTAIV.exe' }
$gameModule=$gameProcess.Modules | Where-Object ModuleName -eq 'GTAIV.exe' | Select-Object -First 1
if ($gameModule.FileVersionInfo.FileVersion -ne '1.2.0.59') { throw 'Requires CE 1.2.0.59' }
$moduleBase=$gameModule.BaseAddress.ToInt64()
$readHandle=[GtaVrVisibilityRead]::OpenProcess(0x1010,$false,$GameProcessId)
if ($readHandle -eq [IntPtr]::Zero) { throw 'Cannot open game for read' }
function Read-CandidateBytes([long]$Address,[int]$Count) {
    if ($Address -lt 0x10000 -or $Count -le 0 -or $Count -gt 65536 -or $Address+$Count -gt 0x100000000L) { throw 'Invalid bounded read' }
    $bytes=[byte[]]::new($Count)
    $got=[UIntPtr]::Zero
    if (-not [GtaVrVisibilityRead]::ReadProcessMemory($readHandle,[IntPtr]$Address,$bytes,[UIntPtr]$Count,[ref]$got) -or $got.ToUInt64() -ne $Count) { throw 'Read unavailable' }
    return ,$bytes
}
try {
    $start=$moduleBase+0x159B768-0x400000
    $visibilityTick=[BitConverter]::ToUInt16((Read-CandidateBytes ($moduleBase+0x11A8908-0x400000) 2),0)
    $end=[BitConverter]::ToUInt32((Read-CandidateBytes ($moduleBase+0x1593BC4-0x400000) 4),0)
    $length=$end-$start
    if (!$FineGrid -and ($length -le 0 -or $length -gt 65536 -or $length%8)) { throw 'Candidate array unavailable or changed' }
    $candidates=if (!$FineGrid) {Read-CandidateBytes $start $length} else {$null}
    $truncated=$false
    if ($FineGrid) {
        # Live 4A48C0: 120x120 cells, 12 bytes/cell at Ghidra 11A9D20.
        # AE47B0 traverses cell+0 and cell+4 list heads; each link is {entity,next}.
        $entities=[Collections.Generic.List[uint32]]::new()
        $linksSeen=[Collections.Generic.HashSet[uint32]]::new()
        $cellX=[Math]::Floor($X*.02+60)
        $cellY=[Math]::Floor($Y*.02+60)
        for ($cy=$cellY-2;$cy -le $cellY+2;$cy++) {
            for ($cx=$cellX-2;$cx -le $cellX+2;$cx++) {
                if ($cx -lt 0 -or $cx -ge 120 -or $cy -lt 0 -or $cy -ge 120) {continue}
                $cell=Read-CandidateBytes ($moduleBase+0x11A9D20-0x400000+12*($cx+120*$cy)) 8
                foreach ($headOffset in @(0,4)) {
                    $link=[BitConverter]::ToUInt32($cell,$headOffset)
                    $walked=0
                    while ($link -ne 0 -and $linksSeen.Add($link)) {
                        if ($walked++ -ge 256 -or $entities.Count -ge 8192) {$truncated=$true;break}
                        try {$node=Read-CandidateBytes $link 8} catch {break}
                        $entities.Add([BitConverter]::ToUInt32($node,0))
                        $link=[BitConverter]::ToUInt32($node,4)
                    }
                }
            }
        }
        $length=$entities.Count*8
        $candidates=[byte[]]::new($length)
        for ($i=0;$i -lt $entities.Count;$i++) { [BitConverter]::GetBytes($entities[$i]).CopyTo($candidates,$i*8) }
    }
    $seen=[Collections.Generic.HashSet[uint32]]::new()
    $queueEntries=@{}
    if ($Queues) {
        # AE43A0/900780: eight {pointer,ushort count,ushort capacity} headers
        # per phase at 1614CA0 + phase*54; entries are {entity*,float}.
        $headers=Read-CandidateBytes ($moduleBase+0x1614CA0-0x400000+$QueuePhase*0x54) 64
        for ($slot=0;$slot -lt 8;$slot++) {
            $array=[BitConverter]::ToUInt32($headers,$slot*8)
            $count=[BitConverter]::ToUInt16($headers,$slot*8+4)
            $capacity=[BitConverter]::ToUInt16($headers,$slot*8+6)
            if (!$array -or !$count -or $count -gt $capacity -or $count -gt 8192) {continue}
            try {$entries=Read-CandidateBytes $array ($count*8)} catch {continue}
            for ($i=0;$i -lt $count;$i++) {
                $key=('{0:X8}' -f [BitConverter]::ToUInt32($entries,$i*8))
                if (!$queueEntries.ContainsKey($key)) {$queueEntries[$key]=@()}
                $queueEntries[$key]=@($queueEntries[$key])+@($slot)
            }
        }
    }
    $records=@()
    for ($offset=0;$offset -lt $length;$offset+=8) {
        $entity=[BitConverter]::ToUInt32($candidates,$offset)
        if ($entity -eq 0 -or -not $seen.Add($entity)) { continue }
        try {
            $header=Read-CandidateBytes $entity 104
            $matrix=[BitConverter]::ToUInt32($header,0x20)
            $position=if($matrix) { Read-CandidateBytes ($matrix+0x30) 12 } else { $header[0x10..0x1B] }
            $px=[BitConverter]::ToSingle($position,0)
            $py=[BitConverter]::ToSingle($position,4)
            $pz=[BitConverter]::ToSingle($position,8)
            $distance=[Math]::Sqrt(($px-$X)*($px-$X)+($py-$Y)*($py-$Y)+($pz-$Z)*($pz-$Z))
            if ([double]::IsNaN($distance) -or $distance -gt $Radius) { continue }
            $records += [pscustomobject]@{
                entity=('{0:X8}' -f $entity); model=[BitConverter]::ToInt16($header,0x2E)
                position=@($px,$py,$pz); rootDistance=$distance
                nativeDistance=if($FineGrid) {$null} else {[BitConverter]::ToSingle($candidates,$offset+4)}
                drawable=('{0:X8}' -f [BitConverter]::ToUInt32($header,0x34))
                drawDistance=[BitConverter]::ToSingle($header,0x50)
                parent=('{0:X8}' -f [BitConverter]::ToUInt32($header,0x4C))
                candidates=('{0:X8}' -f [BitConverter]::ToUInt32($header,8))
                rejected=('{0:X8}' -f [BitConverter]::ToUInt32($header,12))
                flags=('{0:X8}' -f [BitConverter]::ToUInt32($header,0x24))
                typeFlags=('{0:X8}' -f [BitConverter]::ToUInt32($header,0x28))
                lodSelected=('{0:X8}' -f [BitConverter]::ToUInt32($header,0x54))
                lodChildren=('{0:X8}' -f [BitConverter]::ToUInt32($header,0x58))
                childCount=$header[0x61]
                queueSlots=if($Queues){@($queueEntries[('{0:X8}' -f $entity)])}else{$null}
                alpha=$header[0x63]
                lastVisitedTick=[BitConverter]::ToUInt32($header,0x3C)
            }
        } catch { continue } # Entity may unload while the game runs.
    }
    [pscustomobject]@{pid=$GameProcessId; moduleBase=('{0:X8}' -f $moduleBase); visibilityTick=$visibilityTick; source=if($FineGrid){'fine-grid-5x5'}else{'candidate-array'}; truncated=$truncated; candidateBytes=$length; capacity=65536; radius=$Radius; snapshotAtomic=$false; records=@($records | Sort-Object rootDistance)} | ConvertTo-Json -Depth 5
} finally { [void][GtaVrVisibilityRead]::CloseHandle($readHandle) }
