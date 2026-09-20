# Generate-PendulumReference.ps1 — F-PENDULUM generator entry point (App Platform / AP-04).
# Prefers `py -3 generate_pendulum_reference.py` (the checked-in CSV was produced by it);
# without a Python launcher it evaluates the same closed form in PowerShell 5.1 (double
# precision, round-trip "R" formatting — the values agree to the last digit or one ulp).
# "python" is a Store alias on the reference machine, so only `py` is probed.
param([string]$Out = (Join-Path $PSScriptRoot 'pendulum_reference.csv'))
$ErrorActionPreference = 'Stop'

$py = Get-Command py -ErrorAction SilentlyContinue
if ($py) {
    & $py.Source -3 (Join-Path $PSScriptRoot 'generate_pendulum_reference.py') $Out
    if ($LASTEXITCODE -ne 0) { throw "generate_pendulum_reference.py failed ($LASTEXITCODE)" }
    exit 0
}

$L = 1.0; $G = 9.80665; $theta0 = 5.0 * [Math]::PI / 180.0; $hz = 240; $n = 2400
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# F-PENDULUM analytic small-angle reference: L=1 g=9.80665 theta0=5deg dt=1/240 (AP-04)')
$lines.Add('t,theta_c0,omega_c0,theta_c005,omega_c005')
$inv = [System.Globalization.CultureInfo]::InvariantCulture
function Series([double]$c, [int]$i) {
    $t = $i / $hz
    $w0sq = $G / $L; $gamma = $c / 2.0; $wd = [Math]::Sqrt($w0sq - $gamma * $gamma)
    $env = $theta0 * [Math]::Exp(-$gamma * $t)
    $theta = $env * ([Math]::Cos($wd * $t) + ($gamma / $wd) * [Math]::Sin($wd * $t))
    $omega = -$env * ($w0sq / $wd) * [Math]::Sin($wd * $t)
    return @($t, $theta, $omega)
}
for ($i = 0; $i -le $n; $i++) {
    $a = Series 0.0 $i; $b = Series 0.05 $i
    $lines.Add(($a[0].ToString('R', $inv) + ',' + $a[1].ToString('R', $inv) + ',' + $a[2].ToString('R', $inv) + ',' + $b[1].ToString('R', $inv) + ',' + $b[2].ToString('R', $inv)))
}
[System.IO.File]::WriteAllText($Out, (($lines -join "`n") + "`n"))
Write-Host "wrote $Out $($n + 1) rows (PowerShell fallback)"
