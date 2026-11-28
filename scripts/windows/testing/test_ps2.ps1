# ReClaw - szybki test wersji PS2 w PCSX2 (Windows, PowerShell 5.1+)
#
# 1. Pobiera najnowszy build testowy PS2 (release "test-ps2", gałąź claw-ps2)
# 2. Rozpakowuje go do folderu gry (domyślnie %USERPROFILE%\ReClaw-PS2)
#    i dokłada CLAW.REZ / CLAW.FEC z Twojego folderu ReClawa w Ryujinxie
#    (albo z folderu, który wskażesz)
# 3. Włącza w PCSX2 "Host Filesystem" (gra czyta pliki z folderu obok reclaw.elf)
# 4. Uruchamia reclaw.elf w PCSX2
# 5. Po zamknięciu PCSX2 kopiuje logi (reclaw.log, emulog PCSX2) do
#    logs\claw-ps2\<data_godzina>_<commit>\ (obok tego skryptu) i otwiera ten folder
#
# Uruchomienie: dwuklik na ps-test.bat
# Opcje:  -Force    pobierz nawet gdy ten build już jest rozpakowany
#         -NoLogs   nie czekaj na zamknięcie PCSX2, nie zbieraj logów
#         -Setup    wybierz ponownie PCSX2 i folder z CLAW.REZ (test_ps2.json)

