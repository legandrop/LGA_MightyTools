#include "modules/dictation/DictationQa.h"

#include "app/HotkeyHub.h"
#include "app/ModuleContext.h"
#include "core/AutomatedRun.h"
#include "core/I18n.h"
#include "modules/dictation/DictationEngine.h"
#include "modules/dictation/DictationFlow.h"
#include "modules/dictation/DictationModels.h"
#include "modules/dictation/DictationModule.h"
#include "modules/dictation/DictationProtocol.h"
#include "modules/dictation/DictationText.h"
#include "modules/dictation/DictationTrigger.h"
#include "platform/AudioCapture.h"
#include "platform/DictationProcess.h"
#include "platform/TextTyper.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QtEndian>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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

    const QList<Message> all = {begin, audio, end, cancel, ready, partial, done, error};
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
    check(same, QStringLiteral("protocolo: los 8 tipos de mensaje vuelven identicos"));

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
    check(seen == all.size() && !early && drip.isEmpty(), QStringLiteral("protocolo: llegando de a un byte salen los 8, completos"));
    QByteArray oldQuit = QByteArrayLiteral("DCT1");
    char zero[4];
    qToLittleEndian<quint32>(0, zero);
    oldQuit.append(zero, 4);
    oldQuit.append(char(5));
    oldQuit += DictationProtocol::encode(end);
    check(DictationProtocol::decode(&oldQuit).size() == 1, QStringLiteral("protocolo: el tipo 5 (el Quit que nadie mandaba) ya no existe y se saltea"));

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

// ---------------------------------------------------------------- Voz o silencio

// Ruido blanco parejo con un RMS dado, reproducible (sin depender del reloj).
std::vector<float> noise(int samples, float rmsDb)
{
    std::vector<float> pcm(static_cast<size_t>(samples));
    quint32 state = 12345;
    const float scale = std::pow(10.0f, rmsDb / 20.0f) * std::sqrt(3.0f); // uniforme en [-a, a]: RMS = a/sqrt(3)
    for (float &sample : pcm) {
        state = state * 1664525u + 1013904223u;
        sample = (float(state >> 8) / float(1u << 24) * 2.0f - 1.0f) * scale;
    }
    return pcm;
}

