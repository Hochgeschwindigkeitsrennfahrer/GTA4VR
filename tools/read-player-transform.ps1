param([Parameter(Mandatory=$true)][int]$GameProcessId, [switch]$CollisionShape, [switch]$Rig, [switch]$FingerRig, [switch]$BodyRig)
$ErrorActionPreference = 'Stop'
if($FingerRig -or $BodyRig) {$Rig=$true}
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
    if($BodyRig) {
        # CE post-update call+10 -> fixed GetPad (B8 imm32 C3), read only.
        $taskPadCall=$taskBase+0x8F4D00-0x400000+10
        $taskCallBytes=Read-GameBytes $taskPadCall 5
        if($taskCallBytes[0] -eq 0xE8) {
            $taskGetter=$taskPadCall+5+[BitConverter]::ToInt32($taskCallBytes,1)
            $taskGetterBytes=Read-GameBytes $taskGetter 6
            if($taskGetterBytes[0] -eq 0xB8 -and $taskGetterBytes[5] -eq 0xC3) {
                $taskPad=[BitConverter]::ToUInt32($taskGetterBytes,1)
                foreach($taskAxis in @(12,13,14,15,90,91)) {
                    $taskValue=Read-GameBytes ($taskPad+0x2698+16*$taskAxis) 8
                    'pad axis {0}: current={1}' -f $taskAxis,($taskValue[6] -bxor $taskValue[4])
                }
            }
        }
    }
    $taskHeader = Read-GameBytes $taskPed 256
    $taskHeldWeapon = [BitConverter]::ToUInt32((Read-GameBytes ($taskPed+0xB30) 4),0)
    if ($taskHeldWeapon) {
        $taskHeldModel = [BitConverter]::ToInt16((Read-GameBytes ($taskHeldWeapon+0x2E) 2),0)
        $taskHeldState = [BitConverter]::ToInt32((Read-GameBytes ($taskHeldWeapon+0x1304) 4),0)
        'task entity candidate CPed+B30=0x{0:X8} model={1} state+1304={2}' -f $taskHeldWeapon,$taskHeldModel,$taskHeldState
    } else { 'task entity candidate CPed+B30=null (not equipped-weapon evidence)' }
    $taskWeaponManager=Read-GameBytes ($taskPed+0x2B0) 160
    $taskWeaponSlot=[BitConverter]::ToInt32($taskWeaponManager,0)
    $taskWeaponEntity=[BitConverter]::ToUInt32($taskWeaponManager,0x14)
    if($taskWeaponEntity) {
        $taskWeaponObject=[BitConverter]::ToUInt32((Read-GameBytes ($taskWeaponEntity+0x25C) 4),0)
        if($taskWeaponObject) {
            'weapon manager slot={0} entity=0x{1:X8} CWeapon=0x{2:X8} type={3}' -f $taskWeaponSlot,$taskWeaponEntity,$taskWeaponObject,[BitConverter]::ToInt32((Read-GameBytes ($taskWeaponObject+0x18) 4),0)
            'weapon entity model={0} matrix=0x{1:X8}' -f [BitConverter]::ToInt16((Read-GameBytes ($taskWeaponEntity+0x2E) 2),0),[BitConverter]::ToUInt32((Read-GameBytes ($taskWeaponEntity+0x20) 4),0)
            if($Rig) {
                $taskWeaponMatrix=[BitConverter]::ToUInt32((Read-GameBytes ($taskWeaponEntity+0x20) 4),0)
                $taskWeaponMatrixBytes=Read-GameBytes $taskWeaponMatrix 64
                for($taskRow=0;$taskRow -lt 4;$taskRow++) {
                    'weapon matrix row{0}: {1}' -f $taskRow,((0..3 | ForEach-Object {[BitConverter]::ToSingle($taskWeaponMatrixBytes,16*$taskRow+4*$_).ToString('G6',[Globalization.CultureInfo]::InvariantCulture)}) -join ',')
                }
                $taskWeaponModelIndex=[BitConverter]::ToInt16((Read-GameBytes ($taskWeaponEntity+0x2E) 2),0)
                $taskWeaponModel=[BitConverter]::ToUInt32((Read-GameBytes ($taskBase+0x1295CD8-0x400000+4*$taskWeaponModelIndex) 4),0)
                'weapon model=0x{0:X8} muzzle bone+70={1}' -f $taskWeaponModel,[BitConverter]::ToInt32((Read-GameBytes ($taskWeaponModel+0x70) 4),0)
                try {
                    $taskWeaponFrag=[BitConverter]::ToUInt32((Read-GameBytes ($taskWeaponModel+8) 4),0)
                    if($taskWeaponFrag) {$taskWeaponDrawable=[BitConverter]::ToUInt32((Read-GameBytes ($taskWeaponFrag+0xB4) 4),0)} else {
                        $taskWeaponDrawPointer=[BitConverter]::ToUInt32((Read-GameBytes ($taskWeaponModel+0xC) 4),0)
                        $taskWeaponDrawable=[BitConverter]::ToUInt32((Read-GameBytes $taskWeaponDrawPointer 4),0)
                    }
                    $taskWeaponHierarchy=[BitConverter]::ToUInt32((Read-GameBytes ($taskWeaponDrawable+0xC) 4),0)
                    $taskWeaponBoneMatrices=[BitConverter]::ToUInt32((Read-GameBytes ($taskWeaponHierarchy+0x10) 4),0)
                    $taskWeaponMuzzleIndex=[BitConverter]::ToInt32((Read-GameBytes ($taskWeaponModel+0x70) 4),0)
                    $taskWeaponMuzzleBytes=Read-GameBytes ($taskWeaponBoneMatrices+64*$taskWeaponMuzzleIndex) 64
                    for($taskRow=0;$taskRow -lt 4;$taskRow++) {
                        'weapon muzzle local row{0}: {1}' -f $taskRow,((0..3 | ForEach-Object {[BitConverter]::ToSingle($taskWeaponMuzzleBytes,16*$taskRow+4*$_).ToString('G6',[Globalization.CultureInfo]::InvariantCulture)}) -join ',')
                    }
                } catch { 'weapon local muzzle unavailable: '+$_.Exception.Message }
            }
        }
    } else { 'weapon manager slot={0}, no equipped entity' -f $taskWeaponSlot }
    if($Rig) {
        $taskModelIndex=[BitConverter]::ToInt16($taskHeader,0x2E)
        if($taskModelIndex -lt 0) { throw 'Invalid ped model index' }
        $taskRigModel=[BitConverter]::ToUInt32((Read-GameBytes ($taskBase+0x1295CD8-0x400000+4*$taskModelIndex) 4),0)
        $taskRigVtable=[BitConverter]::ToUInt32((Read-GameBytes $taskRigModel 4),0)
        $taskRigMap=[BitConverter]::ToUInt32((Read-GameBytes ($taskRigVtable+0x38) 4),0)
        $taskRigSkeleton=[BitConverter]::ToUInt32((Read-GameBytes ($taskPed+0x100) 4),0)
        'rig modelIndex={0} model=0x{1:X8} semanticMapperVA=0x{2:X8} skeleton=0x{3:X8}' -f $taskModelIndex,$taskRigModel,($taskRigMap-$taskBase+0x400000),$taskRigSkeleton
        $taskRigSemanticBytes=Read-GameBytes ($taskRigModel+0x134) 32
        'rig semanticIndices[0..31]={0}' -f ($taskRigSemanticBytes -join ',')
        $taskRigFragment=[BitConverter]::ToUInt32((Read-GameBytes ($taskPed+0x7B4) 4),0)
        if($taskRigFragment) {
            $taskRigFragmentVtable=[BitConverter]::ToUInt32((Read-GameBytes $taskRigFragment 4),0)
            $taskRigFragmentGetter=[BitConverter]::ToUInt32((Read-GameBytes ($taskRigFragmentVtable+0xE0) 4),0)
            'rig fragment=0x{0:X8} skeletonGetterVA=0x{1:X8}' -f $taskRigFragment,($taskRigFragmentGetter-$taskBase+0x400000)
            # Bounded observation of the live CE getter's known Niko layout.
            # Do not apply this layout to other fragment implementations.
            if(($taskRigFragmentGetter-$taskBase+0x400000) -eq 0x005FB8B0) {
                $taskRigState=[BitConverter]::ToUInt32((Read-GameBytes ($taskRigFragment+0x64) 4),0)
                if($taskRigState) { $taskRigSkeleton=[BitConverter]::ToUInt32((Read-GameBytes ($taskRigState+0x160) 4),0) }
            }
        }
        if($taskRigSkeleton) {
            if($FingerRig -or $BodyRig) {
                $taskDefinition=[BitConverter]::ToUInt32((Read-GameBytes ($taskRigSkeleton+4) 4),0)
                $taskDefinitionBytes=Read-GameBytes $taskDefinition 64
                $taskBones=[BitConverter]::ToUInt32($taskDefinitionBytes,0)
                'finger definition header={0}' -f [BitConverter]::ToString($taskDefinitionBytes)
                'finger skeleton header={0}' -f [BitConverter]::ToString((Read-GameBytes $taskRigSkeleton 48))
                $taskBoneIndices=@(45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89)
                if($BodyRig) {
                    $taskBoneCount=[BitConverter]::ToUInt16((Read-GameBytes ($taskRigSkeleton+0x10) 2),0)
                    if($taskBoneCount -lt 1 -or $taskBoneCount -gt 255) {throw 'Invalid rig bone count'}
                    $taskBoneIndices=0..($taskBoneCount-1)
                }
                foreach($taskBone in $taskBoneIndices) {
                    $taskBoneBytes=Read-GameBytes ($taskBones+0xE0*$taskBone) 48
                    $taskNamePointer=[BitConverter]::ToUInt32($taskBoneBytes,0)
                    $taskName=''
                    if($taskNamePointer -ge 0x10000) {
                        try {$taskName=[Text.Encoding]::ASCII.GetString((Read-GameBytes $taskNamePointer 48)).Split([char]0)[0]} catch {}
                    }
                    'finger bone[{0}] nameCandidate={1} header={2}' -f $taskBone,$taskName,[BitConverter]::ToString($taskBoneBytes)
                    if($taskBone -in @(47,48,49,53,54,71,72,73,75,76)) {
                        $taskFullBone=Read-GameBytes ($taskBones+0xE0*$taskBone) 224
                        for($taskOff=0x20;$taskOff -lt 224;$taskOff+=16) {
                            'finger bone[{0}]+{1:X2}={2}' -f $taskBone,$taskOff,((0..3 | ForEach-Object {[BitConverter]::ToSingle($taskFullBone,$taskOff+4*$_).ToString('G6',[Globalization.CultureInfo]::InvariantCulture)}) -join ',')
                        }
                    }
                }
            }
            $taskRigMatrices=[BitConverter]::ToUInt32((Read-GameBytes ($taskRigSkeleton+0x14) 4),0)
            'rig matrixArray=0x{0:X8}; bone indices not assumed from semantic IDs' -f $taskRigMatrices
            if($taskRigMatrices) {
                foreach($taskRigSemantic in @(9,10,11,13,14,15)) {
                    $taskRigBoneIndex=$taskRigSemanticBytes[$taskRigSemantic]
                    if($taskRigBoneIndex -eq 255) { continue }
                    $taskRigMatrix=Read-GameBytes ($taskRigMatrices+64*$taskRigBoneIndex) 64
                    $taskRigXYZ=0..2 | ForEach-Object { [BitConverter]::ToSingle($taskRigMatrix,48+4*$_).ToString('G6',[Globalization.CultureInfo]::InvariantCulture) }
                    'rig semantic={0} boneIndex={1} matrixTranslation={2} (native skeleton space)' -f $taskRigSemantic,$taskRigBoneIndex,($taskRigXYZ -join ',')
                }
            }
        }
    }
    $taskVtable = [BitConverter]::ToUInt32($taskHeader,0)
    if($Rig) {
        $taskRigInstanceGetter=[BitConverter]::ToUInt32((Read-GameBytes ($taskVtable+0xA0) 4),0)
        'rig instanceGetterVA=0x{0:X8}' -f ($taskRigInstanceGetter-$taskBase+0x400000)
    }
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
