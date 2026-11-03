#include "modules/dictation/DictationModels.h"

#include "core/AutomatedRun.h"
#include "core/I18n.h"
#include "platform/DictationProcess.h"
#include "platform/FileSystemOps.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QUuid>

namespace {

constexpr int kTransferTimeoutMs = 30000; // de inactividad: una descarga lenta no se corta
constexpr qint64 kHashChunk = 4 * 1024 * 1024;

QString native(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

QString markerPath(const DictationModels::Piece &piece, const QString &target)
{
    return piece.archiveFolder.isEmpty() ? target + QStringLiteral(".verified") : target + QStringLiteral("/.verified");
}

// Borra lo que ya estaba (una pieza vieja o rota) sin seguir nunca un enlace. En una corrida
// automatizada solo actua dentro de la carpeta de pruebas (FileSystemOps lo controla).
bool removeTree(const QString &path)
{
    using FileSystemOps::Kind;
    using FileSystemOps::Result;
    const Kind kind = FileSystemOps::kind(path);
    const auto done = [](Result result) { return result == Result::Removed || result == Result::Missing; };
    switch (kind) {
    case Kind::Missing:
        return true;
    case Kind::File:
        return done(FileSystemOps::removeFile(FileSystemOps::nativePath(path)));
    case Kind::Link:
        // El enlace mismo, nunca lo que hay del otro lado.
        return done(FileSystemOps::removeDir(FileSystemOps::nativePath(path)))
            || done(FileSystemOps::removeFile(FileSystemOps::nativePath(path)));
    case Kind::Dir:
        break;
    }
    const QStringList children =
        QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &child : children) {
        if (!removeTree(QDir(path).filePath(child))) {
            return false;
        }
    }
    return done(FileSystemOps::removeDir(FileSystemOps::nativePath(path)));
}

// Para el log del tacho: lo que ocupaba la pieza (sin seguir enlaces).
qint64 treeSize(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isDir()) {
        return info.isFile() && !info.isSymLink() ? info.size() : 0;
    }
    qint64 total = 0;
    for (const QFileInfo &child : QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        total += treeSize(child.absoluteFilePath());
    }
    return total;
}

QByteArray fileSha256(const QString &path, qint64 *size)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&in)) {
        return {};
    }
    *size = in.size();
    return hash.result().toHex();
}

// Los archivos de la carpeta del motor, relativos y ordenados (sin el marcador ni enlaces).
QStringList engineFiles(const QString &dir)
{
    QStringList files;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        const QString relative = QDir(dir).relativeFilePath(info.absoluteFilePath());
        if (!info.isSymLink() && relative != QLatin1String(".verified")) files << relative;
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

// "file=<relativo>|<tamano>|<sha256>" del marcador del motor.
struct EngineFile
{
    QString relative;
    qint64 size = 0;
    QByteArray sha256;
};

QList<EngineFile> markerFiles(const QByteArray &marker)
{
    QList<EngineFile> files;
    for (const QByteArray &line : marker.split('\n')) {
        if (!line.startsWith("file=")) continue;
        const QList<QByteArray> parts = line.mid(5).trimmed().split('|');
        if (parts.size() == 3) files.append({QString::fromUtf8(parts.at(0)), parts.at(1).toLongLong(), parts.at(2)});
    }
    return files;
}

} // namespace

bool DictationModels::checkEngineFiles(const QString &engineDir, QString *problem)
{
    QFile marker(QDir(engineDir).filePath(QStringLiteral(".verified")));
    const QList<EngineFile> listed = marker.open(QIODevice::ReadOnly) ? markerFiles(marker.readAll()) : QList<EngineFile>();
    if (listed.isEmpty()) {
        *problem = QStringLiteral(".verified");
        return false;
    }
    QSet<QString> known;
    for (const EngineFile &file : listed) {
        known.insert(file.relative.toLower());
        qint64 size = -1;
        const QByteArray sha = fileSha256(QDir(engineDir).filePath(file.relative), &size);
        if (size != file.size || sha != file.sha256) {
            *problem = file.relative;
            return false;
        }
    }
    // Una DLL que no vino en el paquete la cargaria el motor (busca sus modulos en su carpeta).
    for (const QString &relative : engineFiles(engineDir)) {
        if (relative.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive) && !known.contains(relative.toLower())) {
            *problem = relative;
            return false;
        }
    }
    return true;
}

