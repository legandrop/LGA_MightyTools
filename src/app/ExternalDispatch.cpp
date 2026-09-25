#include "app/ExternalDispatch.h"

#include "app/SettingsStore.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include <memory>

namespace ExternalDispatch {

QString firstPlainArgument(const QStringList &arguments)
{
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &arg = arguments.at(i);
        if (!arg.startsWith(QLatin1String("--")) && !arg.isEmpty()) {
            return arg;
        }
    }
    return QString();
}

const ModuleDescriptor *claimant(const QList<ModuleDescriptor> &descriptors, const QString &argument)
{
    if (argument.isEmpty()) {
        return nullptr;
    }
    for (const ModuleDescriptor &d : descriptors) {
        if (d.claimsExternal && d.runExternal && d.claimsExternal(argument)) {
            return &d;
        }
    }
    return nullptr;
}

Outcome run(const QList<ModuleDescriptor> &descriptors, const QString &argument, SettingsStore *store, bool dryRun,
            int timeoutMs)
{
    Outcome outcome;
    const ModuleDescriptor *d = claimant(descriptors, argument);
    if (!d) {
        return outcome;
    }
    outcome.handled = true;

    // El estado de la espera vive en el heap: si el modulo llama a finished despues del tope (un
    // socket que contesta tarde), no escribe sobre la pila de una funcion que ya volvio.
    struct Wait
    {
        bool done = false;
        int code = 0;
        QEventLoop *loop = nullptr;
    };
    auto wait = std::make_shared<Wait>();

    ExternalRequest request;
    request.argument = argument;
    request.moduleEnabled = store->value(QStringLiteral("modules/%1/enabled").arg(d->id), false).toBool();
    request.resident = false;
    request.dryRun = dryRun;
    const QString id = d->id;
    request.value = [store, id](const QString &key, const QVariant &defaultValue) {
        return store->value(id + QLatin1Char('/') + key, defaultValue);
    };
    request.finished = [wait](int exitCode) {
        if (wait->done) {
            return;
        }
        wait->done = true;
        wait->code = exitCode;
        if (wait->loop) {
            wait->loop->quit();
        }
    };

    qInfo() << "[ExternalDispatch] Entrada para" << id << "(prendida:" << request.moduleEnabled << ")";
    const ExternalResult result = d->runExternal(request);
    if (result == ExternalResult::NotMine) {
        outcome.handled = false;
        return outcome;
    }
    if (result == ExternalResult::Done) {
        outcome.exitCode = 0;
        return outcome;
    }
    if (!wait->done) {
        QEventLoop loop;
        wait->loop = &loop;
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        loop.exec();
        wait->loop = nullptr;
    }
    if (!wait->done) {
        qWarning() << "[ExternalDispatch]" << id << "no termino en" << timeoutMs << "ms: se sale con 1";
        wait->done = true;
        outcome.timedOut = true;
        outcome.exitCode = 1;
        return outcome;
    }
    outcome.exitCode = wait->code;
    return outcome;
}

} // namespace ExternalDispatch
