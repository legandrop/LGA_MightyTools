#include "modules/openinnukex/win/OldClientMigration.h"

#include "app/SettingsStore.h"
#include "core/AppPaths.h"
#include "modules/openinnukex/win/WinFileAssociation.h"
#include "platform/win/RegistryHelper.h"

#include <QDebug>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QThread>

#include <cstdio>

#include <objbase.h>
#include <shellapi.h>

namespace {

const QString kEnabledKey = QStringLiteral("modules/openInNukeX/enabled");
const QString kMarkKey = QStringLiteral("migration/openInNukeX");
const QString kMarkMigrated = QStringLiteral("migrated");
const QString kMarkNoTrace = QStringLiteral("no-trace");
const QString kOldExeName = QStringLiteral("LGA_OpenInNukeX.exe");
const QString kOwnExeName = QStringLiteral("LGA_MightyTools.exe");
const QString kUninstallSuffix =
    QStringLiteral("Microsoft\\Windows\\CurrentVersion\\Uninstall\\{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1");
// Inno Setup: progreso sin preguntas, sin reinicio y sin carteles. El desinstalador del cliente viejo
// es per-machine y pide el UAC el mismo.
const QString kSilentUninstallArgs = QStringLiteral("/SILENT /NORESTART /SUPPRESSMSGBOXES");

void addLine(OldClientMigration::Report &report, const QString &line)
{
    report.lines << line;
}

// El exe de un comando del registro. Con comillas, lo de adentro; sin comillas, hasta el `.exe`
// (un comando sin comillas con espacios, `C:\Program Files\x\a.exe %1`, se corta ahi y no en el
// primer espacio); si no hay `.exe`, lo que diga RegistryHelper::commandExecutable().
QString commandExe(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.startsWith(QLatin1Char('"'))) {
        return RegistryHelper::commandExecutable(trimmed);
    }
    const int exeEnd = trimmed.indexOf(QStringLiteral(".exe"), 0, Qt::CaseInsensitive);
    if (exeEnd > 0) {
        return trimmed.left(exeEnd + 4);
    }
    return RegistryHelper::commandExecutable(trimmed);
}

QString exeFileName(const QString &exePath)
{
    return QFileInfo(QDir::fromNativeSeparators(exePath)).fileName();
}

bool isOldExe(const QString &exePath)
{
    return exeFileName(exePath).compare(kOldExeName, Qt::CaseInsensitive) == 0;
}

// Una clave de desinstalacion. `root`+`sub`: la vista del registro que corresponde.
OldClientMigration::UninstallEntry readEntry(HKEY root, const QString &sub, const QString &where)
{
    OldClientMigration::UninstallEntry entry;
    entry.where = where;
    entry.displayName = RegistryHelper::readString(root, sub, QStringLiteral("DisplayName")).trimmed();
    entry.uninstallString = RegistryHelper::readString(root, sub, QStringLiteral("UninstallString")).trimmed();
    // Inno escribe DisplayIcon = {app}\LGA_OpenInNukeX.exe (UninstallDisplayIcon) e InstallLocation.
    const QString icon = commandExe(RegistryHelper::readString(root, sub, QStringLiteral("DisplayIcon")));
    if (isOldExe(icon)) {
        entry.oldExe = QDir::toNativeSeparators(icon);
    } else {
        const QString location = RegistryHelper::readString(root, sub, QStringLiteral("InstallLocation")).trimmed();
        if (!location.isEmpty()) {
            entry.oldExe = QDir::toNativeSeparators(QDir(QDir::fromNativeSeparators(location)).filePath(kOldExeName));
        }
    }
    return entry;
}

QList<OldClientMigration::UninstallEntry> readEntries()
{
    return {
        readEntry(HKEY_LOCAL_MACHINE, QStringLiteral("Software\\") + kUninstallSuffix, QStringLiteral("HKLM")),
        readEntry(HKEY_LOCAL_MACHINE, QStringLiteral("Software\\WOW6432Node\\") + kUninstallSuffix,
                  QStringLiteral("HKLM WOW6432Node")),
        readEntry(HKEY_CURRENT_USER, QStringLiteral("Software\\") + kUninstallSuffix, QStringLiteral("HKCU")),
    };
}

