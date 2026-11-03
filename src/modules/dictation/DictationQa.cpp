#include "modules/dictation/DictationQa.h"

#include "core/AutomatedRun.h"
#include "modules/dictation/DictationEngine.h"
#include "modules/dictation/DictationModels.h"
#include "modules/dictation/DictationProtocol.h"
#include "modules/dictation/DictationText.h"
#include "platform/DictationProcess.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

#include <cstdio>

using DictationProtocol::Message;
using DictationProtocol::Type;

namespace {

using Check = std::function<void(bool ok, const QString &what)>;

// Lo que usa la prueba de Lega (2026-11-03): con esto "Nuke Studio" salio 5 de 5.
const QStringList kVocabulary = {QStringLiteral("Nuke Studio"), QStringLiteral("Nuke"), QStringLiteral("NukeX"),
                                 QStringLiteral("Hiero"), QStringLiteral("XYplorer"), QStringLiteral("comp"),
                                 QStringLiteral("roto")};
const QString kPrompt = QStringLiteral("Che, ¿vos ya revisaste el comp en Nuke Studio? Fijate si el roto anda bien.");

bool waitFor(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return done();
}

void out(const QString &text)
{
    std::printf("%s\n", text.toUtf8().constData());
    std::fflush(stdout);
}

// ---------------------------------------------------------------- Protocolo

void testProtocol(const Check &check)
{
    Message begin;
    begin.type = Type::Begin;
    begin.session = 41;
    begin.language = QStringLiteral("es");
    begin.vocabulary = kVocabulary;
    begin.prompt = kPrompt;
    Message audio;
    audio.type = Type::Audio;
    audio.samples = {0.0f, 0.5f, -1.0f, 0.25f};
    Message end;
    end.type = Type::End;
    end.session = 41;
    Message cancel;
    cancel.type = Type::Cancel;
    cancel.session = 42;
    Message quit;
    quit.type = Type::Quit;
    Message ready;
    ready.type = Type::Ready;
    ready.text = QStringLiteral("NVIDIA GeForce RTX 4090 (vulkan)");
    ready.passMs = 1234;
    Message partial;
    partial.type = Type::Partial;
    partial.session = 41;
    partial.audioMs = 1500;
    partial.passMs = 96;
    partial.text = QStringLiteral("¿Anda Nuke Studio? Sí, señor…");
    Message done = partial;
    done.type = Type::Final;
    Message error;
    error.type = Type::Error;
    error.text = QStringLiteral("transcribe_open: backend error");

    const QList<Message> all = {begin, audio, end, cancel, quit, ready, partial, done, error};
    QByteArray stream;
    for (const Message &m : all) {
        stream += DictationProtocol::encode(m);
    }
    QByteArray buffer = stream;
    const QList<Message> back = DictationProtocol::decode(&buffer);
    bool same = back.size() == all.size() && buffer.isEmpty();
    for (int i = 0; same && i < all.size(); ++i) {
        const Message &a = all.at(i);
        const Message &b = back.at(i);
        same = a.type == b.type && a.session == b.session && a.language == b.language && a.vocabulary == b.vocabulary
            && a.prompt == b.prompt && a.samples == b.samples && a.text == b.text && a.audioMs == b.audioMs
            && a.passMs == b.passMs;
    }
    check(same, QStringLiteral("protocolo: los 9 tipos de mensaje vuelven identicos"));

    const QByteArray beginFrame = DictationProtocol::encode(begin);
    check(beginFrame.contains(QByteArray("\xC2\xBF" "vos")) && back.value(0).prompt.startsWith(QStringLiteral("Che, ¿vos")),
          QStringLiteral("protocolo: la frase de contexto viaja en UTF-8 (\"¿\" = C2 BF)"));

    // Byte por byte: nada sale hasta que el mensaje esta completo.
    QByteArray drip;
    int seen = 0;
    bool early = false;
    for (int i = 0; i < stream.size(); ++i) {
        drip.append(stream.at(i));
        const int got = int(DictationProtocol::decode(&drip).size());
        seen += got;
        early = early || (got > 0 && seen > all.size());
    }
    check(seen == all.size() && !early && drip.isEmpty(), QStringLiteral("protocolo: llegando de a un byte salen los 9, completos"));

    // Basura adelante (algo que la DLL escribio en la salida estandar) y un largo imposible.
    QByteArray dirty = QByteArrayLiteral("ggml_vulkan: Found 1 Vulkan devices:\r\nDCT");
    QByteArray huge = QByteArrayLiteral("DCT1");
    char size[4];
    qToLittleEndian<quint32>(DictationProtocol::kMaxBody + 1, size);
    huge.append(size, 4);
    huge.append(char(Type::Partial));
    dirty += huge + DictationProtocol::encode(partial) + QByteArrayLiteral("DC");
    const QList<Message> resynced = DictationProtocol::decode(&dirty);
    check(resynced.size() == 1 && resynced.value(0).text == partial.text && dirty == QByteArrayLiteral("DC"),
          QStringLiteral("protocolo: saltea basura y un largo imposible, y guarda un \"DCT1\" cortado"));

    // Cuerpo mal armado (audio con bytes sueltos): se descarta ese mensaje y sigue el resto.
    QByteArray broken = QByteArrayLiteral("DCT1");
    qToLittleEndian<quint32>(3, size);
    broken.append(size, 4);
    broken.append(char(Type::Audio));
    broken.append("abc", 3);
    broken += DictationProtocol::encode(end);
    const QList<Message> afterBroken = DictationProtocol::decode(&broken);
    check(afterBroken.size() == 1 && afterBroken.value(0).type == Type::End && afterBroken.value(0).session == 41,
          QStringLiteral("protocolo: un mensaje mal armado se descarta sin perder el siguiente"));

    check(DictationProtocol::cleanText(QByteArray("ma\xC3\xB1" "ana \xC3")) == QStringLiteral("mañana "),
          QStringLiteral("protocolo: un caracter UTF-8 cortado del motor se saca (texto siempre valido)"));
}

// ---------------------------------------------------------------- Texto

void testText(const Check &check)
{
    using DictationText::stabilize;
    check(DictationText::normalizedWord(QStringLiteral("¿Qué?")) == QStringLiteral("qué"),
          QStringLiteral("texto: \"¿Qué?\" se compara como \"qué\""));

    DictationText::Split s = stabilize(QString(), QStringLiteral("¿Qué?"), QStringLiteral("París aquí."));
    check(s.confirmed.isEmpty() && s.provisional == QStringLiteral("París aquí"),
          QStringLiteral("texto: dos pasadas distintas no confirman nada (y el punto final no se muestra)"));

    s = stabilize(QString(), QStringLiteral("Parece que anda bastante bien."), QStringLiteral("Parece que anda bastante bien..."));
    check(s.confirmed == QStringLiteral("Parece que anda bastante") && s.provisional == QStringLiteral("bien"),
          QStringLiteral("texto: lo repetido se confirma salvo la ultima palabra, sin los \"...\" (%1 | %2)")
              .arg(s.confirmed, s.provisional));

    s = stabilize(QStringLiteral("Parece que anda bastante bien. Yo"), QStringLiteral("Parece que anda bastante bien. Yo probaría..."),
                  QStringLiteral("parece que anda bastante bien yo probaría meterle"));
    check(s.confirmed == QStringLiteral("Parece que anda bastante bien. Yo probaría") && s.provisional == QStringLiteral("meterle"),
          QStringLiteral("texto: lo confirmado no cambia de mayuscula ni de punto aunque la pasada si (%1 | %2)")
              .arg(s.confirmed, s.provisional));

    s = stabilize(QStringLiteral("uno dos tres"), QStringLiteral("uno dos tres cuatro"), QStringLiteral("uno dos"));
    check(s.confirmed == QStringLiteral("uno dos tres") && s.provisional.isEmpty(),
          QStringLiteral("texto: una pasada mas corta no retira palabras confirmadas"));

    // La secuencia real de la prueba (resultados\01, primera grabacion): lo confirmado solo crece.
    const QStringList passes = {
        QStringLiteral("¿Qué?"), QStringLiteral("París aquí."), QStringLiteral("Parece que anda bastante"),
        QStringLiteral("Parece que anda bastante bien."), QStringLiteral("Parece que anda bastante bien..."),
        QStringLiteral("Parece que anda bastante bien..."), QStringLiteral("parece que anda bastante bien yo"),
        QStringLiteral("parece que anda bastante bien yo probaría"), QStringLiteral("Parece que anda bastante bien. Yo probaría..."),
        QStringLiteral("parece que anda bastante bien yo probaría meterle"),
        QStringLiteral("Parece que anda bastante bien. Yo probaría meterle un poquito caliente"),
        QStringLiteral("parece que anda bastante bien yo probaría meterle un poquito más de la burla"),
        QStringLiteral("parece que anda bastante bien yo probaría meterle un poquito más de laburo a la coma"),
        QStringLiteral("Parece que anda bastante bien. Yo probaría meterle un poquito más de laburo a la compo."),
    };
    QString confirmed;
    QString previous;
    bool grows = true;
    for (const QString &pass : passes) {
        const DictationText::Split next = stabilize(confirmed, previous, pass);
        grows = grows && next.confirmed.startsWith(confirmed);
        confirmed = next.confirmed;
        previous = pass;
    }
    check(grows && confirmed == QStringLiteral("Parece que anda bastante bien yo probaría meterle un poquito más de laburo a la"),
          QStringLiteral("texto: secuencia real, lo confirmado solo crece y \"la burla\" nunca se confirma (%1)").arg(confirmed));

    using DictationText::echoesContext;
    check(echoesContext(QStringLiteral("Glossary"), kVocabulary, QString())
              && echoesContext(QStringLiteral("Glossary, Nuke, Nuke, Nuke, Nuke,"), kVocabulary, QString())
              && echoesContext(QStringLiteral("Fijate si el roto anda bien"), {}, kPrompt) && echoesContext(QString(), {}, {}),
          QStringLiteral("texto: una pasada en vivo que solo repite el contexto se reconoce (resultados 02 y de esta prueba)"));
    check(!echoesContext(QStringLiteral("parece que anda bastante bien"), kVocabulary, QString())
              && !echoesContext(QStringLiteral("Fijate que anda bastante"), kVocabulary, kPrompt),
          QStringLiteral("texto: una pasada con una sola palabra fuera del contexto no se descarta"));

    using DictationText::applyReplacements;
    const DictationText::Replacements jairo = {{QStringLiteral("Jairo"), QStringLiteral("Hiero")}};
    check(applyReplacements(QStringLiteral("Abrí jairo y JAIRO."), jairo) == QStringLiteral("Abrí Hiero y Hiero."),
          QStringLiteral("reemplazos: palabra completa, sin distinguir mayusculas"));
    check(applyReplacements(QStringLiteral("Jairos, ajairo, ñjairo y jairo2"), jairo) == QStringLiteral("Jairos, ajairo, ñjairo y jairo2"),
          QStringLiteral("reemplazos: no toca palabras que solo la contienen (tambien con eñe y numeros)"));
    check(applyReplacements(QStringLiteral("en nook   studio hoy"), {{QStringLiteral("Nook Studio"), QStringLiteral("Nuke Studio")}})
              == QStringLiteral("en Nuke Studio hoy"),
          QStringLiteral("reemplazos: frases de varias palabras"));
    check(applyReplacements(QStringLiteral("x"), {{QStringLiteral("x"), QStringLiteral("\\1 $1 \\")}, {QString(), QStringLiteral("no")}})
              == QStringLiteral("\\1 $1 \\"),
          QStringLiteral("reemplazos: el reemplazo va tal cual y un origen vacio no hace nada"));
}

// ---------------------------------------------------------------- Descarga y verificacion

QByteArray sha256Hex(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

bool writeBytes(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

bool settled(const DictationModels &models, const QString &id)
{
    const DictationModels::State s = models.state(id);
    return s != DictationModels::State::Downloading && s != DictationModels::State::Verifying;
}

void testModels(const Check &check)
{
    using State = DictationModels::State;
    QTemporaryDir sandboxDir(QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_dictation_XXXXXX")));
    QTemporaryDir outsideDir(QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_dictation_canary_XXXXXX")));
    if (!sandboxDir.isValid() || !outsideDir.isValid()) {
        check(false, QStringLiteral("descargas: no se pudieron crear las carpetas de prueba"));
        return;
    }
    const QString sandbox = QFileInfo(sandboxDir.path()).canonicalFilePath();
    const QString previousSandbox = AutomatedRun::sandbox();

    // Archivos de origen (lo que "baja" por file://).
    QByteArray payload;
    for (int i = 0; i < 300000; ++i) {
        payload.append(char('a' + i % 23));
    }
    const QString source = QDir(sandbox).filePath(QStringLiteral("source/model.bin"));
    writeBytes(source, payload);

    DictationModels::Piece file;
    file.id = QStringLiteral("file");
    file.title = QStringLiteral("modelo de prueba");
    file.url = QUrl::fromLocalFile(source);
    file.size = payload.size();
    file.sha256 = sha256Hex(payload);
    file.relativePath = QStringLiteral("whisper/test.gguf");

    // Fuera de la carpeta de pruebas, en una corrida automatizada, borrar no actua.
    const QString canaryRoot = QFileInfo(outsideDir.path()).canonicalFilePath();
    writeBytes(QDir(canaryRoot).filePath(file.relativePath), payload);
    {
        DictationModels outside(canaryRoot, {file});
        const bool removed = outside.remove(file.id);
        check(AutomatedRun::active() && !removed && QFileInfo::exists(QDir(canaryRoot).filePath(file.relativePath)),
              QStringLiteral("descargas: en una corrida automatizada no se borra nada fuera de la carpeta de pruebas"));
    }

    AutomatedRun::setSandbox(sandbox);
    const QString root = QDir(sandbox).filePath(QStringLiteral("models"));
    const QString target = QDir(root).filePath(file.relativePath);
    {
        DictationModels models(root, {file});
        models.refresh();
        check(models.state(file.id) == State::Missing, QStringLiteral("descargas: sin archivo, la pieza falta"));
        QList<State> seen;
        QObject::connect(&models, &DictationModels::changed, &models, [&]() { seen << models.state(file.id); });
        models.download(file.id);
        const bool done = waitFor([&]() { return settled(models, file.id); }, 15000);
        check(done && models.state(file.id) == State::Ready && seen.contains(State::Downloading) && models.progress(file.id) == 100,
              QStringLiteral("descargas: falta -> bajando -> lista (%1)").arg(models.errorText(file.id)));
        check(QFileInfo(target).size() == payload.size() && QFileInfo::exists(target + QStringLiteral(".verified"))
                  && !QFileInfo::exists(target + QStringLiteral(".part")),
              QStringLiteral("descargas: queda el archivo con su marcador y sin el .part"));
    }
    {
        DictationModels models(root, {file});
        models.refresh();
        check(models.state(file.id) == State::Ready,
              QStringLiteral("descargas: con el marcador queda lista al instante, sin volver a leer el archivo"));
        QFile touched(target);
        touched.open(QIODevice::ReadWrite);
        touched.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime);
        touched.close();
        models.refresh();
        const bool reverified = models.state(file.id) == State::Verifying;
        waitFor([&]() { return settled(models, file.id); }, 15000);
        check(reverified && models.state(file.id) == State::Ready,
              QStringLiteral("descargas: si el archivo cambio de fecha se vuelve a verificar"));
    }
    {
        // Mismo tamano, otro contenido y sin marcador: danado.
        QByteArray other = payload;
        other[100] = 'Z';
        writeBytes(target, other);
        QFile::remove(target + QStringLiteral(".verified"));
        DictationModels models(root, {file});
        models.refresh();
        waitFor([&]() { return settled(models, file.id); }, 15000);
        check(models.state(file.id) == State::Error && QFileInfo::exists(target),
              QStringLiteral("descargas: un archivo danado queda en error y no se borra solo"));
        QString why;
        check(models.remove(file.id, &why) && !QFileInfo::exists(target) && models.state(file.id) == State::Missing,
              QStringLiteral("descargas: borrar la pieza (dentro de la carpeta de pruebas) %1").arg(why));
    }
    {
        DictationModels::Piece wrongHash = file;
        wrongHash.sha256 = sha256Hex("otra cosa");
        DictationModels::Piece wrongSize = file;
        wrongSize.id = QStringLiteral("size");
        wrongSize.relativePath = QStringLiteral("whisper/size.gguf");
        wrongSize.size = payload.size() + 1;
        DictationModels models(root, {wrongHash, wrongSize});
        models.downloadMissing();
        waitFor([&]() { return settled(models, wrongHash.id) && settled(models, wrongSize.id); }, 15000);
        const QString sizeTarget = QDir(root).filePath(wrongSize.relativePath);
        check(models.state(wrongHash.id) == State::Error && !QFileInfo::exists(target) && !QFileInfo::exists(target + QStringLiteral(".part")),
              QStringLiteral("descargas: SHA-256 distinto -> error, sin archivo y sin .part (%1)").arg(models.errorText(wrongHash.id)));
        check(models.state(wrongSize.id) == State::Error && !QFileInfo::exists(sizeTarget) && !QFileInfo::exists(sizeTarget + QStringLiteral(".part")),
              QStringLiteral("descargas: tamano distinto -> error, sin archivo y sin .part (%1)").arg(models.errorText(wrongSize.id)));
    }
    {
        // La red: en una corrida automatizada ni se crea.
        DictationModels::Piece remote = file;
        remote.url = QUrl(QStringLiteral("https://example.invalid/model.gguf"));
        DictationModels models(root, {remote});
        models.download(remote.id);
        check(models.state(remote.id) == State::Error && models.findChildren<QNetworkAccessManager *>().isEmpty()
                  && !QFileInfo::exists(target + QStringLiteral(".part")),
              QStringLiteral("descargas: en una corrida automatizada una URL de red no se baja y no se crea la red"));
    }

    // El motor: un .tar.gz con su carpeta adentro, armado con el tar.exe de Windows.
    const QString tar = DictationProcess::systemTar();
    if (tar.isEmpty()) {
        check(false, QStringLiteral("descargas: falta %SystemRoot%\\System32\\tar.exe"));
    } else {
        const QString work = QDir(sandbox).filePath(QStringLiteral("archive"));
        writeBytes(QDir(work).filePath(QStringLiteral("inner/transcribe.dll")), QByteArrayLiteral("MZ prueba"));
        writeBytes(QDir(work).filePath(QStringLiteral("inner/licenses/LICENSE")), QByteArrayLiteral("MIT"));
        QProcess pack;
        DictationProcess::hideConsole(&pack);
        pack.setWorkingDirectory(work);
        pack.start(tar, {QStringLiteral("-czf"), QStringLiteral("engine.tar.gz"), QStringLiteral("inner")});
        const bool packed = pack.waitForFinished(15000) && pack.exitCode() == 0;
        QFile archiveFile(QDir(work).filePath(QStringLiteral("engine.tar.gz")));
        const QByteArray archive = archiveFile.open(QIODevice::ReadOnly) ? archiveFile.readAll() : QByteArray();
        archiveFile.close();
        DictationModels::Piece engine;
        engine.id = QStringLiteral("engine");
        engine.title = QStringLiteral("motor de prueba");
        engine.url = QUrl::fromLocalFile(archiveFile.fileName());
        engine.size = archive.size();
        engine.sha256 = sha256Hex(archive);
        engine.relativePath = QStringLiteral("transcribe.cpp/test-engine");
        engine.archiveFolder = QStringLiteral("inner");
        engine.requiredFile = QStringLiteral("transcribe.dll");
        const QString engineDir = QDir(root).filePath(engine.relativePath);
        // Una carpeta vieja sin marcador: la instalacion la reemplaza.
        writeBytes(QDir(engineDir).filePath(QStringLiteral("viejo.txt")), QByteArrayLiteral("viejo"));
        DictationModels models(root, {engine});
        models.refresh();
        const bool missingFirst = models.state(engine.id) == State::Missing;
        models.download(engine.id);
        waitFor([&]() { return settled(models, engine.id); }, 20000);
        const QStringList leftovers = QDir(QFileInfo(engineDir).absolutePath())
                                          .entryList({QStringLiteral(".extract-*"), QStringLiteral("*.part")},
                                                     QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        check(packed && missingFirst && models.state(engine.id) == State::Ready
                  && QFileInfo::exists(QDir(engineDir).filePath(QStringLiteral("transcribe.dll")))
                  && QFileInfo::exists(QDir(engineDir).filePath(QStringLiteral("licenses/LICENSE")))
                  && QFileInfo::exists(QDir(engineDir).filePath(QStringLiteral(".verified")))
                  && !QFileInfo::exists(QDir(engineDir).filePath(QStringLiteral("viejo.txt"))) && leftovers.isEmpty(),
              QStringLiteral("descargas: el .tar.gz se desempaqueta con tar.exe, reemplaza la carpeta vieja y no deja temporales (%1)")
                  .arg(models.errorText(engine.id)));
        DictationModels again(root, {engine});
        again.refresh();
        check(again.state(engine.id) == State::Ready, QStringLiteral("descargas: el motor instalado queda listo en el proximo arranque"));
        check(again.remove(engine.id) && !QFileInfo::exists(engineDir) && again.state(engine.id) == State::Missing,
              QStringLiteral("descargas: borrar el motor saca la carpeta entera"));
    }

    AutomatedRun::setSandbox(previousSandbox);
}

// ---------------------------------------------------------------- Motor sin proceso

void testEngine(const Check &check)
{
    DictationEngine engine;
    QStringList failures;
    QObject::connect(&engine, &DictationEngine::failed, &engine, [&](const QString &error) { failures << error; });
    engine.begin(QStringLiteral("es"), {}, {});
    engine.start(QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_no_such_engine")),
                 QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_no_such_model.gguf")));
    check(failures.size() == 2 && !engine.isRunning() && !engine.busy(),
          QStringLiteral("motor: sin start() o sin el motor en disco avisa failed() y no lanza ningun proceso"));
}

// ---------------------------------------------------------------- Simulaciones

// WAV PCM16 mono 16 kHz, recorriendo los chunks (no asume una cabecera de 44 bytes).
QVector<float> loadWav(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    const QByteArray data = file.readAll();
    if (data.size() < 12 || !data.startsWith("RIFF") || data.mid(8, 4) != "WAVE") {
        *error = QStringLiteral("no es un WAV");
        return {};
    }
    bool formatOk = false;
    for (qsizetype pos = 12; pos + 8 <= data.size();) {
        const QByteArray id = data.mid(pos, 4);
        const quint32 length = qFromLittleEndian<quint32>(data.constData() + pos + 4);
        if (id == "fmt " && length >= 16 && pos + 8 + 16 <= data.size()) {
            const char *f = data.constData() + pos + 8;
            formatOk = qFromLittleEndian<quint16>(f) == 1 && qFromLittleEndian<quint16>(f + 2) == 1
                && qFromLittleEndian<quint32>(f + 4) == 16000 && qFromLittleEndian<quint16>(f + 14) == 16;
        } else if (id == "data" && formatOk) {
            const qsizetype bytes = qMin<qsizetype>(length, data.size() - pos - 8);
            QVector<float> samples(bytes / 2);
            for (qsizetype i = 0; i < samples.size(); ++i) {
                samples[i] = float(qFromLittleEndian<qint16>(data.constData() + pos + 8 + i * 2)) / 32768.0f;
            }
            return samples;
        }
        pos += 8 + qsizetype(length) + (length & 1);
    }
    *error = QStringLiteral("se espera PCM16 mono 16 kHz");
    return {};
}

int simulateTranscribe(const QStringList &args)
{
    QStringList wavs;
    QString root;
    bool concat = false;
    bool context = true;
    for (const QString &arg : args) {
        if (arg == QLatin1String("--concat")) {
            concat = true;
        } else if (arg == QLatin1String("--no-context")) {
            context = false;
        } else if (arg.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive)) {
            wavs << arg;
        } else if (root.isEmpty()) {
            root = arg;
        }
    }
    if (wavs.isEmpty()) {
        std::fprintf(stderr, "uso: dictation:transcribe <a.wav> [<b.wav>...] [<raiz de modelos>] [--concat] [--no-context]\n");
        return 2;
    }
    const DictationModels models(root.isEmpty() ? DictationModels::defaultRoot() : root);
    const QString engineDir = models.engineDir();
    const QString modelPath = models.modelPath();
    out(QStringLiteral("motor:  %1\nmodelo: %2").arg(QDir::toNativeSeparators(engineDir), QDir::toNativeSeparators(modelPath)));

    QList<QPair<QString, QVector<float>>> recordings;
    for (const QString &wav : wavs) {
        QString error;
        const QVector<float> samples = loadWav(wav, &error);
        if (samples.isEmpty()) {
            out(QStringLiteral("ERROR %1: %2").arg(wav, error));
            return 1;
        }
        if (concat && !recordings.isEmpty()) {
            recordings.last().first += QStringLiteral(" + ") + QFileInfo(wav).fileName();
            recordings.last().second += samples;
        } else {
            recordings.append({QFileInfo(wav).fileName(), samples});
        }
    }
    const QStringList vocabulary = context ? kVocabulary : QStringList();
    const QString prompt = context ? kPrompt : QString();
    out(QStringLiteral("vocabulario: %1\nfrase de contexto: %2 (\"¿\" en UTF-8: %3)")
            .arg(vocabulary.isEmpty() ? QStringLiteral("(ninguno)") : vocabulary.join(QStringLiteral(", ")),
                 prompt.isEmpty() ? QStringLiteral("(ninguna)") : prompt,
                 QString::fromLatin1(QStringLiteral("¿").toUtf8().toHex(' ').toUpper())));

    DictationEngine engine;
    bool ready = false;
    QString failure;
    QString finalText;
    bool gotFinal = false;
    QString confirmed;
    QString previous;
    QElapsedTimer clock;
    QObject::connect(&engine, &DictationEngine::ready, &engine, [&](const QString &device, int loadMs) {
        ready = true;
        out(QStringLiteral("READY %1 | carga del motor y del modelo + pasada en vacio: %2 ms").arg(device).arg(loadMs));
    });
    QObject::connect(&engine, &DictationEngine::failed, &engine, [&](const QString &error) {
        failure = error;
        out(QStringLiteral("FAILED %1").arg(error));
    });
    QObject::connect(&engine, &DictationEngine::partial, &engine, [&](const QString &text) {
        const DictationText::Split split = DictationText::stabilize(confirmed, previous, text);
        confirmed = split.confirmed;
        previous = text;
        out(QStringLiteral("  [%1 s | audio %2 s | pasada %3 ms] %4  ||  %5")
                .arg(clock.elapsed() / 1000.0, 5, 'f', 2)
                .arg(engine.lastAudioMs() / 1000.0, 4, 'f', 1)
                .arg(engine.lastPassMs(), 4)
                .arg(split.confirmed, split.provisional));
    });
    QObject::connect(&engine, &DictationEngine::final, &engine, [&](const QString &text) {
        gotFinal = true;
        finalText = text;
    });

    QElapsedTimer total;
    total.start();
    engine.start(engineDir, modelPath);
    if (!waitFor([&]() { return ready || !failure.isEmpty(); }, 120000) || !ready) {
        out(QStringLiteral("ERROR el motor no quedo listo: %1").arg(failure.isEmpty() ? QStringLiteral("tiempo agotado") : failure));
        return 1;
    }

    int withNukeStudio = 0;
    int finals = 0;
    for (const auto &recording : std::as_const(recordings)) {
        const QVector<float> &samples = recording.second;
        out(QStringLiteral("\n=== %1 (%2 s de audio, entra en tiempo real)").arg(recording.first).arg(samples.size() / 16000.0, 0, 'f', 1));
        confirmed.clear();
        previous.clear();
        gotFinal = false;
        finalText.clear();
        failure.clear();
        engine.begin(QStringLiteral("es"), vocabulary, prompt);
        clock.start();
        constexpr int kChunk = 1600; // 100 ms, como un microfono
        for (qsizetype at = 0; at < samples.size(); at += kChunk) {
            const qint64 due = qint64(at) * 1000 / 16000;
            while (clock.elapsed() < due) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                QThread::msleep(1);
            }
            engine.feed(samples.mid(at, kChunk));
        }
        const qint64 releasedAt = clock.elapsed();
        engine.finish();
        waitFor([&]() { return gotFinal || !failure.isEmpty(); }, 60000);
        if (!gotFinal) {
            out(QStringLiteral("  ERROR sin final: %1").arg(failure.isEmpty() ? QStringLiteral("tiempo agotado") : failure));
            continue;
        }
        ++finals;
        const bool nuke = finalText.contains(QLatin1String("Nuke Studio"));
        withNukeStudio += nuke ? 1 : 0;
        out(QStringLiteral("  FINAL %1 ms despues de soltar (pasada final %2 ms): %3")
                .arg(clock.elapsed() - releasedAt)
                .arg(engine.lastPassMs())
                .arg(finalText));
        out(QStringLiteral("  \"Nuke Studio\": %1").arg(nuke ? QStringLiteral("si") : QStringLiteral("no")));
    }
    out(QStringLiteral("\nfinales: %1 de %2 | con \"Nuke Studio\": %3").arg(finals).arg(recordings.size()).arg(withNukeStudio));

    // Apagar mata el proceso del motor.
    const qint64 pid = engine.workerPid();
    engine.stop();
    const bool stopped = waitFor([&]() { return !DictationProcess::processAlive(pid); }, 5000);
    out(QStringLiteral("apagar: el proceso del motor (PID %1) %2").arg(pid).arg(stopped ? QStringLiteral("ya no existe") : QStringLiteral("SIGUE VIVO")));

    // Un motor que se cae: failed() y la app sigue.
    ready = false;
    failure.clear();
    engine.start(engineDir, modelPath);
    waitFor([&]() { return ready || !failure.isEmpty(); }, 120000);
    const qint64 crashPid = engine.workerPid();
    engine.begin(QStringLiteral("es"), vocabulary, prompt);
    engine.killWorkerForQa();
    const bool reported = waitFor([&]() { return !failure.isEmpty(); }, 10000);
    const bool crashGone = waitFor([&]() { return !DictationProcess::processAlive(crashPid); }, 5000);
    out(QStringLiteral("caida: failed() %1, proceso (PID %2) %3, la app sigue | sesion abierta despues: %4")
            .arg(reported ? QStringLiteral("recibido") : QStringLiteral("NO llego"))
            .arg(crashPid)
            .arg(crashGone ? QStringLiteral("ya no existe") : QStringLiteral("SIGUE VIVO"))
            .arg(engine.busy() ? QStringLiteral("si") : QStringLiteral("no")));
    engine.stop();
    out(QStringLiteral("tiempo total: %1 s").arg(total.elapsed() / 1000.0, 0, 'f', 1));
    return finals == recordings.size() && stopped && reported && crashGone && !engine.busy() ? 0 : 1;
}

const char *stateName(DictationModels::State state)
{
    switch (state) {
    case DictationModels::State::Missing:
        return "falta";
    case DictationModels::State::Downloading:
        return "bajando";
    case DictationModels::State::Verifying:
        return "verificando";
    case DictationModels::State::Ready:
        return "lista";
    case DictationModels::State::Error:
        return "error";
    }
    return "?";
}

int simulateModels(const QStringList &args)
{
    if (args.isEmpty()) {
        std::fprintf(stderr, "uso: dictation:models <raiz de modelos> [<motor.tar.gz local>]\n");
        return 2;
    }
    QList<DictationModels::Piece> pieces = DictationModels::defaultPieces();
    const QString localTar = args.value(1);
    if (!localTar.isEmpty()) {
        pieces[0].url = QUrl::fromLocalFile(QFileInfo(localTar).absoluteFilePath());
    }
    DictationModels models(args.at(0), pieces);
    QHash<QString, QString> last;
    QElapsedTimer clock;
    clock.start();
    QObject::connect(&models, &DictationModels::changed, &models, [&](const QString &id) {
        // Un renglon por estado y por cada 10 %.
        const QString line = QStringLiteral("%1 %2").arg(QLatin1String(stateName(models.state(id)))).arg(models.progress(id) / 10 * 10);
        if (last.value(id) != line) {
            last.insert(id, line);
            out(QStringLiteral("  [%1 s] %2: %3 %4% %5")
                    .arg(clock.elapsed() / 1000.0, 5, 'f', 1)
                    .arg(id, QLatin1String(stateName(models.state(id))))
                    .arg(models.progress(id))
                    .arg(models.errorText(id)));
        }
    });
    const auto print = [&]() {
        for (const DictationModels::Piece &piece : models.pieces()) {
            out(QStringLiteral("%1 (%2, %3 bytes, %4): %5 %6")
                    .arg(piece.title, piece.id)
                    .arg(piece.size)
                    .arg(piece.license, QLatin1String(stateName(models.state(piece.id))), models.errorText(piece.id)));
            out(QStringLiteral("    %1").arg(QDir::toNativeSeparators(models.path(piece.id))));
        }
    };
    const auto settledAll = [&]() {
        for (const DictationModels::Piece &piece : models.pieces()) {
            if (!settled(models, piece.id)) {
                return false;
            }
        }
        return true;
    };
    models.refresh();
    waitFor(settledAll, 300000);
    out(QStringLiteral("estado:"));
    print();
    if (!localTar.isEmpty() && models.state(DictationModels::kEngine) != DictationModels::State::Ready) {
        out(QStringLiteral("instalando el motor desde %1").arg(QDir::toNativeSeparators(localTar)));
        models.download(DictationModels::kEngine);
        waitFor(settledAll, 120000);
        print();
    }
    return 0;
}

} // namespace

namespace DictationQa {

void selfTest(const std::function<void(bool ok, const QString &what)> &check)
{
    testProtocol(check);
    testText(check);
    testModels(check);
    testEngine(check);
}

int simulateAction(const QString &action, const QStringList &args)
{
    if (action == QLatin1String("transcribe")) {
        return simulateTranscribe(args);
    }
    if (action == QLatin1String("models")) {
        return simulateModels(args);
    }
    return 2;
}

} // namespace DictationQa
