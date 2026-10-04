param([Parameter(Mandatory=$true)][int]$GameProcessId)
$ErrorActionPreference='Stop'
# Read-only CE1.2.0.59 snapshot. Resolve the input manager from live code,
# because the imported executable retains packed bytes at some addresses.
if (-not ('GtaVrInputRead' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GtaVrInputRead {
 [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr read);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
}
$taskGame=Get-Process -Id $GameProcessId
if ($taskGame.ProcessName -ne 'GTAIV') {throw 'Expected GTAIV.exe'}
$taskModule=$taskGame.Modules | Where-Object ModuleName -eq 'GTAIV.exe' | Select-Object -First 1
if (!$taskModule -or $taskModule.FileVersionInfo.FileVersion -ne '1.2.0.59') {throw 'Expected CE1.2.0.59'}
$taskBase=$taskModule.BaseAddress.ToInt64()
$taskHandle=[GtaVrInputRead]::OpenProcess(0x1010,$false,$GameProcessId)
if ($taskHandle -eq [IntPtr]::Zero) {throw 'Cannot open game for read'}
function Read-InputBytes([long]$Address,[int]$Count) {
    if ($Address -lt 0x10000 -or $Count -lt 1 -or $Count -gt 256 -or $Address+$Count -gt 0x100000000L) {throw 'Invalid read'}
    $bytes=[byte[]]::new($Count); $got=[UIntPtr]::Zero
    if (![GtaVrInputRead]::ReadProcessMemory($taskHandle,[IntPtr]$Address,$bytes,[UIntPtr]$Count,[ref]$got) -or $got.ToUInt64() -ne $Count) {throw 'Read unavailable'}
    return ,$bytes
}
try {
    $code=Read-InputBytes ($taskBase+0x8F7240-0x400000) 16
    if ($code[0] -ne 0x51 -or $code[1] -ne 0xB9 -or $code[6] -ne 0xE8) {throw 'Input updater signature mismatch'}
    $manager=[BitConverter]::ToUInt32($code,2)
    $header=Read-InputBytes ($manager+0x8C) 20
    $count=[BitConverter]::ToUInt32($header,16)
    if ($count -gt 4) {throw 'Input manager count exceeds verified bound'}
    $devices=@(for($i=0;$i -lt $count;$i++) {
        $device=[BitConverter]::ToUInt32($header,$i*4)
        $table=$null; $target=$null; $errorText=$null
        try {
            if ($device) {
                $table=[BitConverter]::ToUInt32((Read-InputBytes $device 4),0)
                $target=[BitConverter]::ToUInt32((Read-InputBytes ($table+0x78) 4),0)
            }
        } catch {$errorText=$_.Exception.Message}
        [pscustomobject]@{index=$i;device=('{0:X8}' -f $device);vtable=if($null -ne $table){'{0:X8}' -f $table}else{$null};slot78=if($null -ne $target){'{0:X8}' -f $target}else{$null};error=$errorText}
    })
    [pscustomobject]@{pid=$GameProcessId;time=(Get-Date).ToString('o');moduleBase=('{0:X8}' -f $taskBase);manager=('{0:X8}' -f $manager);snapshotAtomic=$false;devices=$devices} | ConvertTo-Json -Depth 4
} finally {[void][GtaVrInputRead]::CloseHandle($taskHandle)}
