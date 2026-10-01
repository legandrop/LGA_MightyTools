#include "modules/diskspace/cleanup/ScanEngine.h"

#include "platform/DirEnumerator.h"

#include <QDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <vector>

namespace {

// El nombre que el estandar "Cache Directory Tagging" pide dentro de una carpeta de cache.
const char16_t kCacheTag[] = u"CACHEDIR.TAG";
constexpr int kCacheTagLength = 12;

std::atomic<int> g_threads{0};

bool heavier(const ScanEngine::TopFile &a, const ScanEngine::TopFile &b)
{
    return a.bytes > b.bytes;
}

// Monticulo con el archivo MAS LIVIANO arriba: es el que sale cuando entra uno mas pesado.
void pushTop(std::vector<ScanEngine::TopFile> &heap, ScanEngine::TopFile &&file, int limit)
{
    if (int(heap.size()) < limit) {
        heap.push_back(std::move(file));
        std::push_heap(heap.begin(), heap.end(), heavier);
        return;
    }
    if (file.bytes <= heap.front().bytes) {
        return;
    }
    std::pop_heap(heap.begin(), heap.end(), heavier);
    heap.back() = std::move(file);
    std::push_heap(heap.begin(), heap.end(), heavier);
}

bool bySizeThenName(const ScanEngine::TopFile &a, const ScanEngine::TopFile &b)
{
    if (a.bytes != b.bytes) {
        return a.bytes > b.bytes;
    }
    return a.name < b.name;
}

struct Task
{
    ScanTree::Index node = ScanTree::kNone;
    DirEnumerator::NativeString path;
};

} // namespace

namespace CleanupThreads {

int alive()
{
    return g_threads.load();
}

bool waitForNone(int msecs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(msecs);
    while (g_threads.load() > 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

Scope::Scope()
{
    g_threads.fetch_add(1);
}

Scope::~Scope()
{
    g_threads.fetch_sub(1);
}

} // namespace CleanupThreads

// Todo lo que tocan los hilos. Lo comparten el motor y cada hilo: vive hasta que sale el ultimo.
struct ScanEngine::Shared
{
    ScanTree tree;
    std::mutex treeMutex;
    std::vector<TopFile> topFiles; ///< con treeMutex

    std::mutex queueMutex;
    std::condition_variable queueSignal;
    std::vector<Task> queue;
    int active = 0;
    bool passDone = true;

    std::atomic<int> workers{0};
    std::atomic<bool> cancel{false};
    std::chrono::steady_clock::time_point started;
    std::atomic<qint64> elapsedMs{0};

    void workerLoop();
};

void ScanEngine::Shared::workerLoop()
{
    std::vector<char> scratch;
    std::vector<TopFile> top;
    std::u16string names; // nombres de las subcarpetas de la carpeta en curso, pegados
    std::vector<int> nameLengths;
    std::vector<Task> children;

    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueSignal.wait(lock, [this]() { return !queue.empty() || passDone; });
            if (queue.empty()) {
                break;
            }
            task = std::move(queue.back());
            queue.pop_back();
            ++active;
        }

        quint64 ownBytes = 0;
        quint32 ownFiles = 0;
        qint64 newest = 0;
        bool cacheTagged = false;
        names.clear();
        nameLengths.clear();
        children.clear();
        const bool opened = DirEnumerator::enumerate(task.path, scratch, [&](const DirEnumerator::Entry &entry) {
            if (cancel.load(std::memory_order_relaxed)) {
                return;
            }
            if (entry.isDir) {
                // Un enlace lleva a otro lado (quiza a otro disco); una carpeta de nube sin bajar se
                // traeria de la red al listarla. Ninguna de las dos ocupa lugar aca.
                if (entry.isLink || entry.isCloud) {
                    return;
                }
                nameLengths.push_back(DirEnumerator::appendUtf16(names, entry.name, entry.nameLength));
                Task child;
                child.path.reserve(task.path.size() + size_t(entry.nameLength) + 1);
                child.path = task.path;
                child.path.append(entry.name, size_t(entry.nameLength));
                child.path += DirEnumerator::separator();
                children.push_back(std::move(child));
                return;
            }
            if (entry.isLink) {
                return; // un symlink de archivo no ocupa nada propio
            }
            ++ownFiles;
            ownBytes += entry.allocated;
            if (entry.modifiedSecs > newest) {
                newest = entry.modifiedSecs;
            }
            // El nombre solo se convierte cuando mide lo mismo que la marca: casi nunca.
            if (!cacheTagged && entry.nameLength == kCacheTagLength
                && DirEnumerator::nameToString(entry.name, entry.nameLength) == QStringView(kCacheTag, kCacheTagLength)) {
                cacheTagged = true;
            }
            if (int(top.size()) < kTopFiles || entry.allocated > top.front().bytes) {
                TopFile file;
                file.bytes = entry.allocated;
                file.dir = task.node;
                file.name = DirEnumerator::nameToString(entry.name, entry.nameLength);
                file.modified = entry.modifiedSecs;
                pushTop(top, std::move(file), kTopFiles);
            }
        });

        if (!cancel.load()) {
            std::lock_guard<std::mutex> lock(treeMutex);
            size_t offset = 0;
            for (size_t i = 0; i < children.size(); ++i) {
                children[i].node = tree.addChild(task.node, QStringView(names.data() + offset, nameLengths[i]));
                offset += size_t(nameLengths[i]);
            }
            tree.setListed(task.node, ownBytes, ownFiles, newest, !opened, cacheTagged);
        }

        bool wake = !children.empty();
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!cancel.load()) {
                for (Task &child : children) {
                    queue.push_back(std::move(child));
                }
            }
            --active;
            if (queue.empty() && active == 0) {
                passDone = true;
                wake = true;
            }
        }
        if (wake) {
            queueSignal.notify_all();
        }
    }

    // Lo mas pesado que vio este hilo se suma a la lista comun, que se recorta a su tope.
    if (!cancel.load()) {
        std::lock_guard<std::mutex> lock(treeMutex);
        for (TopFile &file : top) {
            topFiles.push_back(std::move(file));
        }
        if (int(topFiles.size()) > kTopFiles) {
            std::nth_element(topFiles.begin(), topFiles.begin() + kTopFiles, topFiles.end(), heavier);
            topFiles.resize(kTopFiles);
        }
    }
    elapsedMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
}

