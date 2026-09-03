# Proves the determinism claim, locally, in under a minute.
#
# This check lives here rather than in CI because CI is dispatch-only: the
# repository is private, so automatic runs are charged, and the owner has chosen
# not to spend that. A claim that is only checked when somebody remembers to
# check it is not enforced - so this is the smallest thing that can be run
# before a push and actually says something.
#
# THREE STEPS, AND THE THIRD IS THE ONE THAT MATTERS.
#
#   1. Record a session.
#   2. Replay it. It must reproduce.
#   3. Corrupt one checkpoint and replay again. It must FAIL, at the tick that
#      was corrupted.
#
# Without step 3 this is a test that passes without checking anything. A replay
# harness that never diverges and a replay harness that cannot detect divergence
# produce identical output on step 2, and this project has shipped that shape of
# mistake before - see docs/planning/2026-08-28-determinism-audit.md, where the
# state hash agreed with everything because it was reading the wrong field.
#
# WHAT THIS DOES NOT PROVE. The demo scene reads no input inside a tick, so a
# green run here shows the simulation reproduces and NOT that recorded input
# drives it. That claim is carried by test_replay's last four cases, which run
# under ctest. Do not read this script as covering them.
#
#   pwsh tools/verify-replay.ps1
#   pwsh tools/verify-replay.ps1 -Config Release -Scene assets/scenes/MainScene.scene

[CmdletBinding()]
param(
    [string]$Config = "Debug",
    [string]$Scene  = "assets/scenes/MainScene.scene",
    [int]   $Frames = 180
)

$ErrorActionPreference = "Stop"

# From the project root: asset paths resolve against the working directory, so
# running this from tools/ would fail to find shaders and look like a bug in the
# engine rather than in the invocation.
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$exe = Join-Path $root "build/$Config/SupersonicEngine.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $root "build/SupersonicEngine"      # single-config generators
}
if (-not (Test-Path $exe)) {
    Write-Error "No engine binary. Build first: cmake --build build --config $Config"
}

$work     = Join-Path ([System.IO.Path]::GetTempPath()) "supersonic-verify-replay"
$recorded = Join-Path $work "session.replay"
$corrupt  = Join-Path $work "corrupted.replay"
New-Item -ItemType Directory -Force -Path $work | Out-Null

function Invoke-Engine([string[]]$EngineArgs) {
    # Output captured rather than streamed: the verdict line is what this script
    # decides on, and the profiler report around it is noise here.
    #
    # THE EXIT CODE IS THE VERDICT, NOT WHETHER ANYTHING REACHED STDERR. With
    # $ErrorActionPreference at Stop - which the rest of this script needs, so
    # that a Write-Error below actually stops it - PowerShell turns ANY line a
    # native program writes to stderr into a terminating NativeCommandError.
    # A Vulkan loader that warns about somebody else's overlay layer therefore
    # killed this script before it ran a single check, with a message about
    # naming policy that says nothing about determinism. It is not a rare
    # configuration: any overlay that injects a Vulkan layer does it.
    #
    # So the preference is lowered around the call and put straight back. The
    # engine's own failures still reach us - through $LASTEXITCODE, which is
    # what every caller here already tests.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $exe @EngineArgs 2>&1 | Out-String
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    return [pscustomobject]@{ Code = $code; Output = $out }
}

Write-Host "1/3  Recording $Frames frames of $Scene ..." -ForegroundColor Cyan
$rec = Invoke-Engine @("--frames", $Frames, "--fixed-step", "--scene", $Scene, "--record", $recorded)
if ($rec.Code -ne 0 -or -not (Test-Path $recorded)) {
    Write-Host $rec.Output
    Write-Error "Recording failed (exit $($rec.Code))."
}
$ticks = (Select-String -Path $recorded -Pattern '^ticks (\d+)$').Matches.Groups[1].Value
$points = @(Select-String -Path $recorded -Pattern '^checkpoint ').Count
Write-Host "     $ticks tick(s), $points checkpoint(s), $([math]::Round((Get-Item $recorded).Length / 1KB, 1)) KB"

Write-Host "2/3  Replaying it ..." -ForegroundColor Cyan
$rep = Invoke-Engine @("--frames", $Frames, "--fixed-step", "--scene", $Scene, "--replay", $recorded)
if ($rep.Code -ne 0) {
    Write-Host $rep.Output
    Write-Error "The replay did not reproduce, and it should have."
}
$verdict = ($rep.Output -split "`n" | Select-String -Pattern 'Replay reproduced').Line
Write-Host "     $($verdict.Trim())" -ForegroundColor Green

# --- the negative control -------------------------------------------------
#
# One checkpoint, one byte. The LAST checkpoint rather than the first, so the
# run has to get all the way there before disagreeing - corrupting tick zero
# would also pass a harness that only ever checks its first checkpoint.
Write-Host "3/3  Corrupting one checkpoint, which must be caught ..." -ForegroundColor Cyan
$lines = Get-Content $recorded
$targets = @(0..($lines.Count - 1) | Where-Object { $lines[$_] -match '^checkpoint (\d+) ' })
if ($targets.Count -lt 2) {
    Write-Error "The recording has fewer than two checkpoints; there is nothing to corrupt meaningfully."
}
$at = $targets[-1]
$tick = [regex]::Match($lines[$at], '^checkpoint (\d+) ').Groups[1].Value
$lines[$at] = "checkpoint $tick deadbeefdeadbeef"
Set-Content -Path $corrupt -Value $lines

$bad = Invoke-Engine @("--frames", $Frames, "--fixed-step", "--scene", $Scene, "--replay", $corrupt)
if ($bad.Code -eq 0) {
    Write-Host $bad.Output
    Write-Error "A corrupted checkpoint replayed CLEANLY. The oracle is not checking anything."
}
$reported = ($bad.Output -split "`n" | Select-String -Pattern 'diverged at tick').Line | Select-Object -First 1
if (-not $reported) {
    Write-Host $bad.Output
    Write-Error "It failed, but never said where. A divergence with no tick number is not a bug report."
}
if ($reported -notmatch "tick $tick(\D|$)") {
    Write-Host $bad.Output
    Write-Error "Reported the wrong tick. Corrupted $tick, got: $($reported.Trim())"
}
Write-Host "     $($reported.Trim())" -ForegroundColor Green

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "Replay reproduces, and a one-byte change is caught at the right tick." -ForegroundColor Green
