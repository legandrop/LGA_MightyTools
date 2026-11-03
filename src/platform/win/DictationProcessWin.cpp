#include "platform/DictationProcess.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <windows.h>

namespace {

// El stdout de verdad, reservado para el protocolo. STD_OUTPUT_HANDLE pasa a ser stderr.
HANDLE g_protocolOut = INVALID_HANDLE_VALUE;

} // namespace

namespace DictationProcess {

void hideConsole(QProcess *process)
{
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
}

void prepareWorker()
{
    // Ni "falta una DLL" ni "dejo de funcionar": un driver de GPU roto tiene que terminar en un ERROR
    // del protocolo o en un proceso caido, nunca en un cartel en el escritorio.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE
        && DuplicateHandle(GetCurrentProcess(), out, GetCurrentProcess(), &g_protocolOut, 0, FALSE,
                           DUPLICATE_SAME_ACCESS)) {
        // Lo que la DLL del motor (o su CRT, si arranca despues) escriba en la salida estandar va a stderr
        // y no rompe el protocolo.
        SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));
    } else {
        g_protocolOut = out;
    }
}

qint64 readInput(char *data, qint64 size)
{
    DWORD read = 0;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), data, DWORD(qMin<qint64>(size, 1 << 20)), &read, nullptr)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    }
    return qint64(read);
}

bool writeOutput(const QByteArray &data)
{
    const char *p = data.constData();
    qint64 left = data.size();
    while (left > 0) {
        DWORD written = 0;
        if (!WriteFile(g_protocolOut, p, DWORD(qMin<qint64>(left, 1 << 20)), &written, nullptr) || written == 0) {
            return false;
        }
        p += written;
        left -= written;
    }
    return true;
}

void *loadLibrary(const QString &path, QString *error)
{
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    // La carpeta del motor primero (y ya no la carpeta actual) para las dependencias que carga despues.
    SetDllDirectoryW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(QFileInfo(path).absolutePath()).utf16()));
    HMODULE module = LoadLibraryExW(reinterpret_cast<const wchar_t *>(native.utf16()), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module && error) {
        *error = QStringLiteral("LoadLibrary %1: error %2").arg(native).arg(GetLastError());
    }
    return module;
}

QFunctionPointer resolve(void *library, const char *name)
{
    return reinterpret_cast<QFunctionPointer>(GetProcAddress(static_cast<HMODULE>(library), name));
}

QString systemTar()
{
    const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
    const QString tar = QDir(root).filePath(QStringLiteral("System32/tar.exe"));
    return QFileInfo::exists(tar) ? QDir::toNativeSeparators(tar) : QString();
}

bool processAlive(qint64 pid)
{
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!process) {
        return false;
    }
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

} // namespace DictationProcess
