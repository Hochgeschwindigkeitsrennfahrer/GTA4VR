param(
    [Parameter(Position=0)]
    [ValidateLength(1,128)]
    [string]$Command = 'status',
    [int]$GameProcessId = 0,
    [int]$TimeoutMs = 5000
)

$ErrorActionPreference = 'Stop'
if ($GameProcessId -eq 0) {
    $games = @(Get-Process GTAIV -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw "Expected one GTAIV process; found $($games.Count). Specify -GameProcessId." }
    $GameProcessId = $games[0].Id
}
$bytes = [System.Text.Encoding]::UTF8.GetBytes($Command)
if ($bytes.Length -gt 128) { throw 'Command exceeds the 128-byte protocol limit.' }
$client = [System.IO.Pipes.NamedPipeClientStream]::new('.', "GTA4VR.Dev.$GameProcessId",
    [System.IO.Pipes.PipeDirection]::InOut, [System.IO.Pipes.PipeOptions]::Asynchronous)
try {
    $client.Connect($TimeoutMs)
    $client.ReadMode = [System.IO.Pipes.PipeTransmissionMode]::Message
    $client.Write($bytes, 0, $bytes.Length)
    $client.Flush()
    $buffer = [byte[]]::new(4096)
    $read = $client.ReadAsync($buffer, 0, $buffer.Length)
    if (-not $read.Wait($TimeoutMs)) { throw 'Game command response timed out.' }
    $reply = [System.Text.Encoding]::UTF8.GetString($buffer, 0, $read.Result)
    if (-not $client.IsMessageComplete) { throw 'Game response exceeds the protocol limit.' }
    $result = $reply | ConvertFrom-Json
    $ack = [System.Text.Encoding]::ASCII.GetBytes('ack')
    $client.Write($ack, 0, $ack.Length)
    $client.Flush()
    $result
} finally {
    $client.Dispose()
}
