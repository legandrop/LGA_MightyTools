cmake_minimum_required(VERSION 3.16)
# Unica fuente de verdad del numero de version: el codigo lo lee de MIGHTYTOOLS_VERSION, y el
# Info.plist, el instalador y el zip lo derivan de aca. Es la ultima version PUBLICADA (de a centesimos:
# 1.51, 1.52...): la sube bump_version.sh/.bat a la que encabeza el changelog solo al publicar un release.
project(LGA_MightyTools VERSION 1.53 LANGUAGES CXX)

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
    # AGL (OpenGL legacy) ya no esta en los SDK nuevos y Qt 6.5 lo pide: que FindOpenGL no lo proponga
    # (mismo arreglo que LGA_Base_QT_C_Py y LGA_LinkRedirector; ver tambien despues de find_package).
    set(OPENGL_agl_LIBRARY "" CACHE STRING "AGL library" FORCE)
    set(OPENGL_AGL_INCLUDE_DIR "" CACHE PATH "AGL include dir" FORCE)
endif()

if(WIN32 AND NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
endif()

set(CMAKE_AUTORCC ON)

# Qt lo pasa el script de compilacion (-DCMAKE_PREFIX_PATH). Estos son los valores por defecto de
# las maquinas de trabajo, por si se configura a mano.
if(NOT CMAKE_PREFIX_PATH)
    if(WIN32)
        set(CMAKE_PREFIX_PATH "C:/Qt/6.5.3/mingw_64")
    elseif(APPLE)
        set(CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.5.3/macos")
    endif()
endif()

find_package(Qt6 6.5.3 COMPONENTS Core Gui Widgets Network REQUIRED)
qt_standard_project_setup()

# Las novedades del cartel de update (LgaWhatsNew): libs/lgaupdate es la copia identica de la pieza
# canonica de LGA_Base_QT_C_Py (ver su README). De la biblioteca se usa solo LgaWhatsNew, desde
# src/updates/UpdateNotes.cpp; el updater de esta app sigue siendo el propio (UpdateService).
add_subdirectory(libs/lgaupdate)

if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    # El FindWrapOpenGL de Qt 6.5 agrega "-framework AGL" aunque no lo encuentre: se saca de la interfaz
    # para que ningun consumidor de Qt6::Gui lo arrastre al link.
    get_target_property(_wo_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_wo_libs)
        set(_wo_new_libs "")
        foreach(_wo_lib ${_wo_libs})
            if(NOT "${_wo_lib}" MATCHES "(^|/)AGL(\\.framework)?$" AND NOT "${_wo_lib}" MATCHES "-framework[ \t]+AGL")
                list(APPEND _wo_new_libs "${_wo_lib}")
            endif()
        endforeach()
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wo_new_libs}")
    endif()
endif()

set(RESOURCES
    resources/resources.qrc
    # Payload del Nuke Bridge (init.py, el adaptador y VERSION), embebido en el exe
    # con su propia version (D-04). Ver nuke_plugin/OpenInNukeXBridge.qrc.
    nuke_plugin/OpenInNukeXBridge.qrc
    # Plugin de Nuke de Nuke Shortcuts (el keyframe), en su carpeta propia de la .nuke (D-41).
    nuke_plugin/NukeShortcutsPlugin.qrc
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
    src/core/NukePlugin.cpp
    src/core/Shortcut.cpp
    src/core/UiScale.cpp
    src/modules/diskspace/DiskCard.cpp
    src/modules/diskspace/DiskMonitor.cpp
    src/modules/diskspace/DiskReminderToast.cpp
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
    src/modules/diskspace/cleanup/AppCacheRules.cpp
    src/modules/diskspace/cleanup/AppCacheSelfTest.cpp
    src/modules/diskspace/cleanup/DeleteGuard.cpp
    src/modules/diskspace/cleanup/InstallerOrphans.cpp
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
    src/modules/nukeshortcuts/KeyframePlugin.cpp
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
    src/modules/windowlayouts/AreaPicker.cpp
    src/modules/windowlayouts/LayoutGeometry.cpp
    src/modules/windowlayouts/WindowLayoutsModule.cpp
    src/modules/windowlayouts/WindowLayoutsPanel.cpp
    src/modules/windowlayouts/WindowLayoutsState.cpp
    src/modules/apptoggle/AppToggleModule.cpp
    src/modules/apptoggle/AppTogglePanel.cpp
    src/modules/apptoggle/AppToggleState.cpp
    src/platform/AppToggleSystemCommon.cpp
    # Meet Frame: la logica, el modulo, el panel y el marco son comunes; la captura es de cada plataforma.
    src/modules/meetframe/MeetFrameModule.cpp
    src/modules/meetframe/MeetFrameOverlay.cpp
    src/modules/meetframe/MeetFramePanel.cpp
    src/modules/meetframe/MeetFrameRules.cpp
    src/platform/AutoStartCommon.cpp
    src/platform/InputInjectorCommon.cpp
    src/platform/LocalDrivesCommon.cpp
    src/platform/NukeWatcherCommon.cpp
    src/platform/SystemNotifierCommon.cpp
    src/platform/ToastActivationCommon.cpp
    src/platform/WindowPlacementCommon.cpp
    src/qa/Measure.cpp
    src/qa/NetRetryProbe.cpp
    src/qa/SelfTest.cpp
    src/qa/UiShot.cpp
    src/ui/ShortcutRow.cpp
    src/ui/Palette.cpp
    src/dev/PaletteEditor.cpp
    src/dev/LgaColorPickerDialog.cpp
    src/ui/Theme.cpp
    src/ui/TitleBar.cpp
    src/ui/UiWidgets.cpp
    src/ui/CustomTooltip.cpp
    src/updates/UpdateDialog.cpp
    src/updates/UpdateManifest.cpp
    src/updates/UpdateNotes.cpp
    src/updates/UpdateSelfTest.cpp
    src/updates/UpdateService.cpp
    src/updates/VersionCompare.cpp
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
    src/app/PanelState.h
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
    src/core/LgaNetRetry.h
    src/core/LgaRegistry.h
    src/core/NukePlugin.h
    src/core/Shortcut.h
    src/core/UiScale.h
    src/modules/diskspace/DiskCard.h
    src/modules/diskspace/DiskMonitor.h
    src/modules/diskspace/DiskReminderToast.h
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
    src/modules/diskspace/cleanup/AppCacheRules.h
    src/modules/diskspace/cleanup/DeleteGuard.h
    src/modules/diskspace/cleanup/InstallerOrphans.h
    src/platform/InstallerPackages.h
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
    src/modules/nukeshortcuts/KeyframePlugin.h
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
    src/modules/windowlayouts/AreaPicker.h
    src/modules/windowlayouts/LayoutGeometry.h
    src/modules/windowlayouts/WindowLayoutsModule.h
    src/modules/windowlayouts/WindowLayoutsPanel.h
    src/modules/windowlayouts/WindowLayoutsState.h
    src/modules/apptoggle/AppToggleModule.h
    src/modules/apptoggle/AppTogglePanel.h
    src/modules/apptoggle/AppToggleState.h
    src/platform/AppToggleSystem.h
    src/modules/meetframe/MeetFrameModule.h
    src/modules/meetframe/MeetFrameOverlay.h
    src/modules/meetframe/MeetFramePanel.h
    src/modules/meetframe/MeetFrameRules.h
    src/platform/MeetFrameSystem.h
    src/platform/AutoStart.h
    src/platform/ComApartment.h
    src/platform/DirEnumerator.h
    src/platform/FileSystemOps.h
    src/platform/RecycleBin.h
    src/platform/ScreenInfo.h
    src/platform/SystemPaths.h
    src/platform/AppPreferences.h
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
    src/platform/WindowPlacement.h
    src/qa/Measure.h
    src/qa/NetRetryProbe.h
    src/qa/SelfTest.h
    src/qa/UiShot.h
    src/ui/HelpSection.h
    src/ui/ShortcutRow.h
    src/ui/Palette.h
    src/dev/PaletteEditor.h
    src/dev/LgaColorPickerDialog.h
    src/ui/Theme.h
    src/ui/TitleBar.h
    src/ui/UiWidgets.h
    src/ui/CustomTooltip.h
    src/updates/UpdateDialog.h
    src/updates/UpdateManifest.h
    src/updates/UpdateNotes.h
    src/updates/UpdateSelfTest.h
    src/updates/UpdateService.h
    src/updates/VersionCompare.h
    src/platform/UpdateInstaller.h
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
        # Ayudante elevado de Window Layouts para ventanas de administrador.
        src/platform/win/AdminWindowHelperWin.cpp
        src/platform/win/AppToggleSystemWin.cpp
        src/platform/win/MeetFrameSystemWin.cpp
        src/platform/win/AutoStartWin.cpp
        src/platform/win/ComApartmentWin.cpp
        src/platform/win/DirEnumeratorWin.cpp
        src/platform/win/FileSystemOpsWin.cpp
        src/platform/win/RecycleBinWin.cpp
        src/platform/win/ScreenInfoWin.cpp
        src/platform/win/SystemPathsWin.cpp
        src/platform/win/AppPreferencesWin.cpp
        src/modules/diskspace/cleanup/win/CleanupRulesWin.cpp
        # Instaladores huerfanos de %WINDIR%\Installer: lectura (API de Windows Installer) y paso elevado.
        src/platform/win/InstallerPackagesWin.cpp
        src/modules/diskspace/cleanup/win/InstallerCleanupWin.cpp
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
        src/platform/win/WindowPlacementWin.cpp
        # Self-test del registro sobre un hive privado (RegLoadAppKey): solo Windows.
        src/qa/RegistryHiveTest.cpp
        # Ultimo paso del updater en Windows: lanza el instalador de Inno.
        src/platform/win/UpdateInstallerWin.cpp
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
        # Modulo Mouse Buttons: solo Windows (gancho WH_MOUSE_LL y SendInput).
        src/modules/mousebuttons/MouseButtonsModule.cpp
        src/modules/mousebuttons/MouseButtonsPanel.cpp
        src/modules/mousebuttons/MouseButtonsState.cpp
        src/platform/MouseButtonsSystemCommon.cpp
        src/platform/win/MouseButtonsSystemWin.cpp
        # Modulo Desktop Icons: solo Windows (la vista del escritorio del Explorador).
        src/modules/desktopicons/DesktopIconsModule.cpp
        src/modules/desktopicons/DesktopIconsPanel.cpp
        src/modules/desktopicons/DesktopSnapshots.cpp
        src/platform/win/DesktopIconViewWin.cpp
        # Dictation (por ahora solo Windows): el motor corre en otro proceso (--dictation-worker) y carga
        # transcribe.dll en tiempo de ejecucion; la app no enlaza contra el motor.
        src/modules/dictation/DictationEngine.cpp
        src/modules/dictation/DictationFlow.cpp
        src/modules/dictation/DictationModels.cpp
        src/modules/dictation/DictationModule.cpp
        src/modules/dictation/DictationOverlay.cpp
        src/modules/dictation/DictationPanel.cpp
        src/modules/dictation/DictationProtocol.cpp
        src/modules/dictation/DictationQa.cpp
        src/modules/dictation/DictationText.cpp
        src/modules/dictation/DictationTrigger.cpp
        src/modules/dictation/DictationWorker.cpp
        src/platform/AudioCaptureCommon.cpp
        src/platform/win/AudioCaptureWin.cpp
        src/platform/win/DictationProcessWin.cpp
        src/platform/win/OverlayWindowWin.cpp
        src/platform/win/TextTyperWin.cpp
    )
    list(APPEND HEADERS
        src/modules/desktopicons/DesktopIconsModule.h
        src/modules/desktopicons/DesktopIconsPanel.h
        src/modules/desktopicons/DesktopSnapshots.h
        src/platform/DesktopIconView.h
        src/platform/AdminWindowHelper.h
        src/modules/diskspace/cleanup/InstallerCleanup.h
        src/modules/openinnukex/win/OldClientMigration.h
        src/modules/openinnukex/win/WinFileAssociation.h
        src/platform/win/RegistryHelper.h
        src/qa/RegistryHiveTest.h
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
        src/modules/mousebuttons/MouseButtonsModule.h
        src/modules/mousebuttons/MouseButtonsPanel.h
        src/modules/mousebuttons/MouseButtonsState.h
        src/platform/MouseButtonsSystem.h
        src/modules/dictation/DictationEngine.h
        src/modules/dictation/DictationFlow.h
        src/modules/dictation/DictationModels.h
        src/modules/dictation/DictationModule.h
        src/modules/dictation/DictationOverlay.h
        src/modules/dictation/DictationPanel.h
        src/modules/dictation/DictationProtocol.h
        src/modules/dictation/DictationQa.h
        src/modules/dictation/DictationText.h
        src/modules/dictation/DictationTrigger.h
        src/modules/dictation/DictationWorker.h
        # Encabezados de transcribe.cpp 0.4.1 (MIT): solo tipos y firmas.
        src/modules/dictation/third_party/transcribe.h
        src/modules/dictation/third_party/whisper.h
        src/platform/AudioCapture.h
        src/platform/DictationProcess.h
        src/platform/OverlayWindow.h
        src/platform/TextTyper.h
    )
elseif(APPLE)
    list(APPEND SOURCES
        src/modules/linkredirector/mac/BrowserDetectionMac.cpp
        src/modules/linkredirector/mac/BrowserRegistrationMac.mm
        src/modules/linkredirector/mac/ProcessLatencyMac.cpp
        src/modules/openinnukex/mac/MacFileAssociation.mm
        # Folder Switch en mac (D-40): la logica, el estado, el panel y el popup son los mismos de
        # Windows; el modulo y lo de Accesibilidad/Finder son propios de mac.
        src/modules/folderswitch/FolderSwitchLogic.cpp
        src/modules/folderswitch/FolderSwitchPanel.cpp
        src/modules/folderswitch/FolderSwitchState.cpp
        src/modules/folderswitch/RecentFoldersPopup.cpp
        src/modules/folderswitch/mac/FolderSwitchMacModule.cpp
        src/modules/folderswitch/mac/MacFileDialogs.mm
        src/platform/mac/AppToggleSystemMac.mm
        src/platform/mac/MeetFrameSystemMac.mm
        src/platform/mac/AutoStartMac.mm
        src/platform/mac/ComApartmentMac.cpp
        src/platform/mac/DirEnumeratorMac.cpp
        src/platform/mac/FileSystemOpsMac.cpp
        src/platform/mac/RecycleBinMac.cpp
        src/platform/mac/ScreenInfoMac.mm
        src/platform/mac/SystemPathsMac.cpp
        src/platform/mac/AppPreferencesMac.cpp
        # Ultimo paso del updater en macOS: el script que reemplaza el bundle instalado.
        src/platform/mac/UpdateInstallerMac.cpp
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
        src/platform/mac/WindowActivationMac.mm
        src/platform/mac/WindowFrameMac.mm
        src/platform/mac/WindowPlacementMac.mm
    )
    list(APPEND HEADERS
        src/modules/openinnukex/mac/MacFileAssociation.h
        src/modules/folderswitch/FolderSwitchLogic.h
        src/modules/folderswitch/FolderSwitchPanel.h
        src/modules/folderswitch/FolderSwitchState.h
        src/modules/folderswitch/RecentFoldersPopup.h
        src/modules/folderswitch/mac/FolderSwitchMacModule.h
        src/modules/folderswitch/mac/MacFileDialogs.h
    )
endif()

qt_add_executable(${PROJECT_NAME}
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
    LGAUpdate
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
        # App Toggle: nombre del producto del exe (GetFileVersionInfo).
        version
        # Disk Space: que instaladores de %WINDIR%\Installer reclama Windows Installer (solo lectura).
        msi
        # Meet Frame: captura del monitor con DXGI Desktop Duplication.
        d3d11
        dxgi
    )
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework AppKit"
        "-framework ApplicationServices"
        "-framework Carbon"
        "-framework ServiceManagement"
        # Esquinas redondeadas de las ventanas sin marco (WindowFrameMac.mm).
        "-framework QuartzCore"
        # UTType (Open in NukeX, MacFileAssociation.mm). El QtCore de Homebrew lo traia de arrastre; el
        # oficial no.
        "-framework UniformTypeIdentifiers"
        # Meet Frame: captura de la region (ScreenCaptureKit) y sus cuadros.
        "-framework ScreenCaptureKit"
        "-framework CoreMedia"
        "-framework CoreVideo"
    )
    # Los .mm con ARC: sin retain/release a mano.
    set_source_files_properties(
        src/modules/linkredirector/mac/BrowserRegistrationMac.mm
        src/platform/mac/AppToggleSystemMac.mm
        src/platform/mac/MeetFrameSystemMac.mm
        src/platform/mac/AutoStartMac.mm
        src/platform/mac/ForegroundWatcherMac.mm
        src/platform/mac/NukeWatcherMac.mm
        src/platform/mac/WindowActivationMac.mm
        src/platform/mac/WindowPlacementMac.mm
        src/modules/folderswitch/mac/MacFileDialogs.mm
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

# macOS, build de desarrollo (no Release): el bundle de build/ no lleva Qt adentro (eso lo hace
# deploy.sh con macdeployqt), asi que ademas de @executable_path/../Frameworks busca los frameworks en
# el Qt con el que se compilo. El Release que se instala no lleva esta ruta.
if(APPLE AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    get_target_property(_qtcore_location Qt6::Core LOCATION)
    # .../lib/QtCore.framework/Versions/A/QtCore -> .../lib
    string(REGEX REPLACE "/QtCore\\.framework/.*$" "" _qt_lib_dir "${_qtcore_location}")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY INSTALL_RPATH "${_qt_lib_dir}")
endif()

# Icono de los .nk en macOS (Info.plist, CFBundleTypeIconFile): el documento de Nuke de Open in NukeX,
# el mismo dibujo que resources/icons/NukeScript.ico de Windows (512 y 1024 ampliados desde el de 256).
if(APPLE)
    set(NK_ICON_RESOURCE_MAC "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/NukeScript.icns")
    target_sources(${PROJECT_NAME} PRIVATE "${NK_ICON_RESOURCE_MAC}")
    set_source_files_properties("${NK_ICON_RESOURCE_MAC}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    # Icono de la app en macOS, como en LGA_FrameRev: el .icon de Icon Composer
    # (resources/icons/Alta/LGA_MightyTools.icon, con el maestro aprobado adentro) compilado con actool
    # a los DOS recursos del bundle. Assets.car es el icono moderno (CFBundleIconName) y AppIcon.icns el
    # de compatibilidad (CFBundleIconFile), con --standalone-icon-behavior all para los 10 tamanos:
    #   xcrun actool AppIcon.icon --compile <out> --platform macosx --minimum-deployment-target 26.0 \
    #       --app-icon AppIcon --standalone-icon-behavior all --output-partial-info-plist <out>/p.plist
    # (copiar antes el .icon como AppIcon.icon: actool nombra el asset por el archivo).
    set(APP_ICON_MACOS_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/AppIcon.icns"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/Assets.car")
    target_sources(${PROJECT_NAME} PRIVATE ${APP_ICON_MACOS_FILES})
    set_source_files_properties(${APP_ICON_MACOS_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

# El plugin offscreen va al lado del exe: sin el, una corrida con QT_QPA_PLATFORM=offscreen
# (--ui-shot, QA sin pantalla) no arranca y Qt muestra un cartel fatal en el escritorio.
# compilar.bat ademas lo verifica con el resto de las dependencias.
