# ReClaw dla Windows z wybranej gałęzi: pobranie, dane gry, uruchomienie
# (samo albo obok oryginalnego Pazura - do nagrań porównawczych).
#
#  1. Pobiera najnowszą paczkę ReClaw dla Windows z gałęzi -Galaz z GitHuba
#     (tylko gdy jest nowsza niż ta, którą już masz) do
#     builds\reclaw-windows\<Nazwa> (zapisy i ustawienia w save\ zostają).
#  2. Raz kopiuje do niej zapis z builds\save (wszystko odblokowane).
#  3. Podpina dane gry z oryginału (orginal\CLAW.REZ albo orginal\Assets).
#  4. -Porownanie: uruchamia oryginał (orginal\CLAW.EXE) i ReClaw w trybie
#     lustra (okno 4:3, powtarza klawisze z okna oryginału), obok siebie.
#     Bez -Porownanie: uruchamia tylko ReClaw.
#
# Wymaga raz: GitHub CLI (winget install --id GitHub.cli) i zalogowania
# (gh auth login) - paczki z GitHub Actions pobiera się tylko po zalogowaniu.
#
# Uruchamiaj przez main*.bat / Comparision-*.bat albo:
#   powershell -ExecutionPolicy Bypass -File reclaw-windows.ps1 -Galaz claw-main -Nazwa main [-Porownanie] [-BezPobierania]

param(
    [string]$Folder = $PSScriptRoot,
    [string]$Repo = "eHerbaciarz/reclaw-engine",
    [string]$Galaz = "claw-experimental",
    [string]$Nazwa = "experimental",
    [switch]$Porownanie,
    [switch]$BezPobierania
)

$ErrorActionPreference = "Stop"
$Oryginal = Join-Path $Folder "orginal"
$ReClawDir = Join-Path $Folder "builds\reclaw-windows\$Nazwa"
$SaveSrc = Join-Path $Folder "builds\save"
$Tmp = Join-Path $env:TEMP "reclaw-pobieranie-$Nazwa"

function Krok($tekst) { Write-Host ""; Write-Host "== $tekst" -ForegroundColor Cyan }

if ($Porownanie -and -not (Test-Path (Join-Path $Oryginal "CLAW.EXE"))) {
    throw "Nie ma $Oryginal\CLAW.EXE - sprawdz parametr -Folder."
}
# ReClaw z tego folderu (inne buildy moga dzialac dalej)
function Zamknij-TenReClaw {
    Get-Process ReClaw -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($ReClawDir, [StringComparison]::OrdinalIgnoreCase) } |
        Stop-Process -Force
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

        Zamknij-TenReClaw
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

# --- 2. Zapis (raz) -------------------------------------------------------------
$saveDst = Join-Path $ReClawDir "save"
$saveZnacznik = Join-Path $saveDst ".zapis-skopiowany"
if (-not (Test-Path $saveZnacznik)) {
    if (Test-Path $SaveSrc) {
        Krok "Kopiuje zapis z builds\save (jednorazowo)"
        New-Item -ItemType Directory $saveDst -Force | Out-Null
        robocopy $SaveSrc $saveDst /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Kopiowanie zapisu nie powiodlo sie (robocopy $LASTEXITCODE)." }
        Set-Content $saveZnacznik (Get-Date -Format 'yyyy-MM-dd HH:mm')
    } else {
        Write-Host "Brak $SaveSrc - zapis nie skopiowany." -ForegroundColor Yellow
    }
}

# --- 3. Dane gry ---------------------------------------------------------------
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

# --- 4. Uruchomienie -----------------------------------------------------------
if (-not $Porownanie) {
    Krok "Uruchamiam ReClaw ($Galaz)"
    Zamknij-TenReClaw
    Start-Sleep -Milliseconds 300
    Start-Process (Join-Path $ReClawDir "ReClaw.exe") -WorkingDirectory $ReClawDir | Out-Null
    Write-Host ""
    Write-Host "Gotowe: ReClaw z $Galaz ($ReClawDir)." -ForegroundColor Green
    exit 0
}

Krok "Uruchamiam oryginal i ReClaw ($Galaz)"
Get-Process CLAW -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process ReClaw -ErrorAction SilentlyContinue | Stop-Process -Force   # (lustro: tylko jedno okno ReClaw)
Start-Sleep -Milliseconds 500

$claw = Start-Process (Join-Path $Oryginal "CLAW.EXE") -WorkingDirectory $Oryginal -PassThru
Start-Sleep -Seconds 2
$env:RECLAW_MIRROR_KEYS = "1"
# rozmiar jak okno oryginalu (cnc-ddraw: 1148x861) przy skalowaniu Windows 150%:
# 765x574 punktow = 1148x861 pikseli; skala 1.195 = widac 640x480 planszy
$env:RECLAW_WINDOW = "765x574@1.195"
$reclaw = Start-Process (Join-Path $ReClawDir "ReClaw.exe") -WorkingDirectory $ReClawDir -PassThru

# --- 5. Okna obok siebie -------------------------------------------------------
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
