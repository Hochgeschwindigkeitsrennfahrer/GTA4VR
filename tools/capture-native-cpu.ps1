param(
    [ValidatePattern('^[a-zA-Z0-9_-]{1,64}$')][string]$Label='street',
    [ValidateRange(10,45)][int]$Seconds=30,
    [int]$GameProcessId=0
)
$ErrorActionPreference='Stop'
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$principal=[Security.Principal.WindowsPrincipal]::new($identity)
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Native CPU sampling requires an elevated PowerShell. No recording was started.'
}
$recorder=(Get-Command wpr.exe -ErrorAction Stop).Source
$recordingStatus=& $recorder -status
if($LASTEXITCODE -ne 0 -or ($recordingStatus -join "`n") -notmatch 'WPR is not recording') {
    throw 'WPR is already recording or its status cannot be confirmed. Existing recordings were left untouched.'
}
$client=Join-Path $PSScriptRoot 'game-command.ps1'
$before=& $client status -GameProcessId $GameProcessId
$profilerBefore=& $client profile-status -GameProcessId $before.pid
if($profilerBefore.active) {
    throw 'Stop the in-game profiler before native CPU recording so its instrumentation does not contaminate the trace.'
}
if(-not $before.stereo.ready -or $before.openxr.sessionState -ne 'FOCUSED') {
    throw 'Keep the headset tracked with focused stereo before starting this capture.'
}
$game=Get-Process -Id $before.pid
$gameDirectory=Split-Path $game.MainModule.FileName -Parent
$installedAsi=Join-Path $gameDirectory 'plugins/GTAIV.EFLC.FusionFix.asi'
$installedAsiHash=if(Test-Path -LiteralPath $installedAsi) {
    (Get-FileHash -LiteralPath $installedAsi -Algorithm SHA256).Hash
} else {$null}
$installedVulkanDll=Join-Path $gameDirectory 'vulkan.dll'
$installedVulkanHash=if(Test-Path -LiteralPath $installedVulkanDll) {
    (Get-FileHash -LiteralPath $installedVulkanDll -Algorithm SHA256).Hash
} else {$null}
$outputDirectory=Join-Path (Split-Path $PSScriptRoot -Parent) 'build/profiles/native'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$stem=Join-Path $outputDirectory ($Label+'-'+[DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss'))
$metadata=[ordered]@{
    label=$Label; capturedAt=[DateTimeOffset]::Now.ToString('o'); seconds=$Seconds
    before=$before; profilerBefore=$profilerBefore; moduleBase=$game.MainModule.BaseAddress.ToInt64()
    gameVersion=$game.MainModule.FileVersionInfo.FileVersion
    installedAsiSHA256=$installedAsiHash
    installedVulkanDllSHA256=$installedVulkanHash
    backendIdentityNote='The DLL hash identifies the installed file; confirm loaded backend/build through the fresh DXVK log and ETL image rundown.'
    moduleEnumerationNote='PowerShell may omit x86 modules under WOW64; use ETL image rundown for attribution.'
    frameCounterNote='before/after include setup and WPR stop. Use recordingStart/recordingEnd and their query bounds for the recording interval; do not divide before/after frame deltas by seconds.'
    qpcFrequency=[Diagnostics.Stopwatch]::Frequency
    modules=@($game.Modules | ForEach-Object {
        [ordered]@{name=$_.ModuleName; base=$_.BaseAddress.ToInt64(); size=$_.ModuleMemorySize}
    })
}
function Read-RecordingStatus {
    $queryBefore=[Diagnostics.Stopwatch]::GetTimestamp()
    $utcBefore=[DateTimeOffset]::UtcNow.ToString('o')
    $result=& $client status -GameProcessId $before.pid
    [ordered]@{status=$result; queryBeforeQpc=$queryBefore; queryAfterQpc=[Diagnostics.Stopwatch]::GetTimestamp(); utcBefore=$utcBefore; utcAfter=[DateTimeOffset]::UtcNow.ToString('o')}
}
$metadata.wprStartBeforeQpc=[Diagnostics.Stopwatch]::GetTimestamp()
& $recorder -start CPU -filemode
if($LASTEXITCODE -ne 0) {throw 'WPR CPU capture failed to start; no existing recording will be stopped.'}
$metadata.wprStartAfterQpc=[Diagnostics.Stopwatch]::GetTimestamp()
try {
    Write-Output "Recording CPU samples for $Seconds seconds. Hold the game view."
    $metadata.recordingStart=Read-RecordingStatus
    Start-Sleep -Seconds $Seconds
} finally {
    try {$metadata.recordingEnd=Read-RecordingStatus}
    catch {$metadata.recordingCounterError=$_.Exception.Message}
    finally {
        $metadata.wprStopBeforeQpc=[Diagnostics.Stopwatch]::GetTimestamp()
        & $recorder -stop ($stem+'.etl')
        if($LASTEXITCODE -ne 0) {throw 'WPR capture stop failed. Inspect wpr -status before starting another recording.'}
        $metadata.wprStopAfterQpc=[Diagnostics.Stopwatch]::GetTimestamp()
    }
}
$metadata.after=& $client status -GameProcessId $before.pid
$metadata.profilerAfter=& $client profile-status -GameProcessId $before.pid
$metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath ($stem+'.json') -Encoding UTF8
Write-Output "Saved $stem.etl and metadata locally; keep both out of Git."
