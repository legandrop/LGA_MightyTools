cmake_minimum_required(VERSION 3.16)
# Unica fuente de verdad del numero de version: el codigo lo lee de MIGHTYTOOLS_VERSION, y el
# Info.plist, el instalador y el zip lo derivan de aca. La mueve sync_version desde el changelog, de a
# centesimos (0.01, 0.02...).
project(LGA_MightyTools VERSION 1.13 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(APPLE)
    enable_language(OBJCXX)
    # SMAppService (Open at login) pide macOS 13.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
endif()

if(WIN32 AND NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt lo pasa el script de compilacion (-DCMAKE_PREFIX_PATH). Estos son los valores por defecto de
# las maquinas de trabajo, por si se configura a mano.
if(NOT CMAKE_PREFIX_PATH)
    if(WIN32)
        set(CMAKE_PREFIX_PATH "C:/Qt/6.5.3/mingw_64")
    elseif(APPLE)
        set(CMAKE_PREFIX_PATH "/opt/homebrew")
    endif()
endif()

find_package(Qt6 COMPONENTS Core Gui Widgets Network REQUIRED)

set(RESOURCES
    resources/resources.qrc
    # Payload del Nuke Bridge (init.py, LGA_QtAdapter_OpenInNukeX.py, VERSION), embebido en el exe
    # con su propia version (D-04). Ver nuke_plugin/OpenInNukeXBridge.qrc.
    nuke_plugin/OpenInNukeXBridge.qrc
)

if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/LGA_MightyTools.ico")
    set(NK_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/NukeScript.ico")
    # Version del recurso de Windows en numeros ("0.04" -> 0,4,0,0): sin ceros a la izquierda, que el
    # compilador de recursos leeria como octal ("08" no existe).
    string(REGEX REPLACE "^0+([0-9])" "\\1" APP_VERSION_MAJOR_NUM "${PROJECT_VERSION_MAJOR}")
    string(REGEX REPLACE "^0+([0-9])" "\\1" APP_VERSION_MINOR_NUM "${PROJECT_VERSION_MINOR}")
    set(APP_VERSION_COMMAS "${APP_VERSION_MAJOR_NUM},${APP_VERSION_MINOR_NUM},0,0")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows_icon.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/windows_icon.rc" @ONLY)
    # El .rc solo NOMBRA al .ico: sin OBJECT_DEPENDS, cambiar el icono no recompila el .rc y el exe
    # se queda con el icono viejo.
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/windows_icon.rc"
        PROPERTIES OBJECT_DEPENDS "${APP_ICON_RESOURCE_WINDOWS};${NK_ICON_RESOURCE_WINDOWS}")
    list(APPEND RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/windows_icon.rc")
endif()

# Codigo comun a las dos plataformas.
set(SOURCES
    src/main.cpp
    src/app/AppController.cpp
    src/app/ExternalDispatch.cpp
    src/app/GeneralPage.cpp
    src/app/HotkeyHub.cpp
    src/app/MainWindow.cpp
    src/app/ModuleContextImpl.cpp
    src/app/ModuleHost.cpp
    src/app/ModuleRegistry.cpp
    src/app/SettingsStore.cpp
    src/app/SingleInstance.cpp
    src/app/UninstallCleanup.cpp
    src/app/TrayMenu.cpp
    src/app/WindowParts.cpp
    src/core/AppPaths.cpp
    src/core/AppSettings.cpp
    src/core/AutomatedRun.cpp
    src/core/BuildTree.cpp
    src/core/DebugFlags.cpp
    src/core/I18n.cpp
    src/core/I18nSpanish.cpp
    src/core/LgaRegistry.cpp
    src/core/Shortcut.cpp
    src/core/UiScale.cpp
    src/modules/diskspace/DiskCard.cpp
    src/modules/diskspace/DiskMonitor.cpp
    src/modules/diskspace/DiskSpace.cpp
    src/modules/diskspace/DiskSpaceModule.cpp
    src/modules/diskspace/DiskState.cpp
    src/modules/diskspace/cleanup/CleanupJob.cpp
    src/modules/diskspace/cleanup/CleanupDialogs.cpp
    src/modules/diskspace/cleanup/CleanupExport.cpp
    src/modules/diskspace/cleanup/CleanupFixture.cpp
    src/modules/diskspace/cleanup/CleanupPane.cpp
    src/modules/diskspace/cleanup/CleanupWindow.cpp
    src/modules/diskspace/cleanup/ScanSnapshot.cpp
    src/modules/diskspace/cleanup/SizeListView.cpp
    src/modules/diskspace/cleanup/CleanupRules.cpp
    src/modules/diskspace/cleanup/DeleteGuard.cpp
    src/modules/diskspace/cleanup/CleanupQa.cpp
    src/modules/diskspace/cleanup/CleanupSelfTest.cpp
    src/modules/diskspace/cleanup/ScanEngine.cpp
    src/modules/diskspace/cleanup/ScanTree.cpp
    src/modules/linkredirector/BrowserDetectionCommon.cpp
    src/modules/linkredirector/LinkRedirectorDescriptor.cpp
    src/modules/linkredirector/LinkRedirectorExternal.cpp
    src/modules/linkredirector/LinkRedirectorModule.cpp
    src/modules/linkredirector/LinkRedirectorPanel.cpp
    src/modules/linkredirector/LinkRedirectorRouting.cpp
    src/modules/nukeshortcuts/ActionRunner.cpp
    src/modules/nukeshortcuts/CalibrationDialog.cpp
    src/modules/nukeshortcuts/CalibrationSession.cpp
    src/modules/nukeshortcuts/NukeShortcutsModule.cpp
    src/modules/nukeshortcuts/NukeShortcutsPanel.cpp
    src/modules/nukeshortcuts/NukeShortcutsState.cpp
    src/modules/openinnukex/NukeBridge.cpp
    src/modules/openinnukex/NukeOpener.cpp
    src/modules/openinnukex/NukeScanner.cpp
    src/modules/openinnukex/NukeXPath.cpp
    src/modules/openinnukex/OpenInNukeXDescriptor.cpp
    src/modules/openinnukex/OpenInNukeXMessages.cpp
    src/modules/openinnukex/OpenInNukeXModule.cpp
    src/modules/openinnukex/OpenInNukeXOperations.cpp
    src/modules/openinnukex/OpenInNukeXPanel.cpp
    src/platform/AutoStartCommon.cpp
    src/platform/InputInjectorCommon.cpp
    src/platform/LocalDrivesCommon.cpp
    src/platform/NukeWatcherCommon.cpp
    src/platform/SystemNotifierCommon.cpp
    src/platform/ToastActivationCommon.cpp
    src/qa/Measure.cpp
    src/qa/SelfTest.cpp
    src/qa/UiShot.cpp
    src/ui/HelpDialog.cpp
    src/ui/ShortcutRow.cpp
    src/ui/Theme.cpp
    src/ui/TitleBar.cpp
    src/ui/UiWidgets.cpp
    src/ui/CustomTooltip.cpp
    src/updates/UpdateDialog.cpp
)

set(HEADERS
    src/app/AppController.h
    src/app/ExternalDispatch.h
    src/app/GeneralPage.h
    src/app/HostServices.h
    src/app/HotkeyHub.h
    src/app/MainWindow.h
    src/app/Module.h
    src/app/ModuleContext.h
    src/app/ModuleContextImpl.h
    src/app/ModuleHost.h
    src/app/ModuleRegistry.h
    src/app/SettingsStore.h
    src/app/SingleInstance.h
    src/app/UninstallCleanup.h
    src/app/TrayMenu.h
    src/app/WindowParts.h
    src/core/AppPaths.h
    src/core/AppSettings.h
    src/core/AutomatedRun.h
    src/core/BuildTree.h
    src/core/DebugFlags.h
    src/core/LgaRegistry.h
    src/core/Shortcut.h
    src/core/UiScale.h
    src/modules/diskspace/DiskCard.h
    src/modules/diskspace/DiskMonitor.h
    src/modules/diskspace/DiskSpace.h
    src/modules/diskspace/DiskSpaceModule.h
    src/modules/diskspace/DiskState.h
    src/modules/diskspace/cleanup/CleanupJob.h
    src/modules/diskspace/cleanup/CleanupDialogs.h
    src/modules/diskspace/cleanup/CleanupExport.h
    src/modules/diskspace/cleanup/CleanupFixture.h
    src/modules/diskspace/cleanup/CleanupPane.h
    src/modules/diskspace/cleanup/CleanupWindow.h
    src/modules/diskspace/cleanup/ScanSnapshot.h
    src/modules/diskspace/cleanup/SizeListView.h
    src/modules/diskspace/cleanup/CleanupModel.h
    src/modules/diskspace/cleanup/CleanupRules.h
    src/modules/diskspace/cleanup/DeleteGuard.h
    src/modules/diskspace/cleanup/CleanupQa.h
    src/modules/diskspace/cleanup/ScanEngine.h
    src/modules/diskspace/cleanup/ScanTree.h
    src/modules/linkredirector/BrowserDetection.h
    src/modules/linkredirector/BrowserRegistration.h
    src/modules/linkredirector/LinkRedirectorDescriptor.h
    src/modules/linkredirector/LinkRedirectorExternal.h
    src/modules/linkredirector/LinkRedirectorModule.h
    src/modules/linkredirector/LinkRedirectorPanel.h
    src/modules/linkredirector/LinkRedirectorRouting.h
    src/modules/linkredirector/LinkRedirectorTypes.h
    src/modules/linkredirector/ProcessLatency.h
    src/modules/nukeshortcuts/ActionRunner.h
    src/modules/nukeshortcuts/CalibrationDialog.h
    src/modules/nukeshortcuts/CalibrationSession.h
    src/modules/nukeshortcuts/NukeShortcutsModule.h
    src/modules/nukeshortcuts/NukeShortcutsPanel.h
    src/modules/nukeshortcuts/NukeShortcutsState.h
    src/modules/openinnukex/NukeBridge.h
    src/modules/openinnukex/NukeOpener.h
    src/modules/openinnukex/NukeScanner.h
    src/modules/openinnukex/NukeXPath.h
    src/modules/openinnukex/ApplyIssue.h
    src/modules/openinnukex/OpenInNukeXDescriptor.h
    src/modules/openinnukex/OpenInNukeXMessages.h
    src/modules/openinnukex/OpenInNukeXModule.h
    src/modules/openinnukex/OpenInNukeXOperations.h
    src/modules/openinnukex/OpenInNukeXPanel.h
    src/platform/AutoStart.h
    src/platform/ComApartment.h
    src/platform/DirEnumerator.h
    src/platform/FileSystemOps.h
    src/platform/RecycleBin.h
    src/platform/ScreenInfo.h
    src/platform/SystemPaths.h
    src/platform/ForegroundWatcher.h
    src/platform/HotkeyService.h
    src/platform/InputInjector.h
    src/platform/LocalDrives.h
    src/platform/NukeWatcher.h
    src/platform/ProcessStats.h
    src/platform/SystemInput.h
    src/platform/SystemNotifier.h
    src/platform/ToastActivation.h
    src/platform/WindowActivation.h
    src/platform/WindowFrame.h
    src/qa/Measure.h
    src/qa/SelfTest.h
    src/qa/UiShot.h
    src/ui/HelpDialog.h
    src/ui/HelpSection.h
    src/ui/ShortcutRow.h
    src/ui/Theme.h
    src/ui/TitleBar.h
    src/ui/UiWidgets.h
    src/ui/CustomTooltip.h
    src/updates/UpdateDialog.h
)

# Capa de plataforma: una implementacion por sistema detras de los headers de src/platform/.
if(WIN32)
    list(APPEND SOURCES
        src/modules/linkredirector/win/BrowserDetectionWin.cpp
        src/modules/linkredirector/win/BrowserRegistrationWin.cpp
        src/modules/linkredirector/win/ProcessLatencyWin.cpp
        src/modules/openinnukex/win/OldClientMigration.cpp
        src/modules/openinnukex/win/UserChoiceLatest.cpp
        src/modules/openinnukex/win/WinFileAssociation.cpp
        src/platform/win/AutoStartWin.cpp
        src/platform/win/ComApartmentWin.cpp
        src/platform/win/DirEnumeratorWin.cpp
        src/platform/win/FileSystemOpsWin.cpp
        src/platform/win/RecycleBinWin.cpp
        src/platform/win/ScreenInfoWin.cpp
        src/platform/win/SystemPathsWin.cpp
        src/modules/diskspace/cleanup/win/CleanupRulesWin.cpp
        src/platform/win/ForegroundWatcherWin.cpp
        src/platform/win/HotkeyServiceWin.cpp
        src/platform/win/InputInjectorWin.cpp
        src/platform/win/LocalDrivesWin.cpp
        src/platform/win/NukeWatcherWin.cpp
        src/platform/win/ProcessStatsWin.cpp
        src/platform/win/RegistryHelper.cpp
        src/platform/win/SystemInputWin.cpp
        src/platform/win/SystemNotifierWin.cpp
        src/platform/win/ToastActivationWin.cpp
        src/platform/win/WindowActivationWin.cpp
        src/platform/win/WindowFrameWin.cpp
        # Self-test del registro sobre un hive privado (RegLoadAppKey): solo Windows.
        src/qa/RegistryHiveTest.cpp
        # El updater baja y lanza el instalador de Inno: solo Windows por ahora.
        src/updates/UpdateService.cpp
        src/updates/VersionCompare.cpp
        # Modulo Folder Switch: solo Windows (plan, 2 "Folder Switch").
        src/modules/folderswitch/DialogSwitcher.cpp
        src/modules/folderswitch/FolderResolver.cpp
        src/modules/folderswitch/FolderSwitchLogic.cpp
        src/modules/folderswitch/FolderSwitchModule.cpp
        src/modules/folderswitch/FolderSwitchPanel.cpp
        src/modules/folderswitch/FolderSwitchState.cpp
        src/modules/folderswitch/RecentFoldersPopup.cpp
        src/modules/folderswitch/UiaSwitcher.cpp
        src/modules/folderswitch/UiaTimeouts.cpp
        src/modules/folderswitch/WindowUtils.cpp
    )
    list(APPEND HEADERS
        src/modules/openinnukex/win/OldClientMigration.h
        src/modules/openinnukex/win/WinFileAssociation.h
        src/platform/win/RegistryHelper.h
        src/qa/RegistryHiveTest.h
        src/updates/UpdateService.h
        src/updates/VersionCompare.h
        src/modules/folderswitch/DialogSwitcher.h
        src/modules/folderswitch/FolderResolver.h
        src/modules/folderswitch/FolderSwitchLogic.h
        src/modules/folderswitch/FolderSwitchModule.h
        src/modules/folderswitch/FolderSwitchPanel.h
        src/modules/folderswitch/FolderSwitchState.h
        src/modules/folderswitch/RecentFoldersPopup.h
        src/modules/folderswitch/UiaSwitcher.h
        src/modules/folderswitch/UiaTimeouts.h
        src/modules/folderswitch/WindowUtils.h
    )
elseif(APPLE)
    list(APPEND SOURCES
        src/modules/linkredirector/mac/BrowserDetectionMac.cpp
        src/modules/linkredirector/mac/BrowserRegistrationMac.mm
        src/modules/linkredirector/mac/ProcessLatencyMac.cpp
        src/modules/openinnukex/mac/MacFileAssociation.mm
        src/platform/mac/AutoStartMac.mm
        src/platform/mac/ComApartmentMac.cpp
        src/platform/mac/DirEnumeratorMac.cpp
        src/platform/mac/FileSystemOpsMac.cpp
        src/platform/mac/RecycleBinMac.cpp
        src/platform/mac/ScreenInfoMac.mm
        src/platform/mac/SystemPathsMac.cpp
        src/modules/diskspace/cleanup/mac/CleanupRulesMac.cpp
        src/platform/mac/ForegroundWatcherMac.mm
        src/platform/mac/HotkeyServiceMac.cpp
        src/platform/mac/InputInjectorMac.cpp
        src/platform/mac/LocalDrivesMac.cpp
        src/platform/mac/NukeWatcherMac.mm
        src/platform/mac/ProcessStatsMac.cpp
        src/platform/mac/SystemInputMac.cpp
        src/platform/mac/SystemNotifierMac.cpp
        src/platform/mac/ToastActivationMac.cpp
        src/platform/mac/WindowActivationMac.cpp
        src/platform/mac/WindowFrameMac.cpp
    )
    list(APPEND HEADERS
        src/modules/openinnukex/mac/MacFileAssociation.h
    )
endif()

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    # Solo por qpa/qwindowsysteminterface.h: la sonda --ui-probe simula clicks y teclas del sistema.
    Qt6::GuiPrivate
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        user32
        dwmapi
        advapi32
        psapi
        # Folder Switch: Shell COM (FolderResolver), UI Automation (WindowUtils/UiaSwitcher).
        # Open in NukeX: IOpenWithLauncher, ShellExecuteW, SHChangeNotify (WinFileAssociation).
        shell32
        ole32
        oleaut32
        uuid
    )
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework AppKit"
        "-framework ApplicationServices"
        "-framework Carbon"
        "-framework ServiceManagement"
    )
    # Los .mm con ARC: sin retain/release a mano.
    set_source_files_properties(
        src/modules/linkredirector/mac/BrowserRegistrationMac.mm
        src/platform/mac/AutoStartMac.mm
        src/platform/mac/ForegroundWatcherMac.mm
        src/platform/mac/NukeWatcherMac.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    MIGHTYTOOLS_VERSION="${PROJECT_VERSION}"
    # Arboles de desarrollo: AutoStart y LgaRegistry reconocen esta salida aunque la carpeta no se
    # llame build/ (ver LGA_Base_QT_C_Py/docs/Doc_Autostart_Windows.md).
    LGA_BUILD_TREE_DIR="${CMAKE_BINARY_DIR}"
    LGA_SOURCE_TREE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        # Convencion LGA: el nombre visible arranca con "LGA" para que todas las apps queden juntas
        # en /Applications. El nombre de ARCHIVO de los artefactos (.zip/.dmg) va sin espacios.
        OUTPUT_NAME "LGA Mighty Tools"
        MACOSX_BUNDLE_BUNDLE_NAME "LGA Mighty Tools"
        MACOSX_BUNDLE_EXECUTABLE_NAME "LGA Mighty Tools"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.lga.mightytools"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# Icono de los .nk en macOS (Info.plist, CFBundleTypeIconFile): el documento de Nuke de Open in NukeX,
# el mismo dibujo que resources/icons/NukeScript.ico de Windows (512 y 1024 ampliados desde el de 256).
if(APPLE)
    set(NK_ICON_RESOURCE_MAC "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/NukeScript.icns")
    target_sources(${PROJECT_NAME} PRIVATE "${NK_ICON_RESOURCE_MAC}")
    set_source_files_properties("${NK_ICON_RESOURCE_MAC}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

# El plugin offscreen va al lado del exe: sin el, una corrida con QT_QPA_PLATFORM=offscreen
# (--ui-shot, QA sin pantalla) no arranca y Qt muestra un cartel fatal en el escritorio.
# compilar.bat ademas lo verifica con el resto de las dependencias.
