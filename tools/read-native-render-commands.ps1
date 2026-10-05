param([Parameter(Mandatory=$true)][int]$GameProcessId)
$ErrorActionPreference='Stop'
# Diagnostic snapshot only, never a timing capture. No suspension or writes.
. "$PSScriptRoot/read-input-devices.ps1" -GameProcessId $GameProcessId | Out-Null
if (-not ('GtaVrCommandSnapshot' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GtaVrCommandSnapshot {
 [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr process,IntPtr address,byte[] bytes,UIntPtr size,out UIntPtr read);
 public sealed class Packet {public byte[] before, bytes, after;}
 static byte[] Read(IntPtr handle, long address, int size) {
  if(address<0x10000 || size<1 || size>0x800000 || address+size>0x100000000L) throw new Exception("Invalid bounded snapshot read");
  var bytes=new byte[size]; UIntPtr got;
  if(!ReadProcessMemory(handle,new IntPtr(address),bytes,new UIntPtr((uint)size),out got) || got.ToUInt64()!=(ulong)size) throw new Exception("Snapshot read unavailable");
  return bytes;
 }
 public static Packet Capture(IntPtr handle, long manager, uint capacity) {
  var p=new Packet();p.before=Read(handle,manager,36);
  uint slot=BitConverter.ToUInt32(p.before,16), length=BitConverter.ToUInt32(p.before,24);
  if(slot>1 || length<8 || length>capacity || p.before[8+slot]!=0) throw new Exception("No bounded readable prefix");
  p.bytes=Read(handle,BitConverter.ToUInt32(p.before,(int)slot*4),(int)length);
  p.after=Read(handle,manager,36);return p;
 }
}
'@
}
$taskHandle=[GtaVrInputRead]::OpenProcess(0x1010,$false,$GameProcessId)
if ($taskHandle -eq [IntPtr]::Zero) {throw 'Cannot open game for read'}
try {
    $code=Read-InputBytes ($taskBase+0x8DC3A0-0x400000) 80
    if ($code[0] -ne 0x53 -or $code[18] -ne 0xB9 -or $code[64] -ne 0x81 -or $code[65] -ne 0xF9) {throw 'Command allocator signature mismatch'}
    $manager=[BitConverter]::ToUInt32($code,19)
    $capacity=[BitConverter]::ToUInt32($code,66)
    if ($capacity -ne 0x200000 -and $capacity -ne 0x800000) {throw 'Unrecognized command capacity'}
    # Keep the three RPM calls together; PowerShell dispatch between them can
    # cross a phase cursor reset even when the underlying copy is very short.
    $packet=[GtaVrCommandSnapshot]::Capture($taskHandle,$manager,$capacity)
    $header=$packet.before
    $slot=[BitConverter]::ToUInt32($header,16)
    $length=[BitConverter]::ToUInt32($header,24)
    if ($slot -gt 1 -or $header[8+$slot] -ne 0 -or $length -lt 8 -or $length -gt $capacity) {throw 'No bounded readable command prefix'}
    $address=[BitConverter]::ToUInt32($header,$slot*4)
    if ($address -lt 0x10000 -or [long]$address+$length -gt 0x100000000L) {throw 'Invalid command buffer'}
    $bytes=$packet.bytes
    $after=$packet.after
    # The reader may advance through the same immutable buffer while copying.
    if ([BitConverter]::ToUInt32($after,16) -ne $slot -or [BitConverter]::ToUInt32($after,24) -lt $length -or [BitConverter]::ToUInt32($after,$slot*4) -ne $address -or $after[8+$slot] -ne 0) {throw 'Command prefix changed during snapshot; retry'}
    $counts=@{}; $models=@{}; $offset=0; $commandCount=0
    while ($offset -lt $length) {
        if ($offset+8 -gt $length) {throw 'Partial command header'}
        $table=[BitConverter]::ToUInt32($bytes,$offset)
        $word=[BitConverter]::ToUInt32($bytes,$offset+4)
        $size=(($word -shr 14) -band 0x7FF)*16
        if ($size -lt 8 -or $offset+$size -gt $length) {throw "Invalid or partial command at $offset"}
        $key='{0:X8}' -f $table
        if (!$counts.ContainsKey($key)) {
            $callback=[BitConverter]::ToUInt32((Read-InputBytes ([long]$table+4) 4),0)
            $counts[$key]=[ordered]@{vtable=$key;callbackRuntime=('{0:X8}' -f $callback);callbackAnalysis=if ($callback -ge $taskBase -and $callback -lt $taskBase+$taskModule.ModuleMemorySize){'{0:X8}' -f ($callback-$taskBase+0x400000)}else{$null};commands=0;bytes=0;firstOffset=$offset}
        }
        $counts[$key].commands++; $counts[$key].bytes+=$size
        $kind=$counts[$key].callbackAnalysis
        $modelOffset=if ($kind -eq '008DCBF0') {0x30} elseif ($kind -eq '008DD0B0') {0x58} else {-1}
        if ($modelOffset -ge 0) {
            if ($modelOffset+2 -gt $size) {throw 'Model identifier outside command'}
            $modelId=[BitConverter]::ToUInt16($bytes,$offset+$modelOffset)
            $modelKey="$kind/$modelId"
            if (!$models.ContainsKey($modelKey)) {$models[$modelKey]=[ordered]@{callbackAnalysis=$kind;modelId=$modelId;commands=0;positions=@{};variants=@{}}}
            $models[$modelKey].commands++
            if ($kind -eq '008DCBF0') {
                # Three translation floats; quaternion data may be normalized in place.
                $position=[BitConverter]::ToString($bytes,$offset+0x20,12)
                $models[$modelKey].positions[$position]=$true
                $variant=[int]$bytes[$offset+0x35]
                if (!$models[$modelKey].variants.ContainsKey($variant)) {$models[$modelKey].variants[$variant]=0}
                $models[$modelKey].variants[$variant]++
            }
        }
        $commandCount++; $offset+=$size
    }
    $modelRows=@(foreach($row in $models.Values) {
        [pscustomobject]@{callbackAnalysis=$row.callbackAnalysis;modelId=$row.modelId;commands=$row.commands;distinctTranslationBits=$row.positions.Count;variant35=@(foreach($entry in $row.variants.GetEnumerator()){[pscustomobject]@{value=$entry.Key;commands=$entry.Value}})}
    })
    [pscustomobject]@{pid=$GameProcessId;time=(Get-Date).ToString('o');moduleBase=('{0:X8}' -f $taskBase);readSlot=$slot;prefixBytes=$length;commands=$commandCount;snapshotAtomic=$false;scope='Consumed prefix, not a complete frame; slot/cursor checked before and after copy, ABA reuse remains possible';callbacks=@($counts.Values | ForEach-Object {[pscustomobject]$_} | Sort-Object commands -Descending);models=@($modelRows | Sort-Object commands -Descending)} | ConvertTo-Json -Depth 6
} finally {[void][GtaVrInputRead]::CloseHandle($taskHandle)}