void testVoiceGate(const Check &check)
{
    using DictationProtocol::kVoiceFloorDb;
    using DictationProtocol::loudestFrameDb;
    const std::vector<float> silence(16000 * 3 / 2, 0.0f);
    check(loudestFrameDb(silence.data(), silence.size()) < kVoiceFloorDb && loudestFrameDb(nullptr, 0) < kVoiceFloorDb,
          QStringLiteral("voz: 1,5 s de silencio no llegan al piso: el final sale vacio sin transcribir"));
    const std::vector<float> hum = noise(32000, -50.0f);
    const float humDb = loudestFrameDb(hum.data(), hum.size());
    check(humDb < kVoiceFloorDb, QStringLiteral("voz: 2 s de ruido a -50 dBFS (%1 dBFS) tampoco").arg(double(humDb), 0, 'f', 1));
    // Un poco de voz (300 ms a -20 dBFS) en medio de 2 s de ruido bajo.
    std::vector<float> spoken = hum;
    const std::vector<float> word = noise(4800, -20.0f);
    std::copy(word.begin(), word.end(), spoken.begin() + 16000);
    check(loudestFrameDb(spoken.data(), spoken.size()) >= kVoiceFloorDb,
          QStringLiteral("voz: una palabra a -20 dBFS entre el ruido alcanza para transcribir"));

    using DictationText::finalEchoesContext;
    check(finalEchoesContext(QStringLiteral("Glossary: Nuke Studio, Hiero."), kVocabulary, kPrompt)
              && finalEchoesContext(QStringLiteral("Nuke, Nuke, Nuke."), kVocabulary, kPrompt)
              && finalEchoesContext(QStringLiteral("Che, ¿vos ya revisaste el comp en Nuke Studio?"), kVocabulary, kPrompt),
          QStringLiteral("voz: un final que es el contexto repetido se descarta (Glossary, terminos repetidos, la frase)"));
    check(!finalEchoesContext(QStringLiteral("Nuke Studio."), kVocabulary, kPrompt)
              && !finalEchoesContext(QStringLiteral("El roto anda bien."), kVocabulary, kPrompt)
              && !finalEchoesContext(QStringLiteral("Fijate si el roto anda bien."), kVocabulary, kPrompt)
              && !finalEchoesContext(QStringLiteral("Gracias."), kVocabulary, kPrompt),
          QStringLiteral("voz: un final corto con palabras del contexto puede ser de verdad y se escribe"));
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

    s = stabilize(QString(), QStringLiteral("parece que anda"), QStringLiteral("parece que anda bastante"));
    const DictationText::Split onlyProvisional = stabilize(QString(), QString(), QStringLiteral("¿yo probaría?"));
    check(s.confirmed == QStringLiteral("Parece que anda") && onlyProvisional.provisional == QStringLiteral("¿Yo probaría?"),
          QStringLiteral("texto: el dictado empieza con mayuscula aunque la pasada en vivo no (%1 | %2)")
              .arg(s.confirmed, onlyProvisional.provisional));

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

        // Cada archivo del motor queda en el marcador con su tamano y SHA-256, y se revisa antes de cargarlo.
        QFile marker(QDir(engineDir).filePath(QStringLiteral(".verified")));
        const QByteArray markerText = marker.open(QIODevice::ReadOnly) ? marker.readAll() : QByteArray();
        marker.close();
        QString problem;
        check(markerText.contains("file=transcribe.dll|9|" + sha256Hex("MZ prueba"))
                  && markerText.contains("file=licenses/LICENSE|3|") && DictationModels::checkEngineFiles(engineDir, &problem),
              QStringLiteral("motor: el marcador guarda tamano y SHA-256 de cada archivo y la carpeta los cumple (%1)").arg(problem));
        writeBytes(QDir(engineDir).filePath(QStringLiteral("transcribe.dll")), QByteArrayLiteral("MZ otro!!"));
        check(!DictationModels::checkEngineFiles(engineDir, &problem) && problem == QLatin1String("transcribe.dll"),
              QStringLiteral("motor: una DLL cambiada (mismo tamano) no pasa: no se carga nada"));
        writeBytes(QDir(engineDir).filePath(QStringLiteral("transcribe.dll")), QByteArrayLiteral("MZ prueba"));
        writeBytes(QDir(engineDir).filePath(QStringLiteral("ggml-extra.dll")), QByteArrayLiteral("MZ"));
        check(!DictationModels::checkEngineFiles(engineDir, &problem) && problem == QLatin1String("ggml-extra.dll"),
              QStringLiteral("motor: una DLL que no vino en el paquete tampoco (el motor carga las de su carpeta)"));
        QFile::remove(QDir(engineDir).filePath(QStringLiteral("ggml-extra.dll")));
        check(DictationModels::checkEngineFiles(engineDir, &problem), QStringLiteral("motor: sacada la de mas, vuelve a pasar"));
        again.markDamaged(engine.id, QStringLiteral("danado"));
        check(again.state(engine.id) == State::Error && again.errorText(engine.id) == QLatin1String("danado"),
              QStringLiteral("motor: si el proceso del motor lo rechaza, la pieza queda en error en la pagina"));
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
        out(QStringLiteral("  nivel: tramo de 30 ms mas fuerte %1 dBFS (piso de voz %2 dBFS)")
                .arg(double(DictationProtocol::loudestFrameDb(samples.constData(), size_t(samples.size()))), 0, 'f', 1)
                .arg(double(DictationProtocol::kVoiceFloorDb), 0, 'f', 0));
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

// ---------------------------------------------------------------- Atajo, microfono y escritura

using Action = DictationTrigger::Action;

void testTrigger(const Check &check)
{
    DictationTrigger trigger;
    check(trigger.press(1000) == Action::Start && trigger.recording(), QStringLiteral("atajo: apretar empieza a grabar"));
    check(trigger.release(1250) == Action::None && trigger.state() == DictationTrigger::State::Latched,
          QStringLiteral("atajo: soltar antes de 300 ms deja grabando (tocar)"));
    check(trigger.press(5000) == Action::Finish && !trigger.recording() && trigger.release(5080) == Action::None,
          QStringLiteral("atajo: el proximo toque termina y su liberacion no hace nada"));
    check(trigger.press(100) == Action::Start && trigger.release(400) == Action::Finish && !trigger.recording(),
          QStringLiteral("atajo: mantener 300 ms o mas y soltar termina (mantener)"));
    check(trigger.press(0) == Action::Start && trigger.release(299) == Action::None && trigger.recording(),
          QStringLiteral("atajo: a los 299 ms todavia es un toque"));
    check(trigger.cancel() == Action::Cancel && !trigger.recording() && trigger.cancel() == Action::None,
          QStringLiteral("atajo: Esc cancela; sin grabar no hace nada"));
    trigger.press(0);
    check(trigger.cancel() == Action::Cancel && trigger.release(900) == Action::None,
          QStringLiteral("atajo: Esc mientras se mantiene cancela, y soltar despues no termina nada"));
    trigger.press(0);
    check(trigger.press(2000) == Action::Finish && !trigger.recording(),
          QStringLiteral("atajo: si la liberacion se perdio, volver a apretar termina"));
    trigger.setMaxDurationMs(30000);
    trigger.press(1000);
    check(trigger.tick(30999) == Action::None && trigger.tick(31000) == Action::Finish && !trigger.recording(),
          QStringLiteral("atajo: el tope de duracion termina solo (mantener)"));
    trigger.press(0);
    trigger.release(100);
    check(trigger.tick(29999) == Action::None && trigger.tick(30000) == Action::Finish,
          QStringLiteral("atajo: el tope vale tambien en modo tocar"));
    trigger.setMaxDurationMs(0);
    trigger.press(0);
    check(trigger.tick(10000000) == Action::None, QStringLiteral("atajo: sin tope no termina solo"));
    trigger.reset();
    check(!trigger.recording() && trigger.release(50) == Action::None,
          QStringLiteral("atajo: reset (fallo el microfono) vuelve a esperar sin accion"));
}

void testHotkeyRelease(const Check &check)
{
    // La liberacion viaja HotkeyService -> HotkeyHub -> la herramienta, por su id local (el hub corre en modo
    // automatizado: sin el servicio del sistema).
    HotkeyHub hub(true, {});
    ModuleHotkeysImpl *hotkeys = hub.acquire(QStringLiteral("dictation"), QStringLiteral("Dictation"), 4);
    QList<int> activated;
    QList<int> released;
    QObject::connect(hotkeys, &ModuleHotkeys::activated, [&activated](int id) { activated << id; });
    QObject::connect(hotkeys, &ModuleHotkeys::released, [&released](int id) { released << id; });
    const int globalId = 5 * ModuleHotkeysImpl::kRangeSize + 1;
    hotkeys->registerPriorityHotkey(1, Shortcut::fromPortableString(QStringLiteral("Meta+H")));
    hotkeys->fire(globalId);
    hotkeys->fire(globalId, true);
    check(activated == QList<int>{1} && released == QList<int>{1},
          QStringLiteral("atajo: la liberacion llega a la herramienta con su id local, aparte de la activacion"));
    hotkeys->unregisterHotkey(1);
    hotkeys->fire(globalId, true);
    check(released.size() == 1, QStringLiteral("atajo: un atajo retirado no avisa al soltar"));
    delete hotkeys;
}

void testAudio(const Check &check)
{
    {
        AudioCapture::Converter converter(AudioCapture::kSampleRate, 1);
        const float in[] = {0.1f, -0.2f, 0.3f};
        QVector<float> out;
        converter.push(in, 3, &out);
        check(out == QVector<float>{0.1f, -0.2f, 0.3f}, QStringLiteral("audio: 16 kHz mono pasa igual"));
    }
    {
        AudioCapture::Converter converter(48000, 2);
        QVector<float> in;
        for (int i = 0; i < 4800; ++i) in << 0.5f << -0.5f;
        QVector<float> out;
        converter.push(in.constData(), 4800, &out);
        bool silent = true;
        for (const float sample : out) silent = silent && qAbs(sample) < 1e-6f;
        check(out.size() == 1600 && silent,
              QStringLiteral("audio: estereo de 48 kHz a mono de 16 kHz: 100 ms dan 1600 muestras y L/R opuestos se anulan"));
    }
    {
        // Un seno de 440 Hz a 44,1 kHz estereo: de a bloques irregulares da lo mismo que de una vez.
        constexpr int kFrames = 4410;
        QVector<float> in;
        for (int i = 0; i < kFrames; ++i) {
            const float s = float(std::sin(2.0 * M_PI * 440.0 * i / 44100.0)) * 0.5f;
            in << s << s;
        }
        AudioCapture::Converter whole(44100, 2);
        AudioCapture::Converter pieces(44100, 2);
        QVector<float> a;
        QVector<float> b;
        whole.push(in.constData(), kFrames, &a);
        int done = 0;
        for (int size : {1, 7, 100, 333, 1000, 13}) {
            pieces.push(in.constData() + done * 2, size, &b);
            done += size;
        }
        pieces.push(in.constData() + done * 2, kFrames - done, &b);
        double worst = 0.0;
        for (int k = 0; k < a.size(); ++k) {
            worst = qMax(worst, qAbs(a.at(k) - 0.5 * std::sin(2.0 * M_PI * 440.0 * k / 16000.0)));
        }
        check(a == b && qAbs(a.size() - 1600) <= 1 && worst < 0.01,
              QStringLiteral("audio: 44,1 kHz por bloques da lo mismo que de una vez, sin saltos (error %1)").arg(worst, 0, 'g', 2));
    }
    check(AudioCapture::levelOf(QVector<float>(800, 0.0f)) == 0.0f && AudioCapture::levelOf({}) == 0.0f,
          QStringLiteral("nivel: silencio da 0"));
    check(qAbs(AudioCapture::levelOf(QVector<float>(800, 1.0f)) - 1.0f) < 1e-6f
              && qAbs(AudioCapture::levelOf(QVector<float>(800, 0.1f)) - 2.0f / 3.0f) < 1e-3f,
          QStringLiteral("nivel: escala completa da 1 y -20 dBFS da 2/3"));
}

void testTyping(const Check &check)
{
    const auto unicode = [](const char *hex) {
        return QStringList{QStringLiteral("unicode U+%1 abajo").arg(QLatin1String(hex)),
                           QStringLiteral("unicode U+%1 arriba").arg(QLatin1String(hex))};
    };
    const QStringList shiftEnter = {QStringLiteral("tecla vk=0x10 abajo"), QStringLiteral("tecla vk=0x0d abajo"),
                                    QStringLiteral("tecla vk=0x0d arriba"), QStringLiteral("tecla vk=0x10 arriba")};
    check(TextTyper::describe(QStringLiteral("á")) == unicode("00E1") && TextTyper::describe(QStringLiteral("ñ")) == unicode("00F1"),
          QStringLiteral("escritura: la a con tilde y la enie van como caracteres Unicode, sin depender del teclado"));
    const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
    check(TextTyper::describe(emoji) == unicode("D83D") + unicode("DE00"),
          QStringLiteral("escritura: un emoji va como su par sustituto, en orden"));
    check(TextTyper::describe(QStringLiteral("a\r\nb")) == unicode("0061") + shiftEnter + unicode("0062")
              && TextTyper::describe(QStringLiteral("\n")) == shiftEnter && TextTyper::describe(QStringLiteral("\r")) == shiftEnter,
          QStringLiteral("escritura: un salto de linea (\\r\\n, \\n o \\r) es UN Shift+Enter: en un chat Enter solo enviaria"));
    check(TextTyper::describe(QString(QChar(0x01))).isEmpty() && TextTyper::describe(QString()).isEmpty(),
          QStringLiteral("escritura: los caracteres de control no se mandan"));
    const QStringList atEnd = TextTyper::describe(QString(11, QLatin1Char('x')) + emoji + QLatin1Char('y'));
    const QStringList pushed = TextTyper::describe(QString(12, QLatin1Char('x')) + emoji);
    check(atEnd.indexOf(QStringLiteral("--")) == 26 && atEnd.mid(22, 4) == unicode("D83D") + unicode("DE00")
              && pushed.indexOf(QStringLiteral("--")) == 24 && pushed.mid(25) == unicode("D83D") + unicode("DE00"),
          QStringLiteral("escritura: lotes de 12 caracteres; un emoji nunca queda partido entre dos lotes"));
    const QStringList mixed = TextTyper::describe(QStringLiteral("Nuke, ¿andás?\n") + emoji);
    check(!mixed.isEmpty() && mixed.filter(QStringLiteral("SIN-MARCA")).isEmpty(),
          QStringLiteral("escritura: cada tecla lleva la marca propia (los ganchos de la app no la toman)"));
}

void testAutomatedRun(const Check &check)
{
    check(AutomatedRun::active(), QStringLiteral("corrida automatizada: el interruptor esta prendido"));
    AudioCapture capture;
    int events = 0;
    QObject::connect(&capture, &AudioCapture::samples, [&events]() { ++events; });
    QObject::connect(&capture, &AudioCapture::failed, [&events]() { ++events; });
    check(!capture.start() && !capture.isRunning() && !capture.start(QStringLiteral("{0.0.1.00000000}.{falso}")),
          QStringLiteral("corrida automatizada: start() no abre el microfono ni arranca el hilo"));

    TextTyper typer;
    TextTyper::Result result = TextTyper::Result::Failed;
    int typed = -1;
    QEventLoop loop;
    QObject::connect(&typer, &TextTyper::finished, &loop, [&](TextTyper::Result r, int t) {
        result = r;
        typed = t;
        loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    // Una ventana inventada: en una corrida automatizada type() no la mira.
    const bool accepted = typer.type(quintptr(1), QStringLiteral("hola"));
    loop.exec();
    QCoreApplication::processEvents();
    check(accepted && result == TextTyper::Result::LoggedOnly && typed == 0 && !typer.isBusy() && events == 0,
          QStringLiteral("corrida automatizada: type() solo anota en el log; no manda ninguna tecla"));
}

// ---------------------------------------------------------------- Recorrido (DictationFlow con dobles)

using Phase = DictationFlow::Phase;
using OverlayState = DictationOverlay::State;

// El sistema de mentira: anota cada pedido y lleva lo que importa (microfono abierto, Esc, ventanita).
class FakeIo : public DictationFlow::Io
{
public:
    QString problem;
    bool ready = true;
    bool captureStarts = true;
    bool typeAccepts = true;
    bool captureOpen = false;
    bool active = false;
    bool overlayShown = false;
    OverlayState overlay = OverlayState::Listening;
    QString overlayMessage;
    QString overlayConfirmed;
    int fed = 0;
    int begins = 0;
    int finishes = 0;
    int cancels = 0;
    QStringList typedTexts;
    quintptr typedTarget = 0;

    quintptr foregroundWindow() override { return 0x51; }
    QString engineProblem() override { return problem; }
    bool engineReady() override { return ready; }
    void engineBegin() override { ++begins; }
    void engineFeed(const QVector<float> &samples) override { fed += int(samples.size()); }
    void engineFinish() override { ++finishes; }
    void engineCancel() override { ++cancels; }
    bool startCapture() override { captureOpen = captureStarts; return captureStarts; }
    void stopCapture() override { captureOpen = false; }
    bool type(quintptr target, const QString &text) override
    {
        typedTarget = target;
        typedTexts << text;
        return typeAccepts;
    }
    int stops = 0;
    void stopTyping() override { ++stops; }
    void showOverlay(quintptr) override { overlayShown = true; }
    void overlayState(OverlayState state, const QString &message) override
    {
        overlay = state;
        overlayMessage = message;
    }
    void overlayText(const QString &confirmed, const QString &) override { overlayConfirmed = confirmed; }
    void overlayLevel(float) override {}
    // Se va (con la demora pedida): queda anotada la demora, para ver que un error se lee unos segundos.
    void hideOverlay(int delayMs) override
    {
        overlayShown = false;
        hideDelay = delayMs;
    }
    int hideDelay = -1;
    void setActive(bool on) override { active = on; }
};

struct FlowRig
{
    FakeIo io;
    DictationFlow flow{io};
    QList<DictationFlow::Entry> entries;
    FlowRig()
    {
        QObject::connect(&flow, &DictationFlow::finished, &flow, [this](const DictationFlow::Entry &e) { entries << e; });
    }
    // Nada queda abierto: microfono cerrado, Esc retirado, sin dictado en curso.
    bool clean() const { return !io.captureOpen && !io.active && flow.phase() == Phase::Idle; }
};

void testFlow(const Check &check)
{
    {
        FlowRig r;
        r.flow.press(0);
        check(r.flow.phase() == Phase::Recording && r.io.captureOpen && r.io.active && r.io.begins == 1 && r.io.overlayShown
                  && r.io.overlay == OverlayState::Listening,
              QStringLiteral("recorrido: el atajo abre el microfono, empieza el motor, registra Esc y muestra la ventanita"));
        r.flow.release(120);
        check(r.flow.phase() == Phase::Recording && r.io.captureOpen, QStringLiteral("recorrido: un toque corto (tocar) sigue grabando"));
        r.flow.samples(QVector<float>(1600, 0.1f));
        r.flow.partial(QStringLiteral("Abrí el comp"));
        r.flow.partial(QStringLiteral("Abrí el comp en Nuke"));
        check(r.io.fed == 1600 && r.io.overlayConfirmed == QStringLiteral("Abrí el comp"),
              QStringLiteral("recorrido: el audio va al motor y lo que se repite entre pasadas queda confirmado en la ventanita"));
        r.flow.press(4000);
        check(r.flow.phase() == Phase::Transcribing && !r.io.captureOpen && r.io.finishes == 1 && r.io.active
                  && r.io.overlay == OverlayState::Transcribing,
              QStringLiteral("recorrido: el segundo toque cierra el microfono y pide el final (Esc sigue para cancelar)"));
        r.flow.press(4100);
        r.flow.release(4150);
        check(r.io.begins == 1 && r.flow.phase() == Phase::Transcribing,
              QStringLiteral("recorrido: transcribiendo, el atajo no empieza otro dictado"));
        r.flow.final(QStringLiteral(" Abrí el comp en Nuke Studio. "));
        check(r.flow.phase() == Phase::Typing && r.io.typedTexts == QStringList{QStringLiteral("Abrí el comp en Nuke Studio.")}
                  && r.io.typedTarget == 0x51 && r.io.active,
              QStringLiteral("recorrido: el final se escribe en la ventana que estaba al frente al empezar (Esc sigue, para cortar)"));
        r.flow.press(4300);
        check(r.io.begins == 1, QStringLiteral("recorrido: escribiendo, el atajo no empieza otro dictado"));
        r.flow.typed(TextTyper::Result::Typed, 28);
        check(r.clean() && !r.io.overlayShown && r.entries.size() == 1 && r.entries.first().typed
                  && r.entries.first().text == QStringLiteral("Abrí el comp en Nuke Studio."),
              QStringLiteral("recorrido: escrito, la ventanita se va y queda en Recent"));
    }
    {
        FlowRig r;
        r.flow.press(0);
        r.flow.release(900);
        check(r.flow.phase() == Phase::Transcribing && !r.io.captureOpen,
              QStringLiteral("recorrido: mantener y soltar termina (mantener)"));
        r.flow.final(QString());
        check(r.clean() && r.io.typedTexts.isEmpty() && r.entries.isEmpty() && !r.io.overlayShown,
              QStringLiteral("recorrido: un final vacio no escribe nada ni queda en Recent"));
    }
    {
        FlowRig r;
        r.flow.press(0);
        r.flow.partial(QStringLiteral("esto no va"));
        r.flow.escape();
        check(r.clean() && r.io.cancels == 1 && r.io.typedTexts.isEmpty() && r.entries.size() == 1 && !r.entries.first().typed
                  && r.entries.first().note == I18n::tr("Cancelled"),
              QStringLiteral("recorrido: Esc grabando cancela sin escribir; lo dicho queda en Recent como cancelado"));
        r.flow.release(500);
        check(r.clean() && r.io.finishes == 0, QStringLiteral("recorrido: soltar el atajo despues de Esc no hace nada"));
        r.flow.press(1000);
        r.flow.release(2000);
        r.flow.escape();
        r.flow.final(QStringLiteral("tarde"));
        check(r.clean() && r.io.typedTexts.isEmpty() && r.io.cancels == 2,
              QStringLiteral("recorrido: Esc transcribiendo descarta el final"));
    }
    {
        FlowRig r;
        r.io.ready = false;
        r.flow.press(0);
        r.flow.samples(QVector<float>(800, 0.0f));
        r.flow.samples(QVector<float>(800, 0.0f));
        check(r.io.overlay == OverlayState::Loading && r.io.fed == 1600 && r.io.begins == 1,
              QStringLiteral("recorrido: con el motor cargando, la ventanita dice Loading y el audio se guarda igual"));
        r.io.ready = true;
        r.flow.engineReady();
        check(r.io.overlay == OverlayState::Listening, QStringLiteral("recorrido: al quedar listo pasa a Listening sin cortar"));
        r.flow.release(800);
        r.flow.final(QStringLiteral("lo dicho mientras cargaba"));
        check(r.io.typedTexts == QStringList{QStringLiteral("lo dicho mientras cargaba")},
              QStringLiteral("recorrido: lo dicho mientras cargaba se escribe"));
    }
    {
        FlowRig r;
        r.flow.press(0);
        r.flow.release(700);
        r.flow.final(QStringLiteral("hola"));
        r.flow.typed(TextTyper::Result::FocusChanged, 0);
        check(r.clean() && r.entries.size() == 1 && !r.entries.first().typed
                  && r.entries.first().note == I18n::tr("Not typed: the window changed") && r.io.overlay == OverlayState::Error,
              QStringLiteral("recorrido: con el foco cambiado no se escribe; la ventanita lo dice en rojo y queda en Recent"));
        r.flow.press(1000);
        r.flow.release(1700);
        r.flow.final(QStringLiteral("hola de nuevo"));
        r.flow.typed(TextTyper::Result::Elevated, 0);
        check(r.entries.size() == 2 && r.entries.last().note == I18n::tr("Not typed: the window runs as administrator"),
              QStringLiteral("recorrido: una ventana de administrador tampoco recibe nada"));
        check(DictationFlow::noteFor(TextTyper::Result::FocusChanged, true) == I18n::tr("Partly typed: the window changed"),
              QStringLiteral("recorrido: cortado a la mitad dice que se escribio en parte"));
    }
    {
        FlowRig r;
        r.io.problem = QStringLiteral("Download the dictation model in Mighty Tools first.");
        r.flow.press(0);
        check(r.clean() && r.io.begins == 0 && r.io.overlay == OverlayState::Error && r.io.hideDelay == DictationFlow::kErrorMs,
              QStringLiteral("recorrido: sin el modelo no se abre el microfono; la ventanita avisa en rojo unos segundos"));
        r.flow.release(100);
        check(r.clean(), QStringLiteral("recorrido: y soltar no deja nada a medias"));
    }
    {
        FlowRig r;
        r.flow.press(0);
        r.flow.partial(QStringLiteral("algo dicho"));
        r.flow.captureFailed(QStringLiteral("The microphone is not connected."));
        check(r.clean() && r.io.cancels == 1 && r.io.overlay == OverlayState::Error && r.entries.size() == 1,
              QStringLiteral("recorrido: si el microfono falla grabando, se cancela el motor y se avisa en rojo"));
        r.io.captureStarts = false;
        r.flow.press(1000);
        check(r.clean() && r.io.cancels == 2 && r.io.overlay == OverlayState::Error,
              QStringLiteral("recorrido: si el microfono ni siquiera arranca, igual"));
    }
    {
        FlowRig r;
        r.flow.press(0);
        r.flow.release(600);
        r.flow.engineFailed(QStringLiteral("Dictation failed: backend error"));
        check(r.clean() && r.io.typedTexts.isEmpty() && r.io.overlay == OverlayState::Error,
              QStringLiteral("recorrido: si el motor falla en la pasada final, no se escribe y se avisa"));
        r.flow.press(1000);
        r.flow.release(1600);
        // 0,6 s grabados: espera 30 s + 3 x 0,6 s.
        r.flow.tick(1600 + 31800);
        const bool waited = r.flow.phase() == Phase::Transcribing;
        r.flow.tick(1600 + 31801);
        check(waited && r.clean() && r.io.overlay == OverlayState::Error,
              QStringLiteral("recorrido: un motor que no contesta (30 s mas 3 veces lo grabado) no deja el atajo trabado"));
        // 100 s grabados: sin GPU la pasada final tarda; a los 2 minutos todavia se espera.
        r.flow.press(40000);
        r.flow.release(40100);
        r.flow.press(140000);
        r.flow.tick(140000 + 120000);
        check(r.flow.phase() == Phase::Transcribing, QStringLiteral("recorrido: con 100 s grabados el final se espera mas de 2 minutos (sin GPU)"));
        r.flow.escape();
    }
    {
        FlowRig r;
        r.flow.press(0);
        r.flow.release(900);
        r.flow.final(QStringLiteral("Un texto largo que se esta escribiendo"));
        r.flow.escape();
        check(r.io.stops == 1 && r.flow.phase() == Phase::Typing, QStringLiteral("recorrido: Esc escribiendo pide cortar la escritura"));
        r.flow.typed(TextTyper::Result::Stopped, 9);
        check(r.clean() && r.entries.size() == 1 && !r.entries.first().typed
                  && r.entries.first().note == I18n::trc("dictation", "Stopped") && !r.io.overlayShown && r.io.hideDelay == 0,
              QStringLiteral("recorrido: lo que falta no se escribe; queda en Recent como detenido y la ventanita se va"));
    }
    {
        FlowRig r;
        r.flow.setMaxDurationMs(1000);
        r.flow.press(0);
        r.flow.release(100);
        r.flow.tick(999);
        const bool stillRecording = r.flow.phase() == Phase::Recording;
        r.flow.tick(1000);
        check(stillRecording && r.flow.phase() == Phase::Transcribing && !r.io.captureOpen,
              QStringLiteral("recorrido: el tope de duracion termina solo y cierra el microfono"));
        r.flow.press(2000);
        r.flow.limitReached();
        check(r.io.finishes == 1, QStringLiteral("recorrido: el aviso de tope del motor no termina dos veces"));
    }
    {
        FlowRig r;
        r.flow.setContext({QStringLiteral("Nuke Studio"), QStringLiteral("Hiero")}, QString());
        r.flow.setReplacements({{QStringLiteral("jairo"), QStringLiteral("Hiero")}});
        r.flow.press(0);
        r.flow.partial(QStringLiteral("Glossary, Nuke Studio"));
        check(r.io.overlayConfirmed.isEmpty(), QStringLiteral("recorrido: una pasada que solo repite el vocabulario no se muestra"));
        r.flow.release(800);
        r.flow.final(QStringLiteral("Abrí jairo."));
        check(r.io.typedTexts == QStringList{QStringLiteral("Abrí Hiero.")}, QStringLiteral("recorrido: los reemplazos se aplican al final"));
    }
    {
        FlowRig r;
        r.flow.press(0);
        r.flow.samples(QVector<float>(1600, 0.0f));
        r.flow.shutdown();
        check(r.clean() && r.io.cancels == 1 && r.io.typedTexts.isEmpty() && !r.io.overlayShown,
              QStringLiteral("recorrido: apagar la herramienta grabando cierra el microfono, cancela el motor y no escribe"));
        r.flow.release(500);
        r.flow.samples(QVector<float>(1600, 0.0f));
        check(r.clean() && r.io.fed == 1600, QStringLiteral("recorrido: lo que llega despues de apagar no va al motor"));
        r.flow.press(1000);
        r.flow.release(1600);
        r.flow.final(QStringLiteral("a medias"));
        r.flow.shutdown();
        r.flow.typed(TextTyper::Result::Typed, 8);
        check(r.clean() && r.entries.isEmpty(), QStringLiteral("recorrido: apagar escribiendo no deja nada pendiente"));
    }
}

// ---------------------------------------------------------------- Modulo (contexto y atajos de mentira)

class FakeHotkeys : public ModuleHotkeys
{
public:
    QHash<int, Shortcut> registered;
    QHash<int, Shortcut> declared;
    Shortcut takenByOther;
    bool registerHotkey(int id, const Shortcut &keys) override { registered.insert(id, keys); return true; }
    bool registerPriorityHotkey(int id, const Shortcut &keys) override { registered.insert(id, keys); return true; }
    void unregisterHotkey(int id) override { registered.remove(id); }
    void unregisterAll() override { registered.clear(); }
    bool isRegistered(int id) const override { return registered.contains(id); }
    bool probe(const Shortcut &) override { return true; }
    void passThrough(const Shortcut &) override {}
    QString declare(int id, const Shortcut &keys) override
    {
        if (keys.isValid()) declared.insert(id, keys); else declared.remove(id);
        return QString();
    }
    QString declaredByOtherModule(const Shortcut &keys) const override
    {
        return keys == takenByOther ? QStringLiteral("App Toggle") : QString();
    }
    void press(int id) { emit activated(id); }
    void let(int id) { emit released(id); }
};

class FakeContext : public ModuleContext
{
public:
    QString moduleId() const override { return QStringLiteral("dictation"); }
    QString moduleTitle() const override { return QStringLiteral("Dictation"); }
    QVariant value(const QString &key, const QVariant &fallback = {}) const override { return values.value(key, fallback); }
    void setValue(const QString &key, const QVariant &value) override { values.insert(key, value); }
    void removeValue(const QString &key) override { values.remove(key); }
    bool captureMode() const override { return false; }
    bool dryRunInput() const override { return true; }
    bool automatedRun() const override { return true; }
    bool persistentRegistrationAllowed() const override { return false; }
    bool startingWithApp() const override { return starting; }
    bool starting = false;
    ModuleHotkeys *hotkeys() override { return &fake; }
    InputInjector *injector() override { return nullptr; }
    ForegroundWatcher *foreground() override { return nullptr; }
    void notify(const QString &, const QString &, NoticeIcon, int) override {}
    void showPanel() override {}
    void hideWindowTemporarily() override {}
    void restoreWindow() override {}
    QWidget *window() const override { return nullptr; }

    FakeHotkeys fake;
    QHash<QString, QVariant> values;
};

// El modulo de verdad con el microfono, la ventana del frente y, si se pide, el motor reemplazados.
//  - fakeEngine: el motor anota y nunca falla (self-test).
//  - wav: el microfono entrega ese audio en tiempo real (simulacion con el motor real).
class QaModule : public DictationModule
{
public:
    using DictationModule::DictationModule;
    bool fakeEngine = false;
    QVector<float> wav;
    bool captureOpen = false;
    int engineCancels = 0;
    using DictationModule::flow;

protected:
    bool preloadAllowed() const override { return !fakeEngine; }
    quintptr foregroundWindow() override { return 0x51; }
    QString engineProblem() override { return fakeEngine ? QString() : DictationModule::engineProblem(); }
    bool engineReady() override { return fakeEngine || DictationModule::engineReady(); }
    void engineBegin() override { if (!fakeEngine) DictationModule::engineBegin(); }
    void engineFeed(const QVector<float> &samples) override { if (!fakeEngine) DictationModule::engineFeed(samples); }
    void engineFinish() override { if (!fakeEngine) DictationModule::engineFinish(); }
    void engineCancel() override
    {
        ++engineCancels;
        if (!fakeEngine) DictationModule::engineCancel();
    }
    bool startCapture() override
    {
        captureOpen = true;
        m_fedUntil = 0;
        m_since.start();
        if (!m_feeder) {
            m_feeder = new QTimer(this);
            m_feeder->setInterval(50);
            QObject::connect(m_feeder, &QTimer::timeout, this, [this]() { feed(m_since.elapsed()); });
        }
        if (!wav.isEmpty()) m_feeder->start();
        return true;
    }
    void stopCapture() override
    {
        // Como el microfono de verdad: lo que ya estaba grabado sale antes de cerrar.
        if (captureOpen && !wav.isEmpty()) feed(m_since.elapsed());
        if (m_feeder) m_feeder->stop();
        captureOpen = false;
    }

private:
    void feed(qint64 elapsedMs)
    {
        const qsizetype until = qMin<qsizetype>(wav.size(), qsizetype(elapsedMs) * 16);
        if (until > m_fedUntil) {
            flow().samples(wav.mid(m_fedUntil, until - m_fedUntil));
            m_fedUntil = until;
        }
    }
    QTimer *m_feeder = nullptr;
    QElapsedTimer m_since;
    qsizetype m_fedUntil = 0;
};

void testModule(const Check &check)
{
    // Ajustes y textos.
    FakeContext context;
    check(DictationModule::vocabularyFrom(QStringLiteral(" Nuke,  Nuke Studio ,,nuke, Hiero "))
              == QStringList({QStringLiteral("Nuke"), QStringLiteral("Nuke Studio"), QStringLiteral("Hiero")}),
          QStringLiteral("modulo: las palabras propias se separan por coma, sin vacias ni repetidas"));
    const DictationText::Replacements pairs{{QStringLiteral("jairo"), QStringLiteral("Hiero")},
                                            {QStringLiteral("nuke estudio"), QStringLiteral("Nuke Studio")}};
    check(DictationModule::replacementsFromJson(DictationModule::replacementsToJson(pairs)) == pairs
              && DictationModule::replacementsFromJson(QStringLiteral("no es json")).isEmpty()
              && DictationModule::replacementsFromJson(QStringLiteral("[[\"\",\"x\"],[\"a\"],[\"b\",\"c\"]]")).size() == 1,
          QStringLiteral("modulo: los reemplazos van y vuelven en JSON; los rotos o sin texto se descartan"));
    const QString phrase = DictationModule::contextPhrase(QStringLiteral("es"));
    check(phrase.contains(QStringLiteral("¿vos")) && phrase.toUtf8().contains("\xC2\xBF")
              && DictationModule::contextPhrase(QStringLiteral("en")).isEmpty()
              && DictationModule::contextPhrase(QStringLiteral("auto")).isEmpty(),
          QStringLiteral("modulo: la frase de contexto va solo en castellano, en UTF-8 real"));
    context.values.insert(QStringLiteral("modelsRoot"),
                          QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_dictation_no_models")));
    {
        DictationModule module(context);
        check(module.shortcut() == Shortcut::fromPortableString(QStringLiteral("Meta+H")) && module.language() == QLatin1String("es")
                  && module.unloadMinutes() == 10 && module.replacements().isEmpty()
                  && module.customWords() == QStringLiteral("Nuke, NukeX, Nuke Studio, Hiero, comp, roto"),
              QStringLiteral("modulo: de fabrica Win+H, castellano, 10 min, sin reemplazos y las palabras de Nuke"));
        context.values.insert(QStringLiteral("language"), QStringLiteral("klingon"));
        context.values.insert(QStringLiteral("unloadMinutes"), 7);
        check(module.language() == QLatin1String("es") && module.unloadMinutes() == 10,
              QStringLiteral("modulo: un idioma o un tiempo que no estan en la lista vuelven al de fabrica"));
        context.values.remove(QStringLiteral("language"));
        context.values.remove(QStringLiteral("unloadMinutes"));

        module.start();
        check(context.fake.registered.value(1) == Shortcut::fromPortableString(QStringLiteral("Meta+H"))
                  && context.fake.declared.contains(1) && context.fake.registered.size() == 1,
              QStringLiteral("modulo: prendido registra solo el atajo (con prioridad); Esc todavia no"));
        check(module.status().tone == ModuleTone::Attention && module.enginePidForQa() == 0,
              QStringLiteral("modulo: sin el modelo pide descargarlo y no lanza el motor"));
        context.fake.press(1);
        context.fake.let(1);
        check(module.phase() == Phase::Idle && context.fake.registered.size() == 1 && module.history().isEmpty(),
              QStringLiteral("modulo: el atajo sin el modelo no graba ni registra Esc"));
        module.download(DictationModels::kModel);
        const DictationModule::PieceInfo model = module.pieces().value(1);
        check(model.state == DictationModels::State::Error && model.error == I18n::tr("Downloads are off during automated tests."),
              QStringLiteral("modulo: en una corrida automatizada el boton de descarga no abre la red"));
        context.fake.takenByOther = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+D"));
        QString error;
        check(!module.setShortcut(context.fake.takenByOther, &error) && error == I18n::tr("Already used by %1.").arg(QStringLiteral("App Toggle"))
                  && module.shortcut() == Shortcut::fromPortableString(QStringLiteral("Meta+H")),
              QStringLiteral("modulo: un atajo de otra herramienta se rechaza y queda el anterior"));
        check(module.setShortcut(Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+J")), &error)
                  && context.fake.registered.value(1) == Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+J")),
              QStringLiteral("modulo: un atajo nuevo se registra en el acto"));
        module.stop();
        check(context.fake.registered.isEmpty() && context.fake.declared.isEmpty() && module.enginePidForQa() == 0,
              QStringLiteral("modulo: apagado no deja atajos ni motor"));
    }
    context.values.remove(QStringLiteral("shortcut"));
    {
        // Un dictado en curso y la herramienta se apaga: nada queda abierto.
        QaModule module(context);
        module.fakeEngine = true;
        module.start();
        context.fake.press(1);
        Shortcut escape;
        escape.key = Qt::Key_Escape;
        Shortcut heldEscape = escape;
        heldEscape.modifiers = Qt::MetaModifier;
        check(module.phase() == Phase::Recording && module.captureOpen && module.escapeRegisteredForQa()
                  && context.fake.registered.size() == 3 && context.fake.registered.value(2) == escape
                  && context.fake.registered.value(3) == heldEscape,
              QStringLiteral("modulo: grabando, el microfono esta abierto y Esc registrado, sola y con Win (mantener Win+H)"));
        context.fake.press(2);
        check(module.phase() == Phase::Idle && !module.captureOpen && !module.escapeRegisteredForQa()
                  && context.fake.registered.size() == 1 && module.engineCancels == 1,
              QStringLiteral("modulo: Esc cancela, cierra el microfono y se retira"));
        context.fake.press(1);
        context.fake.press(3); // Win+Esc: Esc con Win todavia apretado
        check(module.phase() == Phase::Idle && !module.captureOpen && context.fake.registered.size() == 1 && module.engineCancels == 2,
              QStringLiteral("modulo: Win+Esc (manteniendo el atajo) tambien cancela"));
        context.fake.press(1);
        context.fake.let(1);
        context.fake.press(1); // segundo toque: transcribe
        module.flow().final(QStringLiteral("texto para escribir"));
        const bool escWhileTyping = module.phase() == Phase::Typing && module.escapeRegisteredForQa();
        waitFor([&]() { return module.phase() == Phase::Idle; }, 3000);
        check(escWhileTyping && !module.escapeRegisteredForQa() && context.fake.registered.size() == 1,
              QStringLiteral("modulo: escribiendo, Esc sigue registrado para cortar; al terminar se retira"));
        context.fake.press(1);
        context.fake.let(1); // un toque: sigue grabando
        module.stop();
        check(module.phase() == Phase::Idle && !module.captureOpen && context.fake.registered.isEmpty()
                  && module.engineCancels == 3,
              QStringLiteral("modulo: apagar con un dictado en curso cierra el microfono, cancela el motor y suelta los atajos"));
    }
    {
        // El motor no se carga al arrancar la app; si al prenderla el usuario (sin piezas no se lanza nada).
        context.starting = true;
        QaModule atLaunch(context);
        atLaunch.start();
        context.starting = false;
        QaModule byUser(context);
        byUser.start();
        check(!atLaunch.warmUpPendingForQa() && byUser.warmUpPendingForQa() && atLaunch.enginePidForQa() == 0
                  && byUser.enginePidForQa() == 0,
              QStringLiteral("modulo: al arrancar la app no se carga el motor; prendida por el usuario, apenas esten las piezas"));
        atLaunch.stop();
        byUser.stop();
    }
    check(DictationModule::fullPath(QStringLiteral("C:/models")) && DictationModule::fullPath(QStringLiteral("E:\\LGA_AI"))
              && DictationModule::fullPath(QStringLiteral("\\\\servidor\\recurso\\models"))
              && !DictationModule::fullPath(QStringLiteral("\\models")) && !DictationModule::fullPath(QStringLiteral("C:models"))
              && !DictationModule::fullPath(QStringLiteral("models")) && !DictationModule::fullPath(QStringLiteral("\\\\servidor"))
              && !DictationModule::fullPath(QString()),
          QStringLiteral("modulo: la carpeta de modelos tiene que ser unidad + barra o UNC (\\models y C:models no)"));
    {
        DictationModule module(context);
        const QString before = module.modelsRoot();
        module.setModelsRoot(QStringLiteral("C:models"));
        module.setModelsRoot(QStringLiteral("\\models"));
        check(module.modelsRoot() == before, QStringLiteral("modulo: una carpeta incompleta no se guarda"));
    }
}

int simulateFlow(const QStringList &args)
{
    QString wavPath;
    QString root;
    for (const QString &arg : args) {
        if (arg.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive)) wavPath = arg;
        else if (root.isEmpty()) root = arg;
    }
    if (wavPath.isEmpty()) {
        std::fprintf(stderr, "uso: dictation:flow <a.wav> [<raiz de modelos>]\n");
        return 2;
    }
    QString error;
    const QVector<float> wav = loadWav(wavPath, &error);
    if (wav.isEmpty()) {
        out(QStringLiteral("ERROR %1: %2").arg(wavPath, error));
        return 1;
    }
    const qint64 wavMs = qint64(wav.size()) * 1000 / 16000;
    FakeContext context;
    if (!root.isEmpty()) context.values.insert(QStringLiteral("modelsRoot"), root);
    QaModule module(context);
    module.wav = wav;
    QList<DictationFlow::Entry> entries;
    QObject::connect(&module.flow(), &DictationFlow::finished, &module, [&](const DictationFlow::Entry &e) { entries << e; });
    QElapsedTimer clock;
    clock.start();
    int failures = 0;
    const auto report = [&](bool ok, const QString &what) {
        out(QStringLiteral("%1 %2").arg(ok ? QStringLiteral("ok  ") : QStringLiteral("FAIL"), what));
        failures += ok ? 0 : 1;
    };
    const auto idle = [&]() { return module.phase() == Phase::Idle; };
    const auto settleMs = [&](qint64 ms) {
        const qint64 until = clock.elapsed() + ms;
        waitFor([&]() { return clock.elapsed() >= until; }, int(ms) + 1000);
    };

    module.start();
    // Lo que esta sin marcador se verifica en otro hilo: se espera a que cada pieza quede lista o con error.
    waitFor([&]() {
        for (const DictationModule::PieceInfo &piece : module.pieces()) {
            if (piece.state == DictationModels::State::Verifying) return false;
        }
        return true;
    }, 120000);
    out(QStringLiteral("raiz de modelos: %1 | piezas listas: %2 | motor: %3")
            .arg(QDir::toNativeSeparators(module.modelsRoot()))
            .arg(module.piecesReady() ? QStringLiteral("si") : QStringLiteral("no"))
            .arg(module.engineState() == DictationModule::EngineState::Loading ? QStringLiteral("cargando") : QStringLiteral("otro")));
    if (!module.piecesReady()) {
        out(QStringLiteral("ERROR faltan el motor o el modelo en esa raiz"));
        return 1;
    }

    // 1) Mantener, apretado ANTES de que el motor este listo: el audio espera en el proceso del motor.
    const bool loadingAtPress = module.engineState() == DictationModule::EngineState::Loading;
    context.fake.press(1);
    settleMs(wavMs + 300);
    const qint64 releasedAt = clock.elapsed();
    context.fake.let(1);
    waitFor([&]() { return idle(); }, 60000);
    const DictationFlow::Entry hold = entries.value(0);
    out(QStringLiteral("mantener: final %1 ms despues de soltar: %2 [%3]").arg(clock.elapsed() - releasedAt).arg(hold.text, hold.note));
    report(loadingAtPress && entries.size() == 1 && hold.text.contains(QLatin1String("Nuke Studio"))
               && hold.note == I18n::tr("Not typed: automated test run"),
           QStringLiteral("mantener con el motor cargando: el texto llega entero, con \"Nuke Studio\", y en una corrida automatizada no se escribe"));

    // 2) Tocar: un toque corto empieza, otro termina.
    context.fake.press(1);
    settleMs(80);
    context.fake.let(1);
    settleMs(wavMs + 300);
    const bool stillRecording = module.phase() == Phase::Recording;
    context.fake.press(1);
    context.fake.let(1);
    waitFor([&]() { return idle(); }, 60000);
    report(stillRecording && entries.size() == 2 && !entries.at(1).text.isEmpty(),
           QStringLiteral("tocar: graba entre dos toques y transcribe: %1").arg(entries.value(1).text));

    // 3) Esc grabando: no se escribe nada.
    context.fake.press(1);
    settleMs(1500);
    context.fake.press(2);
    report(idle() && !module.captureOpen && !module.escapeRegisteredForQa(),
           QStringLiteral("Esc: corta, cierra el microfono y retira Esc"));

    // 4) Apagar con un dictado en curso: el proceso del motor no queda.
    context.fake.press(1);
    settleMs(1500);
    const qint64 pid = module.enginePidForQa();
    module.stop();
    const bool gone = pid > 0 && waitFor([&]() { return !DictationProcess::processAlive(pid); }, 5000);
    report(gone && !module.captureOpen && context.fake.registered.isEmpty() && module.enginePidForQa() == 0,
           QStringLiteral("apagar grabando: el motor (PID %1) ya no existe, microfono cerrado, 0 atajos").arg(pid));
    out(QStringLiteral("dictation:flow %1: %2 fallas").arg(failures ? QStringLiteral("FALLO") : QStringLiteral("ok")).arg(failures));
    return failures ? 1 : 0;
}

} // namespace

namespace DictationQa {

void selfTest(const std::function<void(bool ok, const QString &what)> &check)
{
    testProtocol(check);
    testText(check);
    testVoiceGate(check);
    testModels(check);
    testEngine(check);
    testTrigger(check);
    testHotkeyRelease(check);
    testAudio(check);
    testTyping(check);
    testAutomatedRun(check);
    testFlow(check);
    testModule(check);
}

int simulateAction(const QString &action, const QStringList &args)
{
    if (action == QLatin1String("transcribe")) {
        return simulateTranscribe(args);
    }
    if (action == QLatin1String("flow")) {
        return simulateFlow(args);
    }
    if (action == QLatin1String("models")) {
        return simulateModels(args);
    }
    if (action == QLatin1String("devices")) {
        const QList<AudioCapture::Device> devices = AudioCapture::devices();
        std::printf("dictation:devices %d microfonos activos (ninguno se abre)\n", int(devices.size()));
        for (const AudioCapture::Device &device : devices) {
            std::printf("  %s\t%s\n", device.name.toUtf8().constData(), device.id.toUtf8().constData());
        }
        return 0;
    }
    return 2;
}

} // namespace DictationQa
