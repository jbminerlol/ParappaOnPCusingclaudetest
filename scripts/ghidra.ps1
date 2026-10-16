# Launches Ghidra (GUI or headless) with the repository's settings directory.
#   .\scripts\ghidra.ps1 gui
#   .\scripts\ghidra.ps1 headless <AnalyzeHeadless args...>
# Ghidra: GHIDRA_INSTALL_DIR, else the newest ghidra_* folder in the repository root or tools\.
# JDK 21: JAVA_HOME, else the newest jdk-* folder in the repository root or tools\.
param([Parameter(Mandatory)][ValidateSet('gui', 'headless')][string]$Mode,
      [Parameter(ValueFromRemainingArguments)][string[]]$Rest)

$root = Split-Path -Parent $PSScriptRoot
function Find-Dir($envValue, $pattern, $check) {
    if ($envValue -and (Test-Path (Join-Path $envValue $check))) { return $envValue }
    $found = @($root, (Join-Path $root 'tools')) | Where-Object { Test-Path $_ } |
        ForEach-Object { Get-ChildItem -Directory -Path $_ -Filter $pattern } |
        Where-Object { Test-Path (Join-Path $_.FullName $check) } | Sort-Object Name -Descending | Select-Object -First 1
    if ($found) { return $found.FullName }
}
$ghidra = Find-Dir $env:GHIDRA_INSTALL_DIR 'ghidra_*' 'Ghidra\Framework\Utility\lib\Utility.jar'
if (-not $ghidra) { Write-Error 'Ghidra not found: set GHIDRA_INSTALL_DIR or put a ghidra_* folder in the repository root or tools\'; exit 1 }
$env:JAVA_HOME = Find-Dir $env:JAVA_HOME 'jdk-*' 'bin\java.exe'
if (-not $env:JAVA_HOME) { Write-Error 'JDK 21 not found: set JAVA_HOME or put a jdk-* folder in the repository root or tools\'; exit 1 }
$java = Join-Path $env:JAVA_HOME 'bin\java.exe'

$vmargs = @(
    '-Djava.system.class.loader=ghidra.GhidraClassLoader', '-Dfile.encoding=UTF8',
    '-Duser.country=US', '-Duser.language=en', '-Duser.variant=',
    '-Dsun.java2d.opengl=false', '-Dsun.java2d.d3d=false', '-Dlog4j.skipJansi=true',
    '-Djdk.tls.client.protocols=TLSv1.2,TLSv1.3', '-Dpython.console.encoding=UTF-8',
    '-Xshare:off', '--enable-native-access=ALL-UNNAMED',
    "-Dapplication.settingsdir=$(Join-Path $root 'settings')",
    '-cp', (Join-Path $ghidra 'Ghidra\Framework\Utility\lib\Utility.jar')
)

if ($Mode -eq 'gui') {
    $proj = Join-Path $root 'project\PaRappa.gpr'
    Start-Process -FilePath (Join-Path $env:JAVA_HOME 'bin\javaw.exe') `
        -ArgumentList (($vmargs + '-Xmx4G', 'ghidra.Ghidra', 'ghidra.GhidraRun', $proj) | ForEach-Object { if ($_ -match ' ') { "`"$_`"" } else { $_ } })
} else {
    & $java @vmargs $(if ($env:GHIDRA_XMX) { "-Xmx$env:GHIDRA_XMX" } else { '-Xmx1536M' }) '-XX:+UseSerialGC' 'ghidra.Ghidra' 'ghidra.app.util.headless.AnalyzeHeadless' @Rest
    exit $LASTEXITCODE
}
