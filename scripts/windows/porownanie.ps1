# ReClaw obok oryginalnego Pazura - do nagran porownawczych (Windows).
#
#  1. Pobiera najnowsza testowa paczke ReClaw dla Windows (galaz claw-experimental)
#     z GitHuba - tylko gdy jest nowsza niz ta, ktora juz masz.
#  2. Rozpakowuje ja do <Folder>\reclaw (zapisy i ustawienia w save\ zostaja).
#  3. Podpina dane gry z oryginalu: <Folder>\orginal\Assets jako data\pl\Assets
#     (dowiazanie, nic sie nie kopiuje) i kopiuje CLAW.fec (filmy).
#  4. Uruchamia oryginal (orginal\CLAW.EXE) i ReClaw w trybie lustra
#     (okno 4:3, powtarza klawisze wciskane w oknie oryginalu).
#  5. Ustawia oba okna obok siebie: oryginal z lewej, ReClaw z prawej.
#
# Wymaga raz: GitHub CLI (winget install --id GitHub.cli) i zalogowania
# (gh auth login) - paczki z GitHub Actions pobiera sie tylko po zalogowaniu.
#
# Uruchamiaj przez porownanie.bat albo:
#   powershell -ExecutionPolicy Bypass -File porownanie.ps1 [-Folder "..."] [-BezPobierania]

param(
    [string]$Folder = "C:\Users\Herbaciarz\Desktop\Claw\0 - Testing",
    [string]$Repo = "eHerbaciarz/reclaw-engine",
    [string]$Galaz = "claw-experimental",
    [switch]$BezPobierania
)

$ErrorActionPreference = "Stop"
$Oryginal = Join-Path $Folder "orginal"
$ReClawDir = Join-Path $Folder "reclaw"
$Tmp = Join-Path $env:TEMP "reclaw-pobieranie"

function Krok($tekst) { Write-Host ""; Write-Host "== $tekst" -ForegroundColor Cyan }

if (-not (Test-Path (Join-Path $Oryginal "CLAW.EXE"))) {
    throw "Nie ma $Oryginal\CLAW.EXE - sprawdz parametr -Folder."
}

# --- 1. Pobranie --------------------------------------------------------------
if (-not $BezPobierania) {
    Krok "Szukam najnowszej paczki ReClaw ($Galaz)"
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw "Brak GitHub CLI. Zainstaluj: winget install --id GitHub.cli, potem: gh auth login"
    }
    # pierwsze uruchomienie: logowanie do GitHuba (otworzy przegladarke)
    # przez cmd: w PowerShell 5 komunikat gh na stderr przerwalby skrypt
    cmd /c "gh auth status >nul 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Nie jestes zalogowany w GitHub CLI - logowanie (jednorazowo)." -ForegroundColor Yellow
        cmd /c "gh auth login --hostname github.com --git-protocol https --web"
        if ($LASTEXITCODE -ne 0) { throw "Logowanie do GitHuba nie powiodlo sie." }
    }
    $json = cmd /c "gh api `"repos/$Repo/actions/artifacts?name=reclaw-windows-x86_64&per_page=50`" 2>nul"
    if ($LASTEXITCODE -ne 0) { throw "gh api nie zadzialalo - czy jestes zalogowany (gh auth login)?" }
    $art = ($json | ConvertFrom-Json).artifacts |
        Where-Object { -not $_.expired -and $_.workflow_run.head_branch -eq $Galaz } |
        Sort-Object created_at -Descending | Select-Object -First 1
    if (-not $art) { throw "Nie znalazlem paczki Windows dla galezi $Galaz." }

    $znacznik = Join-Path $ReClawDir "wersja.txt"
    $obecna = if (Test-Path $znacznik) { (Get-Content $znacznik -Raw).Trim() } else { "" }
    $nowa = "$($art.id) $($art.workflow_run.head_sha.Substring(0,7)) $($art.created_at)"
    if ($obecna -eq $nowa) {
        Write-Host "Masz juz najnowsza paczke ($nowa)."
    } else {
        Krok "Pobieram paczke $nowa"
        if (Test-Path $Tmp) { Remove-Item $Tmp -Recurse -Force }
        New-Item -ItemType Directory $Tmp | Out-Null
        $zip = Join-Path $Tmp "artifact.zip"
        # binarne dane: przekierowanie przez cmd (PowerShell 5 psuje bajty przy ">")
        cmd /c "gh api repos/$Repo/actions/artifacts/$($art.id)/zip > `"$zip`""
        if (-not (Test-Path $zip) -or (Get-Item $zip).Length -lt 1000000) { throw "Pobieranie nie powiodlo sie." }

        Krok "Rozpakowuje"
        Expand-Archive $zip -DestinationPath $Tmp -Force
        $wewn = Get-ChildItem $Tmp -Filter "reclaw-windows-x86_64.zip" -Recurse | Select-Object -First 1
        if ($wewn) { Expand-Archive $wewn.FullName -DestinationPath $Tmp -Force }
        $pkg = Get-ChildItem $Tmp -Directory -Recurse | Where-Object { Test-Path (Join-Path $_.FullName "ReClaw.exe") } | Select-Object -First 1
        if (-not $pkg) { throw "W paczce nie ma ReClaw.exe." }

        Get-Process ReClaw -ErrorAction SilentlyContinue | Stop-Process -Force
        Start-Sleep -Milliseconds 500
        New-Item -ItemType Directory $ReClawDir -Force | Out-Null
        # nadpisuje pliki gry, zostawia save\ i data\
        robocopy $pkg.FullName $ReClawDir /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Kopiowanie paczki nie powiodlo sie (robocopy $LASTEXITCODE)." }
        Set-Content $znacznik $nowa
        Remove-Item $Tmp -Recurse -Force
        Write-Host "ReClaw zaktualizowany: $ReClawDir"
    }
}

