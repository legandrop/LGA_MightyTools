@echo off
setlocal enabledelayedexpansion
REM Uso: instalador.bat [--no-run] [--replace]
REM   --no-run   arma el instalador y su SHA256SUMS y no ofrece ejecutarlo ni revelarlo en el
REM              Explorer. La parte de commit y release sigue igual.
REM   --replace  si el release v<version> ya tiene el instalador de Windows, lo reemplaza.
REM El release v<version> lo crea la primera plataforma que publica, Windows o la Mac: si ya
REM existe, este script le suma el instalador y fusiona SHA256SUMS en vez de crearlo.
set "NO_RUN="
set "REPLACE="
for %%A in (%*) do (
    if /I "%%~A"=="--no-run" set "NO_RUN=1"
    if /I "%%~A"=="--replace" set "REPLACE=1"
)
REM Sin consola (corrida encadenada, automatizada o con la entrada redirigida) un choice puede
REM leer la respuesta de esa entrada y terminar ejecutando el instalador, abriendo el Explorer o
REM publicando. Se detecta ANTES de cualquier pregunta: sin consola no se ejecuta, no se revela ni
REM se publica nada, y las preguntas del preflight se resuelven como "solo generacion local". Si
REM PowerShell no corre, se toma como sin consola: no hacer nada es la direccion segura. Mismo
REM criterio que instalador.bat de LGA_VideoDownloader y LGA_SceneBuilder.
set "INTERACTIVE=1"
powershell -NoProfile -NonInteractive -Command "if ([Console]::IsInputRedirected) { exit 3 } else { exit 0 }"
if errorlevel 1 set "INTERACTIVE="
REM Solo se deshace un tag que creo ESTA corrida.
set "TAG_CREATED_HERE="
REM Codigo de salida deliberado (Doc_Migracion_Scripts_Build_Windows.md de LGA_Base_QT_C_Py,
REM 5.12): HAD_ERROR marca que un paso de PUBLICACION que se intento fallo (git add/commit/push,
REM tag, gh release). Se inicializa aca arriba para que no herede el valor del entorno. NO la
REM levanta lo que se saltea a proposito (el usuario dice que no, sin consola, gh ausente), ni el
REM refresco del manifiesto, que falla en silencio por diseno.
set "HAD_ERROR=false"
cd /d "%~dp0"
set "SCRIPT_DIR=%~dp0"
set "INSTALLER_DIR=%SCRIPT_DIR%installer"
REM El repo es PUBLICO y la release se publica en el MISMO repo del codigo:
REM legandrop/LGA_MightyTools. El tag del codigo y el de la release son el
REM mismo tag: se crea una sola vez, se pushea a origin, y se publica ahi.
set "PUBLIC_RELEASE_REPO=legandrop/LGA_MightyTools"
set "GITHUB_READY=true"
set "GH_CMD="
set "GITHUB_LOCAL_ONLY_CONFIRMED=false"

echo.
echo ============================================================
echo  Preflight checks de Git/GitHub
echo ============================================================
echo.

where git >nul 2>nul
if !errorlevel! EQU 0 (
    echo OK: Git disponible en PATH.
) else (
    echo ERROR: Git no esta disponible en PATH.
    echo Abortando...
    pause
    exit /b 1
)

git rev-parse --show-toplevel >nul 2>nul
if !errorlevel! EQU 0 (
    echo OK: Repositorio Git valido.
) else (
    echo ERROR: Esta carpeta no es un repositorio Git valido.
    echo Abortando...
    pause
    exit /b 1
)

for /f "tokens=*" %%B in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set "CURRENT_BRANCH=%%B"
if "!CURRENT_BRANCH!"=="" (
    echo ERROR: No se pudo detectar el branch activo de Git.
    echo Abortando...
    pause
    exit /b 1
) else (
    echo OK: Branch activo detectado: !CURRENT_BRANCH!
)

if /i "!CURRENT_BRANCH!" NEQ "main" (
    echo ERROR: El branch activo es '!CURRENT_BRANCH!' y este instalador debe generarse desde 'main'.
    echo Abortando...
    pause
    exit /b 1
)

set "HAS_PREEXISTING_CHANGES=false"
for /f "tokens=*" %%S in ('git status --porcelain 2^>nul') do (
    set "HAS_PREEXISTING_CHANGES=true"
)

if "!HAS_PREEXISTING_CHANGES!"=="true" (
    echo.
    echo ERROR: Hay cambios sin commitear antes de crear el instalador:
    git status --short
    echo.
    echo Abortando para evitar mezclar cambios previos con el commit del installer.
    pause
    exit /b 1
) else (
    echo OK: El repositorio esta limpio antes de crear el instalador.
)

where gh >nul 2>nul
if !errorlevel! EQU 0 (
    set "GH_CMD=gh"
) else (
    if exist "C:\Program Files\GitHub CLI\gh.exe" (
        set "GH_CMD=C:\Program Files\GitHub CLI\gh.exe"
    )
)

