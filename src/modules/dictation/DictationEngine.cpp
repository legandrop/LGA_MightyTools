#include "modules/dictation/DictationEngine.h"

#include "core/I18n.h"
#include "modules/dictation/DictationProtocol.h"
#include "platform/DictationProcess.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

using DictationProtocol::Message;
using DictationProtocol::Type;

namespace {

static_assert(DictationEngine::kMaxSeconds == DictationProtocol::kMaxSeconds, "el tope es uno solo");
static_assert(DictationEngine::kSampleRate == DictationProtocol::kSampleRate, "16 kHz en los dos lados");

QByteArray frame(Type type, quint32 session = 0)
{
    Message message;
    message.type = type;
    message.session = session;
    return DictationProtocol::encode(message);
}

} // namespace

DictationEngine::DictationEngine(QObject *parent)
    : QObject(parent)
{
    m_idle = new QTimer(this);
    m_idle->setSingleShot(true);
    connect(m_idle, &QTimer::timeout, this, [this]() {
        if (m_process && !busy()) {
            qInfo() << "[Dictado] Sin uso por" << m_idleMinutes << "min: se cierra el motor y se libera su memoria.";
            closeProcess();
            emit unloaded();
        }
    });
}

DictationEngine::~DictationEngine()
{
    closeProcess();
}

void DictationEngine::start(const QString &enginePath, const QString &modelPath)
{
    closeProcess();
    m_enginePath = enginePath;
    m_modelPath = modelPath;
    launch();
}

void DictationEngine::stop()
{
    closeProcess();
    m_enginePath.clear();
    m_modelPath.clear();
}

void DictationEngine::launch()
{
    m_ready = false;
    m_failedSent = false;
    m_buffer.clear();
    m_errorBuffer.clear();
    if (!QFileInfo::exists(QDir(m_enginePath).filePath(QStringLiteral("transcribe.dll")))) {
        emit failed(I18n::tr("The dictation engine is not installed."));
        return;
    }
    if (!QFileInfo(m_modelPath).isFile()) {
        emit failed(I18n::tr("The dictation model is not installed."));
        return;
    }
    m_process = new QProcess(this);
    DictationProcess::hideConsole(m_process);
    m_process->setProgram(QCoreApplication::applicationFilePath());
    m_process->setArguments({QStringLiteral("--dictation-worker"), QDir::toNativeSeparators(m_enginePath),
                             QDir::toNativeSeparators(m_modelPath)});
    connect(m_process, &QProcess::readyReadStandardOutput, this, &DictationEngine::onOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &DictationEngine::onErrorOutput);
    connect(m_process, &QProcess::finished, this, &DictationEngine::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            onFinished(); // sin proceso no llega finished()
        }
    });
    m_process->start(QIODevice::ReadWrite);
    qInfo() << "[Dictado] Motor lanzado, PID" << m_process->processId();
}

void DictationEngine::closeProcess()
{
    m_idle->stop();
    m_open = false;
    m_awaitingFinal.clear();
    m_ready = false;
    if (!m_process) {
        return;
    }
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    // Matar el proceso devuelve toda la memoria de video (cerrar solo la sesion de Vulkan no alcanza).
    process->kill();
    process->waitForFinished(3000);
    // deleteLater: puede que estemos adentro de una senal de este mismo QProcess.
    process->deleteLater();
}

void DictationEngine::send(const QByteArray &bytes)
{
    if (m_process) {
        m_process->write(bytes);
    }
}

void DictationEngine::begin(const QString &language, const QStringList &vocabulary, const QString &prompt)
{
    if (m_enginePath.isEmpty()) {
        emit failed(I18n::tr("The dictation engine is not running."));
        return;
    }
    if (!m_process) {
        launch(); // se habia cerrado por inactividad (o se cayo): vuelve a cargar el modelo
        if (!m_process) {
            return;
        }
    }
    m_idle->stop();
    ++m_session;
    m_open = true;
    m_samples = 0;
    m_limitSent = false;
    Message message;
    message.type = Type::Begin;
    message.session = m_session;
    message.language = language;
    message.vocabulary = vocabulary;
    message.prompt = prompt;
    send(DictationProtocol::encode(message));
}

