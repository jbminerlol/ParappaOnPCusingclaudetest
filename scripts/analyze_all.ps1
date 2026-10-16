# Rebuilds the Ghidra project from scratch and exports the decompilation to analysis\decomp.
# Inputs come from your disc: python scripts\extract_disc.py <PARAPPA.CUE>  (writes game\ghidra_in)
#   1. Main EXE via the PSX loader (PsyQ signatures, GTE) + function seeding.
#   2. Every stage overlay (COMODx.BIN at 0x801C3870) with the main EXE mapped in and its names imported.
$root = Split-Path -Parent $PSScriptRoot
$ghidra = Join-Path $PSScriptRoot 'ghidra.ps1'
$in = Join-Path $root 'game\ghidra_in'
$out = Join-Path $root 'analysis\decomp'
$scripts = $PSScriptRoot
if (-not (Test-Path (Join-Path $in 'SCUS_941.83'))) { Write-Error "Run python scripts\extract_disc.py <PARAPPA.CUE> first ($in is empty)"; exit 1 }
New-Item -ItemType Directory -Force (Join-Path $root 'project'), $out | Out-Null

& $ghidra headless (Join-Path $root 'project') PaRappa -import (Join-Path $in 'SCUS_941.83') -overwrite `
    -loader PsxLoader -scriptPath $scripts -preScript PsxSetup.java seedmain `
    -postScript PsxFixups.java -postScript PsxExport.java $out
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$ovl = Get-ChildItem (Join-Path $in 'COMOD*.BIN') | ForEach-Object FullName
& $ghidra headless (Join-Path $root 'project') PaRappa -import @ovl -overwrite `
    -loader BinaryLoader -loader-baseAddr 0x801C3870 -processor 'PSX:LE:32:default' -cspec default `
    -scriptPath $scripts -preScript PsxSetup.java overlay (Join-Path $in 'SCUS_text.bin') (Join-Path $out 'SCUS_941.83\functions.tsv') `
    -postScript PsxFixups.java -postScript PsxExport.java $out
exit $LASTEXITCODE