if "!GH_CMD!"=="" (
    echo AVISO: GitHub CLI [gh] no esta instalado.
    set "GITHUB_READY=false"
) else (
    echo OK: GitHub CLI encontrado.
    "!GH_CMD!" auth status >nul 2>nul
    if !errorlevel! NEQ 0 (
        echo AVISO: GitHub CLI no esta autenticado.
        set "GITHUB_READY=false"
    ) else (
        echo OK: GitHub CLI autenticado.
        git ls-remote --exit-code origin HEAD >nul 2>nul
        if !errorlevel! NEQ 0 (
            echo AVISO: No se pudo acceder al remoto origin.
            set "GITHUB_READY=false"
        ) else (
            echo OK: Acceso al remoto origin confirmado.
            "!GH_CMD!" repo view "%PUBLIC_RELEASE_REPO%" --json name -q .name >nul 2>nul
            if !errorlevel! NEQ 0 (
                echo AVISO: No se pudo acceder al repo publico de releases %PUBLIC_RELEASE_REPO%.
                set "GITHUB_READY=false"
            ) else (
                echo OK: Acceso al repo publico de releases confirmado.
            )
        )
    )
)

if /i "!GITHUB_READY!" NEQ "true" (
    echo.
    echo Los chequeos de GitHub fallaron.
    echo Se podra generar el instalador local, pero no publicar la release desde este .bat.
    echo.
    if not defined INTERACTIVE (
        echo Sin consola interactiva: se sigue solo con la generacion local, sin preguntar.
    ) else (
        choice /C YN /M "Continuar solo con la generacion local del instalador?"
        if !errorlevel! NEQ 1 (
            echo Operacion cancelada por el usuario.
            pause
            exit /b 1
        )
        echo OK: Se continuara solo con la generacion local del instalador.
    )
    set "GITHUB_LOCAL_ONLY_CONFIRMED=true"
)

echo Creando instalador de LGA Mighty Tools...

REM Buscar Inno Setup. Va ANTES del build: si falta, enterarse en dos segundos y
REM no despues de una compilacion completa.
set ISCC=""
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" set ISCC="C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if exist "C:\Program Files\Inno Setup 6\ISCC.exe" set ISCC="C:\Program Files\Inno Setup 6\ISCC.exe"

if %ISCC%=="" (
    echo ERROR: No se encontro Inno Setup 6.
    echo Descargar desde: https://jrsoftware.org/isdl.php
    exit /b 1
)

REM ============================================================================
REM VERSION: la fuente unica es CMakeLists.txt (`project(LGA_MightyTools
REM VERSION x.y ...)`). No hay archivo VERSION ni sync_version.bat: se extrae
REM directo de la linea `project(...)` antes de compilar.
REM ============================================================================
echo Extrayendo version desde CMakeLists.txt...
set "VERSION="
for /f "tokens=1-4" %%A in ('findstr /B /C:"project(LGA_MightyTools" CMakeLists.txt') do set "VERSION=%%C"

if "%VERSION%"=="" (
    echo ERROR: No se pudo extraer la version desde CMakeLists.txt.
    echo        Se esperaba una linea del tipo: project^(LGA_MightyTools VERSION x.y LANGUAGES CXX^)
    exit /b 1
)
echo OK: Version detectada: %VERSION%

