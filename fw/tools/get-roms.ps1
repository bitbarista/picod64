# Put the ROMs PicoD64 needs on its SD card, from the zimmers.net Commodore firmware archive (Windows).
# They are Commodore's, so PicoD64 does not include them. Same job as get-roms.sh.
#
#   powershell -ExecutionPolicy Bypass -File get-roms.ps1 E:\
#
# (-ExecutionPolicy Bypass: Windows blocks downloaded scripts by default; this lets this one run, once.)
#   dos1541.bin  the 1541 DOS: 325302-01 ($C000) + 901229-05 ($E000), joined in that order. Required.
#                This exact ROM is the one PicoD64 is tested against (MD5 below).
# Written for Windows PowerShell 5.1, which every Windows 10/11 has.
# Tested 2026-11-04 with PowerShell 7 (Linux): writes the file, MD5 right, same bytes as get-roms.sh's;
# missing folder or a wrong MD5 -> exit 1, nothing written. Not yet run on Windows' own PowerShell 5.1.
param([Parameter(Mandatory = $true)][string]$Card)
$ErrorActionPreference = 'Stop'
# PowerShell 5.1 may not offer TLS 1.2 by default; zimmers.net needs it
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

if (-not (Test-Path -LiteralPath $Card -PathType Container)) { throw "no such folder: $Card" }
$dest = (Resolve-Path -LiteralPath $Card).Path
$z = 'https://www.zimmers.net/anonftp/pub/cbm/firmware'
$web = New-Object System.Net.WebClient
$c000 = $web.DownloadData("$z/drives/new/1541/1541-c000.325302-01.bin")
$e000 = $web.DownloadData("$z/drives/new/1541/1541-e000.901229-05.bin")
if ($c000.Length -ne 8192 -or $e000.Length -ne 8192) { throw 'a 1541 ROM half is not 8,192 bytes' }

$dos = New-Object byte[] 16384
[Array]::Copy($c000, 0, $dos, 0, 8192)
[Array]::Copy($e000, 0, $dos, 8192, 8192)
$md5 = [Security.Cryptography.MD5]::Create()
$hash = -join ($md5.ComputeHash($dos) | ForEach-Object { $_.ToString('x2') })
if ($hash -ne 'a0ce8439d1b8dcf2e1430461f7233a72') { throw "dos1541.bin: MD5 $hash differs from the tested ROM" }

[IO.File]::WriteAllBytes((Join-Path $dest 'dos1541.bin'), $dos)
Write-Host "dos1541.bin (16,384 bytes, MD5 checked) written to $dest"
