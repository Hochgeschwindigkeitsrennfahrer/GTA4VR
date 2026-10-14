param(
    [ValidateSet('Begin','Next','Advance','Spawn','Pass','Problem','Skip','Status','Finish','HeliHere','LeaveRail')][string]$Mode='Status',
    [string]$Model='',
    [string]$Notes='',
    [string]$Origin='build/profiles/vehicle-validation/native-origin.json',
    [string]$State='build/profiles/vehicle-validation/session.json',
    [int]$GameProcessId=0,
    [double]$Distance=12
)
$ErrorActionPreference='Stop'
$client=Join-Path $PSScriptRoot 'game-command.ps1'
function Get-VehicleProfileDirectory([int]$ProcessId) {
    # Query the executable path directly. Windows PowerShell's module list can
    # omit the x86 ASI even while the command bridge is working.
    if(-not ('GtaVrVehicleProcessPath' -as [type])) {
        Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class GtaVrVehicleProcessPath {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int id);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool QueryFullProcessImageName(IntPtr process, uint flags, StringBuilder path, ref uint size);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
    }
    $handle=[GtaVrVehicleProcessPath]::OpenProcess(0x1000,$false,$ProcessId)
    if($handle -eq [IntPtr]::Zero) {throw 'Cannot read running game path'}
    try {
        $path=[Text.StringBuilder]::new(32768)
        [uint32]$size=$path.Capacity
        if(-not [GtaVrVehicleProcessPath]::QueryFullProcessImageName($handle,0,$path,[ref]$size)) {throw 'Cannot read running game path'}
        $image=$path.ToString()
        if([IO.Path]::GetFileName($image) -ine 'GTAIV.exe') {throw 'Expected GTAIV.exe'}
        return Join-Path (Split-Path $image -Parent) 'plugins/VRVehicles'
    } finally {[GtaVrVehicleProcessPath]::CloseHandle($handle) | Out-Null}
}
function Invoke-VehicleOperation([string]$Command) {
    $request=& $client $Command -GameProcessId $GameProcessId
    if(-not $request.ok -or -not $request.accepted) {throw ($request | ConvertTo-Json -Compress)}
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        $result=& $client vehicle-test-status -GameProcessId $GameProcessId
        if($result.id -ne $request.id) {throw 'Vehicle operation changed'}
        if([DateTime]::UtcNow -gt $deadline) {throw 'Vehicle operation timed out'}
    } while($result.pending)
    if(-not $result.valid) {throw $result.error}
    return $result
}
function Set-TestAnchor($Position) {
    $culture=[Globalization.CultureInfo]::InvariantCulture
    $values=@(foreach($key in 'x','y','z','heading') {([double]$Position.$key).ToString('F6',$culture)})
    Invoke-VehicleOperation ('vehicle-test-anchor '+($values -join ' '))
}
function Set-HelicopterPoint($Position) {
    $culture=[Globalization.CultureInfo]::InvariantCulture
    $values=@(foreach($key in 'x','y','z','heading') {([double]$Position.$key).ToString('F6',$culture)})
    Invoke-VehicleOperation ('vehicle-test-heli-point '+($values -join ' '))
}
function Save-TestState {
    $parent=Split-Path $State -Parent
    New-Item -ItemType Directory -Force $parent | Out-Null
    $session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $State -Encoding utf8
}
if($Mode -eq 'Begin') {
    $saved=Get-Content -LiteralPath $Origin -Raw | ConvertFrom-Json
    if($saved.schema -ne 1) {throw 'Unsupported origin snapshot'}
    $ready=Set-TestAnchor $saved.position
    $episodes=@('GTAIV','TLAD','TBoGT')
    if($ready.episode -ge $episodes.Count) {throw 'Unknown active episode'}
    $episode=$episodes[$ready.episode]
    $order=@{car=0;bike=1;heli=2;boat=3;train=4;plane=5}
    $vehicles=@(Import-Csv (Join-Path $PSScriptRoot '../docs/vehicle-catalog.csv') |
        Where-Object episode -eq $episode | Sort-Object @{Expression={$order[$_.type]}},model |
        ForEach-Object {[ordered]@{model=$_.model;hash=$_.hash;type=$_.type;displayKey=$_.display_key;result='pending';notes='';profileSaved=$false}})
    $session=[pscustomobject]@{schema=1;episode=$episode;createdAt=[DateTime]::UtcNow.ToString('o');origin=$saved.position;current='';vehicles=$vehicles}
    Save-TestState
    $session | Select-Object episode,createdAt,origin
    return
}
$session=Get-Content -LiteralPath $State -Raw | ConvertFrom-Json
if($session.schema -ne 1) {throw 'Unsupported vehicle session'}
if($Mode -eq 'HeliHere') {
    if([double]::IsNaN($Distance) -or [double]::IsInfinity($Distance)) {throw 'Distance must be finite'}
    $game=& $client status -GameProcessId $GameProcessId
    $view=& $client audio-status -GameProcessId ([int]$game.pid)
    $location=& (Join-Path $PSScriptRoot 'read-player-transform.ps1') -GameProcessId ([int]$game.pid) -LocationOnly
    if($location.source -ne 'player root') {throw 'Step out before choosing the helicopter spawn'}
    $forward=$view.audio.headForward
    $length=[Math]::Sqrt($forward[0]*$forward[0]+$forward[1]*$forward[1])
    if($game.openxr.sessionState -ne 'FOCUSED' -or $view.audio.poseAgeMs -gt 250 -or $length -lt 0.000001) {throw 'Fresh focused horizontal headset direction required'}
    $heading=[Math]::Atan2(-$forward[0],$forward[1])*180/[Math]::PI
    if($heading -lt 0) {$heading+=360}
    $point=[ordered]@{x=$location.position.x+$Distance*$forward[0]/$length;y=$location.position.y+$Distance*$forward[1]/$length;z=$location.position.z;heading=$heading}
    Set-HelicopterPoint $point | Out-Null
    $session | Add-Member -NotePropertyName helicopterSpawn -NotePropertyValue $point -Force
    Save-TestState
    $point
    return
}
if($Mode -eq 'LeaveRail') {Invoke-VehicleOperation vehicle-test-rail-exit;return}
$advance=$Mode -eq 'Advance'
if($advance) {$Mode='Pass'}
if($Mode -in @('Pass','Problem','Skip')) {
    $entry=@($session.vehicles | Where-Object model -eq $session.current)
    if($entry.Count -ne 1) {throw 'No current test vehicle'}
    $entry[0].result=$Mode.ToLowerInvariant();$entry[0].notes=$Notes
    if($Mode -eq 'Pass') {
        $game=& $client status -GameProcessId $GameProcessId
        $installed=Join-Path (Get-VehicleProfileDirectory ([int]$game.pid)) ($entry[0].hash+'.ini')
        if(Test-Path -LiteralPath $installed) {
            $destination=Join-Path $PSScriptRoot ('../data/plugins/VRVehicles/'+$entry[0].hash+'.ini')
            Copy-Item -LiteralPath $installed -Destination $destination -Force
            $entry[0].profileSaved=$true
        }
    }
    Save-TestState
}
if($advance) {$Mode='Next'}
if($Mode -in @('Next','Spawn')) {
    $native=& $client vehicle-test-status -GameProcessId $GameProcessId
    if(-not $native.anchorValid) {Set-TestAnchor $session.origin | Out-Null}
    $episodes=@('GTAIV','TLAD','TBoGT')
    if($episodes[$native.episode] -ne $session.episode) {throw 'Episode changed; begin a separate session'}
    if($Mode -eq 'Next') {
        if($session.current -and @($session.vehicles | Where-Object { $_.model -eq $session.current -and $_.result -eq 'pending' }).Count) {throw 'Record Pass, Problem or Skip before advancing'}
        $entry=$session.vehicles | Where-Object { $_.result -eq 'pending' -and $_.type -ne 'plane' } | Select-Object -First 1
    } else {$entry=$session.vehicles | Where-Object model -eq $Model | Select-Object -First 1}
    if(-not $entry) {
        if($Mode -eq 'Spawn') {throw 'No matching vehicle model'}
        $testable=@($session.vehicles | Where-Object { $_.type -ne 'plane' })
        $excluded=@($session.vehicles | Where-Object { $_.type -eq 'plane' })
        [ordered]@{status='sequence complete';passed=@($testable | Where-Object result -eq 'pass').Count;testable=$testable.Count;catalogTotal=$session.vehicles.Count;excluded=($excluded.model -join ', ');message='No spawnable vehicles remain. Plane entries require separate testing.'}
        return
    }
    if($entry.type -eq 'heli' -and $session.helicopterSpawn) {Set-HelicopterPoint $session.helicopterSpawn | Out-Null}
    $result=Invoke-VehicleOperation ('vehicle-test-spawn '+$entry.hash)
    $session.current=$entry.model;Save-TestState
    [ordered]@{model=$entry.model;type=$entry.type;hash=$entry.hash;label=$result.label;vehicle=$result.vehicle;position=$result.position;progress=(@($session.vehicles | Where-Object result -ne 'pending').Count.ToString()+'/'+$session.vehicles.Count)}
    return
}
if($Mode -eq 'Finish') {Invoke-VehicleOperation vehicle-test-remove;return}
[ordered]@{episode=$session.episode;current=$session.current;complete=@($session.vehicles | Where-Object result -ne 'pending').Count;total=$session.vehicles.Count;vehicles=$session.vehicles}
