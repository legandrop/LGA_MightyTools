@echo off
setlocal
cd /d "%~dp0"
REM La carpeta del script se toma ANTES de parsear: `shift` corre tambien el parametro cero, y
REM despues del parseo "~dp0" ya no da la carpeta del script sino la actual.
set "APP_ROOT=%~dp0"

set "NO_RUN=false"
set "BUILD_DIR=build-release"
set "QT_DIR=C:\Qt\6.5.3\mingw_64"
set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"

:parse_args
if "%~1"=="" goto after_args
if /I "%~1"=="--no-run" set "NO_RUN=true"
shift
goto parse_args

:after_args
echo Implementando LGA Mighty Tools...

REM Cerrar SOLO las copias que corren desde deploy\ y build-release\ de ESTE repo (deploy\ se
REM borra mas abajo). Antes era "taskkill /F /IM", que cerraba tambien la instalada. Ver
REM tools\close_by_path.ps1. Sale con 2 solo si rechazo los parametros.
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName LGA_MightyTools.exe -ExactPath "%APP_ROOT%deploy\LGA_MightyTools.exe,%APP_ROOT%%BUILD_DIR%\LGA_MightyTools.exe"
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros & exit /b 1 )

echo Compilando (modo Release) via compilar.bat...
call "%APP_ROOT%compilar.bat" --release --no-run
if %ERRORLEVEL% neq 0 (
    echo Error en la compilacion.
    exit /b 1
)

if not exist "%BUILD_DIR%\LGA_MightyTools.exe" (
    echo ERROR: no se genero %BUILD_DIR%\LGA_MightyTools.exe
    exit /b 1
)

echo Limpiando carpeta de deploy anterior...
if exist deploy rmdir /S /Q deploy
mkdir deploy

echo Copiando ejecutable...
copy /Y "%BUILD_DIR%\LGA_MightyTools.exe" deploy\ >nul

echo Copiando runtime Qt y MinGW (dependencias directas del exe)...
copy /Y "%QT_DIR%\bin\Qt6Core.dll" deploy\ >nul
copy /Y "%QT_DIR%\bin\Qt6Gui.dll" deploy\ >nul
copy /Y "%QT_DIR%\bin\Qt6Widgets.dll" deploy\ >nul
copy /Y "%QT_DIR%\bin\Qt6Network.dll" deploy\ >nul
copy /Y "%MINGW_BIN%\libgcc_s_seh-1.dll" deploy\ >nul
copy /Y "%MINGW_BIN%\libstdc++-6.dll" deploy\ >nul
copy /Y "%MINGW_BIN%\libwinpthread-1.dll" deploy\ >nul

if not exist deploy\platforms mkdir deploy\platforms
copy /Y "%QT_DIR%\plugins\platforms\qwindows.dll" deploy\platforms\ >nul

REM Plugin TLS: sin el, QNetworkAccessManager no puede hacer HTTPS (falla en
REM silencio en el deploy portable, aunque el mismo exe ande bien corriendo
REM desde el arbol de Qt). Lo necesita el chequeo de updates.
if not exist deploy\tls mkdir deploy\tls
copy /Y "%QT_DIR%\plugins\tls\qschannelbackend.dll" deploy\tls\ >nul

REM Plugin ICO: las notificaciones sacan la imagen del .ico de la app; sin el, el toast sale sin
REM icono.
if not exist deploy\imageformats mkdir deploy\imageformats
copy /Y "%QT_DIR%\plugins\imageformats\qico.dll" deploy\imageformats\ >nul

set PATH=%PATH%;%QT_DIR%\bin;%MINGW_BIN%

REM windeployqt resuelve la clausura transitiva completa (ICU, ANGLE, etc. si
REM aplicaran). En este entorno, windeployqt detecta mal debug/release en los
REM plugins de C:\Qt\6.5.3\mingw_64\plugins (los reporta como "debug" pese a
REM ser release) y aborta con "Unable to find the platform plugin" ANTES de
REM copiar nada. Por eso las dependencias criticas (arriba) se copian a mano
REM primero, y windeployqt corre despues solo como complemento best-effort:
REM si falla, no es fatal, porque deploy\ ya tiene lo necesario para arrancar.
echo Ejecutando windeployqt como complemento (best-effort)...
set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    for /f "delims=" %%W in ('where windeployqt.exe 2^>nul') do set "WINDEPLOYQT=%%W"
)
if exist "%WINDEPLOYQT%" (
    "%WINDEPLOYQT%" --release --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --dir deploy deploy\LGA_MightyTools.exe
    if errorlevel 1 echo ADVERTENCIA: windeployqt devolvio error ^(no fatal, ver comentario arriba^).
) else (
    echo ADVERTENCIA: no se encontro windeployqt.exe. Se continua solo con las copias manuales.
)

echo Verificando dependencias criticas...
set DEPS_MISSING=false
if not exist "deploy\Qt6Core.dll" set DEPS_MISSING=true
if not exist "deploy\Qt6Gui.dll" set DEPS_MISSING=true
if not exist "deploy\Qt6Widgets.dll" set DEPS_MISSING=true
if not exist "deploy\Qt6Network.dll" set DEPS_MISSING=true
if not exist "deploy\libgcc_s_seh-1.dll" set DEPS_MISSING=true
if not exist "deploy\libstdc++-6.dll" set DEPS_MISSING=true
if not exist "deploy\libwinpthread-1.dll" set DEPS_MISSING=true
if not exist "deploy\platforms\qwindows.dll" set DEPS_MISSING=true
if not exist "deploy\tls\qschannelbackend.dll" set DEPS_MISSING=true

if "%DEPS_MISSING%"=="true" (
    echo ERROR: faltan dependencias criticas de Qt en deploy\. Verifica la instalacion de Qt 6.5.3 mingw_64.
    exit /b 1
)
echo Dependencias criticas verificadas.

echo.
echo Implementacion completada. App portable en carpeta 'deploy'.
echo.
if /I "%NO_RUN%"=="true" goto skip_run
start deploy\LGA_MightyTools.exe
goto end_run

:skip_run
echo Omitiendo ejecucion de LGA_MightyTools (--no-run).

:end_run
endlocal
exit /b 0
