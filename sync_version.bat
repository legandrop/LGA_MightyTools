@echo off
setlocal
rem Sincroniza la version del repo (CMakeLists.txt, Docs\Changelog.md y VERSION).
rem La logica esta en tools\sync_version.ps1; esto es solo el wrapper de Windows.
rem Uso: sync_version.bat          propaga la version mas alta
rem      sync_version.bat --check  no escribe; sale 1 si algo difiere
set "FLAG="
if /I "%~1"=="--check" set "FLAG=-Check"
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0tools\sync_version.ps1" %FLAG%
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%
