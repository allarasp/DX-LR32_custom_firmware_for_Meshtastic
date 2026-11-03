param([Parameter(Mandatory=$true)][string]$PortName, [int]$BootTimeoutSeconds = 60)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$logPath = Join-Path $projectRoot 'logs/bringup/uart-windows-transcript.json'
[IO.Directory]::CreateDirectory((Split-Path -Parent $logPath)) | Out-Null
$events = New-Object System.Collections.Generic.List[object]
$serial = New-Object System.IO.Ports.SerialPort($PortName,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.DtrEnable = $true
$serial.RtsEnable = $true
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
function Record([string]$Direction,[string]$Data) {
    $events.Add([pscustomobject]@{utc=[DateTime]::UtcNow.ToString('o');direction=$Direction;ascii=$Data})
    [IO.File]::WriteAllText($logPath,($events | ConvertTo-Json -Depth 4))
    Write-Output ($Direction + ' ' + ($Data | ConvertTo-Json -Compress))
}
function ReceiveUntil([string]$Expected,[int]$TimeoutMs) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $received = ''
    while ($watch.ElapsedMilliseconds -lt $TimeoutMs) {
        $chunk = $serial.ReadExisting()
        if ($chunk.Length -gt 0) {
            # Record emits output; keep it outside the function's return pipeline.
            Record 'RX' $chunk | Out-Host
            $received += $chunk
            if ($received.Contains($Expected)) { return $received }
        }
        Start-Sleep -Milliseconds 5
    }
    throw "Timeout waiting for $($Expected | ConvertTo-Json -Compress); received $($received | ConvertTo-Json -Compress)"
}
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    Write-Output "WINDOWS UART READY: $PortName, 115200 8N1; waiting for physical boot banner"
    $boot = ReceiveUntil "DXLR32 CUSTOM FW BOOT`r`n" ($BootTimeoutSeconds*1000)
    if ($boot -ne "DXLR32 CUSTOM FW BOOT`r`n") { throw 'Unexpected or duplicated boot bytes' }
    $tests = @(
        @("PING`r`n","PONG`r`n"),
        @("VERSION`r`n","DXLR32 UARTDIAG 0.1.1`r`n"),
        @("STATUS`r`n","STATUS CLK=22120000 BAUD=115200 RADIO=UNINITIALIZED`r`n")
    )
    for ($i=0; $i -lt 5; $i++) { $tests += ,@("PING`r`n","PONG`r`n") }
    foreach ($test in $tests) {
        Record 'TX' $test[0]
        $serial.Write($test[0])
        $received = ReceiveUntil $test[1] 2000
        if ($received -ne $test[1]) { throw 'Unexpected or duplicated command response' }
    }
    Write-Output 'PHYSICAL WINDOWS UART TEST PASSED: boot banner, VERSION, STATUS, six PING/PONG exchanges'
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
