#include "modules/dictation/DictationWorker.h"

#include "modules/dictation/DictationProtocol.h"
#include "modules/dictation/DictationText.h"
#include "platform/DictationProcess.h"

// Solo los tipos y las firmas de la API en C del motor (transcribe.cpp 0.4.1, MIT): las funciones se
// buscan con GetProcAddress y se llaman por puntero.
#include "modules/dictation/third_party/transcribe.h"
#include "modules/dictation/third_party/whisper.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

using DictationProtocol::Message;
using DictationProtocol::Type;

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kLiveInterval = std::chrono::milliseconds(500);
// Con medio segundo de audio Whisper inventa ("Lukas", "GarneX, volken까요?") y tarda: la primera pasada
// en vivo espera a tener 1 s.
constexpr size_t kMinLiveSamples = DictationProtocol::kSampleRate;

struct Params
{
    QString language;
    QStringList vocabulary;
    QString prompt;
};

// Una sesion terminada (llego End) que espera su pasada final.
struct FinalJob
{
    quint32 session = 0;
    std::vector<float> audio;
    Params params;
};

// Lo que comparten el hilo que lee los comandos y el que transcribe.
struct Shared
{
    std::mutex mutex;
    std::condition_variable wake;
    bool quit = false;
    // Sesion abierta (grabando)
    bool active = false;
    quint32 session = 0;
    Params params;
    std::vector<float> audio;
    quint64 audioVersion = 0;
    quint64 epoch = 0;         // sube con Begin, End y Cancel: una pasada en vivo vieja no manda nada
    Clock::time_point lastStart; // del Begin o de la ultima pasada en vivo
    // Sesiones terminadas, en orden
    std::deque<FinalJob> finals;
    quint32 runningFinal = 0;
    quint32 cancelledFinal = 0;
};

// Aborto de la pasada que corre: el motor lo mira entre ventanas y entre tokens. La pasada en vivo se
// corta con cualquier comando nuevo; la final, solo con Cancel de su sesion o Quit.
std::atomic<bool> g_abortLive{false};
std::atomic<bool> g_abortFinal{false};

bool abortRequested(void *flag)
{
    return static_cast<std::atomic<bool> *>(flag)->load();
}

void logToStderr(transcribe_log_level level, const char *message, void *)
{
    if (!message || (level != TRANSCRIBE_LOG_LEVEL_WARN && level != TRANSCRIBE_LOG_LEVEL_ERROR)) {
        return;
    }
    std::fputs(message, stderr);
    const size_t size = std::strlen(message);
    if (size == 0 || message[size - 1] != '\n') {
        std::fputc('\n', stderr);
    }
    std::fflush(stderr);
}

void note(const QString &text)
{
    std::fprintf(stderr, "%s\n", text.toUtf8().constData());
    std::fflush(stderr);
}

void send(const Message &message)
{
    DictationProcess::writeOutput(DictationProtocol::encode(message));
}

void sendError(quint32 session, const QString &text)
{
    Message message;
    message.type = Type::Error;
    message.session = session;
    message.text = text;
    send(message);
}

// ---------------------------------------------------------------- El motor

struct Pass
{
    bool ok = false;
    bool aborted = false;
    bool backendError = false;
    QString text;
    QString error;
    quint32 ms = 0;
};

class Engine
{
public:
    bool load(const QString &engineDir, const QString &modelPath, QString *error);
    // live: pasada en vivo, sin los reintentos de Whisper a otra temperatura (con audio que todavia no
    // dice nada multiplicaban el tiempo de la pasada); la final los conserva.
    Pass run(const std::vector<float> &pcm, const Params &params, std::atomic<bool> *abortFlag, bool live = false);
    void close();
    QString device() const { return m_device; }

private:
    template <typename T>
    bool bind(void *library, const char *name, T *out, QString *error)
    {
        *out = reinterpret_cast<T>(DictationProcess::resolve(library, name));
        if (!*out) {
            *error = QStringLiteral("transcribe.dll: no exporta %1").arg(QLatin1String(name));
        }
        return *out != nullptr;
    }
    transcribe_status open(transcribe_backend_request backend);
    QString status(int code) const { return QString::fromUtf8(m_statusString(code)); }
    QString readDevice() const;