if (-not (Test-Path (Join-Path $ReClawDir "ReClaw.exe"))) { throw "Nie ma $ReClawDir\ReClaw.exe - uruchom bez -BezPobierania." }

# --- 2. Dane gry ---------------------------------------------------------------
Krok "Podpinam dane gry"
$dataPl = Join-Path $ReClawDir "data\pl"
New-Item -ItemType Directory $dataPl -Force | Out-Null
$assetsSrc = Join-Path $Oryginal "Assets"
$assetsDst = Join-Path $dataPl "Assets"
$rezSrc = Join-Path $Oryginal "CLAW.REZ"
if ((Test-Path $rezSrc) -and (Get-Item $rezSrc).Length -gt 1000000) {
    # pelny CLAW.REZ (nie atrapa 1 KB): skopiuj, jesli jeszcze go nie ma
    $rezDst = Join-Path $dataPl "CLAW.REZ"
    if (-not (Test-Path $rezDst) -or (Get-Item $rezDst).Length -ne (Get-Item $rezSrc).Length) {
        Copy-Item $rezSrc $rezDst -Force
    }
    Write-Host "CLAW.REZ skopiowany."
} elseif (Test-Path $assetsSrc) {
    if (-not (Test-Path $assetsDst)) {
        # dowiazanie katalogu (junction) - bez kopiowania i bez praw administratora
        cmd /c "mklink /J `"$assetsDst`" `"$assetsSrc`"" | Out-Null
    }
    Write-Host "Assets z oryginalu podpiete jako data\pl\Assets."
} else {
    throw "W $Oryginal nie ma ani pelnego CLAW.REZ, ani folderu Assets."
}
# atrapa CLAW.REZ (1 KB) w data\pl przeslonilaby folder Assets
$atrapa = Join-Path $dataPl "CLAW.REZ"
if ((Test-Path $atrapa) -and (Get-Item $atrapa).Length -lt 1000000) { Remove-Item $atrapa -Force }
foreach ($fec in @((Join-Path $Folder "CLAW.fec"), (Join-Path $Oryginal "CLAW.FEC"))) {
    if (Test-Path $fec) {
        $fecDst = Join-Path $dataPl "CLAW.FEC"
        if (-not (Test-Path $fecDst)) { Copy-Item $fec $fecDst; Write-Host "CLAW.FEC skopiowany." }
        break
    }
}

# --- 3. Uruchomienie -----------------------------------------------------------
Krok "Uruchamiam oryginal i ReClaw"
Get-Process CLAW -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process ReClaw -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$claw = Start-Process (Join-Path $Oryginal "CLAW.EXE") -WorkingDirectory $Oryginal -PassThru
Start-Sleep -Seconds 2
$env:RECLAW_MIRROR_KEYS = "1"
$env:RECLAW_WINDOW = "960x720@1.5"
$reclaw = Start-Process (Join-Path $ReClawDir "ReClaw.exe") -WorkingDirectory $ReClawDir -PassThru

# --- 4. Okna obok siebie -------------------------------------------------------
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Okna {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@

function Czekaj-NaOkno($proc, $nazwa) {
    for ($i = 0; $i -lt 60; $i++) {
        $proc.Refresh()
        if ($proc.HasExited) { throw "$nazwa sie zamknal." }
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { return $proc.MainWindowHandle }
        Start-Sleep -Milliseconds 500
    }
    throw "Nie doczekalem sie okna: $nazwa"
}

$hClaw = Czekaj-NaOkno $claw "CLAW.EXE"
$hRe = Czekaj-NaOkno $reclaw "ReClaw"
Start-Sleep -Seconds 1

$SWP_NOSIZE = 0x0001; $SWP_NOZORDER = 0x0004
[Okna]::ShowWindow($hClaw, 9) | Out-Null   # przywroc, gdyby byl zminimalizowany
[Okna]::ShowWindow($hRe, 9) | Out-Null
[Okna]::SetWindowPos($hClaw, [IntPtr]::Zero, 0, 0, 0, 0, $SWP_NOSIZE -bor $SWP_NOZORDER) | Out-Null
$r = New-Object Okna+RECT
[Okna]::GetWindowRect($hClaw, [ref]$r) | Out-Null
[Okna]::SetWindowPos($hRe, [IntPtr]::Zero, $r.R, 0, 0, 0, $SWP_NOSIZE -bor $SWP_NOZORDER) | Out-Null

# sterujesz w oknie oryginalu, ReClaw powtarza klawisze
[Okna]::SetForegroundWindow($hClaw) | Out-Null
Write-Host ""
Write-Host "Gotowe: oryginal z lewej, ReClaw z prawej. Graj w oknie oryginalu." -ForegroundColor Green