QList<DictationModels::Piece> DictationModels::defaultPieces()
{
    Piece engine;
    engine.id = kEngine;
    engine.title = QStringLiteral("transcribe.cpp 0.4.1 (CPU + Vulkan)");
    engine.url = QUrl(QStringLiteral("https://github.com/handy-computer/transcribe.cpp/releases/download/v0.4.1/"
                                     "transcribe-native-0.4.1-windows-x86_64-cpu-vulkan.tar.gz"));
    engine.size = 18052295;
    engine.sha256 = QByteArrayLiteral("272eb97795048ccb557c85bb9dd44c9e530e1c928443114c39816a8a5f76269d");
    engine.license = QStringLiteral("MIT");
    engine.relativePath = QStringLiteral("transcribe.cpp/0.4.1-windows-x86_64-cpu-vulkan");
    engine.archiveFolder = QStringLiteral("transcribe-native-windows-x86_64-cpu-vulkan");
    engine.requiredFile = QStringLiteral("transcribe.dll");

    Piece model;
    model.id = kModel;
    model.title = QStringLiteral("Whisper large-v3 turbo Q8_0");
    model.url = QUrl(QStringLiteral("https://huggingface.co/handy-computer/whisper-large-v3-turbo-gguf/resolve/main/"
                                    "whisper-large-v3-turbo-Q8_0.gguf"));
    model.size = 886381728;
    model.sha256 = QByteArrayLiteral("257aa7ec9121c22c167d5e2c49e3e76b4ba0e6e27ebc13074eba0a80f8190220");
    model.license = QStringLiteral("MIT");
    model.relativePath = QStringLiteral("whisper/whisper-large-v3-turbo-Q8_0.gguf");
    return {engine, model};
}

QString DictationModels::defaultRoot()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models"));
}

DictationModels::DictationModels(const QString &root, const QList<Piece> &pieces, QObject *parent)
    : QObject(parent)
    , m_root(QDir::cleanPath(root))
    , m_pieces(pieces)
{
    for (const Piece &piece : m_pieces) {
        m_jobs[piece.id];
    }
}

DictationModels::~DictationModels()
{
    for (auto &entry : m_jobs) {
        Job &job = entry.second;
        stopVerifier(job);
        if (job.reply) {
            job.reply->disconnect(this);
            job.reply->abort();
            job.reply->deleteLater();
        }
        if (job.file) {
            job.file->close();
            QFile::remove(job.file->fileName()); // el .part que dejaba esta descarga
            delete job.file;
        }
        delete job.hash;
        if (job.tar) {
            job.tar->disconnect(this);
            job.tar->kill();
            job.tar->waitForFinished(3000);
        }
        if (!job.extractDir.isEmpty()) {
            removeTree(job.extractDir);
        }
    }
}

const DictationModels::Piece *DictationModels::piece(const QString &id) const
{
    for (const Piece &piece : m_pieces) {
        if (piece.id == id) {
            return &piece;
        }
    }
    return nullptr;
}

QString DictationModels::path(const QString &id) const
{
    const Piece *p = piece(id);
    return p ? QDir(m_root).filePath(p->relativePath) : QString();
}

QString DictationModels::partPath(const Piece &piece) const
{
    return path(piece.id) + (piece.archiveFolder.isEmpty() ? QStringLiteral(".part") : QStringLiteral(".tar.gz.part"));
}

DictationModels::State DictationModels::state(const QString &id) const
{
    const auto it = m_jobs.find(id);
    return it == m_jobs.end() ? State::Missing : it->second.state;
}

int DictationModels::progress(const QString &id) const
{
    const auto it = m_jobs.find(id);
    return it == m_jobs.end() ? 0 : it->second.progress;
}