    decltype(&transcribe_status_string) m_statusString = nullptr;
    decltype(&transcribe_version) m_version = nullptr;
    decltype(&transcribe_abi_struct_size) m_abiSize = nullptr;
    decltype(&transcribe_log_set) m_logSet = nullptr;
    decltype(&transcribe_init_backends_default) m_initBackends = nullptr;
    decltype(&transcribe_model_load_params_init) m_loadParamsInit = nullptr;
    decltype(&transcribe_open) m_open = nullptr;
    decltype(&transcribe_close) m_close = nullptr;
    decltype(&transcribe_get_model) m_getModel = nullptr;
    decltype(&transcribe_model_device) m_modelDevice = nullptr;
    decltype(&transcribe_device_info_init) m_deviceInfoInit = nullptr;
    decltype(&transcribe_device_get_info) m_deviceGetInfo = nullptr;
    decltype(&transcribe_run_params_init) m_runParamsInit = nullptr;
    decltype(&transcribe_run) m_run = nullptr;
    decltype(&transcribe_set_abort_callback) m_setAbort = nullptr;
    decltype(&transcribe_full_text) m_fullText = nullptr;
    decltype(&transcribe_model_accepts_ext_kind) m_acceptsExt = nullptr;
    decltype(&transcribe_whisper_run_ext_init) m_whisperExtInit = nullptr;

    QByteArray m_model;
    bool m_whisper = false; // el modelo acepta la extension de Whisper
    transcribe_session *m_session = nullptr;
    QString m_device;
};

bool Engine::load(const QString &engineDir, const QString &modelPath, QString *error)
{
    void *library = DictationProcess::loadLibrary(QDir(engineDir).filePath(QStringLiteral("transcribe.dll")), error);
    if (!library) {
        return false;
    }
    if (!bind(library, "transcribe_status_string", &m_statusString, error)
        || !bind(library, "transcribe_version", &m_version, error)
        || !bind(library, "transcribe_abi_struct_size", &m_abiSize, error)
        || !bind(library, "transcribe_log_set", &m_logSet, error)
        || !bind(library, "transcribe_init_backends_default", &m_initBackends, error)
        || !bind(library, "transcribe_model_load_params_init", &m_loadParamsInit, error)
        || !bind(library, "transcribe_open", &m_open, error)
        || !bind(library, "transcribe_close", &m_close, error)
        || !bind(library, "transcribe_get_model", &m_getModel, error)
        || !bind(library, "transcribe_model_device", &m_modelDevice, error)
        || !bind(library, "transcribe_device_info_init", &m_deviceInfoInit, error)
        || !bind(library, "transcribe_device_get_info", &m_deviceGetInfo, error)
        || !bind(library, "transcribe_run_params_init", &m_runParamsInit, error)
        || !bind(library, "transcribe_run", &m_run, error)
        || !bind(library, "transcribe_set_abort_callback", &m_setAbort, error)
        || !bind(library, "transcribe_full_text", &m_fullText, error)
        || !bind(library, "transcribe_model_accepts_ext_kind", &m_acceptsExt, error)
        || !bind(library, "transcribe_whisper_run_ext_init", &m_whisperExtInit, error)) {
        return false;
    }
    // Los *_init() del motor escriben su sizeof: si la DLL no es la del encabezado, pisarian memoria.
    if (QByteArray(m_version()) != TRANSCRIBE_VERSION
        || m_abiSize(TRANSCRIBE_ABI_RUN_PARAMS) != sizeof(transcribe_run_params)
        || m_abiSize(TRANSCRIBE_ABI_MODEL_LOAD_PARAMS) != sizeof(transcribe_model_load_params)
        || m_abiSize(TRANSCRIBE_ABI_DEVICE_INFO) != sizeof(transcribe_device_info)) {
        *error = QStringLiteral("transcribe.dll %1 no coincide con el encabezado %2")
                     .arg(QString::fromUtf8(m_version()), QLatin1String(TRANSCRIBE_VERSION));
        return false;
    }
    // Una sola vez y antes de todo (contrato del motor). Los avisos y errores del motor y de ggml van a
    // stderr; nada va a la salida estandar del protocolo.
    m_logSet(&logToStderr, nullptr);
    // Los modulos de CPU y Vulkan estan al lado de transcribe.dll. Sin esto transcribe_open falla con
    // "backend error".
    const transcribe_status backends = m_initBackends();
    if (backends != TRANSCRIBE_OK) {
        *error = QStringLiteral("transcribe_init_backends_default: %1").arg(status(backends));
        return false;
    }
    m_model = QDir::toNativeSeparators(modelPath).toUtf8();

    bool cpu = false;
    transcribe_status opened = open(TRANSCRIBE_BACKEND_AUTO);
    if (opened == TRANSCRIBE_ERR_BACKEND) {
        cpu = true;
        opened = open(TRANSCRIBE_BACKEND_CPU);
    }
    if (opened != TRANSCRIBE_OK) {
        *error = QStringLiteral("transcribe_open: %1").arg(status(opened));
        return false;
    }
    // Pasada en vacio: compila los shaders de Vulkan ahora y no en el primer dictado. Con vocabulario y
    // frase, como una pasada final: sin eso, con la cache del driver vacia, la primera pasada con
    // contexto tardaba varios segundos mas. Un solo intento (como en vivo): sobre silencio los reintentos
    // de Whisper solo suman tiempo. Si la GPU falla aca (el motor dice que se recupera recargando en
    // CPU), se sigue en CPU.
    const std::vector<float> silence(DictationProtocol::kSampleRate, 0.0f);
    const Params context{QString(), {QStringLiteral("Nuke Studio"), QStringLiteral("XYplorer")},
                         QStringLiteral("Una frase de contexto, como la que se usa al dictar, para preparar el motor.")};
    std::atomic<bool> never{false};
    Pass warmup = run(silence, context, &never, true);
    if (!warmup.ok && warmup.backendError && !cpu) {
        note(QStringLiteral("La pasada en vacio fallo en la GPU (%1); se sigue en CPU.").arg(warmup.error));
        close();
        opened = open(TRANSCRIBE_BACKEND_CPU);
        if (opened != TRANSCRIBE_OK) {
            *error = QStringLiteral("transcribe_open (CPU): %1").arg(status(opened));
            return false;
        }
        warmup = run(silence, context, &never, true);
    }
    if (!warmup.ok) {
        *error = warmup.error;
        return false;
    }
    m_device = readDevice();
    note(QStringLiteral("Motor %1 en %2; pasada en vacio: %3 ms").arg(QString::fromUtf8(m_version()), m_device).arg(warmup.ms));
    return true;
}