param(
    [switch]$Force,
    [switch]$NoLogs,
    [switch]$Setup,
    [string]$LogRoot = (Join-Path $PSScriptRoot 'logs\claw-ps2')
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Repo = 'eHerbaciarz/reclaw-engine'
$Base = "https://github.com/$Repo/releases/download/test-ps2"
$SettingsFile = Join-Path $PSScriptRoot 'test_ps2.json'

function Say($text, $color = 'Gray') { Write-Host $text -ForegroundColor $color }

# ------------------------------------------------------------ ustawienia
$settings = [ordered]@{
    Pcsx2Exe  = ''
    GameDir   = (Join-Path $env:USERPROFILE 'ReClaw-PS2')
    RezDir    = (Join-Path $env:APPDATA 'Ryujinx\sdcard\switch\reclaw\data')   # data\pl, data\en
    LastCommit = ''
}
if (Test-Path $SettingsFile) {
    $loaded = Get-Content $SettingsFile -Raw | ConvertFrom-Json
    foreach ($p in $loaded.PSObject.Properties) { $settings[$p.Name] = $p.Value }
}
function Save-Settings { $settings | ConvertTo-Json | Set-Content -Path $SettingsFile -Encoding UTF8 }

function Find-Pcsx2 {
    $roots = @('D:\Gry\Emulatory', "$env:ProgramFiles", "${env:ProgramFiles(x86)}", "$env:LOCALAPPDATA",
               "$env:USERPROFILE\Downloads", "$env:USERPROFILE\Desktop", 'D:\', 'C:\')
    foreach ($r in $roots) {
        if (-not $r -or -not (Test-Path $r)) { continue }
        $hit = Get-ChildItem -Path $r -Filter 'pcsx2*.exe' -Recurse -Depth 3 -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -notmatch 'updater|uninst' } | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return ''
}

if ($Setup -or -not $settings.Pcsx2Exe -or -not (Test-Path $settings.Pcsx2Exe)) {
    Say 'Szukam PCSX2...'
    $found = Find-Pcsx2
    if ($found) { Say "PCSX2: $found (inny: -Setup)" }
    if (-not $found) {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = 'Wskaż pcsx2-qt.exe'
        $dlg.Filter = 'PCSX2 (*.exe)|*.exe'
        if ($dlg.ShowDialog() -ne 'OK') { Say 'Nie wybrano PCSX2.' Red; exit 1 }
        $found = $dlg.FileName
    }
    $settings.Pcsx2Exe = $found
    Save-Settings
}
# Pytanie o folder z CLAW.REZ tylko gdy gra go jeszcze nie ma i nie da się go
# znaleźć (albo z -Setup); odpowiedź jest zapamiętana w test_ps2.json
function Has-Rez($dir) {
    return (Test-Path (Join-Path $dir 'pl\CLAW.REZ')) -or (Test-Path (Join-Path $dir 'en\CLAW.REZ'))
}
$gameHasRez = Has-Rez (Join-Path $settings.GameDir 'data')
if ($Setup -or (-not $gameHasRez -and -not (Has-Rez $settings.RezDir))) {
    $answer = Read-Host "Folder z CLAW.REZ (z podfolderami pl\ i en\) [$($settings.RezDir)]"
    if ($answer) { $settings.RezDir = $answer.Trim('"') }
    Save-Settings
}

$pcsx2 = $settings.Pcsx2Exe
$pcsx2Dir = Split-Path $pcsx2
$gameDir = $settings.GameDir
$elf = Join-Path $gameDir 'reclaw.elf'

# ------------------------------------------------------------ pobieranie
Say 'Sprawdzam najnowszy build testowy PS2...'
try {
    $commit = (Invoke-WebRequest -UseBasicParsing "$Base/commit.txt").Content
    if ($commit -is [byte[]]) { $commit = [Text.Encoding]::UTF8.GetString($commit) }
    $commit = $commit.Trim()
} catch {
    Say "Nie mogę pobrać informacji o buildzie ($Base/commit.txt): $($_.Exception.Message)" Red
    Say 'Build testowy powstaje po pushu na gałąź claw-ps2.' Yellow
    exit 1
}
$short = $commit.Substring(0, [Math]::Min(7, $commit.Length))
Say "Build testowy: commit $short  (https://github.com/$Repo/commit/$commit)" Cyan

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $pcsx2 }
if ($running) {
    Say 'PCSX2 jest uruchomiony - zamykam go, żeby podmienić grę.' Yellow
    $running | Stop-Process -Force
    Start-Sleep -Seconds 2
}

if ($Force -or $settings.LastCommit -ne $commit -or -not (Test-Path $elf)) {
    $zip = Join-Path $env:TEMP 'reclaw-ps2.zip'
    $unz = Join-Path $env:TEMP 'reclaw-ps2'
    Say 'Pobieram reclaw-ps2.zip...'
    Invoke-WebRequest -UseBasicParsing "$Base/reclaw-ps2.zip" -OutFile $zip
    if (Test-Path $unz) { Remove-Item -Recurse -Force $unz }
    Expand-Archive -Path $zip -DestinationPath $unz
    New-Item -ItemType Directory -Force -Path $gameDir | Out-Null
    # pliki gry nadpisane; data\ (CLAW.REZ) i save\ zostają
    Copy-Item -Path (Join-Path $unz 'reclaw\*') -Destination $gameDir -Recurse -Force
    # (bez soundfontu: 8 MB z 32 MB pamięci PS2; starsze buildy go miały)
    $sf = Join-Path $gameDir 'music'
    if ((Test-Path $sf) -and -not (Test-Path (Join-Path $unz 'reclaw\music'))) { Remove-Item -Recurse -Force $sf }
    Remove-Item -Recurse -Force $unz, $zip
    $settings.LastCommit = $commit
    Save-Settings
    Say "Rozpakowane do: $gameDir" Green
} else {
    Say 'Ten build jest już rozpakowany (pobierz ponownie: -Force).' Green
}

# CLAW.REZ / CLAW.FEC z Twojej kopii (kopiowane tylko gdy brak albo inny rozmiar)
$haveData = $false
foreach ($lang in 'pl', 'en') {
    foreach ($f in 'CLAW.REZ', 'CLAW.FEC') {
        $src = Join-Path $settings.RezDir "$lang\$f"
        $dst = Join-Path $gameDir "data\$lang\$f"
        if (Test-Path $src) {
            if (-not (Test-Path $dst) -or (Get-Item $src).Length -ne (Get-Item $dst).Length) {
                New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
                Say "Kopiuję $f ($lang)..."
                Copy-Item $src $dst -Force
            }
        }
        if ($f -eq 'CLAW.REZ' -and (Test-Path $dst)) { $haveData = $true }
    }
}
if (-not $haveData) {
    Say "Uwaga: brak CLAW.REZ w $gameDir\data\pl (albo data\en) - ustaw folder: -Setup" Yellow
}

# ------------------------------------------------------------ PCSX2: Host Filesystem
$ini = $null
foreach ($cand in @((Join-Path $pcsx2Dir 'inis\PCSX2.ini'),
                    (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PCSX2\inis\PCSX2.ini'))) {
    if (Test-Path $cand) { $ini = $cand; break }
}
if ($ini) {
    $text = Get-Content $ini -Raw -Encoding UTF8
    if ($text -notmatch '(?m)^HostFs\s*=\s*true') {
        Copy-Item $ini "$ini.reclaw-backup" -Force
        if ($text -match '(?m)^HostFs\s*=') {
            $text = $text -replace '(?m)^HostFs\s*=.*$', 'HostFs = true'
        } elseif ($text -match '(?m)^\[EmuCore\]\s*$') {
            $text = $text -replace '(?m)^\[EmuCore\]\s*$', "[EmuCore]`r`nHostFs = true"
        } else {
            $text += "`r`n[EmuCore]`r`nHostFs = true`r`n"
        }
        [IO.File]::WriteAllText($ini, $text, (New-Object Text.UTF8Encoding($false)))
        Say "PCSX2: włączony Host Filesystem ($ini, kopia: .reclaw-backup)" Green
    }
} else {
    Say 'Nie znalazłem PCSX2.ini - włącz ręcznie: Ustawienia > Zaawansowane > "Enable Host Filesystem".' Yellow
}

# ------------------------------------------------------------ uruchomienie
$started = Get-Date
Say "Uruchamiam PCSX2: $elf"
$proc = Start-Process -FilePath $pcsx2 -WorkingDirectory $pcsx2Dir -PassThru `
        -ArgumentList @('-fastboot', '--', ('"' + $elf + '"'))
if ($NoLogs) { exit 0 }

Say 'Graj - po zamknięciu PCSX2 zbiorę logi.'
$proc.WaitForExit()

# ------------------------------------------------------------ logi
$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$logDir = Join-Path $LogRoot "$stamp`_$short"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
foreach ($p in @((Join-Path $gameDir 'reclaw.log'), (Join-Path $gameDir 'reclaw_previous.log'),
                 (Join-Path $gameDir 'save\reclaw.log'), (Join-Path $gameDir 'save\reclaw_previous.log'))) {
    if (Test-Path $p) { Copy-Item $p $logDir -Force }
}
foreach ($cand in @((Join-Path $pcsx2Dir 'logs\emulog.txt'),
                    (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PCSX2\logs\emulog.txt'))) {
    if ((Test-Path $cand) -and (Get-Item $cand).LastWriteTime -ge $started.AddMinutes(-1)) {
        Copy-Item $cand $logDir -Force
        break
    }
}
Set-Content -Path (Join-Path $logDir 'commit.txt') -Value $commit
Say "Logi: $logDir" Green
Start-Process explorer.exe $logDir
