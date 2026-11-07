#include "platform/DictationProcess.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <cerrno>
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

// El stdout de verdad, reservado para el protocolo. El descriptor 1 pasa a ser una copia de stderr.
int g_protocolOut = STDOUT_FILENO;

} // namespace

namespace DictationProcess {

void hideConsole(QProcess *)
{
    // En mac un proceso hijo sin ventana no muestra nada: no hay consola que esconder.
}

void prepareWorker()
{
    // Si la app se cae con el motor escribiendo, write() devuelve EPIPE en vez de matar el proceso con SIGPIPE.
    std::signal(SIGPIPE, SIG_IGN);
    const int saved = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    if (saved >= 0 && dup2(STDERR_FILENO, STDOUT_FILENO) >= 0) {
        // Lo que el motor (o ggml) escriba en la salida estandar va a stderr y no rompe el protocolo.
        g_protocolOut = saved;
    } else if (saved >= 0) {
        close(saved);
    }
}

qint64 readInput(char *data, qint64 size)
{
    for (;;) {
        const ssize_t got = ::read(STDIN_FILENO, data, size_t(qMin<qint64>(size, 1 << 20)));
        if (got >= 0) {
            return qint64(got); // 0: la app cerro el canal
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

bool writeOutput(const QByteArray &data)
{
    const char *p = data.constData();
    qint64 left = data.size();
    while (left > 0) {
        const ssize_t written = ::write(g_protocolOut, p, size_t(qMin<qint64>(left, 1 << 20)));
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        p += written;
        left -= written;
    }
    return true;
}

void *loadLibrary(const QString &path, QString *error)
{
    // Las dependencias del motor (libggml*.dylib) vienen con @loader_path: se buscan al lado de esta.
    const QByteArray file = QFile::encodeName(QFileInfo(path).absoluteFilePath());
    void *library = dlopen(file.constData(), RTLD_NOW | RTLD_LOCAL);
    if (!library && error) {
        const char *why = dlerror();
        *error = QStringLiteral("dlopen %1: %2").arg(QString::fromUtf8(file), QString::fromUtf8(why ? why : "?"));
    }
    return library;
}

QFunctionPointer resolve(void *library, const char *name)
{
    return reinterpret_cast<QFunctionPointer>(dlsym(library, name));
}

QString systemTar()
{
    const QString tar = QStringLiteral("/usr/bin/tar");
    return QFileInfo(tar).isExecutable() ? tar : QString();
}

bool processAlive(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
}

} // namespace DictationProcess