transcribe_status Engine::open(transcribe_backend_request backend)
{
    transcribe_model_load_params params;
    m_loadParamsInit(&params);
    params.backend = backend;
    const transcribe_status opened = m_open(m_model.constData(), &params, nullptr, &m_session);
    m_whisper = opened == TRANSCRIBE_OK
        && m_acceptsExt(m_getModel(m_session), TRANSCRIBE_EXT_SLOT_RUN, TRANSCRIBE_EXT_KIND_WHISPER_RUN);
    return opened;
}

QString Engine::readDevice() const
{
    transcribe_device_info info;
    m_deviceInfoInit(&info);
    const transcribe_device_t device = m_session ? m_modelDevice(m_getModel(m_session)) : nullptr;
    if (!device || m_deviceGetInfo(device, &info) != TRANSCRIBE_OK) {
        return QStringLiteral("?");
    }
    const char *name = info.description ? info.description : (info.name ? info.name : "?");
    return QStringLiteral("%1 (%2)").arg(QString::fromUtf8(name), QString::fromUtf8(info.kind ? info.kind : "?"));
}

Pass Engine::run(const std::vector<float> &pcm, const Params &params, std::atomic<bool> *abortFlag, bool live)
{
    Pass pass;
    if (pcm.empty()) {
        pass.ok = true; // nada que transcribir (End sin audio)
        return pass;
    }
    // El motor copia los textos antes de volver: alcanza con que vivan durante la llamada.
    const QByteArray language = params.language.trimmed().toUtf8();
    const QByteArray prompt = params.prompt.trimmed().toUtf8();
    std::vector<QByteArray> terms;
    for (const QString &term : params.vocabulary) {
        const QByteArray utf8 = term.trimmed().toUtf8();
        if (!utf8.isEmpty()) {
            terms.push_back(utf8);
        }
    }
    std::vector<const char *> termPointers;
    for (const QByteArray &term : terms) {
        termPointers.push_back(term.constData());
    }
    transcribe_run_params run;
    m_runParamsInit(&run);
    run.language = language.isEmpty() ? nullptr : language.constData();
    if (!termPointers.empty()) {
        run.vocabulary = termPointers.data();
        run.n_vocabulary = int32_t(termPointers.size());
    }
    run.prompt = prompt.isEmpty() ? nullptr : prompt.constData();
    transcribe_whisper_run_ext whisper;
    if (live && m_whisper) {
        m_whisperExtInit(&whisper);
        whisper.temperature_inc = 0.0f; // un solo intento, el de temperatura 0
        run.family = &whisper.ext;
    }
    m_setAbort(m_session, &abortRequested, abortFlag);

    QElapsedTimer timer;
    timer.start();
    const transcribe_status result = m_run(m_session, pcm.data(), int(pcm.size()), &run);
    pass.ms = quint32(timer.elapsed());
    if (result == TRANSCRIBE_ERR_ABORTED) {
        pass.aborted = true;
        return pass;
    }
    // Truncado o repetido: el motor deja leer lo que alcanzo a transcribir. Mejor eso que nada.
    if (result == TRANSCRIBE_OK || result == TRANSCRIBE_ERR_OUTPUT_TRUNCATED || result == TRANSCRIBE_ERR_OUTPUT_REPETITION) {
        if (result != TRANSCRIBE_OK) {
            note(QStringLiteral("transcribe_run: %1 (se usa el texto parcial)").arg(status(result)));
        }
        const char *text = m_fullText(m_session);
        pass.ok = true;
        pass.text = DictationProtocol::cleanText(QByteArray(text ? text : "")).trimmed();
        return pass;
    }
    pass.backendError = result == TRANSCRIBE_ERR_BACKEND;
    pass.error = QStringLiteral("transcribe_run: %1").arg(status(result));
    return pass;
}

