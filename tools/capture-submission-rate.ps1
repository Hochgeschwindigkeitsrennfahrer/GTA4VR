param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-zA-Z0-9_-]{1,64}$')][string]$Label,
    [int]$GameProcessId=0,
    [ValidateRange(5,45)][int]$Seconds=30,
    [string]$OutputDirectory='build/profiles'
)
$ErrorActionPreference='Stop'
$client=Join-Path $PSScriptRoot 'game-command.ps1'
$profile=& $client profile-status -GameProcessId $GameProcessId
if(-not $profile.ok -or $profile.active){throw 'Disable the in-game profiler before measuring ordinary submissions.'}
$omissionBefore=& $client cpu-omit-status -GameProcessId $GameProcessId
if($omissionBefore.mode -and $omissionBefore.remainingMs -lt (1000*$Seconds+2000)){
    throw 'Omission timeout is too close; rearm the mode before measuring.'
}
$clock=[Diagnostics.Stopwatch]::StartNew()
$before=& $client status -GameProcessId $GameProcessId
$firstReplyMs=$clock.Elapsed.TotalMilliseconds
if(-not $before.ok -or -not $before.stereo.ready -or $before.openxr.sessionState -ne 'FOCUSED'){
    throw 'Measurement requires ready focused stereo.'
}
$GameProcessId=$before.pid
Start-Sleep -Seconds $Seconds
$lastRequestMs=$clock.Elapsed.TotalMilliseconds
$after=& $client status -GameProcessId $GameProcessId
$lastReplyMs=$clock.Elapsed.TotalMilliseconds
$profileAfter=& $client profile-status -GameProcessId $GameProcessId
$omissionAfter=& $client cpu-omit-status -GameProcessId $GameProcessId
if(-not $omissionBefore.ok -or -not $omissionAfter.ok -or $omissionBefore.mode -ne $omissionAfter.mode){
    throw 'CPU omission mode changed during measurement; exclude this window.'
}
if(-not $after.ok -or $after.pid -ne $before.pid -or -not $after.stereo.ready -or
    $after.openxr.sessionState -ne 'FOCUSED' -or $after.openxr.graphicsBackend -ne $before.openxr.graphicsBackend -or
    -not $profileAfter.ok -or $profileAfter.active -or $profileAfter.epoch -ne $profile.epoch){
    throw 'Session/profiling state changed; measurement is invalid.'
}
$submissions=[long]$after.openxr.stereoLayers-[long]$before.openxr.stereoLayers
if($submissions -le 0){throw 'No monotonic stereo submission count.'}
# Counter reads occur during the two RPC round trips. Bound the window rather
# than claiming a precise server timestamp that the protocol does not expose.
$minimumWindowMs=$lastRequestMs-$firstReplyMs
$maximumWindowMs=$lastReplyMs
$midpointWindowMs=($minimumWindowMs+$maximumWindowMs)/2
$result=[ordered]@{
    schema=1;label=$Label;capturedAt=[DateTimeOffset]::Now.ToString('o')
    submissions=$submissions;estimatedHz=1000*$submissions/$midpointWindowMs
    lowerHz=1000*$submissions/$maximumWindowMs;upperHz=1000*$submissions/$minimumWindowMs
    minimumWindowMs=$minimumWindowMs;maximumWindowMs=$maximumWindowMs
    firstRoundTripMs=$firstReplyMs;lastRoundTripMs=$lastReplyMs-$lastRequestMs
    before=$before;after=$after;profilerBefore=$profile;profilerAfter=$profileAfter
    omissionBefore=$omissionBefore;omissionAfter=$omissionAfter
    limitation='Focused endpoints only; intermediate focus/pose/traffic changes are not monitored. No CPU/GPU phase attribution.'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory|Out-Null
$output=Join-Path $OutputDirectory ($Label+'.json')
$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $output -Encoding UTF8
[pscustomobject]$result|Select-Object label,submissions,estimatedHz,lowerHz,upperHz
Write-Output "Saved $output"
