# ReClaw - szybki test wersji Dreamcast we Flycaście (Windows, PowerShell 5.1+)
#
# 1. Pobiera najnowszy build testowy Dreamcasta (release "test-dreamcast",
#    gałąź claw-dreamcast) do folderu gry (domyślnie %USERPROFILE%\ReClaw-DC)
# 2. Robi obraz płyty (.gdi) programem reclaw_disc.py z paczki, z CLAW.REZ
#    z Twojego folderu z danymi gry (potrzebny Python 3, nic więcej)
# 3. Włącza we Flycaście log do pliku i konsolę portu szeregowego (tam gra
#    pisze swój log - na Dreamcaście nie ma gdzie zapisać pliku)
# 4. Uruchamia obraz we Flycaście
# 5. Po zamknięciu Flycasta kopiuje jego log do logs\claw-dreamcast\<data>_<commit>\
#
# Uruchomienie: dwuklik na dreamcast-test.bat
# Opcje:  -Force    zrób obraz płyty nawet gdy ten build już jest
#         -NoLogs   nie czekaj na zamknięcie Flycasta, nie zbieraj logów
#         -Setup    wybierz ponownie Flycasta i folder z CLAW.REZ (test_dreamcast.json)

param(
    [switch]$Force,
    [switch]$NoLogs,
    [switch]$Setup,
    [string]$LogRoot = (Join-Path $PSScriptRoot 'logs\claw-dreamcast')
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Repo = 'eHerbaciarz/reclaw-engine'
$Base = "https://github.com/$Repo/releases/download/test-dreamcast"
$SettingsFile = Join-Path $PSScriptRoot 'test_dreamcast.json'

function Say($text, $color = 'Gray') { Write-Host $text -ForegroundColor $color }

# ------------------------------------------------------------ ustawienia
$settings = [ordered]@{
    FlycastExe = ''
    GameDir    = (Join-Path $env:USERPROFILE 'ReClaw-DC')
    RezDir     = (Join-Path $env:APPDATA 'Ryujinx\sdcard\switch\reclaw\data')   # data\pl, data\en
    LastCommit = ''
}
if (Test-Path $SettingsFile) {
    $loaded = Get-Content $SettingsFile -Raw | ConvertFrom-Json
    foreach ($p in $loaded.PSObject.Properties) { $settings[$p.Name] = $p.Value }
}
function Save-Settings { $settings | ConvertTo-Json | Set-Content -Path $SettingsFile -Encoding UTF8 }

function Find-Flycast {
    $roots = @('D:\Gry\Emulatory', "$env:ProgramFiles", "${env:ProgramFiles(x86)}", "$env:LOCALAPPDATA",
               "$env:USERPROFILE\Downloads", "$env:USERPROFILE\Desktop", 'D:\', 'C:\')
    foreach ($r in $roots) {
        if (-not $r -or -not (Test-Path $r)) { continue }
        $hit = Get-ChildItem -Path $r -Filter 'flycast*.exe' -Recurse -Depth 3 -ErrorAction SilentlyContinue |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return ''
}

if ($Setup -or -not $settings.FlycastExe -or -not (Test-Path $settings.FlycastExe)) {
    Say 'Szukam Flycasta...'
    $found = Find-Flycast
    if ($found) { Say "Flycast: $found (inny: -Setup)" }
    if (-not $found) {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = 'Wskaż flycast.exe'
        $dlg.Filter = 'Flycast (*.exe)|*.exe'
        if ($dlg.ShowDialog() -ne 'OK') { Say 'Nie wybrano Flycasta.' Red; exit 1 }
        $found = $dlg.FileName
    }
    $settings.FlycastExe = $found
    Save-Settings
}

# CLAW.REZ: z folderu z danymi (podfoldery pl\ i en\); pytanie tylko gdy go tam nie ma
function Find-Rez($dir) {
    foreach ($lang in 'pl', 'en') {
        $p = Join-Path $dir "$lang\CLAW.REZ"
        if ((Test-Path $p) -and (Get-Item $p).Length -gt 1000000) { return @{ Path = $p; Lang = $lang } }
    }
    return $null
}
$rez = Find-Rez $settings.RezDir
if (-not $rez) { $rez = Find-Rez (Join-Path $env:USERPROFILE 'ReClaw-PS2\data') }
if ($Setup -or -not $rez) {
    $answer = Read-Host "Folder z CLAW.REZ (z podfolderami pl\ i en\) [$($settings.RezDir)]"
    if ($answer) { $settings.RezDir = $answer.Trim('"') }
    Save-Settings
    $rez = Find-Rez $settings.RezDir
    if (-not $rez) { Say "Nie ma CLAW.REZ w $($settings.RezDir)\pl ani \en." Red; exit 1 }
}

# Python 3 do reclaw_disc.py
$python = $null
foreach ($cand in 'py -3', 'python', 'python3') {
    $parts = $cand -split ' '
    if (Get-Command $parts[0] -ErrorAction SilentlyContinue) { $python = $parts; break }
}
if (-not $python) { Say 'Brak Pythona 3 (potrzebny do zrobienia obrazu płyty): winget install Python.Python.3.12' Red; exit 1 }

$flycast = $settings.FlycastExe
$flycastDir = Split-Path $flycast
$gameDir = $settings.GameDir
$gdi = Join-Path $gameDir 'gdi\disc.gdi'

# ------------------------------------------------------------ pobieranie
Say 'Sprawdzam najnowszy build testowy Dreamcasta...'
try {
    $commit = (Invoke-WebRequest -UseBasicParsing "$Base/commit.txt").Content
    if ($commit -is [byte[]]) { $commit = [Text.Encoding]::UTF8.GetString($commit) }
    $commit = $commit.Trim()
} catch {
    Say "Nie mogę pobrać informacji o buildzie ($Base/commit.txt): $($_.Exception.Message)" Red
    Say 'Build testowy powstaje po pushu na gałąź claw-dreamcast.' Yellow
    exit 1
}
$short = $commit.Substring(0, [Math]::Min(7, $commit.Length))
Say "Build testowy: commit $short  (https://github.com/$Repo/commit/$commit)" Cyan

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $flycast }
if ($running) {
    Say 'Flycast jest uruchomiony - zamykam go, żeby podmienić płytę.' Yellow
    $running | Stop-Process -Force
    Start-Sleep -Seconds 2
}

if ($Force -or $settings.LastCommit -ne $commit -or -not (Test-Path $gdi)) {
    New-Item -ItemType Directory -Force -Path $gameDir | Out-Null
    $zip = Join-Path $gameDir 'reclaw-dreamcast.zip'
    Say 'Pobieram reclaw-dreamcast.zip...'
    Invoke-WebRequest -UseBasicParsing "$Base/reclaw-dreamcast.zip" -OutFile $zip
    $pkg = Join-Path $gameDir 'paczka'
    if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
    Expand-Archive $zip -DestinationPath $pkg -Force
    $disc = Get-ChildItem $pkg -Filter 'reclaw_disc.py' -Recurse | Select-Object -First 1
    if (-not $disc) { Say 'W paczce nie ma reclaw_disc.py.' Red; exit 1 }

    Say "Robię obraz płyty (CLAW.REZ: $($rez.Path))..."
    $out = Join-Path $gameDir 'gdi'
    if (Test-Path $out) { Remove-Item $out -Recurse -Force }
    $pyArgs = @()
    if (@($python).Count -gt 1) { $pyArgs += $python[1..(@($python).Count - 1)] }
    $pyArgs += @($disc.FullName, '--package', $disc.Directory.FullName, '--rez', $rez.Path, '--lang', $rez.Lang, '--out', $out)
    & @($python)[0] @pyArgs
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $gdi)) { Say 'Nie udało się zrobić obrazu płyty.' Red; exit 1 }
    $settings.LastCommit = $commit
    Save-Settings
} else {
    Say 'Ten build już jest (pobranie od nowa: -Force).'
}

