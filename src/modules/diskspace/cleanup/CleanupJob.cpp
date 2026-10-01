#include "modules/diskspace/cleanup/CleanupJob.h"

#include "core/AutomatedRun.h"
#include "modules/diskspace/cleanup/ScanEngine.h"
#include "platform/ComApartment.h"
#include "platform/DirEnumerator.h"
#include "platform/FileSystemOps.h"
#include "platform/RecycleBin.h"
#include "platform/SystemPaths.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSet>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using DirEnumerator::NativeString;

constexpr int kSampleLimit = 5;
constexpr qint64 kDaySecs = 86400;

struct ChildEntry
{
    NativeString name;
    bool isDir = false;
    bool isLink = false;
    bool isCloud = false;
    quint64 allocated = 0;
    qint64 modified = 0;
    qint64 created = 0;
};

// Lista una carpeta ENTERA antes de tocar nada: no se borra mientras se enumera.
bool listDir(const NativeString &dir, std::vector<char> &scratch, std::vector<ChildEntry> &out)
{
    out.clear();
    return DirEnumerator::enumerate(dir, scratch, [&out](const DirEnumerator::Entry &entry) {
        ChildEntry child;
        child.name.assign(entry.name, size_t(entry.nameLength));
        child.isDir = entry.isDir;
        child.isLink = entry.isLink;
        child.isCloud = entry.isCloud;
        child.allocated = entry.allocated;
        child.modified = entry.modifiedSecs;
        child.created = entry.createdSecs;
        out.push_back(std::move(child));
    });
}

// Lo que hay debajo de una carpeta, sin tocarlo.
struct Inspection
{
    qint64 bytes = 0;
    qint64 files = 0;
    qint64 newest = 0;  ///< lo mas nuevo entre creacion y modificacion, de archivos y carpetas
    bool inUse = false; ///< algun archivo esta abierto por otro programa, o una carpeta no se pudo listar
    bool cloud = false; ///< tiene algo de la nube sin bajar
};

void inspect(const NativeString &dir, std::vector<char> &scratch, Inspection &result, bool probeInUse,
             const std::atomic<bool> *cancel)
{
    std::vector<ChildEntry> entries;
    if (!listDir(dir, scratch, entries)) {
        result.inUse = true;
        return;
    }
    for (const ChildEntry &entry : entries) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            return;
        }
        result.newest = qMax(result.newest, qMax(entry.modified, entry.created));
        if (entry.isCloud) {
            result.cloud = true;
            continue;
        }
        if (entry.isDir) {
            if (!entry.isLink) {
                NativeString sub = dir + entry.name;
                sub += DirEnumerator::separator();
                inspect(sub, scratch, result, probeInUse, cancel);
            }
            continue;
        }
        if (entry.isLink) {
            continue;
        }
        ++result.files;
        result.bytes += qint64(entry.allocated);
        if (probeInUse && !result.inUse && FileSystemOps::isInUse(dir + entry.name)) {
            result.inUse = true;
        }
    }
}

} // namespace

struct CleanupJob::Shared
{
    QList<Request> requests;
    DeleteGuard guard;
    std::atomic<bool> cancel{false};
    std::atomic<bool> running{false};
    std::atomic<int> done{0};
    std::atomic<qint64> files{0};
    std::atomic<qint64> bytes{0};
    std::mutex mutex;
    QString current;
    QList<Outcome> outcomes;

    void run();
    void execute(const Request &request, Outcome &outcome, const QSet<QString> &programs, std::vector<char> &scratch);
    bool deleteTree(const NativeString &dir, Outcome &outcome, std::vector<char> &scratch);
    bool removeEntry(const NativeString &path, const ChildEntry &entry, Outcome &outcome, std::vector<char> &scratch);
    void deleteOldChildren(const NativeString &dir, int minAgeDays, Outcome &outcome, std::vector<char> &scratch);
    void skip(Outcome &outcome, const NativeString &path, qint64 files, qint64 bytes);
    void setCurrent(const NativeString &path);
};

void CleanupJob::Shared::skip(Outcome &outcome, const NativeString &path, qint64 count, qint64 size)
{
    outcome.skippedFiles += count;
    outcome.skippedBytes += size;
    if (outcome.skippedSample.size() < kSampleLimit) {
        outcome.skippedSample.append(DirEnumerator::toDisplay(path));
    }
}

void CleanupJob::Shared::setCurrent(const NativeString &path)
{
    std::lock_guard<std::mutex> lock(mutex);
    current = DirEnumerator::toDisplay(path);
}