QString DictationModels::errorText(const QString &id) const
{
    const auto it = m_jobs.find(id);
    return it == m_jobs.end() ? QString() : it->second.error;
}

bool DictationModels::allReady() const
{
    for (const Piece &piece : m_pieces) {
        if (state(piece.id) != State::Ready) {
            return false;
        }
    }
    return true;
}

void DictationModels::setState(const QString &id, State state, int progress, const QString &error)
{
    Job &job = m_jobs[id];
    if (job.state == state && job.progress == progress && job.error == error) {
        return;
    }
    job.state = state;
    job.progress = progress;
    job.error = error;
    emit changed(id);
}

// ---------------------------------------------------------------- Disco y marcador

void DictationModels::refresh()
{
    for (const Piece &piece : m_pieces) {
        const State current = state(piece.id);
        if (current != State::Downloading && current != State::Verifying) {
            readDisk(piece);
        }
    }
}

void DictationModels::readDisk(const Piece &piece)
{
    const QString target = path(piece.id);
    if (!piece.archiveFolder.isEmpty()) {
        const bool ready = QFileInfo::exists(QDir(target).filePath(piece.requiredFile)) && markerOk(piece);
        setState(piece.id, ready ? State::Ready : State::Missing, ready ? 100 : 0);
        return;
    }
    const QFileInfo info(target);
    if (!info.isFile()) {
        setState(piece.id, State::Missing);
    } else if (info.size() != piece.size) {
        setState(piece.id, State::Error, 0, I18n::tr("The file on disk is not the expected one: %1").arg(native(target)));
    } else if (markerOk(piece)) {
        setState(piece.id, State::Ready, 100);
    } else {
        startVerify(piece);
    }
}

bool DictationModels::markerOk(const Piece &piece) const
{
    const QString target = path(piece.id);
    QFile file(markerPath(piece, target));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QHash<QByteArray, QByteArray> values;
    for (const QByteArray &line : file.readAll().split('\n')) {
        const int equals = int(line.indexOf('='));
        if (equals > 0) {
            values.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }
    if (values.value("sha256") != piece.sha256) {
        return false;
    }
    if (!piece.archiveFolder.isEmpty()) {
        // Barato (sin leer los archivos): cada uno esta y con su tamano. El SHA-256 de cada uno lo mira el
        // proceso del motor antes de cargarlo (checkEngineFiles).
        file.seek(0);
        const QList<EngineFile> files = markerFiles(file.readAll());
        for (const EngineFile &listed : files) {
            const QFileInfo info(QDir(target).filePath(listed.relative));
            if (!info.isFile() || info.size() != listed.size) return false;
        }
        return !files.isEmpty();
    }
    // Si el archivo cambio despues de verificarlo (otro tamano u otra fecha), se vuelve a verificar.
    const QFileInfo info(target);
    return values.value("size").toLongLong() == info.size()
        && values.value("modified").toLongLong() == info.lastModified().toMSecsSinceEpoch();
}

void DictationModels::writeMarker(const Piece &piece)
{
    const QString target = path(piece.id);
    QByteArray text = "sha256=" + piece.sha256 + '\n';
    if (piece.archiveFolder.isEmpty()) {
        const QFileInfo info(target);
        text += "size=" + QByteArray::number(info.size()) + '\n';
        text += "modified=" + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '\n';
    } else {
        // Recien desempaquetado del paquete verificado: lo que hay es lo que vino.
        for (const QString &relative : engineFiles(target)) {
            qint64 size = -1;
            const QByteArray sha = fileSha256(QDir(target).filePath(relative), &size);
            text += "file=" + relative.toUtf8() + '|' + QByteArray::number(size) + '|' + sha + '\n';
        }
    }
    QFile file(markerPath(piece, target));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text) != text.size()) {
        // Sin marcador la pieza sirve igual: se vuelve a verificar en el proximo arranque.
        qWarning() << "[Dictado] No se pudo escribir el marcador" << native(file.fileName());
    }
}

