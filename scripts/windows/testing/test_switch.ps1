# ReClaw - szybki test na Ryujinxie (Windows, PowerShell 5.1+)
#
# 1. Pobiera najnowszy build testowy (release "test-switch", gałąź claw-switch)
# 2. Wgrywa reclaw.nro na kartę SD Ryujinxa: sdcard\switch\reclaw\
# 3. Uruchamia grę w Ryujinxie
# 4. Po zamknięciu Ryujinxa kopiuje logi (reclaw.log, log Ryujinxa) do
#    logs\claw-switch\<data_godzina>_<commit>\ (obok tego skryptu) i otwiera ten folder
#
# Uruchomienie: dwuklik na switch-test.bat (albo: powershell -ExecutionPolicy Bypass -File test_switch.ps1)
# Opcje:  -Force    pobierz nawet gdy ten build już jest wgrany
#         -NoLogs   nie czekaj na zamknięcie Ryujinxa, nie zbieraj logów
#         -Setup    wybierz ponownie Ryujinxa (ścieżka zapisana w test_switch.json)
#
# Ryujinx startuje jak w startRyujinx.bat MK: przez runas /trustlevel:0x20000
# (bez uprawnień administratora), od razu z grą. Ustawienie "RunAsTrustLevel"
# w test_switch.json ("" = zwykłe uruchomienie).

