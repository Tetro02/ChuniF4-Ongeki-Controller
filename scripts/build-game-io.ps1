param([string]$Gcc = 'gcc', [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build\game-io'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$source = Join-Path $root 'game-io\chunif4_mu3io.c'
$dll = Join-Path $out 'chunif4_mu3io.dll'
& $Gcc -O2 -Wall -Wextra -Werror -shared $source -o $dll -lsetupapi -lhid -static-libgcc
if ($LASTEXITCODE -ne 0) { throw 'Game DLL build failed.' }
& $Python (Join-Path $PSScriptRoot 'test-game-io.py') --library $dll
if ($LASTEXITCODE -ne 0) { throw 'Built DLL replay checks failed.' }
Write-Output "DLL ready: $dll"
