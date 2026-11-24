# rwh-runner.ps1 — Windows build/test runner (plan Section 18.4).
# Allow-listed actions only; exact-SHA builds; machine-readable report lines
# "RWH key=value". No arbitrary command execution.
param(
  [Parameter(Mandatory)][ValidateSet('status','prepare','build','unit-tests','selftest','probe-tray','clean')][string]$Action,
  [string]$Sha
)
$ErrorActionPreference = 'Stop'
$env:Path = 'C:\Programs\cmake\bin;C:\Programs\ninja;' + $env:Path
$CMake = 'C:\Programs\cmake\bin\cmake.exe'
$Ctest = 'C:\Programs\cmake\bin\ctest.exe'
$root  = 'C:\rwh'
$src   = "$root\pc-coherence"
$proj  = "$src\remote-windows-host"
$build = "$root\build"
$VsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat'

function emit($k, $v) { Write-Output ('RWH {0}={1}' -f $k, ($v -replace '\s+', ' ')) }
function die($msg) { emit 'status' $msg; exit 2 }

# Run a native executable without PowerShell 5.1 turning its stderr into a
# terminating NativeCommandError under EAP=Stop.
function run-native($exe, $params) {
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  $out = & $exe @params 2>&1
  $rc = $LASTEXITCODE
  $ErrorActionPreference = $prev
  # Mirror child output to the host (Write-Host does NOT enter the return
  # pipeline — critical, because callers capture this function's return
  # value in a variable; stray output would corrupt $rc).
  foreach ($o in $out) { Write-Host ($o | Out-String).TrimEnd() }
  return $rc
}

# Import the VS 2026 x64 developer environment (cl, link, rc, INCLUDE/LIB)
# into this PowerShell process. Note: VS 2026 vsdevcmd does NOT accept
# -host_arch; -arch=x64 only (verified 2026-11-24).
function load-vs-env {
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  $lines = & cmd /c "`"$VsDevCmd`" -arch=x64 && set"
  $ErrorActionPreference = $prev
  foreach ($line in $lines) {
    if ($line -match '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$') {
      Set-Item ('Env:' + $matches[1]) $matches[2]
    }
  }
  if (-not $env:VSINSTALLDIR) { die 'vs_env_failed' }
  $cl = Get-Command cl -ErrorAction SilentlyContinue
  if (-not $cl) { die 'cl_not_found' }
}

if ($Sha -ne '' -and $Sha -notmatch '^[0-9a-f]{40}$') { die 'sha_rejected' }
if (($Action -eq 'build' -or $Action -eq 'unit-tests' -or $Action -eq 'selftest' -or $Action -eq 'probe-tray') -and -not (Test-Path $proj)) { die 'not_prepared' }

switch ($Action) {
  'status' {
    emit 'status' 'ok'
    emit 'host' (hostname)
    emit 'cmake' (& $CMake --version | Select-Object -First 1)
    emit 'ninja' (ninja --version)
    emit 'git' (git --version)
    exit 0
  }
  'prepare' {
    if (-not (Test-Path $src)) {
      New-Item -ItemType Directory -Force -Path $root | Out-Null
      $rc = run-native git @('clone', 'https://github.com/Ninzalo/pc-coherence.git', $src)
      if ($rc -ne 0) { die 'clone_failed' }
    }
    $rc = run-native git @('-C', $src, 'fetch', 'origin', 'main')
    if ($Sha -ne '') {
      $rc = run-native git @('-C', $src, 'checkout', '-q', $Sha)
      if ($rc -ne 0) { die 'checkout_failed' }
    }
    emit 'status' 'prepared'
    emit 'sha' (git -C $src rev-parse HEAD)
    exit 0
  }
  'clean' {
    if (Test-Path $build) { Remove-Item $build -Recurse -Force }
    emit 'status' 'cleaned'
    exit 0
  }
  'build' {
    $actual = git -C $src rev-parse HEAD
    emit 'sha' $actual
    load-vs-env
    $rc = run-native $CMake @('-S', $proj, '-B', $build, '-G', 'Ninja',
                              '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                              '-DCMAKE_MAKE_PROGRAM=C:/Programs/ninja/ninja.exe')
    if ($rc -ne 0) { die 'configure_failed' }
    $rc = run-native $CMake @('--build', $build, '-j')
    if ($rc -ne 0) { die 'build_failed' }
    emit 'status' 'build_ok'
    exit 0
  }
  'unit-tests' {
    $actual = git -C $src rev-parse HEAD
    emit 'sha' $actual
    load-vs-env
    # Configure (idempotent) then build: the build dir may not exist yet.
    $rc = run-native $CMake @('-S', $proj, '-B', $build, '-G', 'Ninja',
                              '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                              '-DCMAKE_MAKE_PROGRAM=C:/Programs/ninja/ninja.exe')
    if ($rc -ne 0) { die 'configure_failed' }
    $rc = run-native $CMake @('--build', $build, '-j')
    if ($rc -ne 0) { die 'build_failed' }
    $rc = run-native $Ctest @('--test-dir', $build, '--output-on-failure')
    emit 'ctest_exit' $rc
    if ($rc -eq 0) { emit 'status' 'tests_ok'; exit 0 }
    else { emit 'status' 'tests_failed'; exit $rc }
  }
  'selftest' {
    # Headless smoke test of the app shell: real signaling server on
    # 127.0.0.1 + in-process HELLO/pairing-challenge exchange. Fixed
    # executable, fixed arguments — no arbitrary command path.
    $actual = git -C $src rev-parse HEAD
    emit 'sha' $actual
    load-vs-env
    $rc = run-native $CMake @('-S', $proj, '-B', $build, '-G', 'Ninja',
                              '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                              '-DCMAKE_MAKE_PROGRAM=C:/Programs/ninja/ninja.exe')
    if ($rc -ne 0) { die 'configure_failed' }
    $rc = run-native $CMake @('--build', $build, '-j')
    if ($rc -ne 0) { die 'build_failed' }
    $rc = run-native (Join-Path $build 'rwh-host.exe') @('--selftest')
    emit 'selftest_exit' $rc
    if ($rc -eq 0) { emit 'status' 'selftest_ok'; exit 0 }
    else { emit 'status' 'selftest_failed'; exit $rc }
  }
  'probe-tray' {
    # Window-creation diagnostic (tools/probe/probe-tray.cpp from the
    # prepared checkout). Compiles a standalone probe and runs it in
    # this session; prints one result line per test. Diagnostic only —
    # no product code is modified or run.
    $actual = git -C $src rev-parse HEAD
    emit 'sha' $actual
    load-vs-env
    $probe = "$root\probe"
    if (-not (Test-Path $probe)) { New-Item -ItemType Directory -Force -Path $probe | Out-Null }
    Copy-Item "$proj\tools\probe\probe-tray.cpp" "$probe\probe-tray.cpp" -Force
    $rc = run-native cl @('/nologo','/EHsc','/O2','/std:c++17',
                          "/Fe:$probe\probe-tray.exe", "/Fo:$probe\",
                          "$probe\probe-tray.cpp", 'user32.lib', 'ws2_32.lib')
    if ($rc -ne 0) { die 'probe_build_failed' }
    $rc = run-native "$probe\probe-tray.exe" @()
    emit 'probe_exit' $rc
    emit 'status' 'probe_done'
    exit 0
  }
}
