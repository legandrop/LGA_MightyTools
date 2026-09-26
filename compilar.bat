@echo off
chcp 65001 >nul
REM Motor de compilacion Windows con Ninja para LGA_MightyTools.
REM Estructura tomada de LGA_LinkRedirector\compilar.bat.

set BUILD_TYPE=Debug
set BUILD_DIR=build
set NO_RUN=false
set FORCE_CLEAN=false
set SIM_SLOW=false
set PARALLEL_CORES=%NUMBER_OF_PROCESSORS%
REM La carpeta del script se toma ANTES de parsear: `shift` (sin /1) corre tambien el parametro
REM cero, asi que despues del parseo "~dp0" ya no da la carpeta del script sino la actual.
set "APP_ROOT=%~dp0"

:parse_args
if "%1"=="" goto after_args
if "%1"=="--release" (
    set BUILD_TYPE=Release
    set BUILD_DIR=build-release
    shift
    goto parse_args
)
if "%1"=="--no-run" (
    set NO_RUN=true
    shift
    goto parse_args
)
if "%1"=="--force-clean" (
    set FORCE_CLEAN=true
    shift
    goto parse_args
)
REM --sim-slow: lanza la app degradada (prioridad baja y 2 nucleos) para ver la fluidez en una
REM maquina lenta. Ver LGA_Base_QT_C_PydocsDoc_SimSlow.md.
if "%1"=="--sim-slow" (
    set SIM_SLOW=true
    shift
    goto parse_args
)
shift
goto parse_args

:after_args
cd /d "%APP_ROOT%"

REM La app vive en la bandeja: una instancia viva bloquea el .exe y el link falla
REM con "Permission denied". Cierre por ruta real (tools\close_by_path.ps1), nunca por nombre:
REM - Sin --no-run (compila y LANZA): la app es de instancia unica (QLockFile: con otra
REM   copia abierta, la nueva sale en silencio), asi que se cierran TODAS las copias (la
REM   instalada, la de build, las de otros checkouts) y la nueva queda como la unica. No lanza
REM   auxiliares: sin -Helpers.
REM - Con --no-run (el modo de toda corrida automatizada): SOLO la copia del arbol que se va a
REM   pisar (%BUILD_DIR%).
REM Sale con 2 solo si rechazo los parametros: ahi se corta.
if "%NO_RUN%"=="true" (
    powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName LGA_MightyTools.exe -ExactPath "%APP_ROOT%%BUILD_DIR%\LGA_MightyTools.exe"
) else (
    powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%APP_ROOT%tools\close_by_path.ps1" -ExeName LGA_MightyTools.exe -AllInstances
)
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros & exit /b 1 )
ping -n 2 127.0.0.1 >nul

REM Rutas de las toolchains. El bloque de dependencias las usa en vez de repetir
REM el literal.
set "QT_DIR=C:\Qt\6.5.3\mingw_64"
set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"

set PATH=%PATH%;%QT_DIR%\bin;%MINGW_BIN%;C:\Qt\Tools\Ninja

if "%FORCE_CLEAN%"=="true" (
    echo Limpiando build anterior...
    rmdir /s /q %BUILD_DIR% 2>nul
)

if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

if not exist "CMakeCache.txt" goto :configure
goto :compile

:configure
echo Configurando CMake con Ninja (%BUILD_TYPE%)...
cmake .. -G "Ninja" -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/mingw_64" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %ERRORLEVEL% neq 0 (
    echo Error en configuracion CMake
    cd ..
    exit /b 1
)

:compile
echo Compilando con %PARALLEL_CORES% nucleos usando Ninja...
ninja -j%PARALLEL_CORES%
if %ERRORLEVEL% neq 0 (
    echo Error en compilacion
    cd ..
    exit /b 1
)

cd ..

REM ============================================================
REM  DEPENDENCIAS DE RUNTIME: verificar -> copiar -> windeployqt -> verificar
REM
REM  Sin esto el .exe depende del PATH para encontrar Qt, y eso rompe al arrancar
REM  con Windows: la Run key lo lanza sin el PATH del script y muere con
REM  "Qt6Widgets.dll was not found" antes de escribir una sola linea de log.
REM  Peor: cuando SI habia algo en el PATH, cargaba Qt 6.8.2 y el runtime de
REM  MinGW que trae Git, o sea ni la version ni el compilador con los que se
REM  compilo. Con las DLL al lado del exe, Windows resuelve por directorio de la
REM  aplicacion y el PATH deja de importar.
REM ============================================================

REM Lista canonica, relativa al arbol de build. Derivada de find_package(Qt6
REM COMPONENTS Core Gui Widgets Network) en CMakeLists.txt. El plugin TLS
REM (tls\qschannelbackend.dll) va porque sin el QNetworkAccessManager falla todo
REM HTTPS en silencio: el chequeo de updates daria error de red siempre. El plugin
REM platforms\qoffscreen.dll va porque sin el una corrida con QT_QPA_PLATFORM=offscreen
REM (--ui-shot, QA headless) no arranca y Qt muestra un cartel fatal en el escritorio en vez
REM de fallar por consola. El plugin imageformats\qico.dll va porque las notificaciones sacan la
REM imagen del .ico de la app: sin el, el toast sale sin icono. Solo va al build: deploy.bat arma su
REM propia lista.
set "DEP_LIST=libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll platforms\qwindows.dll platforms\qoffscreen.dll tls\qschannelbackend.dll imageformats\qico.dll"

