#include "modules/openinnukex/NukeScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>

namespace {

// Filtra herramientas auxiliares que igual contienen "nuke" en el nombre (updater, uninstaller...).
bool isAuxiliaryToolName(const QString &fileNameLower)
{
    return fileNameLower.contains(QLatin1String("crash")) || fileNameLower.contains(QLatin1String("feedback"))
        || fileNameLower.contains(QLatin1String("update")) || fileNameLower.contains(QLatin1String("uninstall"))
        || fileNameLower.contains(QLatin1String("setup")) || fileNameLower.contains(QLatin1String("install"));
}

#ifdef Q_OS_WIN

bool isValidNukeExecutable(const QString &executablePath)
{
    QFileInfo info(executablePath);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    if (info.suffix().toLower() != QLatin1String("exe")) {
        return false;
    }
    const QString name = info.fileName().toLower();
    if (!name.contains(QLatin1String("nuke"))) {
        return false;
    }
    return !isAuxiliaryToolName(name);
}

bool isValidNukeDirectory(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        return false;
    }
    const QFileInfoList files = dir.entryInfoList(QStringList() << "Nuke*.exe" << "nuke*.exe", QDir::Files);
    for (const QFileInfo &file : files) {
        if (isValidNukeExecutable(file.absoluteFilePath())) {
            return true;
        }
    }
    return false;
}

QStringList commonNukePaths()
{
    QStringList found;
    const QStringList baseDirs = { "C:/Program Files", "C:/Program Files (x86)", "C:/Program Files/Foundry" };
    for (const QString &baseDir : baseDirs) {
        QDir dir(baseDir);
        if (!dir.exists()) {
            continue;
        }
        const QFileInfoList subdirs = dir.entryInfoList(QStringList() << "*Nuke*", QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &subdir : subdirs) {
            const QString path = subdir.absoluteFilePath();
            if (isValidNukeDirectory(path)) {
                found << path;
            }
        }
    }
    return found;
}

QList<NukeVersion> executablesIn(const QString &dirPath);

#else // macOS

bool isValidNukeExecutable(const QString &executablePath)
{
    QFileInfo info(executablePath);
    if (!info.exists() || !info.isFile() || !info.isExecutable()) {
        return false;
    }
    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("dylib") || suffix == QLatin1String("so") || suffix == QLatin1String("framework")
        || suffix == QLatin1String("a") || suffix == QLatin1String("o")) {
        return false;
    }
    const QString name = info.fileName().toLower();
    if (!name.contains(QLatin1String("nuke"))) {
        return false;
    }
    if (isAuxiliaryToolName(name) || name.contains(QLatin1String("python")) || name.contains(QLatin1String("helper"))) {
        return false;
    }
    return true;
}

bool isValidNukeAppBundle(const QString &bundlePath)
{
    if (!bundlePath.endsWith(QLatin1String(".app"), Qt::CaseInsensitive)) {
        return false;
    }
    QDir macosDir(bundlePath + QStringLiteral("/Contents/MacOS"));
    if (!macosDir.exists()) {
        return false;
    }
    const QFileInfoList files = macosDir.entryInfoList(QDir::Files | QDir::Executable);
    for (const QFileInfo &file : files) {
        if (isValidNukeExecutable(file.absoluteFilePath())) {
            return true;
        }
    }
    return false;
}

QStringList commonNukePaths()
{
    QStringList found;
    QDir dir(QStringLiteral("/Applications"));
    if (!dir.exists()) {
        return found;
    }
    const QFileInfoList entries = dir.entryInfoList(QStringList() << "Nuke*", QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString entryPath = entry.absoluteFilePath();
        if (entryPath.endsWith(QLatin1String(".app"), Qt::CaseInsensitive)) {
            if (isValidNukeAppBundle(entryPath)) {
                found << entryPath;
            }
            continue;
        }
        QDir nukeDir(entryPath);
        const QFileInfoList bundles = nukeDir.entryInfoList(QStringList() << "Nuke*.app", QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &bundle : bundles) {
            if (isValidNukeAppBundle(bundle.absoluteFilePath())) {
                found << bundle.absoluteFilePath();
            }
        }
    }
    return found;
}

QList<NukeVersion> executablesIn(const QString &bundlePath);

#endif