// true si la entrada ya no esta.
bool CleanupJob::Shared::removeEntry(const NativeString &path, const ChildEntry &entry, Outcome &outcome,
                                     std::vector<char> &scratch)
{
    // De la nube sin bajar: borrarlo lo borra en la nube y en los demas dispositivos, sin Papelera.
    if (entry.isCloud) {
        skip(outcome, path, 1, 0);
        return false;
    }
    if (entry.isDir) {
        if (!entry.isLink) {
            NativeString sub = path;
            sub += DirEnumerator::separator();
            if (!deleteTree(sub, outcome, scratch)) {
                return false;
            }
        }
        // Un enlace de carpeta se borra como enlace: su destino no se toca.
        const FileSystemOps::Result result = FileSystemOps::removeDir(path);
        if (result == FileSystemOps::Result::Removed || result == FileSystemOps::Result::Missing) {
            return true;
        }
        skip(outcome, path, 1, 0);
        return false;
    }
    const FileSystemOps::Result result = FileSystemOps::removeFile(path);
    if (result == FileSystemOps::Result::Removed) {
        ++outcome.deletedFiles;
        outcome.freedBytes += qint64(entry.allocated);
        files.fetch_add(1, std::memory_order_relaxed);
        bytes.fetch_add(qint64(entry.allocated), std::memory_order_relaxed);
        return true;
    }
    if (result == FileSystemOps::Result::Missing) {
        return true;
    }
    skip(outcome, path, 1, qint64(entry.allocated));
    return false;
}

// true si la carpeta quedo vacia.
bool CleanupJob::Shared::deleteTree(const NativeString &dir, Outcome &outcome, std::vector<char> &scratch)
{
    std::vector<ChildEntry> entries;
    if (!listDir(dir, scratch, entries)) {
        skip(outcome, dir, 1, 0);
        return false;
    }
    setCurrent(dir);
    bool empty = true;
    for (const ChildEntry &entry : entries) {
        if (cancel.load()) {
            return false;
        }
        if (!removeEntry(dir + entry.name, entry, outcome, scratch)) {
            empty = false;
        }
    }
    return empty;
}

void CleanupJob::Shared::deleteOldChildren(const NativeString &dir, int minAgeDays, Outcome &outcome, std::vector<char> &scratch)
{
    std::vector<ChildEntry> entries;
    if (!listDir(dir, scratch, entries)) {
        skip(outcome, dir, 1, 0);
        return;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const ChildEntry &entry : entries) {
        if (cancel.load()) {
            return;
        }
        const NativeString path = dir + entry.name;
        if (entry.isCloud) {
            skip(outcome, path, 1, 0);
            continue;
        }
        Inspection inside;
        inside.newest = qMax(entry.modified, entry.created);
        const bool isTree = entry.isDir && !entry.isLink;
        NativeString sub = path;
        sub += DirEnumerator::separator();
        if (isTree) {
            inspect(sub, scratch, inside, false, &cancel);
        } else if (!entry.isDir) {
            inside.files = 1;
            inside.bytes = qint64(entry.allocated);
        }
        // La antiguedad se mira ACA, al borrar, no la que se mostro: un instalador recien extraido
        // trae las fechas de modificacion del paquete, por eso cuenta tambien la de creacion.
        if (now - inside.newest < qint64(minAgeDays) * kDaySecs) {
            continue;
        }
        // Recien ahora, y solo a lo que es viejo, se le pregunta si esta en uso: la pregunta abre cada
        // archivo en exclusiva un instante, y no hay por que hacerselo a los archivos nuevos de un
        // programa que esta trabajando.
        if (isTree) {
            Inspection probe;
            inspect(sub, scratch, probe, true, &cancel);
            inside.inUse = probe.inUse;
            inside.cloud = inside.cloud || probe.cloud;
        } else if (!entry.isDir) {
            inside.inUse = FileSystemOps::isInUse(path);
        }
        // Con algo en uso, el hijo entero se queda: borrar la mitad de la carpeta de un programa que
        // esta trabajando la rompe.
        if (inside.inUse || inside.cloud) {
            skip(outcome, path, qMax<qint64>(1, inside.files), inside.bytes);
            continue;
        }
        if (guard.check(DirEnumerator::toDisplay(path), DeleteGuard::Scope::Entire, false) != DeleteGuard::Verdict::Ok) {
            continue;
        }
        setCurrent(path);
        removeEntry(path, entry, outcome, scratch);
    }
}