void DictationModels::startVerify(const Piece &piece)
{
    Job &job = m_jobs[piece.id];
    stopVerifier(job);
    setState(piece.id, State::Verifying, 0);
    auto stop = std::make_shared<std::atomic<bool>>(false);
    job.stopVerify = stop;
    const QString id = piece.id;
    const QString file = path(piece.id);
    const qint64 size = piece.size;
    const QByteArray expected = piece.sha256;
    job.verifier = std::thread([this, id, file, size, expected, stop]() {
        QFile in(file);
        const bool opened = in.open(QIODevice::ReadOnly);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 done = 0;
        int lastPercent = -1;
        while (opened && !stop->load()) {
            const QByteArray chunk = in.read(kHashChunk);
            if (chunk.isEmpty()) {
                break;
            }
            hash.addData(chunk);
            done += chunk.size();
            const int percent = size > 0 ? int(qMin<qint64>(99, done * 100 / size)) : 0;
            if (percent != lastPercent) {
                lastPercent = percent;
                // El objeto vive hasta que este hilo termina: el destructor lo espera.
                QMetaObject::invokeMethod(this, [this, id, percent]() {
                    if (state(id) == State::Verifying) {
                        setState(id, State::Verifying, percent);
                    }
                }, Qt::QueuedConnection);
            }
        }
        const bool stopped = stop->load();
        const bool match = opened && done == size && hash.result().toHex() == expected;
        QMetaObject::invokeMethod(this, [this, id, stopped, match]() { onVerified(id, stopped, match); }, Qt::QueuedConnection);
    });
}

void DictationModels::stopVerifier(Job &job)
{
    if (job.stopVerify) {
        job.stopVerify->store(true);
    }
    if (job.verifier.joinable()) {
        job.verifier.join();
    }
    job.stopVerify.reset();
}

void DictationModels::onVerified(const QString &id, bool stopped, bool match)
{
    Job &job = m_jobs[id];
    if (job.verifier.joinable()) {
        job.verifier.join();
    }
    job.stopVerify.reset();
    const Piece *p = piece(id);
    if (stopped || !p || job.state != State::Verifying) {
        return;
    }
    if (match) {
        writeMarker(*p);
        setState(id, State::Ready, 100);
    } else {
        setState(id, State::Error, 0, I18n::tr("The file on disk is damaged (its checksum does not match): %1").arg(native(path(id))));
    }
}

// ---------------------------------------------------------------- Descarga

void DictationModels::downloadMissing()
{
    for (const Piece &piece : m_pieces) {
        download(piece.id);
    }
}

void DictationModels::download(const QString &id)
{
    const Piece *p = piece(id);
    if (!p) {
        return;
    }
    Job &job = m_jobs[id];
    if (job.state == State::Downloading || job.state == State::Verifying || job.state == State::Ready) {
        return;
    }
    // Segunda guarda, ademas de la de quien llama: una corrida automatizada no abre la red.
    if (AutomatedRun::active() && !p->url.isLocalFile()) {
        setState(id, State::Error, 0, I18n::tr("Downloads are off during automated tests."));
        return;
    }
    const QString part = partPath(*p);
    QDir().mkpath(QFileInfo(part).absolutePath());
    job.file = new QFile(part);
    if (!job.file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString detail = job.file->errorString();
        delete job.file;
        job.file = nullptr;
        setState(id, State::Error, 0, I18n::tr("Could not save the download: %1").arg(detail));
        return;
    }
    job.hash = new QCryptographicHash(QCryptographicHash::Sha256);
    job.received = 0;
    job.cancelled = false;
    job.failure.clear();
    // Recien aca: con la herramienta sin bajar nada no hay ni red ni objetos de red.
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }
    QNetworkRequest request(p->url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGA_MightyTools/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(kTransferTimeoutMs);
    job.reply = m_network->get(request);
    connect(job.reply, &QNetworkReply::readyRead, this, [this, id]() { onData(id); });
    connect(job.reply, &QNetworkReply::finished, this, [this, id]() { onDownloaded(id); });
    qInfo() << "[Dictado] Bajando" << p->title << "a" << native(part);
    setState(id, State::Downloading, 0);
}