// c (C2): la toma de los .nk. `moduleOn`: Open in NukeX prendido despues del paso a.
void takeNkAssociation(bool moduleOn, const OldClientMigration::Options &options, OldClientMigration::Report &report)
{
    if (!moduleOn) {
        addLine(report, QStringLiteral("[nk] Open in NukeX apagado: los .nk no se tocan"));
        return;
    }
    const QString own = RegistryHelper::ownExePath();
    const QString command = WinFileAssociation::progIdCommandLine();
    if (command.trimmed().isEmpty()) {
        addLine(report, QStringLiteral("[nk] sin comando en %1: no se toma nada (queda Apply)").arg(WinFileAssociation::progId()));
        return;
    }
    const QString exe = commandExe(command);
    const bool ours = RegistryHelper::samePath(exe, own);
    if (!ours && exeFileName(exe).compare(kOwnExeName, Qt::CaseInsensitive) == 0) {
        addLine(report, QStringLiteral("[nk] el ProgID apunta a otra copia de LGA Mighty Tools (%1): no se toca").arg(exe));
        return;
    }
    const bool old = isOldExe(exe);
    const bool missing = WinFileAssociation::exeMissingOnPresentLocalDrive(exe);
    if (!ours && !old && !missing) {
        addLine(report, QStringLiteral("[nk] el ProgID apunta a otro programa que existe (%1): no se toca").arg(exe));
        return;
    }
    // La eleccion efectiva (UserChoiceLatest, UserChoice o Classes\.nk): si es de otra app, el
    // usuario la eligio y queda; Apply sigue disponible en el panel.
    const QString effective = WinFileAssociation::currentNkProgId();
    if (!effective.isEmpty() && effective.compare(WinFileAssociation::progId(), Qt::CaseInsensitive) != 0) {
        addLine(report, QStringLiteral("[nk] la eleccion de .nk es de otra app (%1): no se toca (queda Apply)").arg(effective));
        return;
    }
    const QString why = ours ? QStringLiteral("ya apunta a este exe") : old ? QStringLiteral("apunta al cliente viejo")
                                                                           : QStringLiteral("apunta a un exe que no existe");
    if (options.dryRun || options.buildTree) {
        addLine(report, QStringLiteral("[nk] (%1) registraria ProgID, Classes\\.nk y Capabilities a %2 (%3: %4)")
                            .arg(options.dryRun ? QStringLiteral("solo log") : QStringLiteral("arbol de build"), own, why, exe));
        return;
    }
    QStringList errors;
    const bool ok = WinFileAssociation::registerClasses(&errors);
    RegistryHelper::notifyAssociationsChanged();
    report.nkTaken = ok;
    if (!ok) {
        ++report.failures;
    }
    addLine(report, QStringLiteral("[nk] %1 (%2: %3) -> %4%5")
                        .arg(ok ? QStringLiteral("tomados") : QStringLiteral("FALLO al tomarlos"), why, exe, own,
                             errors.isEmpty() ? QString() : QStringLiteral(" | ") + errors.join(QLatin1Char(' '))));
}

bool stillThere(const QString &oldExe)
{
    return OldClientMigration::detect().isInstalled() || (!oldExe.isEmpty() && QFileInfo::exists(oldExe));
}

} // namespace