# ------------------------------------------------------------ log Flycasta
# Flycast trzyma ustawienia w emu.cfg obok programu (wersja przenośna) albo w
# %APPDATA%\flycast. Włączam log do pliku i konsolę portu szeregowego.
$cfgDir = if (Test-Path (Join-Path $flycastDir 'emu.cfg')) { $flycastDir } else { Join-Path $env:APPDATA 'flycast' }
$cfg = Join-Path $cfgDir 'emu.cfg'
function Set-Ini($file, $section, $key, $value) {
    $lines = if (Test-Path $file) { [System.Collections.Generic.List[string]](Get-Content $file) } else { New-Object System.Collections.Generic.List[string] }
    $sec = -1; $end = $lines.Count
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Trim() -eq "[$section]") { $sec = $i; continue }
        if ($sec -ge 0 -and $lines[$i].Trim().StartsWith('[')) { $end = $i; break }
    }
    if ($sec -lt 0) { $lines.Add("[$section]"); $lines.Add("$key = $value"); Set-Content $file $lines; return }
    for ($i = $sec + 1; $i -lt $end; $i++) {
        if ($lines[$i] -match "^\s*$([regex]::Escape($key))\s*=") { $lines[$i] = "$key = $value"; Set-Content $file $lines; return }
    }
    $lines.Insert($end, "$key = $value")
    Set-Content $file $lines
}
try {
    New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
    if ((Test-Path $cfg) -and -not (Test-Path "$cfg.reclaw-kopia")) { Copy-Item $cfg "$cfg.reclaw-kopia" }
    Set-Ini $cfg 'log' 'LogToFile' 'yes'
    Set-Ini $cfg 'config' 'Debug.SerialConsoleEnabled' 'yes'
} catch {
    Say "Nie mogę ustawić logu Flycasta ($cfg): $($_.Exception.Message)" Yellow
}

# ------------------------------------------------------------ uruchomienie
$started = Get-Date
Say "Uruchamiam Flycasta: $gdi"
$proc = Start-Process -FilePath $flycast -WorkingDirectory $flycastDir -PassThru -ArgumentList @('"' + $gdi + '"')
if ($NoLogs) { exit 0 }

Say 'Graj - po zamknięciu Flycasta zbiorę logi.'
$proc.WaitForExit()

# ------------------------------------------------------------ logi
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$logDir = Join-Path $LogRoot "$stamp`_$short"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
foreach ($dir in @($cfgDir, $flycastDir, (Join-Path $cfgDir 'logs'), (Join-Path $flycastDir 'logs'))) {
    if (-not (Test-Path $dir)) { continue }
    Get-ChildItem $dir -Filter '*.log' -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $started.AddMinutes(-1) } |
        ForEach-Object { Copy-Item $_.FullName $logDir -Force }
}
Set-Content -Path (Join-Path $logDir 'commit.txt') -Value $commit
if (-not (Get-ChildItem $logDir -Filter '*.log')) {
    Say 'Flycast nie zostawił logu (sprawdź w nim: Settings > Advanced > Log to file / Serial console).' Yellow
}
Say "Logi: $logDir" Green
Start-Process explorer.exe $logDir
