# Re-exports the decompilation of every program in the Ghidra project (no re-import, so manual
# and GhidraMCP edits made in the project are kept). Applies PsxFixups first.
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'ghidra.ps1') headless (Join-Path $root 'project') PaRappa -process -noanalysis `
    -scriptPath $PSScriptRoot -postScript PsxFixups.java -postScript PsxExport.java (Join-Path $root 'analysis\decomp')
exit $LASTEXITCODE