param(
    [switch]$Force,
    [switch]$NoLogs,
    [switch]$Setup,
    [string]$LogRoot = (Join-Path $PSScriptRoot 'logs\claw-switch')
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Repo = 'eHerbaciarz/reclaw-engine'
$Base = "https://github.com/$Repo/releases/download/test-switch"
$SettingsFile = Join-Path $PSScriptRoot 'test_switch.json'

function Say($text, $color = 'Gray') { Write-Host $text -ForegroundColor $color }

# ------------------------------------------------------------ ustawienia
$settings = [ordered]@{ RyujinxExe = 'D:\Gry\Emulatory\Ryujinx\Ryujinx.exe'; RunAsTrustLevel = '0x20000'; LastCommit = '' }
if (Test-Path $SettingsFile) {
    $loaded = Get-Content $SettingsFile -Raw | ConvertFrom-Json
    foreach ($p in $loaded.PSObject.Properties) { $settings[$p.Name] = $p.Value }
}
function Save-Settings { $settings | ConvertTo-Json | Set-Content -Path $SettingsFile -Encoding UTF8 }

function Find-Ryujinx {
    $roots = @("$env:LOCALAPPDATA", "$env:APPDATA", "$env:ProgramFiles", "${env:ProgramFiles(x86)}",
               "$env:USERPROFILE\Downloads", "$env:USERPROFILE\Desktop", "$env:USERPROFILE\Documents", 'C:\', 'D:\')
    foreach ($r in $roots) {
        if (-not $r -or -not (Test-Path $r)) { continue }
        $hit = Get-ChildItem -Path $r -Filter 'Ryujinx*.exe' -Recurse -Depth 3 -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -notmatch 'Updater|Uninstall' } | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return ''
}

if ($Setup -or -not $settings.RyujinxExe -or -not (Test-Path $settings.RyujinxExe)) {
    Say 'Szukam Ryujinxa...'
    $found = Find-Ryujinx
    if ($found) {
        Say "Znaleziony: $found"
        $answer = Read-Host 'Użyć go? [T/n]'
        if ($answer -match '^[nN]') { $found = '' }
    }
    if (-not $found) {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = 'Wskaż Ryujinx.exe'
        $dlg.Filter = 'Ryujinx (*.exe)|*.exe'
        if ($dlg.ShowDialog() -ne 'OK') { Say 'Nie wybrano Ryujinxa.' Red; exit 1 }
        $found = $dlg.FileName
    }
    $settings.RyujinxExe = $found
    Save-Settings
}

$ryuExe = $settings.RyujinxExe
$ryuDir = Split-Path $ryuExe
# Tryb przenośny (folder "portable" obok exe) albo %APPDATA%\Ryujinx
$ryuData = if (Test-Path (Join-Path $ryuDir 'portable')) { Join-Path $ryuDir 'portable' } else { Join-Path $env:APPDATA 'Ryujinx' }
$gameDir = Join-Path $ryuData 'sdcard\switch\reclaw'
$nroPath = Join-Path $gameDir 'reclaw.nro'

# ------------------------------------------------------------ pobieranie
Say 'Sprawdzam najnowszy build testowy...'
try {
    $commit = (Invoke-WebRequest -UseBasicParsing "$Base/commit.txt").Content
    if ($commit -is [byte[]]) { $commit = [Text.Encoding]::UTF8.GetString($commit) }
    $commit = $commit.Trim()
} catch {
    Say "Nie mogę pobrać informacji o buildzie ($Base/commit.txt): $($_.Exception.Message)" Red
    exit 1
}
$short = $commit.Substring(0, [Math]::Min(7, $commit.Length))
Say "Build testowy: commit $short  (https://github.com/$Repo/commit/$commit)" Cyan

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $ryuExe }
if ($running) {
    Say 'Ryujinx jest uruchomiony - zamykam go, żeby podmienić grę.' Yellow
    $running | Stop-Process -Force
    Start-Sleep -Seconds 2
}

if ($Force -or $settings.LastCommit -ne $commit -or -not (Test-Path $nroPath)) {
    New-Item -ItemType Directory -Force -Path $gameDir | Out-Null
    foreach ($d in 'data\pl', 'data\en', 'save', 'movies', 'custom') {
        New-Item -ItemType Directory -Force -Path (Join-Path $gameDir $d) | Out-Null
    }
    $tmp = Join-Path $env:TEMP 'reclaw.nro.download'
    Say 'Pobieram reclaw.nro...'
    Invoke-WebRequest -UseBasicParsing "$Base/reclaw.nro" -OutFile $tmp
    Move-Item -Force $tmp $nroPath
    $settings.LastCommit = $commit
    Save-Settings
    Say "Wgrane: $nroPath" Green
} else {
    Say 'Ten build jest już wgrany (pobierz ponownie: -Force).' Green
}

if (-not (Test-Path (Join-Path $gameDir 'data\pl\CLAW.REZ')) -and -not (Test-Path (Join-Path $gameDir 'data\en\CLAW.REZ'))) {
    Say "Uwaga: brak CLAW.REZ w $gameDir\data\pl (albo data\en) - skopiuj go ze swojej kopii gry." Yellow
}

# ------------------------------------------------------------ uruchomienie
$started = Get-Date
Say 'Uruchamiam Ryujinxa...'
if ($settings.RunAsTrustLevel) {
    # jak startRyujinx.bat: runas /trustlevel:0x20000 /machine:amd64 ...
    # runas gubi argumenty w cudzysłowach, więc gra jest podawana przez mały
    # plik .cmd w folderze bez spacji (C:\Users\Public)
    $launcher = Join-Path $env:PUBLIC 'reclaw_start_ryujinx.cmd'
    $lines = @('@echo off', 'chcp 65001 >nul', ('cd /d "' + $ryuDir + '"'), ('start "" "' + $ryuExe + '" "' + $nroPath + '"'))
    [IO.File]::WriteAllLines($launcher, $lines, (New-Object Text.UTF8Encoding($false)))  # UTF-8 (polskie litery w ścieżce)
    Say "Start: $ryuExe `"$nroPath`""
    # Bez spacji w ścieżkach: dokładnie to, co działa ręcznie
    # (runas ... "Ryujinx.exe gra.nro"); ze spacjami: przez plik .cmd
    $target = if (($ryuExe + $nroPath) -notmatch ' ') { '"' + $ryuExe + ' ' + $nroPath + '"' } else { $launcher }
    Start-Process -FilePath 'runas.exe' -WindowStyle Hidden `
        -ArgumentList @("/trustlevel:$($settings.RunAsTrustLevel)", '/machine:amd64', $target)
    Say 'Jeśli pojawi się "Keys not found" - kliknij OK, gra wystartuje.' Yellow
} else {
    Start-Process -FilePath $ryuExe -ArgumentList ('"' + $nroPath + '"') -WorkingDirectory $ryuDir | Out-Null
}
if ($NoLogs) { exit 0 }

# runas wraca od razu: czekam, aż Ryujinx się pojawi, potem aż się zamknie
$proc = $null
for ($i = 0; $i -lt 40 -and -not $proc; $i++) {
    Start-Sleep -Milliseconds 500
    $proc = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $ryuExe } | Select-Object -First 1
}
if (-not $proc) { Say 'Ryujinx się nie uruchomił (sprawdź ścieżkę: -Setup).' Red; exit 1 }
Say 'Graj - po zamknięciu Ryujinxa zbiorę logi.'
$proc.WaitForExit()

# ------------------------------------------------------------ logi
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$logDir = Join-Path $LogRoot "$stamp`_$short"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
foreach ($f in 'reclaw.log', 'reclaw_previous.log') {
    $p = Join-Path $gameDir "save\$f"
    if (Test-Path $p) { Copy-Item $p $logDir }
}
$ryuLogs = Join-Path $ryuData 'Logs'
if (-not (Test-Path $ryuLogs)) { $ryuLogs = Join-Path $ryuDir 'Logs' }
if (Test-Path $ryuLogs) {
    $last = Get-ChildItem $ryuLogs -Filter '*.log' | Where-Object { $_.LastWriteTime -ge $started.AddMinutes(-1) } |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($last) { Copy-Item $last.FullName $logDir }
}
Set-Content -Path (Join-Path $logDir 'commit.txt') -Value $commit
Say "Logi: $logDir" Green
Start-Process explorer.exe $logDir