REM Notas para el usuario [What's new]: el control va ANTES de compilar porque puede preguntar. Si
REM corta, no se publica: se arma el instalador local, igual que con cualquier otro chequeo de GitHub.
if /i "!GITHUB_READY!"=="true" (
    call :notes_check
    if errorlevel 1 set "GITHUB_READY=false"
)

REM Cerrar SOLO las copias que corren desde deploy\ y build-release\ de ESTE repo, para evitar
REM bloqueos durante deploy/installer. Antes era "taskkill /F /IM", que cerraba tambien la
REM instalada. Ver tools\close_by_path.ps1. Sale con 2 solo si rechazo los parametros.
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0tools\close_by_path.ps1" -ExeName LGA_MightyTools.exe -ExactPath "%~dp0deploy\LGA_MightyTools.exe,%~dp0build-release\LGA_MightyTools.exe"
if %ERRORLEVEL% equ 2 ( echo Error: close_by_path rechazo los parametros & exit /b 1 )

REM Ejecutar deploy automaticamente (sin abrir la app al finalizar)
echo Ejecutando deploy.bat --no-run...
call "%~dp0deploy.bat" --no-run
if %ERRORLEVEL% neq 0 (
    echo ERROR: Fallo deploy.bat. Abortando creacion de instalador.
    exit /b 1
)

REM ============================================================================
REM GUARD: el binario compilado tiene que llevar la version que se va a publicar.
REM
REM Se busca una cadena dentro del binario porque el .exe no lleva recurso
REM VERSIONINFO (FileVersion/ProductVersion vienen vacios) y la app no tiene un
REM flag `--version`: el string compilado es la unica evidencia disponible.
REM
REM Se busca el MARCADOR LGA_MIGHTYTOOLS_BUILD_VERSION y NO el numero suelto,
REM para no confundirlo con otros literales 0.NNN que pueda traer el binario.
REM ============================================================================
echo Verificando que el binario reporte la version %VERSION%...
findstr /C:"LGA_MIGHTYTOOLS_BUILD_VERSION=%VERSION%" "%~dp0deploy\LGA_MightyTools.exe" >nul 2>nul
if errorlevel 1 (
    echo ERROR: deploy\LGA_MightyTools.exe NO contiene la version %VERSION%.
    echo        El binario se compilo con otra version, asi que el instalador
    echo        publicaria un numero que la app no reporta.
    echo        Revisar que CMakeLists.txt y el binario compilado coincidan.
    exit /b 1
)
echo OK: El binario reporta la version %VERSION%.

REM Estado del tag y del release v<version>: pueden no existir (se crean), o existir porque la Mac
REM publico primero (se les suma el instalador). Frena si el tag es otro codigo, si el release es un
REM borrador o si ya tiene este instalador sin --replace. Ver :publish_analysis.
if /i "!GITHUB_READY!"=="true" (
    call :publish_analysis
    if errorlevel 1 set "GITHUB_READY=false"
)

if /i "!GITHUB_READY!" NEQ "true" if /i "!GITHUB_LOCAL_ONLY_CONFIRMED!" NEQ "true" (
    echo.
    echo No se podra publicar la release v%VERSION% desde este .bat.
    echo Se podra generar el instalador local igualmente.
    echo.
    if not defined INTERACTIVE (
        echo Sin consola interactiva: se sigue solo con la generacion local, sin preguntar.
    ) else (
        choice /C YN /M "Continuar solo con la generacion local del instalador?"
        if !errorlevel! NEQ 1 (
            echo Operacion cancelada por el usuario.
            pause
            exit /b 1
        )
        echo OK: Se continuara solo con la generacion local del instalador.
    )
    set "GITHUB_LOCAL_ONLY_CONFIRMED=true"
)

if not exist "%INSTALLER_DIR%" mkdir "%INSTALLER_DIR%"

set "OUTPUT_EXE=%INSTALLER_DIR%\LGA_MightyTools_Setup_v%VERSION%.exe"
set "SUMS_FILE=%INSTALLER_DIR%\SHA256SUMS"
if exist "%OUTPUT_EXE%" del /F /Q "%OUTPUT_EXE%" >nul 2>nul
REM El SHA256SUMS de una corrida anterior se borra ANTES de compilar: si esta corrida no llega
REM a generarlo, no puede quedar uno viejo para publicarse junto al instalador nuevo.
if exist "%SUMS_FILE%" del /F /Q "%SUMS_FILE%" >nul 2>nul
if exist "%SUMS_FILE%" (
    echo ERROR: No se pudo borrar el SHA256SUMS anterior: %SUMS_FILE%
    exit /b 1
)

%ISCC% /DMyAppVersion=%VERSION% LGA_MightyTools_installer.iss
if %ERRORLEVEL% neq 0 (
    echo Aviso: primer intento de compilacion fallo. Reintentando en 5 segundos...
    ping 127.0.0.1 -n 6 >nul
    %ISCC% /DMyAppVersion=%VERSION% LGA_MightyTools_installer.iss
)
if %ERRORLEVEL% neq 0 (
    echo ERROR: Fallo la compilacion del instalador con Inno Setup.
    exit /b 1
)

REM SHA256SUMS del release, formato sha256sum ("hash  nombre", LF, sin BOM), como en
REM LGA_VideoDownloader. El auto-update de esta app hoy verifica con el digest del manifiesto de
REM LGA_Updates, pero el contrato de Doc_Instaladores_Inno.md 7 pide este archivo en todo release.
powershell -NoProfile -NonInteractive -Command "$n='LGA_MightyTools_Setup_v%VERSION%.exe'; $h=(Get-FileHash -Algorithm SHA256 ('installer\'+$n)).Hash.ToLower(); [IO.File]::WriteAllText('installer\SHA256SUMS', $h+'  '+$n+[char]10)"
if errorlevel 1 (
    echo ERROR: No se pudo generar installer\SHA256SUMS.
    exit /b 1
)
if not exist "%SUMS_FILE%" (
    echo ERROR: No se genero installer\SHA256SUMS.
    exit /b 1
)
echo SHA256SUMS: %SUMS_FILE%

echo.
echo Instalador creado: %OUTPUT_EXE%

REM ---------------------------------------------------------------- instalar local
REM Primero se ofrece instalar / revelar el .exe; recien despues commit + GitHub.
REM El instalador corre en primer plano para que las preguntas de release aparezcan
REM recien cuando el wizard cierra.
REM Con --no-run o sin consola no se pregunta nada de esto (ver arriba, INTERACTIVE).
REM
REM Se resuelve con una bandera y no con un goto: cuando este .bat estaba en LF, cmd.exe calculaba
REM mal donde empieza cada linea al buscar una etiqueta, y un goto a una etiqueta nueva de este
REM tramo ("after_local") fallo con "The system cannot find the batch label specified". Hoy los
REM .bat van en CRLF (.gitattributes), que es lo que cmd.exe necesita.
echo.
set "OFFER_LOCAL=1"
if defined NO_RUN (
    echo Instalador no ejecutado ni revelado [--no-run].
    set "OFFER_LOCAL="
)
if not defined INTERACTIVE (
    echo Sin consola interactiva: el instalador no se ejecuta ni se revela.
    set "OFFER_LOCAL="
)
if defined OFFER_LOCAL (
    choice /C YN /M "Desea ejecutar el instalador ahora mismo (instalar local)?"
    if !errorlevel! EQU 1 (
        REM Antes, la copia abierta, sea la de build, la instalada u otra: primero se le pide que salga
        REM como desde Quit de la bandeja, asi se lleva su icono; un cierre forzado lo deja como
        REM fantasma y parecen dos copias. Lo pide el exe recien armado en deploy, que conoce --quit.
        REM Si la que esta abierta es vieja y no lo entiende, se cierra por ruta, todas las copias.
        echo Cerrando la copia de Mighty Tools que este abierta...
        "%~dp0deploy\LGA_MightyTools.exe" --quit
        powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0tools\close_by_path.ps1" -ExeName LGA_MightyTools.exe -AllInstances
        echo Ejecutando el instalador...
        "%OUTPUT_EXE%"
    ) else (
        echo Instalador no ejecutado.
    )

    choice /C YN /M "Desea revelar el instalador en Windows Explorer?"
    if !errorlevel! EQU 1 (
        explorer /select,"%OUTPUT_EXE%"
    ) else (
        echo No se abrio Windows Explorer.
    )
)

if not defined INTERACTIVE (
    echo Sin consola interactiva: no se ofrece commit ni release.
    goto :END
)
if /i "!GITHUB_READY!" NEQ "true" goto :END

echo.
echo ============================================================
echo  Commit y release opcional
echo ============================================================
echo.

set "RELEASE_ALLOWED=true"
set "COMMIT_CREATED=false"
set "HAS_INSTALLER_CHANGES=false"
for /f "tokens=*" %%S in ('git status --porcelain 2^>nul') do (
    set "HAS_INSTALLER_CHANGES=true"
)

REM Si el tag ya existe [lo creo la Mac], el release sale del codigo de ESE tag: un commit nuevo lo
REM dejaria atras. Con cambios sin commitear despues de compilar no se publica.
if "!HAS_INSTALLER_CHANGES!"=="true" if defined TAG_REMOTE (
    echo ERROR: Hay cambios sin commitear despues de crear el instalador y el tag v%VERSION% ya existe:
    git status --short
    echo No se sube el instalador: un commit nuevo ya no coincidiria con ese tag.
    set "HAD_ERROR=true"
    set "RELEASE_ALLOWED=false"
    set "HAS_INSTALLER_CHANGES=false"
)

if "!HAS_INSTALLER_CHANGES!"=="true" (
    echo Cambios detectados despues de crear el instalador:
    git status --short
    echo.
    choice /C YN /M "Desea commitear estos cambios como installer_v%VERSION%?"
    if !errorlevel! EQU 1 (
        echo Haciendo commit de cambios...
        git add -A
        if !errorlevel! NEQ 0 (
            echo ERROR: git add fallo.
            set "HAD_ERROR=true"
            set "RELEASE_ALLOWED=false"
        ) else (
            git commit -m "installer_v%VERSION%"
            if !errorlevel! NEQ 0 (
                echo AVISO: git commit retorno codigo !errorlevel!.
                REM Si el usuario elige seguir, la release se publica igual, taggeando el ultimo
                REM commit que haya, y el script termina en 1: el commit que se intento fallo.
                set "HAD_ERROR=true"
                echo Puede que no haya cambios nuevos o que haya ocurrido un error.
                echo.
                choice /C YN /M "Desea continuar con la release sin un commit nuevo?"
                if !errorlevel! NEQ 1 (
                    set "RELEASE_ALLOWED=false"
                )
            ) else (
                set "COMMIT_CREATED=true"
            )
        )
    ) else (
        echo Commit omitido por el usuario.
        echo No se ofrecera publicar release para evitar taggear un estado no commiteado.
        set "RELEASE_ALLOWED=false"
    )
)
REM Sin cambios no se pregunta nada: lo normal es que el arbol ya este commiteado y pusheado, y la
REM pregunta que sigue (subir la release) ya deja decir que no.

if /i "!COMMIT_CREATED!"=="true" (
    echo Haciendo push a origin/!CURRENT_BRANCH!...
    git push origin "!CURRENT_BRANCH!"
    if !errorlevel! NEQ 0 (
        echo ERROR: git push fallo.
        set "HAD_ERROR=true"
        echo Verificar conexion a internet y permisos del repositorio.
        set "RELEASE_ALLOWED=false"
    )
)

REM El instalador y el SHA256SUMS de ESTA corrida tienen que estar antes de crear el tag: sin el
REM SHA256SUMS la release quedaria publicada sin el hash que pide el contrato de update. Van antes
REM del goto de abajo, sin goto propio (ver la nota en el tramo de instalar local).
if not exist "%OUTPUT_EXE%" (
    echo ERROR: No se encontro el instalador: %OUTPUT_EXE%
    set "HAD_ERROR=true"
    set "RELEASE_ALLOWED=false"
)
if not exist "%SUMS_FILE%" (
    echo ERROR: No se encontro %SUMS_FILE%. No se crea el tag ni la release.
    set "HAD_ERROR=true"
    set "RELEASE_ALLOWED=false"
)

if /i "!RELEASE_ALLOWED!" NEQ "true" goto :END

echo.
choice /C YN /M "Desea subir el instalador como release v%VERSION% a GitHub?"
if !errorlevel! NEQ 1 goto :END

echo.
REM Se vuelve a mirar el tag y el release: pasaron la compilacion y las preguntas, y la Mac pudo
REM publicar en el medio.
call :publish_analysis
if errorlevel 1 (
    set "HAD_ERROR=true"
    goto :END
)
if defined TAG_REMOTE goto :PUB_TAG_DONE

REM El tag se crea solo sobre un commit que ya esta en origin/main: un tag sobre un commit sin
REM pushear dejaria el release fuera de main, y la otra plataforma no podria sumarse.
git fetch -q origin
if !errorlevel! NEQ 0 (
    echo ERROR: No se pudo leer origin.
    set "HAD_ERROR=true"
    goto :END
)
git merge-base --is-ancestor HEAD origin/main
if !errorlevel! NEQ 0 (
    echo ERROR: HEAD no esta en origin/main. Pushear antes de publicar.
    set "HAD_ERROR=true"
    goto :END
)

echo Creando tag v%VERSION%...
git tag -a "v%VERSION%" -m "Release v%VERSION%"
if !errorlevel! NEQ 0 (
    echo ERROR: No se pudo crear el tag v%VERSION%.
    echo Es posible que el tag ya exista.
    set "HAD_ERROR=true"
    goto :END
)
set "TAG_CREATED_HERE=1"

echo Haciendo push del tag v%VERSION%...
git push origin "v%VERSION%"
if !errorlevel! NEQ 0 (
    echo ERROR: No se pudo hacer push del tag v%VERSION%.
    echo El tag local fue creado, pero no se publico en origin.
    set "HAD_ERROR=true"
    goto :END
)

REM Tag listo en origin: o ya estaba [lo creo la Mac] o se acaba de crear.
:PUB_TAG_DONE
if defined REL_EXISTS goto :PUB_UPLOAD

echo.
echo Creando release en GitHub...
"!GH_CMD!" release create "v%VERSION%" "%OUTPUT_EXE%" "%SUMS_FILE%" --repo "%PUBLIC_RELEASE_REPO%" --target "main" --title "v%VERSION%" --notes "Release v%VERSION%"
if !errorlevel! NEQ 0 (
    echo ERROR: No se pudo crear la release en GitHub.
    set "HAD_ERROR=true"
    REM Solo se ofrece borrar un tag que creo ESTA corrida: uno que ya estaba [lo creo la Mac] no es
    REM nuestro y el release de esa plataforma puede depender de el.
    if not defined TAG_CREATED_HERE (
        echo.
        echo El tag v%VERSION% ya existia antes de esta corrida: se conserva.
        echo Si GitHub dejo un release en borrador de v%VERSION%, borrarlo a mano antes de reintentar.
        goto :END
    )
    REM Si en el medio la otra plataforma creo el release y ya subio lo suyo, no se ofrece borrar
    REM nada: borrar el tag se llevaria tambien ese release.
    set "MT_REL_HAS_MAC="
    "!GH_CMD!" release view "v%VERSION%" --repo "%PUBLIC_RELEASE_REPO%" --json assets -q ".assets[].name" > "%TEMP%\mt_rel_assets.txt" 2>nul
    findstr /C:"_Mac_v" "%TEMP%\mt_rel_assets.txt" >nul 2>nul && set "MT_REL_HAS_MAC=1"
    del "%TEMP%\mt_rel_assets.txt" >nul 2>nul
    if defined MT_REL_HAS_MAC (
        echo.
        echo El release v%VERSION% ya existe en GitHub con los paquetes de macOS: no se borra nada.
        echo Volver a correr instalador.bat para sumar el instalador de Windows a ese release.
        goto :END
    )
    echo.
    echo El commit y el push del branch ya fueron hechos si correspondia.
    echo El tag v%VERSION% ya fue creado y subido a origin.
    echo.
    echo Recomendado: borrar el tag local/remoto para dejar GitHub limpio
    echo y volver a intentar la release despues.
    echo.
    choice /C YN /M "Desea borrar el tag v%VERSION% local/remoto ahora?"
    if !errorlevel! EQU 1 (
        "!GH_CMD!" release delete "v%VERSION%" --repo "%PUBLIC_RELEASE_REPO%" --yes >nul 2>nul
        git tag -d "v%VERSION%" >nul 2>nul
        git push origin ":refs/tags/v%VERSION%" >nul 2>nul
        echo Tag v%VERSION% borrado local/remoto.
    ) else (
        echo Se conserva el tag v%VERSION%.
        echo Puede crear la release manualmente desde:
        echo https://github.com/%PUBLIC_RELEASE_REPO%/releases
    )
    goto :END
)
goto :PUB_NOTES

REM El release ya existe [lo creo la Mac]: se le suma el instalador y se fusiona SHA256SUMS.
:PUB_UPLOAD
call :publish_upload
if errorlevel 1 (
    set "HAD_ERROR=true"
    goto :END
)

REM Las notas van DESPUES de crear o completar el release y ANTES de avisarle al manifiesto. Si
REM fallan, el release ya esta publicado: se informa el comando para reintentar y no se deshace nada.
:PUB_NOTES
call :publish_notes
if errorlevel 1 (
    set "HAD_ERROR=true"
    goto :END
)

echo.
echo ============================================================
echo  Release v%VERSION% publicada exitosamente en GitHub!
echo  https://github.com/%PUBLIC_RELEASE_REPO%/releases/tag/v%VERSION%
echo ============================================================

REM ---- Avisarle al manifiesto de versiones que hay algo nuevo ----
REM Sin esto hay que esperar al cron de legandrop/LGA_Updates, que corre cada 30 minutos:
REM hasta entonces el card de LGA Updates de PipeSync no ve la version recien publicada.
REM
REM Falla en SILENCIO a proposito. La release ya esta publicada y el cron la va a levantar
REM igual, asi que no tiene sentido ensuciar el final de una publicacion exitosa con un
REM error por algo que se arregla solo.
REM
REM El `<nul` cierra la entrada estandar. Hoy `refresh_versions.yml` no declara inputs en su
REM `workflow_dispatch`, pero si alguien le agrega uno, `gh` pasa a modo interactivo y pide
REM el valor por un prompt que aca esta redirigido a nul: el instalador quedaria colgado, en
REM silencio y justo despues de una publicacion exitosa.
"!GH_CMD!" workflow run refresh_versions.yml --repo legandrop/LGA_Updates >nul 2>nul <nul
if !errorlevel! EQU 0 (
    echo Manifiesto de versiones: refresco disparado.
) else (
    echo AVISO: no se pudo disparar el refresco del manifiesto. El cron lo levanta solo.
)

:END
set "FINAL_EXIT=0"
if /i "!HAD_ERROR!"=="true" set "FINAL_EXIT=1"
endlocal & exit /b %FINAL_EXIT%

REM ==== PUBLICAR: subrutinas. Todas se llaman con call y devuelven con exit /b.

:notes_check
REM Notas para el usuario [What's new]: la logica vive en LGA_RepoTools; LGA_REPOTOOLS apunta a otra
REM copia. Deja WN_BAT y WN_FILE para :publish_notes. exit /b 1 si faltan, fallan o se contesto que no.
set "WN_REPOTOOLS=%LGA_REPOTOOLS%"
if not defined WN_REPOTOOLS set "WN_REPOTOOLS=%SCRIPT_DIR%..\LGA_RepoTools"
set "WN_BAT=%WN_REPOTOOLS%\WhatsNew_Win\whats_new_release.bat"
set "WN_FILE=%SCRIPT_DIR%Docs\WhatsNew.md"
if not exist "%WN_BAT%" (
    echo AVISO: no encontre "%WN_BAT%".
    echo Clonar LGA_RepoTools al lado de este repo o definir LGA_REPOTOOLS. Sin notas no se publica.
    exit /b 1
)
echo Verificando las notas para el usuario [What's new] de v%VERSION%...
call "%WN_BAT%" check "%WN_FILE%" "%VERSION%"
if errorlevel 1 (
    echo AVISO: faltan o fallan las notas de v%VERSION%, o se contesto que no. No se publica.
    exit /b 1
)
exit /b 0

:publish_analysis
REM Estado del tag y del release v<version> en origin. Deja TAG, TAG_REMOTE, TAG_COMMIT y REL_EXISTS.
REM exit /b 1 si no se puede publicar. Corre despues de compilar y otra vez justo antes de publicar.
REM  - El tag no existe: se crea mas adelante sobre HEAD [como siempre].
REM  - El tag existe en origin [lo creo la Mac, o una corrida anterior]: el instalador tiene que salir
REM    de ese mismo codigo, ver :tag_matches.
REM  - Un tag local que NO esta en origin es de una corrida que no llego a pushear: se frena.
set "TAG=v%VERSION%"
set "TAG_REMOTE="
set "TAG_COMMIT="
set "TAG_LOCAL="
git rev-parse -q --verify "refs/tags/%TAG%" >nul 2>nul
if not errorlevel 1 set "TAG_LOCAL=1"
git ls-remote --exit-code --tags origin "refs/tags/%TAG%" >nul 2>nul
set "LS_RC=%ERRORLEVEL%"
if "%LS_RC%"=="0" set "TAG_REMOTE=1"
if not "%LS_RC%"=="0" if not "%LS_RC%"=="2" (
    echo AVISO: No se pudo leer los tags de origin.
    exit /b 1
)
if defined TAG_REMOTE goto :analysis_tag_remote
if defined TAG_LOCAL (
    echo AVISO: El tag local %TAG% ya existe y no esta en origin: es de una corrida que no llego a pushear.
    echo        Borrarlo con git tag -d %TAG% y volver a correr.
    exit /b 1
)
echo OK: El tag %TAG% no existe todavia: se crea sobre HEAD.
goto :analysis_release
:analysis_tag_remote
REM Se trae el tag [y su historia] para poder compararlo con HEAD. Si ya hay un tag local con ese
REM nombre y distinto, el fetch no lo pisa y falla.
git fetch -q origin "refs/tags/%TAG%:refs/tags/%TAG%" >nul 2>nul
if errorlevel 1 (
    echo AVISO: No se pudo traer el tag %TAG% de origin, o hay un tag local con ese nombre y distinto.
    exit /b 1
)
for /f %%C in ('git rev-list -n 1 "refs/tags/%TAG%"') do set "TAG_COMMIT=%%C"
call :tag_matches
if errorlevel 1 exit /b 1
:analysis_release
call :release_check
if errorlevel 1 exit /b 1
if defined REL_EXISTS if not defined TAG_REMOTE (
    echo AVISO: El release %TAG% existe en %PUBLIC_RELEASE_REPO% pero su tag no esta en origin. No se toca: revisarlo a mano.
    exit /b 1
)
exit /b 0

:tag_matches
REM HEAD tiene que ser el commit del tag, o uno posterior que difiera SOLO en WHATS_NEW.md: las notas
REM de la plataforma que publico primero dejan un commit en main hecho por la API, y quien llega
REM segundo y hace git pull tiene HEAD un commit mas adelante, con el mismo codigo.
set "HEAD_SHA="
for /f %%C in ('git rev-parse HEAD') do set "HEAD_SHA=%%C"
if not defined TAG_COMMIT (
    echo AVISO: No se pudo leer a que commit apunta el tag %TAG%.
    exit /b 1
)
if /i "%HEAD_SHA%"=="%TAG_COMMIT%" (
    echo OK: El tag %TAG% de origin coincide con HEAD.
    exit /b 0
)
git merge-base --is-ancestor %TAG_COMMIT% HEAD
if errorlevel 1 goto :tag_mismatch
set "TAG_EXTRA="
for /f "delims=" %%F in ('git diff --name-only %TAG_COMMIT% HEAD') do if /i not "%%F"=="WHATS_NEW.md" set "TAG_EXTRA=1"
if defined TAG_EXTRA goto :tag_mismatch
echo OK: El tag %TAG% de origin esta en HEAD o antes, y HEAD solo agrega las notas [WHATS_NEW.md]: mismo codigo.
exit /b 0
:tag_mismatch
echo AVISO: HEAD no es el commit del tag %TAG% ni uno posterior que solo agregue las notas [WHATS_NEW.md].
echo        HEAD: %HEAD_SHA%
echo        tag:  %TAG_COMMIT%
echo        Hacer git pull, o revisar si el tag se creo sobre otro codigo.
exit /b 1

:release_check
REM Lo que hay en el release v<version> antes de tocarlo. Deja REL_EXISTS [vacio si no existe],
REM REL_SUMS, REL_OWN y REL_OTHER. Que no exista es un estado valido: se crea. Corta si es un borrador,
REM si ya tiene este instalador [salvo --replace] o si tiene archivos de macOS sin SHA256SUMS: fusionar
REM contra nada borraria sus lineas, y el auto-update de la Mac dejaria de verlo.
set "REL_EXISTS="
set "REL_DRAFT=0"
set "REL_SUMS=0"
set "REL_OWN=0"
set "REL_OTHER=0"
set "MT_RC=%TEMP%\mt_release_check_%RANDOM%%RANDOM%"
mkdir "%MT_RC%" >nul 2>nul
REM Las respuestas de gh van a archivos: un gh entre comillas adentro de un for /f no itera. Se leen
REM con PowerShell porque gh escribe solo LF y findstr /X no las reconoce.
call "%GH_CMD%" release view "%TAG%" --repo "%PUBLIC_RELEASE_REPO%" --json isDraft -q ".isDraft" >"%MT_RC%\draft.txt" 2>"%MT_RC%\err.txt"
if errorlevel 1 goto :release_check_view_failed
call "%GH_CMD%" release view "%TAG%" --repo "%PUBLIC_RELEASE_REPO%" --json assets -q ".assets[].name" >"%MT_RC%\assets.txt" 2>nul
if errorlevel 1 goto :release_check_gh_failed
set "REL_EXISTS=1"
set "MT_OWN_ASSET=LGA_MightyTools_Setup_v%VERSION%.exe"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $d=$env:MT_RC; $a=@(Get-Content -LiteralPath (Join-Path $d 'assets.txt') | ForEach-Object { $_.Trim() }); $draft=@(Get-Content -LiteralPath (Join-Path $d 'draft.txt') | ForEach-Object { $_.Trim() }) -contains 'true'; $other=@($a | Where-Object { $_ -like 'LGA_MightyTools_Mac_v*' }).Count -gt 0; Set-Content -LiteralPath (Join-Path $d 'state.txt') -Encoding Ascii -Value ('REL_DRAFT=' + [int]$draft), ('REL_SUMS=' + [int]($a -contains 'SHA256SUMS')), ('REL_OWN=' + [int]($a -contains $env:MT_OWN_ASSET)), ('REL_OTHER=' + [int]$other)"
if errorlevel 1 goto :release_check_gh_failed
for /f "usebackq tokens=1,2 delims==" %%A in ("%MT_RC%\state.txt") do set "%%A=%%B"
rmdir /S /Q "%MT_RC%" >nul 2>nul
if "%REL_DRAFT%"=="1" (
    echo AVISO: El release %TAG% existe como borrador. Publicarlo o borrarlo a mano antes de seguir.
    exit /b 1
)
if "%REL_OWN%"=="1" if not defined REPLACE (
    echo AVISO: El release %TAG% ya tiene %MT_OWN_ASSET%. Para reemplazarlo, correr con --replace.
    exit /b 1
)
if "%REL_OTHER%"=="1" if "%REL_SUMS%"=="0" (
    echo AVISO: El release %TAG% tiene los archivos de macOS pero no SHA256SUMS: no se fusiona contra
    echo        nada, porque se perderian sus lineas. Subir primero el SHA256SUMS de la Mac.
    exit /b 1
)
echo OK: El release %TAG% ya existe en %PUBLIC_RELEASE_REPO%: se le suma el instalador de Windows.
exit /b 0
:release_check_view_failed
REM gh dice "release not found" cuando el release no existe; cualquier otro error es otra cosa.
findstr /I /C:"not found" "%MT_RC%\err.txt" >nul 2>nul
if not errorlevel 1 (
    rmdir /S /Q "%MT_RC%" >nul 2>nul
    echo OK: El release %TAG% no existe todavia en %PUBLIC_RELEASE_REPO%: se crea.
    exit /b 0
)
type "%MT_RC%\err.txt"
:release_check_gh_failed
echo AVISO: No se pudo leer el release %TAG% de %PUBLIC_RELEASE_REPO%.
rmdir /S /Q "%MT_RC%" >nul 2>nul
exit /b 1

:publish_upload
REM El release v<version> ya existe [lo creo la Mac]. SHA256SUMS es UNO para las dos plataformas: se
REM baja el del release, se reemplaza solo la linea del instalador de Windows [se agrega si no
REM estaba] y se resube. Todo lo que puede cortar [leer, fusionar] pasa antes de escribir nada. El
REM .exe sube ANTES que el SHA256SUMS: en el medio la app no ofrece nada que no pueda verificar.
echo.
echo El release %TAG% ya existe: se suma el instalador de Windows.
set "WORK=%TEMP%\mt_release_%RANDOM%%RANDOM%"
mkdir "%WORK%" >nul 2>nul
if "%REL_SUMS%"=="0" goto :publish_merge
call "%GH_CMD%" release download "%TAG%" --repo "%PUBLIC_RELEASE_REPO%" --pattern SHA256SUMS --dir "%WORK%"
if errorlevel 1 goto :publish_upload_failed
:publish_merge
REM Una sola linea por nombre: las del release que no son el instalador de Windows, y la propia al
REM final. Una linea ilegible corta: no se adivina que era. El resultado queda en una ruta fija para
REM poder resubirlo a mano si la subida falla [--clobber borra el viejo antes de subir].
mkdir "%INSTALLER_DIR%\release" >nul 2>nul
set "MT_OLD=%WORK%\SHA256SUMS"
set "MT_OWN=%SUMS_FILE%"
set "MT_OUT=%INSTALLER_DIR%\release\SHA256SUMS"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $re='^([0-9a-fA-F]{64}) [ *](.+)$'; $own=@(([IO.File]::ReadAllText($env:MT_OWN)) -split '\r?\n' | Where-Object { $_ -match $re }); if ($own.Count -ne 1) { Write-Host 'ERROR: installer\SHA256SUMS no tiene una sola linea valida'; exit 1 }; $null=$own[0] -match $re; $seen=@{}; $seen[$Matches[2]]=1; $keep=New-Object System.Collections.Generic.List[string]; if (Test-Path -LiteralPath $env:MT_OLD) { foreach ($l in (([IO.File]::ReadAllText($env:MT_OLD)) -split '\r?\n')) { if ($l -match $re) { if (-not $seen.ContainsKey($Matches[2])) { $seen[$Matches[2]]=1; $keep.Add($l) } } elseif ($l.Trim()) { Write-Host ('ERROR: el SHA256SUMS del release tiene una linea ilegible: ' + $l); exit 1 } } }; $keep.Add($own[0]); [IO.File]::WriteAllText($env:MT_OUT, (($keep -join [char]10) + [char]10)); Write-Host ('SHA256SUMS fusionado: ' + $keep.Count + ' lineas')"
if errorlevel 1 goto :publish_upload_failed
call "%GH_CMD%" release upload "%TAG%" "%OUTPUT_EXE%" --repo "%PUBLIC_RELEASE_REPO%" --clobber
if errorlevel 1 goto :publish_upload_failed
call "%GH_CMD%" release upload "%TAG%" "%MT_OUT%" --repo "%PUBLIC_RELEASE_REPO%" --clobber
if errorlevel 1 goto :publish_sums_failed
rmdir /S /Q "%WORK%" >nul 2>nul
exit /b 0
:publish_sums_failed
echo ERROR: El instalador subio, pero SHA256SUMS no: el release puede haber quedado SIN SHA256SUMS
echo        y la app no ofrece el update. El fusionado esta en %MT_OUT%. Subirlo con:
echo        gh release upload %TAG% "%MT_OUT%" --repo %PUBLIC_RELEASE_REPO% --clobber
rmdir /S /Q "%WORK%" >nul 2>nul
exit /b 1
:publish_upload_failed
echo ERROR: Fallo la subida al release %TAG%. Ver el mensaje de arriba.
rmdir /S /Q "%WORK%" >nul 2>nul
exit /b 1

:publish_notes
REM Notas para el usuario [What's new]: body del release, whats_new.json y WHATS_NEW.md en la raiz del
REM repo [un commit en main hecho por la API]. Idempotente: si falla, se reintenta con el mismo comando.
echo Publicando las notas para el usuario [What's new]...
call "%WN_BAT%" publish "%WN_FILE%" "%VERSION%" "%PUBLIC_RELEASE_REPO%" "%TAG%"
if errorlevel 1 (
    echo ERROR: El release %TAG% quedo publicado, pero sus notas no. Reintentar con el comando de arriba.
    exit /b 1
)
echo Las notas dejaron un commit en origin/main [WHATS_NEW.md]: correr git pull.
exit /b 0