ScanEngine::ScanEngine()
    : m_shared(std::make_shared<Shared>())
{
}

ScanEngine::~ScanEngine()
{
    cancel();
}

void ScanEngine::launch(int threads)
{
    Shared &s = *m_shared;
    s.cancel.store(false);
    s.started = std::chrono::steady_clock::now();
    for (int i = 0; i < threads; ++i) {
        s.workers.fetch_add(1);
        try {
            // El hilo se queda con una referencia al estado y se suelta: nadie lo espera.
            std::thread([shared = m_shared]() {
                const CleanupThreads::Scope scope;
                try {
                    shared->workerLoop();
                } catch (...) {
                    // Un hilo que se cae no deja a los demas esperandolo: la pasada se da por cortada.
                    shared->cancel.store(true);
                    {
                        std::lock_guard<std::mutex> lock(shared->queueMutex);
                        shared->queue.clear();
                        shared->passDone = true;
                    }
                    shared->queueSignal.notify_all();
                }
                shared->workers.fetch_sub(1);
            }).detach();
        } catch (...) {
            // No se pudo crear el hilo (sin recursos): la pasada sigue con los que si arrancaron.
            s.workers.fetch_sub(1);
        }
    }
    if (s.workers.load() == 0) {
        std::lock_guard<std::mutex> lock(s.queueMutex);
        s.queue.clear();
        s.passDone = true;
    }
}

void ScanEngine::cancel()
{
    Shared &s = *m_shared;
    s.cancel.store(true);
    {
        std::lock_guard<std::mutex> lock(s.queueMutex);
        s.queue.clear();
        s.passDone = true;
    }
    s.queueSignal.notify_all();
}

void ScanEngine::start(const QString &rootPath)
{
    // Los hilos de una pasada anterior pueden seguir un instante mas: se quedan con SU estado, y esta
    // pasada arranca con uno nuevo.
    cancel();
    m_shared = std::make_shared<Shared>();
    Shared &s = *m_shared;
    s.tree.reset(DirEnumerator::toDisplay(DirEnumerator::nativeDir(rootPath)));
    s.queue.push_back(Task{s.tree.root(), DirEnumerator::nativeDir(rootPath)});
    s.passDone = false;
    const int cores = int(std::thread::hardware_concurrency());
    launch(qBound(2, cores, kMaxThreads));
}

bool ScanEngine::rescan(const QList<ScanTree::Index> &nodes)
{
    Shared &s = *m_shared;
    if (isRunning() || s.cancel.load()) {
        return false;
    }
    std::vector<Task> tasks;
    {
        std::lock_guard<std::mutex> lock(s.treeMutex);
        if (s.tree.isEmpty()) {
            return false;
        }
        for (const ScanTree::Index node : nodes) {
            if (node == ScanTree::kNone || !s.tree.isAlive(node)) {
                continue;
            }
            // Una carpeta que cuelga de otra de la lista se lee con ella.
            bool covered = false;
            for (const ScanTree::Index other : nodes) {
                if (other != node && other != ScanTree::kNone && s.tree.isInside(node, other)) {
                    covered = true;
                    break;
                }
            }
            if (covered) {
                continue;
            }
            const QString path = s.tree.path(node);
            // Los archivos pesados de ese subarbol salen de la lista: la pasada trae los que sigan estando.
            s.topFiles.erase(std::remove_if(s.topFiles.begin(), s.topFiles.end(),
                                            [&s, node](const TopFile &file) { return s.tree.isInside(file.dir, node); }),
                             s.topFiles.end());
            s.tree.resetSubtree(node);
            tasks.push_back(Task{node, DirEnumerator::nativeDir(path)});
        }
    }
    if (tasks.empty()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(s.queueMutex);
        s.queue = std::move(tasks);
        s.active = 0;
        s.passDone = false;
    }
    const int cores = int(std::thread::hardware_concurrency());
    launch(qBound(2, cores, 4));
    return true;
}