namespace OldClientMigration {

Detection detect()
{
    Detection d;
    for (const UninstallEntry &entry : readEntries()) {
        if (!entry.displayName.isEmpty()) {
            d.installed << entry;
        }
    }
    d.progIdCommand = WinFileAssociation::progIdCommandLine();
    d.progIdPointsToOldExe = isOldExe(commandExe(d.progIdCommand));
    return d;
}

Report migrate(SettingsStore *store, const Options &options)
{
    Report report;
    const Detection d = detect();
    QStringList where;
    for (const UninstallEntry &entry : d.installed) {
        where << QStringLiteral("%1 (%2)").arg(entry.where, entry.displayName);
    }
    if (d.progIdPointsToOldExe) {
        where << QStringLiteral("ProgID -> %1").arg(commandExe(d.progIdCommand));
    }
    addLine(report, d.hasTrace() ? QStringLiteral("[deteccion] rastro del cliente viejo: %1").arg(where.join(QStringLiteral(", ")))
                                 : QStringLiteral("[deteccion] sin rastro del cliente viejo"));

    bool moduleOn = true;
    if (store) {
        const QVariant enabled = store->value(kEnabledKey);
        if (d.hasTrace() && !enabled.isValid()) {
            if (options.dryRun) {
                addLine(report, QStringLiteral("[modulo] (solo log) prenderia Open in NukeX"));
            } else {
                store->setValue(kEnabledKey, true);
                report.moduleEnabledNow = true;
                addLine(report, QStringLiteral("[modulo] Open in NukeX prendido (sin tocar el inicio con Windows)"));
            }
        } else if (d.hasTrace()) {
            addLine(report, QStringLiteral("[modulo] ya decidido por el usuario (enabled=%1): no se cambia").arg(enabled.toString()));
        }
        // En solo log, lo que "prenderia" cuenta como prendido para mostrar que haria con los .nk.
        const bool wouldEnable = options.dryRun && d.hasTrace() && !enabled.isValid();
        moduleOn = report.moduleEnabledNow || wouldEnable || store->value(kEnabledKey, false).toBool();
    }

    if (d.hasTrace()) {
        takeNkAssociation(moduleOn, options, report);
        if (d.isInstalled()) {
            addLine(report, QStringLiteral("[restos] el cliente viejo sigue instalado: sus claves quedan"));
        } else if (options.dryRun || options.buildTree) {
            addLine(report, QStringLiteral("[restos] (%1) se revisarian los restos del cliente viejo")
                                .arg(options.dryRun ? QStringLiteral("solo log") : QStringLiteral("arbol de build")));
        } else {
            const QStringList removed = WinFileAssociation::removeOldClientLeftovers(false);
            addLine(report, removed.isEmpty() ? QStringLiteral("[restos] nada para borrar")
                                              : QStringLiteral("[restos] borrados: %1").arg(removed.join(QStringLiteral(", "))));
        }
    }

    // La marca, siempre. Nunca baja de "migrated" a "no-trace": despues de desinstalar el viejo ya
    // no queda rastro, pero la migracion igual paso.
    if (store) {
        const QString result = d.hasTrace() ? kMarkMigrated : kMarkNoTrace;
        const QString existing = store->value(kMarkKey).toString();
        if (existing.isEmpty() || (result == kMarkMigrated && existing != kMarkMigrated)) {
            if (options.dryRun) {
                addLine(report, QStringLiteral("[marca] (solo log) %1=%2").arg(kMarkKey, result));
            } else {
                store->setValue(kMarkKey, result);
                addLine(report, QStringLiteral("[marca] %1=%2").arg(kMarkKey, result));
            }
        } else {
            addLine(report, QStringLiteral("[marca] ya estaba: %1=%2").arg(kMarkKey, existing));
        }
    }
    return report;
}

bool splitUninstallString(const QString &uninstallString, QString *exe, QString *arguments)
{
    const QString trimmed = uninstallString.trimmed();
    QString path;
    QString rest;
    if (trimmed.startsWith(QLatin1Char('"'))) {
        const int close = trimmed.indexOf(QLatin1Char('"'), 1);
        if (close <= 1) {
            return false;
        }
        path = trimmed.mid(1, close - 1);
        rest = trimmed.mid(close + 1).trimmed();
    } else {
        const int exeEnd = trimmed.indexOf(QStringLiteral(".exe"), 0, Qt::CaseInsensitive);
        if (exeEnd <= 0) {
            return false;
        }
        path = trimmed.left(exeEnd + 4);
        rest = trimmed.mid(exeEnd + 4).trimmed();
    }
    if (!path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) || QDir::isRelativePath(QDir::fromNativeSeparators(path))) {
        return false;
    }
    if (exe) {
        *exe = QDir::toNativeSeparators(path);
    }
    if (arguments) {
        *arguments = rest;
    }
    return true;
}