NukeVersion parseNukeExecutable(const QString &executablePath)
{
    NukeVersion version;
    const QFileInfo info(executablePath);
    version.path = executablePath;
    version.name = info.baseName();

    static const QRegularExpression versionRegex(QStringLiteral(R"((\d+\.\d+(?:v\d+)?))"));
    QRegularExpressionMatch match = versionRegex.match(executablePath);
    if (match.hasMatch()) {
        version.version = match.captured(1);
    } else {
        match = versionRegex.match(info.dir().dirName());
        if (match.hasMatch()) {
            version.version = match.captured(1);
        }
    }

    if (!version.version.isEmpty()) {
        version.displayName = QStringLiteral("Nuke %1").arg(version.version);
    } else {
        version.version = QStringLiteral("Unknown");
        version.displayName = QStringLiteral("Nuke (%1)").arg(info.baseName());
    }
    return version;
}

#ifdef Q_OS_WIN
QList<NukeVersion> executablesIn(const QString &dirPath)
{
    QList<NukeVersion> versions;
    QDir dir(dirPath);
    if (!dir.exists()) {
        return versions;
    }
    const QFileInfoList files = dir.entryInfoList(QStringList() << "Nuke*.exe" << "nuke*.exe", QDir::Files);
    for (const QFileInfo &file : files) {
        if (isValidNukeExecutable(file.absoluteFilePath())) {
            versions << parseNukeExecutable(file.absoluteFilePath());
        }
    }
    return versions;
}
#else
QList<NukeVersion> executablesIn(const QString &bundlePath)
{
    QList<NukeVersion> versions;
    QDir macosDir(bundlePath + QStringLiteral("/Contents/MacOS"));
    if (!macosDir.exists()) {
        return versions;
    }
    const QFileInfoList files = macosDir.entryInfoList(QDir::Files | QDir::Executable);
    for (const QFileInfo &file : files) {
        if (isValidNukeExecutable(file.absoluteFilePath())) {
            versions << parseNukeExecutable(file.absoluteFilePath());
        }
    }
    return versions;
}
#endif

/// Corre en el QThread propio de NukeScanner: nada de esto toca la UI directamente, solo emite
/// senales que NukeScanner reenvia (conexion en cola, cruza al hilo del objeto que escucha).
class ScanWorker : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void run()
    {
        emit started();
        const QStringList paths = commonNukePaths();
        QList<NukeVersion> all;
        for (const QString &path : paths) {
            emit progress(path);
            const QList<NukeVersion> found = executablesIn(path);
            for (const NukeVersion &version : found) {
                all << version;
                emit oneFound(version);
            }
        }
        emit finished(all);
    }

signals:
    void started();
    void progress(const QString &path);
    void oneFound(const NukeVersion &version);
    void finished(const QList<NukeVersion> &versions);
};

} // namespace

NukeScanner::NukeScanner(QObject *parent)
    : QObject(parent)
{
}

NukeScanner::~NukeScanner()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

void NukeScanner::startScan()
{
    if (m_scanning) {
        return;
    }
    m_scanning = true;

    m_thread = new QThread(this);
    auto *worker = new ScanWorker();
    worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, worker, &ScanWorker::run);
    connect(worker, &ScanWorker::started, this, &NukeScanner::scanStarted);
    connect(worker, &ScanWorker::progress, this, &NukeScanner::scanProgress);
    connect(worker, &ScanWorker::oneFound, this, &NukeScanner::versionFound);
    connect(worker, &ScanWorker::finished, this, &NukeScanner::onWorkerFinished);
    connect(worker, &ScanWorker::finished, worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_thread->start();
}

void NukeScanner::onWorkerFinished(const QList<NukeVersion> &versions)
{
    m_scanning = false;
    if (m_thread) {
        m_thread->quit();
    }
    m_thread = nullptr;
    emit scanFinished(versions);
}

QList<NukeVersion> NukeScanner::scanSync()
{
    QList<NukeVersion> all;
    const QStringList paths = commonNukePaths();
    for (const QString &path : paths) {
        all << executablesIn(path);
    }
    return all;
}

std::tuple<int, int, int> NukeScanner::versionSortKey(const QString &version)
{
    static const QRegularExpression re(QStringLiteral("^(\\d+)\\.(\\d+)v(\\d+)"));
    const QRegularExpressionMatch m = re.match(version);
    if (!m.hasMatch()) {
        return {0, 0, 0};
    }
    return {m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()};
}

bool NukeScanner::isNewer(const NukeVersion &a, const NukeVersion &b)
{
    return versionSortKey(a.version) > versionSortKey(b.version);
}

NukeVersion NukeScanner::newest(const QList<NukeVersion> &versions)
{
    if (versions.isEmpty()) {
        return NukeVersion();
    }
    const NukeVersion *best = &versions.first();
    for (const NukeVersion &version : versions) {
        if (isNewer(version, *best)) {
            best = &version;
        }
    }
    return *best;
}

#include "NukeScanner.moc"