bool ScanEngine::isRunning() const
{
    return m_shared->workers.load() > 0;
}

ScanEngine::Progress ScanEngine::progress() const
{
    Shared &s = *m_shared;
    Progress p;
    p.running = isRunning();
    {
        std::lock_guard<std::mutex> lock(s.treeMutex);
        if (!s.tree.isEmpty()) {
            const ScanTree::Node &root = s.tree.node(s.tree.root());
            p.files = root.files;
            p.dirs = root.dirs;
            p.bytes = root.bytes;
            p.denied = s.tree.deniedCount();
            p.complete = s.tree.isComplete(s.tree.root());
        }
    }
    if (p.running) {
        p.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.started).count();
    } else {
        p.seconds = double(s.elapsedMs.load()) / 1000.0;
    }
    return p;
}

std::unique_lock<std::mutex> ScanEngine::lock() const
{
    return std::unique_lock<std::mutex>(m_shared->treeMutex);
}

ScanTree &ScanEngine::tree()
{
    return m_shared->tree;
}

const ScanTree &ScanEngine::tree() const
{
    return m_shared->tree;
}

QList<ScanEngine::TopFile> ScanEngine::topFiles() const
{
    Shared &s = *m_shared;
    std::lock_guard<std::mutex> lock(s.treeMutex);
    QList<TopFile> files(s.topFiles.begin(), s.topFiles.end());
    std::sort(files.begin(), files.end(), bySizeThenName);
    return files;
}

void ScanEngine::forgetFile(ScanTree::Index dir, const QString &name, quint64 bytes)
{
    Shared &s = *m_shared;
    std::lock_guard<std::mutex> lock(s.treeMutex);
    const auto it = std::find_if(s.topFiles.begin(), s.topFiles.end(),
                                 [&](const TopFile &file) { return file.dir == dir && file.name == name; });
    if (it != s.topFiles.end()) {
        s.topFiles.erase(it);
    }
    if (!s.tree.isEmpty() && dir != ScanTree::kNone) {
        s.tree.removeFile(dir, bytes);
    }
}

void ScanEngine::forgetDir(ScanTree::Index dir)
{
    Shared &s = *m_shared;
    std::lock_guard<std::mutex> lock(s.treeMutex);
    if (s.tree.isEmpty() || dir == ScanTree::kNone) {
        return;
    }
    s.topFiles.erase(std::remove_if(s.topFiles.begin(), s.topFiles.end(),
                                    [&s, dir](const TopFile &file) { return s.tree.isInside(file.dir, dir); }),
                     s.topFiles.end());
    s.tree.removeSubtree(dir);
}

void ScanEngine::setTopFilesForCapture(const QList<TopFile> &files)
{
    Shared &s = *m_shared;
    std::lock_guard<std::mutex> lock(s.treeMutex);
    s.topFiles.assign(files.begin(), files.end());
}

QList<ScanTree::Index> ScanEngine::cacheTaggedDirs() const
{
    Shared &s = *m_shared;
    std::lock_guard<std::mutex> lock(s.treeMutex);
    QList<ScanTree::Index> result;
    for (int i = 0; i < s.tree.nodeCount(); ++i) {
        const ScanTree::Index index = ScanTree::Index(i);
        if ((s.tree.node(index).flags & ScanTree::CacheTagged) && s.tree.isAlive(index)) {
            result.append(index);
        }
    }
    return result;
}

ScanEngine::FileListing ScanEngine::listFiles(const QString &dirPath, int maxEntries)
{
    FileListing listing;
    std::vector<char> scratch;
    std::vector<TopFile> top;
    int count = 0;
    quint64 total = 0;
    listing.ok = DirEnumerator::enumerate(DirEnumerator::nativeDir(dirPath), scratch, [&](const DirEnumerator::Entry &entry) {
        if (entry.isDir || entry.isLink) {
            return;
        }
        ++count;
        total += entry.allocated;
        if (int(top.size()) < maxEntries || entry.allocated > top.front().bytes) {
            TopFile file;
            file.bytes = entry.allocated;
            file.name = DirEnumerator::nameToString(entry.name, entry.nameLength);
            file.modified = entry.modifiedSecs;
            pushTop(top, std::move(file), maxEntries);
        }
    });
    std::sort(top.begin(), top.end(), bySizeThenName);
    quint64 listed = 0;
    for (const TopFile &file : top) {
        listing.largest.append(FileEntry{file.name, file.bytes, file.modified});
        listed += file.bytes;
    }
    listing.restCount = count - int(top.size());
    listing.restBytes = total - listed;
    return listing;
}
