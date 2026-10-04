param([Parameter(Mandatory=$true)][int]$GameProcessId, [switch]$CollisionShape)
$ErrorActionPreference = 'Stop'
# Read-only CE 1.2.0.59 diagnostic. No process writes, calls or suspension.
if (-not ('GtaVrPlayerRead' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GtaVrPlayerRead {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint rights, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr count, out UIntPtr read);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
}
$taskProcess = Get-Process -Id $GameProcessId
if ($taskProcess.ProcessName -ne 'GTAIV') { throw 'Expected GTAIV.exe' }
$taskModule = $taskProcess.Modules | Where-Object ModuleName -eq 'GTAIV.exe' | Select-Object -First 1
$taskBase = $taskModule.BaseAddress.ToInt64()
$taskHandle = [GtaVrPlayerRead]::OpenProcess(0x1010, $false, $GameProcessId)
if ($taskHandle -eq [IntPtr]::Zero) { throw 'Cannot open process for read' }
function Read-GameBytes([long]$Address, [int]$Count) {
    if ($Address -lt 0x10000 -or $Count -le 0 -or $Address + $Count -gt 0x100000000L -or $Count -gt 4096) { throw 'Invalid bounded read' }
    $taskBytes = [byte[]]::new($Count)
    $taskRead = [UIntPtr]::Zero
    if (-not [GtaVrPlayerRead]::ReadProcessMemory($taskHandle, [IntPtr]$Address, $taskBytes, [UIntPtr]$Count, [ref]$taskRead) -or $taskRead.ToUInt64() -ne $Count) { throw ('Read failed at {0:X}' -f $Address) }
    return ,$taskBytes
}
try {
    $taskLookup = Read-GameBytes ($taskBase + 0x8D20E0 - 0x400000) 39
    if ([BitConverter]::ToString($taskLookup[0..8]) -ne '8B-44-24-04-85-C0-75-18-A1' -or [BitConverter]::ToString($taskLookup[32..38]) -ne '8B-80-98-05-00-00-C3') { throw 'Unsupported player lookup signature' }
    $taskIndexAddress = [BitConverter]::ToUInt32($taskLookup,9)
    $taskTableAddress = [BitConverter]::ToUInt32($taskLookup,21)
    $taskIndex = [BitConverter]::ToInt32((Read-GameBytes $taskIndexAddress 4),0)
    if ($taskIndex -lt 0 -or $taskIndex -gt 31) { throw 'No valid local player slot' }
    $taskInfo = [BitConverter]::ToUInt32((Read-GameBytes ($taskTableAddress + 4*$taskIndex) 4),0)
    $taskPed = [BitConverter]::ToUInt32((Read-GameBytes ($taskInfo + 0x598) 4),0)
    $taskHeader = Read-GameBytes $taskPed 256
    $taskVtable = [BitConverter]::ToUInt32($taskHeader,0)
    $taskVisibilityMethod = [BitConverter]::ToUInt32((Read-GameBytes ($taskVtable+0x30) 4),0)
    'candidate visibility virtual +0x30: runtime=0x{0:X8} Ghidra=0x{1:X8}' -f $taskVisibilityMethod,($taskVisibilityMethod-$taskBase+0x400000)
    $taskRenderMethod = [BitConverter]::ToUInt32((Read-GameBytes ($taskVtable+0x8C) 4),0)
    'candidate render virtual +0x8C: runtime=0x{0:X8} Ghidra=0x{1:X8}' -f $taskRenderMethod,($taskRenderMethod-$taskBase+0x400000)
    # Mirror the read-only collider lookup at CE VA 0x00A31AC0. Never write
    # through these pointers; report candidates for disassembly only.
    try {
    $taskInstance=[BitConverter]::ToUInt32($taskHeader,0x38)
    if(-not $taskInstance) { throw 'No physics instance' }
    $taskPhysicsIndex=[BitConverter]::ToUInt16((Read-GameBytes ($taskInstance+8) 2),0)
    $taskPhysicsManager=[BitConverter]::ToUInt32((Read-GameBytes ($taskBase+0x12B9C7C-0x400000) 4),0)
    $taskPhysicsLevel=[BitConverter]::ToUInt32((Read-GameBytes ($taskPhysicsManager+4) 4),0)
    $taskPhysicsCount=[BitConverter]::ToUInt16((Read-GameBytes ($taskPhysicsLevel+0x26) 2),0)
    if($taskPhysicsIndex -ne 0xFFFF -and $taskPhysicsIndex -lt $taskPhysicsCount) {
        $taskPhysicsFlagsTable=[BitConverter]::ToUInt32((Read-GameBytes ($taskPhysicsLevel+0x70) 4),0)
        $taskPhysicsFlags=[BitConverter]::ToUInt32((Read-GameBytes ($taskPhysicsFlagsTable+8*$taskPhysicsIndex+4) 4),0)
        if(($taskPhysicsFlags -band 3) -ne 0) { throw 'Physics instance has no active collider' }
        $taskColliderTable=[BitConverter]::ToUInt32((Read-GameBytes ($taskPhysicsLevel+0x80) 4),0)
        $taskCollider=[BitConverter]::ToUInt32((Read-GameBytes ($taskColliderTable+4*$taskPhysicsIndex) 4),0)
        if($taskCollider) {
            $taskColliderVtable=[BitConverter]::ToUInt32((Read-GameBytes $taskCollider 4),0)
            $taskVelocityMethod=[BitConverter]::ToUInt32((Read-GameBytes ($taskColliderVtable+0xA8) 4),0)
            'candidate collider=0x{0:X8} velocity virtual +0xA8 Ghidra=0x{1:X8}' -f $taskCollider,($taskVelocityMethod-$taskBase+0x400000)
            $taskForceMethod=[BitConverter]::ToUInt32((Read-GameBytes ($taskColliderVtable+0x84) 4),0)
            'candidate collider force virtual +0x84 VA=0x{0:X8}' -f ($taskForceMethod-$taskBase+0x400000)
        }
    }
    $taskArchetype=[BitConverter]::ToUInt32((Read-GameBytes ($taskInstance+4) 4),0)
    $taskArchetypeVtable=[BitConverter]::ToUInt32((Read-GameBytes $taskArchetype 4),0)
    $taskMassMethod=[BitConverter]::ToUInt32((Read-GameBytes ($taskArchetypeVtable+0x24) 4),0)
    'candidate physics archetype virtual +0x24 VA=0x{0:X8}' -f ($taskMassMethod-$taskBase+0x400000)
    if($CollisionShape) {
        # Candidate archetype bound pointer; inspect the native layout before
        # treating any dimensions as a collision capsule. This is read-only.
        $taskBound=[BitConverter]::ToUInt32((Read-GameBytes ($taskArchetype+0xC) 4),0)
        $taskBoundHeader=Read-GameBytes $taskBound 160
        $taskBoundVtable=[BitConverter]::ToUInt32($taskBoundHeader,0)
        'candidate bound=0x{0:X8} vtableVA=0x{1:X8}' -f $taskBound,($taskBoundVtable-$taskBase+0x400000)
        for($taskShapeOffset=0;$taskShapeOffset -lt 160;$taskShapeOffset+=16) {
            $taskShapeValues=0..3 | ForEach-Object { [BitConverter]::ToSingle($taskBoundHeader,$taskShapeOffset+4*$_).ToString('G6',[Globalization.CultureInfo]::InvariantCulture) }
            'bound+0x{0:X2} raw={1} floats={2}' -f $taskShapeOffset,[BitConverter]::ToString($taskBoundHeader[$taskShapeOffset..($taskShapeOffset+15)]),($taskShapeValues -join ',')
        }
        # CE phBoundComposite virtual+0x38 walks +0x80 child pointers and
        # +0x84 local matrices, bounded by the ushort at +0x92. Check RTTI
        # before following this layout; never infer a primitive from its AABB.
        $taskLocator=[BitConverter]::ToUInt32((Read-GameBytes ($taskBoundVtable-4) 4),0)
        $taskType=[BitConverter]::ToUInt32((Read-GameBytes ($taskLocator+12) 4),0)
        $taskTypeName=[Text.Encoding]::ASCII.GetString((Read-GameBytes ($taskType+8) 80)).Split([char]0)[0]
        'bound RTTI={0}' -f $taskTypeName
        if($taskTypeName -eq '.?AVphBoundComposite@rage@@') {
            $taskChildCount=[BitConverter]::ToUInt16($taskBoundHeader,0x92)
            if($taskChildCount -gt 16) { throw 'Unexpected composite child count' }
            $taskChildren=[BitConverter]::ToUInt32($taskBoundHeader,0x80)
            $taskChildMatrices=[BitConverter]::ToUInt32($taskBoundHeader,0x84)
            for($taskChildIndex=0;$taskChildIndex -lt $taskChildCount;$taskChildIndex++) {
                $taskChild=[BitConverter]::ToUInt32((Read-GameBytes ($taskChildren+4*$taskChildIndex) 4),0)
                if(-not $taskChild) { continue }
                $taskChildBytes=Read-GameBytes $taskChild 128
                $taskChildVtable=[BitConverter]::ToUInt32($taskChildBytes,0)
                $taskChildLocator=[BitConverter]::ToUInt32((Read-GameBytes ($taskChildVtable-4) 4),0)
                $taskChildType=[BitConverter]::ToUInt32((Read-GameBytes ($taskChildLocator+12) 4),0)
                $taskChildName=[Text.Encoding]::ASCII.GetString((Read-GameBytes ($taskChildType+8) 80)).Split([char]0)[0]
                'child[{0}] RTTI={1} vtableVA=0x{2:X8}' -f $taskChildIndex,$taskChildName,($taskChildVtable-$taskBase+0x400000)
                'child bytes={0}' -f [BitConverter]::ToString($taskChildBytes)
                $taskChildMatrix=Read-GameBytes ($taskChildMatrices+64*$taskChildIndex) 64
                for($taskChildRow=0;$taskChildRow -lt 4;$taskChildRow++) {
                    'child matrix[{0}]={1}' -f $taskChildRow,((0..3 | ForEach-Object { [BitConverter]::ToSingle($taskChildMatrix,16*$taskChildRow+4*$_).ToString('G6',[Globalization.CultureInfo]::InvariantCulture) }) -join ',')
                }
            }
        }
    }
    } catch { 'candidate collider unavailable: ' + $_.Exception.Message }
    'PID={0} moduleBase=0x{1:X8} playerInfo=0x{2:X8} ped=0x{3:X8}' -f $GameProcessId,$taskBase,$taskInfo,$taskPed
    # CPed's verified virtual +0xCC routine calls the inline component at
    # ped+0xBB0 through virtual+8 (LEA ECX, not a component-pointer load).
    # Static RTTI identifies this as CIkManager, not the walking motor.
    try {
        $taskComponent=$taskPed+0xBB0
        $taskComponentVtable=[BitConverter]::ToUInt32((Read-GameBytes $taskComponent 4),0)
        $taskComponentMethod=[BitConverter]::ToUInt32((Read-GameBytes ($taskComponentVtable+8) 4),0)
        'ped+0xBB0 IK component=0x{0:X8} virtual+8 Ghidra=0x{1:X8}' -f $taskComponent,($taskComponentMethod-$taskBase+0x400000)
    } catch { 'ped+0xBB0 candidate unavailable: ' + $_.Exception.Message }
    # Constructor/RTTI evidence identifies ped+0xA80 as the move-blend owner.
    # Read back its vtable and owner before considering any motion integration.
    try {
        $taskMoveBlend=[BitConverter]::ToUInt32((Read-GameBytes ($taskPed+0xA80) 4),0)
        $taskMoveBytes=Read-GameBytes $taskMoveBlend 40
        $taskMoveVtable=[BitConverter]::ToUInt32($taskMoveBytes,0)
        $taskMoveOwner=[BitConverter]::ToUInt32($taskMoveBytes,0x24)
        $taskMoveMethod=[BitConverter]::ToUInt32((Read-GameBytes ($taskMoveVtable+0x10) 4),0)
        'ped+0xA80 moveBlend=0x{0:X8} vtableVA=0x{1:X8} ownerMatches={2} virtual+0x10 VA=0x{3:X8}' -f $taskMoveBlend,($taskMoveVtable-$taskBase+0x400000),($taskMoveOwner -eq $taskPed),($taskMoveMethod-$taskBase+0x400000)
    } catch { 'ped+0xA80 move-blend unavailable: ' + $_.Exception.Message }
    for ($taskOffset=0; $taskOffset -lt 128; $taskOffset+=16) {
        $taskValues = 0..3 | ForEach-Object { [BitConverter]::ToSingle($taskHeader,$taskOffset+4*$_).ToString('G6',[Globalization.CultureInfo]::InvariantCulture) }
        'ped+0x{0:X2} raw={1} floats={2}' -f $taskOffset,[BitConverter]::ToString($taskHeader[$taskOffset..($taskOffset+15)]),($taskValues -join ',')
    }
    $taskMatrix = [BitConverter]::ToUInt32($taskHeader,0x20)
    try {
        $taskMatrixBytes = Read-GameBytes $taskMatrix 64
        'candidate ped+0x20 pointer=0x{0:X8} (not yet a verified transform)' -f $taskMatrix
        for ($taskRow=0;$taskRow -lt 4;$taskRow++) {
            (0..3 | ForEach-Object {[BitConverter]::ToSingle($taskMatrixBytes,16*$taskRow+4*$_).ToString('G6',[Globalization.CultureInfo]::InvariantCulture)}) -join ','
        }
    } catch { 'ped+0x20 candidate unavailable: ' + $_.Exception.Message }
} finally { [void][GtaVrPlayerRead]::CloseHandle($taskHandle) }
