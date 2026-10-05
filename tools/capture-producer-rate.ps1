param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-zA-Z0-9_-]{1,64}$')][string]$Label,
    [int]$GameProcessId=0,
    [ValidateRange(3,8)][int]$Seconds=5
)
$ErrorActionPreference='Stop'
$client=Join-Path $PSScriptRoot 'game-command.ps1'
$session=& $client status -GameProcessId $GameProcessId
$GameProcessId=$session.pid
$profile=& $client profile-status -GameProcessId $GameProcessId
if(-not $session.stereo.ready -or $session.openxr.sessionState -ne 'FOCUSED' -or $profile.active){throw 'Requires focused stereo and profiler off.'}
$omissionBefore=& $client cpu-omit-status -GameProcessId $GameProcessId
$start=& $client 'producer-test on' -GameProcessId $GameProcessId
if(-not $start.ok){throw 'Producer test unavailable.'}
try {
    Start-Sleep -Milliseconds 300
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $before=& $client producer-test-status -GameProcessId $GameProcessId
    $firstReplyMs=$clock.Elapsed.TotalMilliseconds
    if(-not $before.active -or $before.remainingMs -lt (1000*$Seconds+2000)){throw 'Producer test failed to start or timeout too close.'}
    Start-Sleep -Seconds $Seconds
    $lastRequestMs=$clock.Elapsed.TotalMilliseconds
    $after=& $client producer-test-status -GameProcessId $GameProcessId
    $lastReplyMs=$clock.Elapsed.TotalMilliseconds
    $omissionAfter=& $client cpu-omit-status -GameProcessId $GameProcessId
    $sessionAfter=& $client status -GameProcessId $GameProcessId
    if(-not $after.active -or $omissionBefore.mode -ne $omissionAfter.mode -or
        -not $sessionAfter.stereo.ready -or $sessionAfter.openxr.sessionState -ne 'FOCUSED' -or
        $sessionAfter.stereo.commandBufferOverflows -ne $session.stereo.commandBufferOverflows){throw 'Producer/session/omission state changed; exclude this window.'}
    $pairs=[long]$after.completedPairs-[long]$before.completedPairs
    $minimumMs=$lastRequestMs-$firstReplyMs
    $result=[ordered]@{label=$Label;capturedAt=[DateTimeOffset]::Now.ToString('o');pairs=$pairs
        estimatedHz=2000*$pairs/($minimumMs+$lastReplyMs);lowerHz=1000*$pairs/$lastReplyMs;upperHz=1000*$pairs/$minimumMs
        before=$before;after=$after;omissionBefore=$omissionBefore;omissionAfter=$omissionAfter;session=$session;sessionAfter=$sessionAfter
        limitation='Cached pose, XR paused. CPU-executed eye command pairs; not GPU completions or headset submissions. Traffic/time not frozen.'}
    $result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "build/profiles/$Label.json" -Encoding utf8
    [pscustomobject]$result|Select-Object label,pairs,estimatedHz,lowerHz,upperHz
} finally {
    & $client 'producer-test off' -GameProcessId $GameProcessId|Out-Null
}