echo Verificando dependencias de runtime...
set DEPS_MISSING=false
for %%D in (%DEP_LIST%) do call :check_dep "%%D"

if "%DEPS_MISSING%"=="true" call :copy_missing_deps
if "%DEPS_MISSING%"=="true" (
    set DEPS_MISSING=false
    for %%D in (%DEP_LIST%) do call :check_dep "%%D"
)

REM Ultimo recurso: windeployqt, el unico que resuelve la clausura transitiva.
if "%DEPS_MISSING%"=="true" call :run_windeployqt
if "%DEPS_MISSING%"=="true" (
    set DEPS_MISSING=false
    for %%D in (%DEP_LIST%) do call :check_dep "%%D"
)

if "%DEPS_MISSING%"=="true" (
    echo.
    echo ERROR: faltan dependencias de runtime y no se pudieron reparar.
    echo        Verifica que Qt 6.5.3 mingw_64 este instalado en "%QT_DIR%".
    exit /b 1
)
echo Dependencias de runtime verificadas.

if not exist "%BUILD_DIR%\LGA_MightyTools.exe" (
    echo ERROR: CMake no genero %BUILD_DIR%\LGA_MightyTools.exe.
    exit /b 1
)

echo Compilacion completada a las %TIME%

if "%NO_RUN%"=="true" (
    echo Ejecucion omitida ^(--no-run^).
    exit /b 0
)

echo Iniciando LGA_MightyTools...
if "%SIM_SLOW%"=="true" (
    echo    --sim-slow: prioridad baja y 2 nucleos.
    start "" /LOW /AFFINITY 3 "%APP_ROOT%%BUILD_DIR%\LGA_MightyTools.exe"
    exit /b 0
)
start "" "%APP_ROOT%%BUILD_DIR%\LGA_MightyTools.exe"
exit /b 0

REM ============================================================
REM  Subrutinas
REM
REM  Van en subrutinas y no inline a proposito: cmd.exe expande los %VAR% de un
REM  bloque `if (...)` al PARSEARLO, no al ejecutarlo, asi que una variable que
REM  se escribe y se lee dentro del mismo bloque lee siempre el valor viejo.
REM ============================================================

:check_dep
if not exist "%BUILD_DIR%\%~1" (
    echo    [falta] %BUILD_DIR%\%~1
    set DEPS_MISSING=true
)
goto :eof

:copy_missing_deps
echo.
echo Faltan dependencias de runtime. Copiandolas...

REM Sin `2>nul`: una copia que falla tiene que verse.
call :copy_dep "%MINGW_BIN%" "" libgcc_s_seh-1.dll
call :copy_dep "%MINGW_BIN%" "" libstdc++-6.dll
call :copy_dep "%MINGW_BIN%" "" libwinpthread-1.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Core.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Gui.dll
call :copy_dep "%QT_DIR%\bin" "" Qt6Widgets.dll
call :copy_dep "%QT_DIR%\bin" ""
call :copy_dep "%QT_DIR%\bin" "" Qt6Network.dll
call :copy_dep "%QT_DIR%\plugins\platforms" "platforms" qwindows.dll
call :copy_dep "%QT_DIR%\plugins\platforms" "platforms" qoffscreen.dll
call :copy_dep "%QT_DIR%\plugins\tls" "tls" qschannelbackend.dll
call :copy_dep "%QT_DIR%\plugins\imageformats" "imageformats" qico.dll
goto :eof

:run_windeployqt
set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    for /f "delims=" %%W in ('where windeployqt.exe 2^>nul') do set "WINDEPLOYQT=%%W"
)
if not exist "%WINDEPLOYQT%" (
    echo ADVERTENCIA: no se encontro windeployqt.exe.
    goto :eof
)
echo Todavia faltan dependencias: probando con windeployqt...
"%WINDEPLOYQT%" --release --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --dir %BUILD_DIR% %BUILD_DIR%\LGA_MightyTools.exe
if errorlevel 1 echo ADVERTENCIA: windeployqt devolvio error.
goto :eof

:copy_dep
REM %1 = directorio origen, %2 = subdirectorio dentro del build [puede ir vacio],
REM %3 = nombre del archivo.
set "DEP_DEST=%BUILD_DIR%"
if not "%~2"=="" set "DEP_DEST=%BUILD_DIR%\%~2"
if exist "%DEP_DEST%\%~3" goto :eof
if not exist "%DEP_DEST%" mkdir "%DEP_DEST%"
if not exist "%~1\%~3" (
    echo    ERROR: no existe el origen "%~1\%~3"
    goto :eof
)
copy /Y "%~1\%~3" "%DEP_DEST%\" >nul
if errorlevel 1 (
    echo    ERROR: fallo la copia de "%~1\%~3"
) else (
    echo    [ok] %~3
)
goto :eof