void DictationEngine::feed(const QVector<float> &samples)
{
    if (!m_open || samples.isEmpty()) {
        return;
    }
    const qint64 room = qint64(kMaxSeconds) * kSampleRate - m_samples;
    const qint64 take = qMin(room, qint64(samples.size()));
    // Mensajes de hasta 1 s de audio.
    for (qint64 at = 0; at < take; at += kSampleRate) {
        Message message;
        message.type = Type::Audio;
        message.samples = samples.mid(at, qMin<qint64>(kSampleRate, take - at));
        send(DictationProtocol::encode(message));
    }
    m_samples += qMax<qint64>(0, take);
    if (m_samples >= qint64(kMaxSeconds) * kSampleRate && !m_limitSent) {
        m_limitSent = true;
        emit limitReached();
    }
}

void DictationEngine::finish()
{
    if (!m_open) {
        return;
    }
    m_open = false;
    m_awaitingFinal.insert(m_session);
    send(frame(Type::End, m_session));
}

void DictationEngine::cancel()
{
    if (m_open) {
        m_open = false;
    } else if (!m_awaitingFinal.remove(m_session)) {
        return;
    }
    send(frame(Type::Cancel, m_session));
    armIdle();
}

void DictationEngine::setIdleUnloadMinutes(int minutes)
{
    m_idleMinutes = qMax(0, minutes);
    armIdle();
}

void DictationEngine::armIdle()
{
    if (m_idleMinutes > 0 && m_process && !busy()) {
        m_idle->start(m_idleMinutes * 60 * 1000);
    } else {
        m_idle->stop();
    }
}

qint64 DictationEngine::workerPid() const
{
    return m_process ? m_process->processId() : 0;
}

void DictationEngine::killWorkerForQa()
{
    if (m_process) {
        m_process->kill(); // sin disconnect: el mismo camino que un proceso que se cae
    }
}

void DictationEngine::onOutput()
{
    if (!m_process) {
        return;
    }
    m_buffer += m_process->readAllStandardOutput();
    const QList<Message> messages = DictationProtocol::decode(&m_buffer);
    for (const Message &message : messages) {
        switch (message.type) {
        case Type::Ready:
            m_ready = true;
            m_device = message.text;
            qInfo() << "[Dictado] Motor listo en" << m_device << "en" << message.passMs << "ms";
            armIdle();
            emit ready(message.text, int(message.passMs));
            break;
        case Type::Partial:
            if (m_open && message.session == m_session) {
                m_lastAudioMs = int(message.audioMs);
                m_lastPassMs = int(message.passMs);
                emit partial(message.text);
            }
            break;
        case Type::Final:
            if (m_awaitingFinal.remove(message.session)) {
                m_lastAudioMs = int(message.audioMs);
                m_lastPassMs = int(message.passMs);
                armIdle();
                emit final(message.text);
            }
            break;
        case Type::Error:
            if (message.session == 0) {
                // El motor no pudo arrancar y sale solo: el finished() que sigue no repite el aviso.
                qWarning() << "[Dictado] El motor no arranco:" << message.text;
                m_failedSent = true;
                emit failed(I18n::tr("The dictation engine could not start: %1").arg(message.text));
            } else if (m_awaitingFinal.remove(message.session) || (m_open && message.session == m_session)) {
                qWarning() << "[Dictado] Fallo la pasada final:" << message.text;
                if (message.session == m_session) {
                    m_open = false;
                }
                armIdle();
                emit failed(I18n::tr("Dictation failed: %1").arg(message.text));
            }
            break;
        default:
            break;
        }
    }
}

void DictationEngine::onErrorOutput()
{
    if (!m_process) {
        return;
    }
    m_errorBuffer += m_process->readAllStandardError();
    int end = 0;
    while ((end = int(m_errorBuffer.indexOf('\n'))) >= 0) {
        const QString line = QString::fromUtf8(m_errorBuffer.left(end)).trimmed();
        m_errorBuffer.remove(0, end + 1);
        if (!line.isEmpty()) {
            qInfo().noquote() << "[Dictado] motor:" << line;
        }
    }
}

void DictationEngine::onFinished()
{
    if (!m_process) {
        return;
    }
    onOutput();
    onErrorOutput();
    if (!m_process) {
        return; // una senal de arriba ya lo cerro
    }
    qWarning() << "[Dictado] El proceso del motor salio, codigo" << m_process->exitCode() << m_process->exitStatus();
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    process->deleteLater();
    m_ready = false;
    m_open = false;
    m_awaitingFinal.clear();
    m_idle->stop();
    if (!m_failedSent) {
        m_failedSent = true;
        emit failed(I18n::tr("The dictation engine stopped unexpectedly."));
    }
}
