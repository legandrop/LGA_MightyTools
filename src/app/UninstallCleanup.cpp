#include "app/UninstallCleanup.h"

#include "app/ModuleRegistry.h"
#include "platform/AutoStart.h"
#include "platform/ToastActivation.h"

#include <QDebug>

#include <cstdio>

namespace UninstallCleanup {

Report run(const QList<ModuleDescriptor> &descriptors)
{
    Report report;
    for (const ModuleDescriptor &d : descriptors) {
        if (!d.releaseSystem) {
            continue;
        }
        QString error;
        const bool ok = d.releaseSystem(&error);
        if (!ok) {
            ++report.failures;
        }
        report.lines << (ok ? QStringLiteral("[%1] ok").arg(d.id)
                            : QStringLiteral("[%1] FALLO: %2").arg(d.id, error.isEmpty() ? QStringLiteral("(sin detalle)") : error));
    }
    QString detail;
    if (!AutoStart::removeIfOwned(&detail)) {
        ++report.failures;
    }
    report.lines << QStringLiteral("[autoStart] %1").arg(detail);
    // La anotacion de los avisos ante Windows (AUMID y activador COM).
    QString toastDetail;
    if (!ToastActivation::removeIfOwned(&toastDetail)) {
        ++report.failures;
    }
    report.lines << QStringLiteral("[avisos] %1").arg(toastDetail);
    return report;
}

int runFromCommandLine()
{
    const Report report = run(ModuleRegistry::all());
    for (const QString &line : report.lines) {
        std::printf("%s\n", qPrintable(line));
        qInfo().noquote() << "[uninstall-cleanup]" << line;
    }
    std::printf("uninstall-cleanup: %d fallas\n", report.failures);
    std::fflush(stdout);
    return report.failures == 0 ? 0 : 1;
}

} // namespace UninstallCleanup