void DictationModels::onData(const QString &id)
{
    Job &job = m_jobs[id];
    const Piece *p = piece(id);
    if (!job.reply || !job.file || !p || !job.failure.isEmpty()) {
        return;
    }
    const auto fail = [this, id](const QString &why) {
        m_jobs[id].failure = why;
        // Diferido: abort() emite finished() en el acto y anidaria onDownloaded aca adentro.
        QTimer::singleShot(0, this, [this, id]() {
            if (m_jobs[id].reply) {
                m_jobs[id].reply->abort();
            }
        });
    };
    const QString scheme = p->url.scheme();
    if (job.received == 0 && (scheme == QLatin1String("https") || scheme == QLatin1String("http"))) {
        const int status = job.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            fail(I18n::tr("The server answered %1.").arg(status));
            return;
        }
    }
    const QByteArray chunk = job.reply->readAll();
    if (chunk.isEmpty()) {
        return;
    }
    if (job.file->write(chunk) != chunk.size()) {
        fail(I18n::tr("Could not save the download: %1").arg(job.file->errorString()));
        return;
    }
    job.hash->addData(chunk);
    job.received += chunk.size();
    if (job.received > p->size) {
        fail(I18n::tr("The download does not have the expected size."));
        return;
    }
    setState(id, State::Downloading, int(job.received * 100 / qMax<qint64>(1, p->size)));
}

void DictationModels::onDownloaded(const QString &id)
{
    Job &job = m_jobs[id];
    const Piece *p = piece(id);
    QNetworkReply *reply = job.reply;
    if (!reply || !p) {
        return;
    }
    if (job.failure.isEmpty() && !job.cancelled) {
        onData(id); // lo que quedo en el buffer al terminar
    }
    job.reply = nullptr;
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    reply->deleteLater();
    job.file->close();
    delete job.file;
    job.file = nullptr;
    const QByteArray digest = job.hash->result().toHex();
    delete job.hash;
    job.hash = nullptr;

    const QString part = partPath(*p);
    QString why;
    if (job.cancelled) {
        QFile::remove(part);
        setState(id, State::Missing);
        return;
    }
    if (!job.failure.isEmpty()) {
        why = job.failure;
    } else if (error != QNetworkReply::NoError) {
        why = I18n::tr("The download failed: %1").arg(errorString);
    } else if (job.received != p->size) {
        why = I18n::tr("The download does not have the expected size.");
    } else if (digest != p->sha256) {
        why = I18n::tr("The download is damaged (its checksum does not match).");
    }
    if (!why.isEmpty()) {
        qWarning() << "[Dictado] Descarga de" << p->title << "fallida:" << why;
        QFile::remove(part);
        setState(id, State::Error, 0, why);
        return;
    }
    if (p->archiveFolder.isEmpty()) {
        install(*p);
    } else {
        extract(*p);
    }
}

void DictationModels::install(const Piece &piece)
{
    const QString part = partPath(piece);
    const QString target = path(piece.id);
    if (!removeTree(target) || !removeTree(markerPath(piece, target))) {
        QFile::remove(part);
        setState(piece.id, State::Error, 0, I18n::tr("Could not replace %1.").arg(native(target)));
        return;
    }
    if (!QFile::rename(part, target)) {
        QFile::remove(part);
        setState(piece.id, State::Error, 0, I18n::tr("Could not save the download: %1").arg(native(target)));
        return;
    }
    writeMarker(piece);
    qInfo() << "[Dictado]" << piece.title << "listo en" << native(target);
    setState(piece.id, State::Ready, 100);
}

