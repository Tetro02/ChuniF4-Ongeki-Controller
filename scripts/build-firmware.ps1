param([string]$Keil = $env:KEIL_UV4)
$ErrorActionPreference = 'Stop'
if (-not $Keil) {
    $found = Get-Command UV4.exe -ErrorAction SilentlyContinue
    if ($found) { $Keil = $found.Source }
}
if (-not $Keil -or -not (Test-Path -LiteralPath $Keil)) { throw 'Pass -Keil <path to UV4.exe> or set KEIL_UV4.' }
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'firmware\MDK-ARM\ChuniF4.uvprojx'
[xml]$config = Get-Content -Raw -LiteralPath $project
foreach ($node in $config.SelectNodes('//RunUserProg1 | //RunUserProg2')) {
    if ($node.InnerText -ne '0') { throw 'Custom build action enabled; refusing unattended build.' }
}
$buildDir = Join-Path $root 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$log = Join-Path $buildDir 'keil-build.log'
$process = Start-Process -FilePath $Keil -ArgumentList '-r', ('"' + $project + '"'), '-t', 'ChuniF4', '-o', ('"' + $log + '"') -WindowStyle Hidden -PassThru
$process.WaitForExit()
$text = Get-Content -Raw -LiteralPath $log
Write-Output $text
if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') { throw 'Keil build failed or produced warnings.' }
