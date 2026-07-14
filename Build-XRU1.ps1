# Build-XRU1.ps1 — сборка редакторского таргета XRU1 одной командой.
#
#   .\Build-XRU1.ps1              # собрать (предупредит, если открыт редактор)
#   .\Build-XRU1.ps1 -StopEditor  # закрыть Unreal и собрать
#
# UBT не может пересобрать модули, пока открыт редактор (Live Coding).

param(
    [switch]$StopEditor,
    [string]$Config = "Development"
)

$ErrorActionPreference = "Stop"
$Engine  = "D:/UE5/UE_5.7"
$Project = "D:/UE5/UnrealProjects/XRU1/XRU1.uproject"
$Build   = Join-Path $Engine "Engine/Build/BatchFiles/Build.bat"

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

& $Build XRU1Editor Win64 $Config -project="$Project" -waitmutex
exit $LASTEXITCODE
