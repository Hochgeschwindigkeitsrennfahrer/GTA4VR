param(
    [Parameter(Mandatory=$true)][string]$Label,
    [int]$GameProcessId=0,
    [string]$OutputDirectory='build/profiles',
    [switch]$CpuOnly,
    [switch]$NoFlushPoll,
    [switch]$NoAO,
    [switch]$Constants,
    [switch]$Draws,
    [switch]$DrawForwarding,
    [switch]$TextureLinear,
    [switch]$BroadVisibility,
    [switch]$States
)
$ErrorActionPreference='Stop'
if($BroadVisibility -and ($TextureLinear -or $DrawForwarding -or $Draws -or $States -or $Constants -or $NoAO -or $NoFlushPoll)) {throw 'Use one diagnostic override per capture.'}
if($TextureLinear -and ($DrawForwarding -or $Draws -or $States -or $Constants -or $NoAO -or $NoFlushPoll)) {throw 'Use one diagnostic override per capture.'}
if($DrawForwarding -and ($Draws -or $States -or $Constants -or $NoAO -or $NoFlushPoll)) {throw 'Use one diagnostic override per capture.'}
if($NoAO -and $NoFlushPoll) {throw 'Use one diagnostic override per capture.'}
if($Constants -and ($NoAO -or $NoFlushPoll)) {throw 'Use one diagnostic override per capture.'}
if($Draws -and ($Constants -or $NoAO -or $NoFlushPoll)) {throw 'Use one diagnostic override per capture.'}
if($States -and ($Draws -or $Constants -or $NoAO -or $NoFlushPoll)) {throw 'Use one diagnostic override per capture.'}
if ($Label -notmatch '^[a-zA-Z0-9_-]{1,64}$') {throw 'Label must contain 1-64 letters, numbers, underscores or hyphens.'}
$client=Join-Path $PSScriptRoot 'game-command.ps1'
$before=& $client status -GameProcessId $GameProcessId
$GameProcessId=$before.pid
if ($before.openxr.sessionState -ne 'FOCUSED' -or -not $before.stereo.ready) {
    throw 'Capture requires focused OpenXR stereo; put on the tracked headset first.'
}
$startCommand=if($BroadVisibility){'profile-start-cpu-broadvisibility'}elseif($TextureLinear){'profile-start-cpu-texturelinear'}elseif($DrawForwarding){'profile-start-cpu-drawforwarding'}elseif($States){'profile-start-cpu-states'}elseif($Draws){'profile-start-cpu-draws'}elseif($Constants){'profile-start-cpu-constants'}elseif($NoAO){'profile-start-cpu-noao'}elseif($NoFlushPoll){'profile-start-cpu-noflush'}elseif($CpuOnly){'profile-start-cpu'}else{'profile-start'}
# Coarse thread CPU deltas complement wall-clock spans. Existing threads only;
# a thread which starts/exits during the window is not assigned a zero baseline.
function Get-ThreadCpuSnapshot([int]$TargetProcessId) {
    $snapshot=@{}
    foreach($thread in (Get-Process -Id $TargetProcessId).Threads) {
        try {
            $snapshot[$thread.Id]=[ordered]@{userMs=$thread.UserProcessorTime.TotalMilliseconds; kernelMs=$thread.PrivilegedProcessorTime.TotalMilliseconds}
        } catch { Write-Warning "CPU time unavailable for thread $($thread.Id)." }
    }
    return $snapshot
}
$threadBefore=Get-ThreadCpuSnapshot $GameProcessId
$cpuWindow=[System.Diagnostics.Stopwatch]::StartNew()
$start=& $client $startCommand -GameProcessId $GameProcessId
if (-not $start.ok) {throw 'Profiler unavailable.'}
# Sleep only in this CLI client; the game keeps rendering continuously.
Start-Sleep -Seconds 31
$status=& $client profile-status -GameProcessId $GameProcessId
$threadAfter=Get-ThreadCpuSnapshot $GameProcessId
$cpuWindow.Stop()
$threadCpu=@(foreach($threadId in $threadAfter.Keys) {
    if (-not $threadBefore.ContainsKey($threadId)) {continue}
    $userMs=$threadAfter[$threadId].userMs-$threadBefore[$threadId].userMs
    $kernelMs=$threadAfter[$threadId].kernelMs-$threadBefore[$threadId].kernelMs
    if($userMs -lt 0 -or $kernelMs -lt 0) {continue}
    [ordered]@{id=$threadId; userMs=$userMs; kernelMs=$kernelMs; totalMs=$userMs+$kernelMs; oneCorePercent=100*($userMs+$kernelMs)/$cpuWindow.Elapsed.TotalMilliseconds}
})
$metrics=@('processStub','framePrepare','visibility','eye0Build','eye1Build','eye0Commands','eye1Commands','publish','d3d9Fence','xrWait','xrCopy0','xrCopy1','xrEnd','submissionInterval','desktopPresent','eye0GPU','eye1GPU','xrImageWait0','xrImageWait1','xrCopyFence0','xrCopyFence1','processCallbacks','gameplayLimiter','cutsceneLimiter','loadingLimiter','loadingSafety60','minigameLimiter','runtimePacingBypass','eye0GBuffer','eye1GBuffer','eye0Lighting','eye1Lighting','eye0DrawScene','eye1DrawScene','xrCopyIssue0','xrCopyIssue1','xrQueryCreate0','xrQueryCreate1','xrFlush0','xrFlush1','xrPoll0','xrPoll1','eye0Routing','eye1Routing','frameOutsideEyes','interEyeGap','renderBeforeXRWait','xrBegin','xrLocateViews','xrQueueBegin','xrQueueAcquire','xrQueueRelease','xrQueueEnd','xrFrameWall','posePredictionLag','ambientOcclusion','eye0VertexConstants','eye1VertexConstants','eye0PixelConstants','eye1PixelConstants')
$results=foreach($metric in $metrics) {
    $result=& $client "profile-metric $metric" -GameProcessId $GameProcessId
    if (-not $result.ok -or $result.active -or $result.epoch -ne $start.epoch) {throw 'Capture changed or metric unavailable.'}
    $result
}
$constantResults=& $client profile-constants -GameProcessId $GameProcessId
if (-not $constantResults.ok -or $constantResults.epoch -ne $start.epoch) {throw 'Capture changed or constant observer unavailable.'}
$drawResults=@(foreach($eye in 0..1) {foreach($stage in 1..3) {
    $drawResult=& $client "profile-draws $eye $stage" -GameProcessId $GameProcessId
    if(-not $drawResult.ok -or $drawResult.active -or $drawResult.epoch -ne $start.epoch) {throw 'Capture changed or draw observer unavailable.'}
    $drawResult
}})
$stateResults=@(foreach($eye in 0..1) {foreach($kind in 0..9) {
    $stateResult=& $client "profile-states $eye $kind" -GameProcessId $GameProcessId
    if(-not $stateResult.ok -or $stateResult.active -or $stateResult.epoch -ne $start.epoch) {throw 'Capture changed or state observer unavailable.'}
    $stateResult
}})
$after=& $client status -GameProcessId $GameProcessId
$comparisonProblems=@(
    if(-not $after.ok -or $after.pid -ne $before.pid) {'Game session changed.'}
    if($after.openxr.sessionState -ne 'FOCUSED') {'OpenXR focus changed.'}
    if(-not $after.stereo.ready -or $after.openxr.failed) {'Stereo is no longer ready.'}
    if($after.openxr.graphicsBackend -ne $before.openxr.graphicsBackend) {'Graphics backend changed.'}
    if($after.stereo.commandBufferOverflows -ne $before.stereo.commandBufferOverflows) {'Render command buffer overflowed.'}
    if(@($results | Where-Object dropped -gt 0).Count) {'Metric samples were dropped.'}
)
$validComparison=$comparisonProblems.Count -eq 0
if(-not $validComparison) {Write-Warning ('Diagnostic capture only: '+($comparisonProblems -join ' '))}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$output=Join-Path $OutputDirectory "$Label.json"
[ordered]@{label=$Label; startCommand=$startCommand; capturedAt=[DateTimeOffset]::Now.ToString('o'); validComparison=$validComparison; comparisonProblems=$comparisonProblems; before=$before; capture=$status; metrics=@($results); constants=$constantResults; draws=$drawResults; states=$stateResults; after=$after; threadCpuWindowMs=$cpuWindow.Elapsed.TotalMilliseconds; threadCpu=$threadCpu} |
    ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath $output
$results | Format-Table metric,samples,medianMs,p95Ms,p99Ms,maxMs,dropped -AutoSize
Write-Output "Saved $output"
if(-not $validComparison) {throw 'Capture saved for diagnosis; exclude it from performance comparisons.'}