void Engine::close()
{
    if (m_session) {
        m_close(m_session);
        m_session = nullptr;
    }
}

// ---------------------------------------------------------------- Los comandos

void handle(Shared *shared, const Message &message)
{
    switch (message.type) {
    case Type::Begin:
        // Una sesion abierta sin End queda descartada.
        shared->active = true;
        shared->session = message.session;
        shared->params = {message.language, message.vocabulary, message.prompt};
        shared->audio.clear();
        ++shared->audioVersion;
        ++shared->epoch;
        shared->lastStart = Clock::now();
        g_abortLive = true;
        break;
    case Type::Audio:
        if (shared->active) {
            const size_t room = size_t(DictationProtocol::kMaxSamples) - qMin(shared->audio.size(), size_t(DictationProtocol::kMaxSamples));
            const size_t take = qMin(room, size_t(message.samples.size()));
            for (size_t i = 0; i < take; ++i) {
                // El motor rechaza la pasada entera si una muestra no es finita.
                const float sample = message.samples.at(int(i));
                shared->audio.push_back(std::isfinite(sample) ? qBound(-1.0f, sample, 1.0f) : 0.0f);
            }
            if (take > 0) {
                ++shared->audioVersion;
            }
        }
        break;
    case Type::End:
        if (shared->active && message.session == shared->session) {
            shared->finals.push_back({shared->session, std::move(shared->audio), shared->params});
            shared->audio.clear();
            shared->active = false;
            ++shared->epoch;
            g_abortLive = true;
        }
        break;
    case Type::Cancel:
        if (shared->active && message.session == shared->session) {
            shared->active = false;
            shared->audio.clear();
            ++shared->epoch;
            g_abortLive = true;
        }
        for (auto it = shared->finals.begin(); it != shared->finals.end();) {
            it = it->session == message.session ? shared->finals.erase(it) : it + 1;
        }
        if (shared->runningFinal == message.session) {
            shared->cancelledFinal = message.session;
            g_abortFinal = true;
        }
        break;
    case Type::Quit:
        shared->quit = true;
        g_abortLive = true;
        g_abortFinal = true;
        break;
    default:
        break;
    }
}

void readCommands(Shared *shared)
{
    QByteArray buffer;
    std::vector<char> chunk(64 * 1024);
    for (;;) {
        const qint64 read = DictationProcess::readInput(chunk.data(), qint64(chunk.size()));
        if (read <= 0) {
            break; // la app cerro el canal: salio o se cayo
        }
        buffer.append(chunk.data(), int(read));
        const QList<Message> messages = DictationProtocol::decode(&buffer);
        if (messages.isEmpty()) {
            continue;
        }
        std::lock_guard<std::mutex> lock(shared->mutex);
        for (const Message &message : messages) {
            handle(shared, message);
        }
        shared->wake.notify_all();
        if (shared->quit) {
            return;
        }
    }
    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->quit = true;
    g_abortLive = true;
    g_abortFinal = true;
    shared->wake.notify_all();
}

