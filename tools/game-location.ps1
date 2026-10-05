param(
    [ValidateSet('Capture','Restore')][string]$Mode='Capture',
    [string]$Path='build/profiles/test-location.json',
    [int]$GameProcessId=0
)
$ErrorActionPreference='Stop'
$client=Join-Path $PSScriptRoot 'game-command.ps1'
$game=& $client status -GameProcessId $GameProcessId
$GameProcessId=$game.pid
$command='location-capture'
if($Mode -eq 'Restore') {
    $saved=Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if($saved.schema -ne 1) {throw 'Unsupported location schema.'}
    $culture=[Globalization.CultureInfo]::InvariantCulture
    $values=@(foreach($name in 'x','y','z','heading') {
        if($null -eq $saved.position.$name) {throw "Missing coordinate $name"}
        $value=[double]$saved.position.$name
        if([double]::IsNaN($value) -or [double]::IsInfinity($value)) {throw 'Non-finite coordinate.'}
        $value.ToString('F6',$culture)
    })
    $command='location-restore '+($values -join ' ')
}
$request=& $client $command -GameProcessId $GameProcessId
if(-not $request.ok -or -not $request.accepted) {throw ($request | ConvertTo-Json -Compress)}
$deadline=[DateTime]::UtcNow.AddSeconds(5)
do {
    Start-Sleep -Milliseconds 100
    $result=& $client location-status -GameProcessId $GameProcessId
    if(-not $result.ok -or $result.id -ne $request.id) {throw 'Location operation changed.'}
    if([DateTime]::UtcNow -gt $deadline) {throw 'Location operation did not finish.'}
} while($result.pending)
if(-not $result.valid) {throw $result.error}
if($Mode -eq 'Capture') {
    $parent=Split-Path $Path -Parent
    if($parent -and -not (Test-Path -LiteralPath $parent)) {New-Item -ItemType Directory -Path $parent | Out-Null}
    [ordered]@{schema=1;capturedAt=[DateTime]::UtcNow.ToString('o');pid=$GameProcessId;position=$result.position} |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Path -Encoding UTF8
} else {
    $distance=[Math]::Sqrt([Math]::Pow($result.position.x-$saved.position.x,2)+
        [Math]::Pow($result.position.y-$saved.position.y,2)+[Math]::Pow($result.position.z-$saved.position.z,2))
    if($distance -gt 0.1) {throw "Native readback differs by $distance game units; do not treat this as a matched view."}
}
$result
