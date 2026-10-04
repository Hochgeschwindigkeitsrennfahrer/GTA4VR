param([Parameter(Mandatory=$true)][int]$GameProcessId)
$ErrorActionPreference='Stop'
# Read-only CE1.2.0.59 snapshot. Reuse the bounded process reader and live
# module/version validation; no debugger attachment or native field writes.
. "$PSScriptRoot/read-input-devices.ps1" -GameProcessId $GameProcessId | Out-Null
$taskHandle=[GtaVrInputRead]::OpenProcess(0x1010,$false,$GameProcessId)
if ($taskHandle -eq [IntPtr]::Zero) {throw 'Cannot open game for read'}
try {
    $code=Read-InputBytes ($taskBase+0x8DC3A0-0x400000) 80
    if ($code[0] -ne 0x53 -or $code[18] -ne 0xB9 -or $code[64] -ne 0x81 -or $code[65] -ne 0xF9) {
        throw 'Command allocator signature mismatch'
    }
    $manager=[BitConverter]::ToUInt32($code,19)
    $capacity=[BitConverter]::ToUInt32($code,66)
    if ($capacity -ne 0x200000 -and $capacity -ne 0x800000) {throw 'Unrecognized command capacity'}
    $header=Read-InputBytes $manager 36
    $readSlot=[BitConverter]::ToUInt32($header,16)
    $writeSlot=[BitConverter]::ToUInt32($header,20)
    if ($readSlot -gt 1 -or $writeSlot -gt 1) {throw 'Invalid native command slots'}
    [pscustomobject]@{
        pid=$GameProcessId;time=(Get-Date).ToString('o');moduleBase=('{0:X8}' -f $taskBase)
        manager=('{0:X8}' -f $manager);capacityBytes=$capacity;snapshotAtomic=$false
        buffers=@(('{0:X8}' -f [BitConverter]::ToUInt32($header,0)),('{0:X8}' -f [BitConverter]::ToUInt32($header,4)))
        overflow=@([bool]$header[8],[bool]$header[9]);readSlot=$readSlot;writeSlot=$writeSlot
        readOffset=[BitConverter]::ToUInt32($header,24);writeOffset=[BitConverter]::ToUInt32($header,28)
    } | ConvertTo-Json -Depth 3
} finally {[void][GtaVrInputRead]::CloseHandle($taskHandle)}