bool runningOnQaDesktop()
{
    HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
    if (!desktop) {
        return false;
    }
    wchar_t name[256] = {};
    DWORD needed = 0;
    if (!GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name) - sizeof(wchar_t), &needed)) {
        return false;
    }
    return QString::fromWCharArray(name).startsWith(QStringLiteral("LGA_QA_"), Qt::CaseInsensitive);
}

Report removeOldClient(SettingsStore *store, const Options &options)
{
    Report report;
    const QDeadlineTimer deadline(options.removeTimeoutMs);
    QStringList tried;
    for (int round = 0; round < 3; ++round) {
        const Detection d = detect();
        if (!d.isInstalled()) {
            addLine(report, round == 0 ? QStringLiteral("[quitar] el cliente viejo no esta instalado")
                                       : QStringLiteral("[quitar] el cliente viejo ya no esta instalado"));
            break;
        }
        const UninstallEntry entry = d.installed.first();
        if (tried.contains(entry.uninstallString, Qt::CaseInsensitive)) {
            addLine(report, QStringLiteral("[quitar] %1 sigue en la lista despues de su desinstalador").arg(entry.where));
            break;
        }
        tried << entry.uninstallString;

        QString exe;
        QString arguments;
        const bool parsed = splitUninstallString(entry.uninstallString, &exe, &arguments);
        // Solo un desinstalador de Inno (unins*.exe): nunca otra cosa escrita en esa clave.
        if (!parsed || !exeFileName(exe).startsWith(QStringLiteral("unins"), Qt::CaseInsensitive)) {
            ++report.failures;
            addLine(report, QStringLiteral("[quitar] %1: UninstallString no utilizable (%2): no se lanza nada")
                                .arg(entry.where, entry.uninstallString));
            break;
        }
        const bool exists = QFileInfo::exists(exe);
        const QString parameters = (arguments.isEmpty() ? QString() : arguments + QLatin1Char(' ')) + kSilentUninstallArgs;
        if (!options.launchAllowed) {
            addLine(report, QStringLiteral("[quitar] (solo log) lanzaria %1 %2 (existe: %3) y esperaria hasta %4 s")
                                .arg(exe, parameters, exists ? QStringLiteral("si") : QStringLiteral("no"))
                                .arg(options.removeTimeoutMs / 1000));
            break;
        }
        if (!exists) {
            ++report.failures;
            addLine(report, QStringLiteral("[quitar] %1: el desinstalador no esta (%2): no se lanza nada").arg(entry.where, exe));
            break;
        }

        // ShellExecuteEx pide COM inicializado en el hilo (el del panel no lo tiene).
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const std::wstring file = exe.toStdWString();
        const std::wstring params = parameters.toStdWString();
        const std::wstring dir = QDir::toNativeSeparators(QFileInfo(exe).absolutePath()).toStdWString();
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        info.lpVerb = L"open";
        info.lpFile = file.c_str();
        info.lpParameters = params.c_str();
        info.lpDirectory = dir.c_str();
        info.nShow = SW_SHOWNORMAL;
        const BOOL launched = ShellExecuteExW(&info);
        const DWORD launchError = launched ? 0 : GetLastError();
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        if (!launched) {
            ++report.failures;
            addLine(report, launchError == ERROR_CANCELLED
                                ? QStringLiteral("[quitar] el usuario cancelo el permiso de administrador")
                                : QStringLiteral("[quitar] no se pudo lanzar %1 (error %2)").arg(exe).arg(launchError));
            break;
        }
        report.launched = true;
        addLine(report, QStringLiteral("[quitar] lanzado %1 %2 (%3)").arg(exe, parameters, entry.where));

        DWORD exitCode = 0;
        if (info.hProcess) {
            const qint64 remaining = qMax<qint64>(0, deadline.remainingTime());
            WaitForSingleObject(info.hProcess, DWORD(remaining));
            if (!GetExitCodeProcess(info.hProcess, &exitCode)) {
                exitCode = 0;
            }
            CloseHandle(info.hProcess);
        }
        // El desinstalador de Inno se copia a %TEMP% y sigue desde ahi (y pide el UAC): el proceso
        // lanzado puede terminar antes que la desinstalacion. Se sondea hasta que desaparecen la
        // entrada de la lista y el exe viejo. Si termino con error (UAC cancelado), un rato corto.
        QDeadlineTimer poll = deadline;
        if (exitCode != 0 && exitCode != STILL_ACTIVE) {
            addLine(report, QStringLiteral("[quitar] el desinstalador termino con codigo %1").arg(exitCode));
            poll = QDeadlineTimer(qMin<qint64>(3000, qMax<qint64>(0, deadline.remainingTime())));
        }
        while (!poll.hasExpired() && stillThere(entry.oldExe)) {
            QThread::msleep(500);
        }
        if (deadline.hasExpired()) {
            break;
        }
    }

    const Detection after = detect();
    report.stillInstalled = after.isInstalled();
    if (report.launched) {
        addLine(report, report.stillInstalled ? QStringLiteral("[quitar] el cliente viejo SIGUE instalado")
                                              : QStringLiteral("[quitar] el cliente viejo ya no esta instalado"));
    }

    // Rehacer la toma de los .nk (el desinstalador corre `assoc .nk=`) y los restos.
    const Report again = migrate(store, options);
    report.lines << again.lines;
    report.failures += again.failures;
    report.nkTaken = again.nkTaken;
    report.moduleEnabledNow = again.moduleEnabledNow;
    return report;
}