quint32 audioMs(size_t samples)
{
    return quint32(quint64(samples) * 1000 / DictationProtocol::kSampleRate);
}

} // namespace

namespace DictationWorker {

int run(int argc, char *argv[])
{
    QElapsedTimer clock;
    clock.start();
    DictationProcess::prepareWorker();
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const int at = int(arguments.indexOf(QStringLiteral("--dictation-worker")));
    const QString engineDir = arguments.value(at + 1);
    const QString modelPath = arguments.value(at + 2);

    // A proposito sin delete: el hilo lector puede seguir bloqueado en ReadFile hasta que el proceso
    // termina. Arranca antes de cargar el modelo: lo que la app mande mientras tanto queda guardado.
    auto *shared = new Shared;
    std::thread(readCommands, shared).detach();

    Engine engine;
    QString error;
    if (engineDir.isEmpty() || modelPath.isEmpty()) {
        sendError(0, QStringLiteral("uso: --dictation-worker <carpeta del motor> <modelo>"));
        return 2;
    }
    if (!engine.load(engineDir, modelPath, &error)) {
        note(error);
        sendError(0, error);
        return 1;
    }
    Message ready;
    ready.type = Type::Ready;
    ready.text = engine.device();
    ready.passMs = quint32(clock.elapsed());
    send(ready);

    quint64 liveVersion = 0;
    for (;;) {
        std::unique_lock<std::mutex> lock(shared->mutex);
        for (;;) {
            if (shared->quit || !shared->finals.empty()) {
                break;
            }
            if (shared->active && shared->audioVersion != liveVersion && shared->audio.size() >= kMinLiveSamples) {
                const Clock::time_point due = shared->lastStart + kLiveInterval;
                if (Clock::now() >= due) {
                    break;
                }
                shared->wake.wait_until(lock, due);
                continue;
            }
            shared->wake.wait(lock);
        }
        if (shared->quit) {
            break;
        }

        if (!shared->finals.empty()) {
            const FinalJob job = std::move(shared->finals.front());
            shared->finals.pop_front();
            shared->runningFinal = job.session;
            g_abortFinal = false;
            lock.unlock();
            const Pass pass = engine.run(job.audio, job.params, &g_abortFinal);
            lock.lock();
            shared->runningFinal = 0;
            const bool deliver = !pass.aborted && shared->cancelledFinal != job.session && !shared->quit;
            // Sin el candado: si la app tarda en leer, el lector de comandos no se traba esperandolo.
            lock.unlock();
            if (deliver) {
                if (pass.ok) {
                    Message done;
                    done.type = Type::Final;
                    done.session = job.session;
                    done.audioMs = audioMs(job.audio.size());
                    done.passMs = pass.ms;
                    done.text = pass.text;
                    send(done);
                } else {
                    sendError(job.session, pass.error);
                }
            }
            continue;
        }

        // Pasada en vivo sobre todo lo acumulado. Sin la frase de contexto: con poco audio Whisper la repite
        // ("Fijate si el roto anda bien" a los 0,5 s) o arranca con sus palabras ("Fijate que anda..." en
        // vez de "Parece que anda..."), y la ventanita confirmaria esa palabra. La final si la usa.
        const std::vector<float> pcm = shared->audio;
        Params params = shared->params;
        params.prompt.clear();
        const quint32 session = shared->session;
        const quint64 epoch = shared->epoch;
        liveVersion = shared->audioVersion;
        shared->lastStart = Clock::now();
        g_abortLive = false;
        lock.unlock();
        const Pass pass = engine.run(pcm, params, &g_abortLive, true);
        lock.lock();
        const bool current = !pass.aborted && !shared->quit && epoch == shared->epoch;
        lock.unlock();
        if (!current) {
            continue;
        }
        if (!pass.ok) {
            // La pasada final va a decir lo mismo con su ERROR; aca solo queda en el log.
            note(pass.error);
            continue;
        }
        if (DictationText::echoesContext(pass.text, params.vocabulary, params.prompt)) {
            continue; // "Glossary", "Nuke, Nuke, Nuke": el contexto repetido, no lo que se dijo
        }
        Message partial;
        partial.type = Type::Partial;
        partial.session = session;
        partial.audioMs = audioMs(pcm.size());
        partial.passMs = pass.ms;
        partial.text = pass.text;
        send(partial);
    }
    engine.close();
    return 0;
}

} // namespace DictationWorker
