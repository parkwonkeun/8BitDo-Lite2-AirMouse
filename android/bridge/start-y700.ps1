param(
    [Parameter(Mandatory=$true)][string]$Binary
)
$ErrorActionPreference = 'Stop'
$deviceText = (adb -d shell getevent -lp) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'The tablet must be connected to PC USB debugging' }
$node = [regex]::Match($deviceText,'(/dev/input/event\d+)\s*\n\s*name:\s+"Lite2 Air Mouse Plus Mouse"').Groups[1].Value

$previous = adb -d shell "if [ -f /data/local/tmp/lite2-bridge.pid ]; then cat /data/local/tmp/lite2-bridge.pid; fi"
if (!$previous) { adb -d shell am force-stop local.lite2.control }
if ($previous -match '^\d+$') {
    $exe = adb -d shell readlink "/proc/$previous/exe"
    if ($exe -eq '/data/local/tmp/lite2-pointer-bridge') { adb -d shell kill -TERM $previous }
}
Start-Sleep -Milliseconds 1300
adb -d push (Resolve-Path -LiteralPath $Binary).Path /data/local/tmp/lite2-pointer-bridge.next
if ($LASTEXITCODE -ne 0) { throw 'Could not copy the bridge to the tablet' }
adb -d shell chmod 700 /data/local/tmp/lite2-pointer-bridge.next
if ($LASTEXITCODE -ne 0) { throw 'Could not make the bridge executable' }
adb -d shell mv /data/local/tmp/lite2-pointer-bridge.next /data/local/tmp/lite2-pointer-bridge
if ($LASTEXITCODE -ne 0) { throw 'Could not install the bridge' }
$proofBytes = New-Object byte[] 16
$rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try { $rng.GetBytes($proofBytes) } finally { $rng.Dispose() }
$proof = [BitConverter]::ToString($proofBytes).Replace('-', '').ToLowerInvariant()
adb -d shell "nohup setsid /data/local/tmp/lite2-pointer-bridge $proof >/data/local/tmp/lite2-bridge.log 2>&1 </dev/null & echo `$! >/data/local/tmp/lite2-bridge.pid"
if ($LASTEXITCODE -ne 0) { throw 'Could not start the bridge' }
$null = adb -d shell am start -W -n local.lite2.control/.ControlActivity --es bridgeToken $proof
if ($LASTEXITCODE -ne 0) { throw 'Could not start Lite 2 Control' }
Start-Sleep -Milliseconds 500
$log = adb -d shell cat /data/local/tmp/lite2-bridge.log
$log
if (-not ($log -match 'Waiting for Lite 2 Control')) { throw 'The bridge did not start' }
if ($node -and -not ($log -match 'Lite 2 mouse redirected|Watching native Lite 2 reports|Monitor captured Lite 2 mouse and keyboard')) { Write-Output 'Waiting for app and physical mouse to reconnect' }
