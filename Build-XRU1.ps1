# Build-XRU1.ps1 — сборка редакторского таргета XRU1 одной командой.
#
#   .\Build-XRU1.ps1              # собрать (предупредит, если открыт редактор)
#   .\Build-XRU1.ps1 -StopEditor  # закрыть Unreal и собрать
#
# UBT не может пересобрать модули, пока открыт редактор (Live Coding).
#
# Пути не хардкодим: проект — рядом со скриптом, движок 5.7 — из реестра
# (скрипт работает на любой машине, где UE 5.7 установлен лаунчером).

param(
    [switch]$StopEditor,
    [string]$Config = "Development"
)

$ErrorActionPreference = "Stop"

# Проект — *.uproject рядом с этим скриптом.
$Project = Join-Path $PSScriptRoot "XRU1.uproject"
if (-not (Test-Path $Project)) {
    Write-Host "Не найден $Project — скрипт должен лежать в корне проекта." -ForegroundColor Red
    exit 1
}

# Движок: реестр лаунчера → известные пути по машинам.
$Engine = $null
$reg = Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.7' -ErrorAction SilentlyContinue
if ($reg -and $reg.InstalledDirectory -and (Test-Path $reg.InstalledDirectory)) { $Engine = $reg.InstalledDirectory }
if (-not $Engine) {
    foreach ($candidate in @("D:\Program Files\Epic Games\UE_5.7", "D:\UE5\UE_5.7", "C:\Program Files\Epic Games\UE_5.7")) {
        if (Test-Path $candidate) { $Engine = $candidate; break }
    }
}
if (-not $Engine) {
    Write-Host "UE 5.7 не найден (ни в реестре, ни по известным путям)." -ForegroundColor Red
    exit 1
}
$Build = Join-Path $Engine "Engine\Build\BatchFiles\Build.bat"

$editor = Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue
if ($editor) {
    if ($StopEditor) {
        Write-Host "Закрываю UnrealEditor (несохранённое будет потеряно)..." -ForegroundColor Yellow
        Get-Process -Name UnrealEditor,LiveCodingConsole,CrashReportClientEditor -ErrorAction SilentlyContinue |
            Stop-Process -Force
        Start-Sleep -Seconds 3
    } else {
        Write-Host "Открыт UnrealEditor — UBT не сможет пересобрать модули (Live Coding)." -ForegroundColor Red
        Write-Host "Закрой редактор, либо запусти: .\Build-XRU1.ps1 -StopEditor" -ForegroundColor Red
        Write-Host "(или пересобери прямо в редакторе: Ctrl+Alt+F11)" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Движок: $Engine" -ForegroundColor DarkGray
Write-Host "Проект: $Project" -ForegroundColor DarkGray
& $Build XRU1Editor Win64 $Config -project="$Project" -waitmutex
exit $LASTEXITCODE
