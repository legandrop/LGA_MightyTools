@echo off
setlocal
rem Bump de version para publicar un release: sube CMakeLists.txt y VERSION a la version que encabeza
rem Docs\Changelog.md y ofrece commitear, pushear y correr instalador.bat. Lo corre Lega cuando decide
rem publicar, nunca se corre solo. La logica esta en tools\bump_version.ps1; esto es solo el wrapper.
rem Uso: bump_version.bat [--check] [--commit^|--no-commit] [--publish^|--no-publish]
set "FLAGS="
:args
if "%~1"=="" goto run
set "FLAG="
if /I "%~1"=="--check" set "FLAG=-Check"
if /I "%~1"=="--commit" set "FLAG=-Commit"
if /I "%~1"=="--no-commit" set "FLAG=-NoCommit"
if /I "%~1"=="--publish" set "FLAG=-Publish"
if /I "%~1"=="--no-publish" set "FLAG=-NoPublish"
if not defined FLAG (
    echo [bump_version] ERROR: flag desconocido: %~1
    endlocal & exit /b 2
)
set "FLAGS=%FLAGS% %FLAG%"
shift
goto args
:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\bump_version.ps1" %FLAGS%
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%