void DictationModels::extract(const Piece &piece)
{
    Job &job = m_jobs[piece.id];
    const QString part = partPath(piece);
    const QString tar = DictationProcess::systemTar();
    if (tar.isEmpty()) {
        QFile::remove(part);
        setState(piece.id, State::Error, 0, I18n::tr("This Windows does not have tar.exe, which is needed to unpack the engine."));
        return;
    }
    // Carpeta temporal al lado del destino (mismo disco: despues es un renombre).
    const QString parent = QFileInfo(path(piece.id)).absolutePath();
    const QString name = QStringLiteral(".extract-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
    if (!QDir(parent).mkdir(name)) {
        QFile::remove(part);
        setState(piece.id, State::Error, 0, I18n::tr("Could not unpack the engine."));
        return;
    }
    job.extractDir = QDir(parent).filePath(name);
    setState(piece.id, State::Verifying, 100);
    job.tar = new QProcess(this);
    DictationProcess::hideConsole(job.tar);
    job.tar->setProgram(tar);
    // Rutas relativas a la carpeta de trabajo: tar.exe no lee bien argumentos fuera de la pagina de codigos.
    job.tar->setWorkingDirectory(parent);
    job.tar->setArguments({QStringLiteral("-xzf"), QFileInfo(part).fileName(), QStringLiteral("-C"), name});
    const QString id = piece.id;
    connect(job.tar, &QProcess::finished, this, [this, id](int code, QProcess::ExitStatus status) {
        onExtracted(id, status == QProcess::NormalExit && code == 0);
    });
    connect(job.tar, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            onExtracted(id, false);
        }
    });
    job.tar->start();
}

void DictationModels::onExtracted(const QString &id, bool ok)
{
    Job &job = m_jobs[id];
    const Piece *p = piece(id);
    if (!job.tar || !p) {
        return;
    }
    job.tar->disconnect(this);
    job.tar->deleteLater();
    job.tar = nullptr;
    QFile::remove(partPath(*p));
    const QString extractDir = job.extractDir;
    job.extractDir.clear();
    const QString unpacked = QDir(extractDir).filePath(p->archiveFolder);
    const QString target = path(id);
    QString why;
    if (job.cancelled) {
        removeTree(extractDir);
        setState(id, State::Missing);
        return;
    }
    if (!ok || !QFileInfo::exists(QDir(unpacked).filePath(p->requiredFile))) {
        why = I18n::tr("Could not unpack the engine.");
    } else if (!removeTree(target)) {
        why = I18n::tr("Could not replace %1.").arg(native(target));
    } else if (!QDir().rename(unpacked, target)) {
        why = I18n::tr("Could not unpack the engine.");
    }
    removeTree(extractDir);
    if (!why.isEmpty()) {
        qWarning() << "[Dictado] Desempaquetar" << p->title << "fallo:" << why;
        setState(id, State::Error, 0, why);
        return;
    }
    writeMarker(*p);
    qInfo() << "[Dictado]" << p->title << "listo en" << native(target);
    setState(id, State::Ready, 100);
}

void DictationModels::cancel(const QString &id)
{
    Job &job = m_jobs[id];
    if (job.reply) {
        job.cancelled = true;
        job.reply->abort(); // finished() en el acto: onDownloaded limpia
    } else if (job.tar) {
        job.cancelled = true;
        job.tar->kill(); // finished(): onExtracted limpia
    }
}

bool DictationModels::remove(const QString &id, QString *error)
{
    const Piece *p = piece(id);
    if (!p) {
        return false;
    }
    Job &job = m_jobs[id];
    if (job.reply || job.tar) {
        cancel(id);
        return true;
    }
    stopVerifier(job);
    job.state = State::Missing; // para que readDisk lo vuelva a mirar
    const QString target = path(id);
    const qint64 bytes = treeSize(target) + (p->archiveFolder.isEmpty() ? treeSize(markerPath(*p, target)) : 0);
    // Primero la pieza: si esta en uso y no se borra, el marcador queda y sigue "lista" sin re-verificar.
    const bool ok = removeTree(target) && removeTree(markerPath(*p, target));
    readDisk(*p);
    if (!ok) {
        const QString text = I18n::tr("Could not delete %1.").arg(native(target));
        qWarning() << "[Dictado]" << text;
        if (error) {
            *error = text;
        }
    } else {
        qInfo().noquote() << "[Dictado] Borrado del disco:" << p->title << native(target)
                          << QStringLiteral("(%1 bytes)").arg(bytes);
    }
    return ok;
}

void DictationModels::markDamaged(const QString &id, const QString &why)
{
    if (piece(id)) {
        setState(id, State::Error, 0, why);
    }
}