void CleanupJob::Shared::execute(const Request &request, Outcome &outcome, const QSet<QString> &programs,
                                 std::vector<char> &scratch)
{
    for (const QString &blocker : request.blockers) {
        if (programs.contains(blocker)) {
            outcome.blockedBy = request.blockerLabel.isEmpty() ? blocker : request.blockerLabel;
            return;
        }
    }
    if (request.action == Cleanup::Action::RecycleBin) {
        if (AutomatedRun::active()) {
            outcome.refused = DeleteGuard::Verdict::AutomatedRun;
            return;
        }
        // La Papelera del pedido tiene que ser la del volumen que la ventana tiene abierto.
        if (!DeleteGuard::samePath(request.path, guard.volumeRoot)) {
            outcome.refused = DeleteGuard::Verdict::OutsideVolume;
            return;
        }
        // El peso se pregunta de nuevo: pudo cambiar desde que se mostro.
        const RecycleBin::Info before = RecycleBin::query(guard.volumeRoot);
        if (RecycleBin::empty(guard.volumeRoot)) {
            outcome.freedBytes = before.ok ? before.bytes : 0;
            outcome.deletedFiles = before.ok ? before.items : 0;
            outcome.removedEntirely = true;
            bytes.fetch_add(outcome.freedBytes);
        } else {
            outcome.error = QStringLiteral("recycle bin");
        }
        return;
    }

    // La ruta REAL: si la escrita pasa por un enlace, se trabaja (y se valida) sobre a donde lleva.
    const QString real = FileSystemOps::canonicalPath(request.path);
    if (real.isEmpty()) {
        outcome.refused = DeleteGuard::Verdict::Missing;
        return;
    }
    outcome.path = real;
    const FileSystemOps::Kind kind = FileSystemOps::kind(real);
    DeleteGuard::Scope scope = DeleteGuard::Scope::Entire;
    if (request.action == Cleanup::Action::Contents) {
        scope = DeleteGuard::Scope::Contents;
    } else if (request.action == Cleanup::Action::OldChildren) {
        scope = DeleteGuard::Scope::Children;
    }
    const DeleteGuard::Verdict verdict = guard.check(real, scope, request.toTrash);
    if (verdict != DeleteGuard::Verdict::Ok) {
        outcome.refused = verdict;
        return;
    }
    if (AutomatedRun::active() && !AutomatedRun::mayModify(real)) {
        outcome.refused = DeleteGuard::Verdict::AutomatedRun;
        return;
    }

    if (request.action != Cleanup::Action::Entire) {
        // Entrar en una carpeta que en realidad es un enlace seria borrar en otro lado.
        if (kind != FileSystemOps::Kind::Dir) {
            outcome.refused = kind == FileSystemOps::Kind::Link ? DeleteGuard::Verdict::Link : DeleteGuard::Verdict::Missing;
            return;
        }
        const NativeString dir = DirEnumerator::nativeDir(real);
        if (request.action == Cleanup::Action::Contents) {
            deleteTree(dir, outcome, scratch);
        } else {
            deleteOldChildren(dir, request.minAgeDays, outcome, scratch);
        }
        return;
    }

    if (request.toTrash) {
        QString error;
        if (RecycleBin::moveToTrash(real, &error)) {
            outcome.trashed = true;
            outcome.removedEntirely = true;
        } else {
            outcome.error = error.isEmpty() ? QStringLiteral("recycle bin") : error;
        }
        return;
    }

    const NativeString native = FileSystemOps::nativePath(real);
    switch (kind) {
    case FileSystemOps::Kind::Missing:
        outcome.refused = DeleteGuard::Verdict::Missing;
        return;
    case FileSystemOps::Kind::Link: {
        // El enlace mismo, sea de carpeta o de archivo.
        FileSystemOps::Result result = FileSystemOps::removeDir(native);
        if (result != FileSystemOps::Result::Removed) {
            result = FileSystemOps::removeFile(native);
        }
        outcome.removedEntirely = result == FileSystemOps::Result::Removed;
        if (!outcome.removedEntirely) {
            skip(outcome, native, 1, 0);
        }
        return;
    }
    case FileSystemOps::Kind::File: {
        const qint64 size = QFileInfo(real).size();
        // De la nube sin bajar: el borrado definitivo lo borraria en la nube, sin Papelera.
        if (FileSystemOps::isCloudPlaceholder(native)) {
            skip(outcome, native, 1, 0);
            return;
        }
        const FileSystemOps::Result result = FileSystemOps::removeFile(native);
        if (result == FileSystemOps::Result::Removed) {
            outcome.removedEntirely = true;
            outcome.deletedFiles = 1;
            outcome.freedBytes = size;
            files.fetch_add(1);
            bytes.fetch_add(size);
        } else {
            skip(outcome, native, 1, size);
        }
        return;
    }
    case FileSystemOps::Kind::Dir: {
        if (deleteTree(DirEnumerator::nativeDir(real), outcome, scratch)) {
            const FileSystemOps::Result result = FileSystemOps::removeDir(native);
            outcome.removedEntirely = result == FileSystemOps::Result::Removed || result == FileSystemOps::Result::Missing;
            if (!outcome.removedEntirely) {
                skip(outcome, native, 1, 0);
            }
        }
        return;
    }
    }
}