namespace {

void printReport(const QString &name, const Report &report)
{
    for (const QString &line : report.lines) {
        std::printf("%s\n", qPrintable(line));
        qInfo().noquote() << QStringLiteral("[%1]").arg(name) << line;
    }
    std::fflush(stdout);
}

Options commandLineOptions(const QStringList &arguments, bool *qaDesktop)
{
    Options options;
    options.buildTree = AppPaths::isBuildTree();
    *qaDesktop = runningOnQaDesktop();
    options.dryRun = *qaDesktop || arguments.contains(QStringLiteral("--dry-run"));
    return options;
}

} // namespace

int runMigrateFromCommandLine(const QStringList &arguments)
{
    bool qaDesktop = false;
    const Options options = commandLineOptions(arguments, &qaDesktop);
    FileSettingsStore store;
    Report report = migrate(&store, options);
    report.lines.prepend(QStringLiteral("[modo] %1%2")
                             .arg(options.dryRun ? QStringLiteral("solo log") : QStringLiteral("real"),
                                  options.buildTree ? QStringLiteral(", arbol de build (el registro solo se loguea)") : QString()));
    report.lines << QStringLiteral("migrate-openinnukex: %1 fallas").arg(report.failures);
    printReport(QStringLiteral("migrate-openinnukex"), report);
    return report.failures == 0 ? 0 : 1;
}

int runRemoveFromCommandLine(const QStringList &arguments)
{
    bool qaDesktop = false;
    Options options = commandLineOptions(arguments, &qaDesktop);
    // Desde la linea de comandos, un arbol de build o el arnes de QA nunca lanzan el desinstalador.
    options.launchAllowed = !options.dryRun && !options.buildTree;
    FileSettingsStore store;
    Report report = removeOldClient(&store, options);
    report.lines.prepend(QStringLiteral("[modo] %1%2")
                             .arg(options.launchAllowed ? QStringLiteral("real") : QStringLiteral("solo log (no se lanza nada)"),
                                  options.buildTree ? QStringLiteral(", arbol de build") : QString()));
    const bool failed = report.failures > 0 || (options.launchAllowed && report.stillInstalled);
    report.lines << QStringLiteral("remove-old-client: %1").arg(failed ? QStringLiteral("FALLO") : QStringLiteral("ok"));
    printReport(QStringLiteral("remove-old-client"), report);
    return failed ? 1 : 0;
}

} // namespace OldClientMigration