void CleanupJob::Shared::run()
{
    // COM en este hilo: la Papelera (IFileOperation, SHEmptyRecycleBin) lo necesita.
    const ComApartment com;
    const QSet<QString> programs = SystemPaths::runningPrograms();
    std::vector<char> scratch;
    for (const Request &request : requests) {
        if (cancel.load()) {
            break;
        }
        Outcome outcome;
        outcome.id = request.id;
        outcome.label = request.label;
        outcome.path = request.path;
        execute(request, outcome, programs, scratch);
        {
            std::lock_guard<std::mutex> lock(mutex);
            outcomes.append(outcome);
        }
        done.fetch_add(1);
    }
}

CleanupJob::CleanupJob()
    : m_shared(std::make_shared<Shared>())
{
}

CleanupJob::~CleanupJob()
{
    cancel();
}

bool CleanupJob::start(const QList<Request> &requests, const DeleteGuard &guard)
{
    if (isRunning()) {
        return false;
    }
    m_shared = std::make_shared<Shared>();
    m_shared->requests = requests;
    m_shared->guard = guard;
    m_shared->running.store(true);
    try {
        // El hilo se queda con una referencia al estado y se suelta: nadie lo espera.
        std::thread([shared = m_shared]() {
            const CleanupThreads::Scope scope;
            try {
                shared->run();
            } catch (...) {
                // Lo ya borrado queda borrado; el resto del pedido no se ejecuta.
            }
            shared->running.store(false);
        }).detach();
    } catch (...) {
        m_shared->running.store(false);
        return false;
    }
    return true;
}

void CleanupJob::cancel()
{
    m_shared->cancel.store(true);
}

bool CleanupJob::isRunning() const
{
    return m_shared->running.load();
}

CleanupJob::Progress CleanupJob::progress() const
{
    Shared &s = *m_shared;
    Progress progress;
    progress.running = s.running.load();
    progress.done = s.done.load();
    progress.total = int(s.requests.size());
    progress.files = s.files.load();
    progress.bytes = s.bytes.load();
    std::lock_guard<std::mutex> lock(s.mutex);
    progress.current = s.current;
    return progress;
}

QList<CleanupJob::Outcome> CleanupJob::outcomes() const
{
    Shared &s = *m_shared;
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.outcomes;
}

CleanupJob::Measure CleanupJob::measureTree(const QString &dir)
{
    Measure measure;
    if (FileSystemOps::kind(dir) != FileSystemOps::Kind::Dir) {
        return measure;
    }
    std::vector<char> scratch;
    Inspection inside;
    inspect(DirEnumerator::nativeDir(dir), scratch, inside, false, nullptr);
    measure.bytes = inside.bytes;
    measure.files = inside.files;
    measure.newest = inside.newest;
    return measure;
}

CleanupJob::Measure CleanupJob::measureOldChildren(const QString &dir, int minAgeDays, qint64 nowSecs)
{
    Measure measure;
    if (FileSystemOps::kind(dir) != FileSystemOps::Kind::Dir) {
        return measure;
    }
    std::vector<char> scratch;
    std::vector<ChildEntry> entries;
    const NativeString native = DirEnumerator::nativeDir(dir);
    if (!listDir(native, scratch, entries)) {
        return measure;
    }
    for (const ChildEntry &entry : entries) {
        if (entry.isCloud) {
            continue;
        }
        Inspection inside;
        inside.newest = qMax(entry.modified, entry.created);
        if (entry.isDir && !entry.isLink) {
            NativeString sub = native + entry.name;
            sub += DirEnumerator::separator();
            inspect(sub, scratch, inside, false, nullptr);
        } else if (!entry.isDir) {
            inside.files = 1;
            inside.bytes = qint64(entry.allocated);
        }
        if (nowSecs - inside.newest < qint64(minAgeDays) * kDaySecs) {
            measure.keptBytes += inside.bytes;
            continue;
        }
        measure.bytes += inside.bytes;
        measure.files += inside.files;
        measure.newest = qMax(measure.newest, inside.newest);
    }
    return measure;
}
