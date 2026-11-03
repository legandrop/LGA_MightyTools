#include "platform/AudioCapture.h"

#include "core/AutomatedRun.h"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <functional>

namespace {

// PKEY_Device_FriendlyName (functiondiscoverykeys_devpkey.h): definido acá para no depender de INITGUID.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
constexpr REFERENCE_TIME kBufferHns = 1000000; // 100 ms
constexpr int kBlockSamples = AudioCapture::kSampleRate / 20; // 50 ms por bloque emitido
constexpr HRESULT kNotFound = HRESULT(0x80070490); // HRESULT_FROM_WIN32(ERROR_NOT_FOUND)

// Un puntero COM que se suelta solo.
template<class T>
struct Com
{
    T *p = nullptr;
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com() { reset(); }
    void reset()
    {
        if (p) p->Release();
        p = nullptr;
    }
    T *operator->() const { return p; }
    T **out()
    {
        reset();
        return &p;
    }
    void **outVoid() { return reinterpret_cast<void **>(out()); }
};

AudioCapture::Error errorOf(HRESULT hr)
{
    switch (hr) {
    case E_ACCESSDENIED:
        return AudioCapture::Error::AccessDenied;
    case AUDCLNT_E_DEVICE_IN_USE:
        return AudioCapture::Error::DeviceInUse;
    case AUDCLNT_E_DEVICE_INVALIDATED:
    case kNotFound:
        return AudioCapture::Error::DeviceGone;
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
        return AudioCapture::Error::UnsupportedFormat;
    default:
        return AudioCapture::Error::Other;
    }
}

bool isFloat32(const WAVEFORMATEX *format)
{
    if (format->wBitsPerSample != 32) return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    // WAVE_FORMAT_EXTENSIBLE: el subformato de IEEE float es {00000003-0000-0010-8000-00aa00389b71}.
    return format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22
        && reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format)->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
}

// El hilo de la grabación. No loguea (el manejador de logs de la app escribe sin cerrojo): todo lo que tiene
// que contar lo manda al hilo de la UI con `post`.
class CaptureThread : public QThread
{
public:
    using Post = std::function<void(std::function<void()>)>;

    CaptureThread(QString deviceId, AudioCapture *owner)
        : m_deviceId(std::move(deviceId))
        , m_owner(owner)
        , m_stop(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
    }

    ~CaptureThread() override
    {
        requestStop();
        wait();
        CloseHandle(m_stop);
    }

    void requestStop() { SetEvent(m_stop); }

    std::function<void(AudioCapture::Error, long)> onFailed;
    std::function<void()> onStarted;
    std::function<void(QVector<float>, float)> onBlock;

protected:
    void run() override
    {
        // Segundo cinturón: start() ya no arranca el hilo en una corrida automatizada.
        if (AutomatedRun::active()) return;
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        capture();
        if (SUCCEEDED(com)) CoUninitialize();
    }

private:
    void post(std::function<void()> call)
    {
        QMetaObject::invokeMethod(m_owner, std::move(call), Qt::QueuedConnection);
    }

    void fail(AudioCapture::Error error, HRESULT hr)
    {
        const auto handler = onFailed;
        post([handler, error, hr]() { handler(error, long(hr)); });
    }

    void capture()
    {
        Com<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                      enumerator.outVoid());
        if (FAILED(hr)) return fail(AudioCapture::Error::Other, hr);
        Com<IMMDevice> device;
        if (m_deviceId.isEmpty()) {
            hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, device.out());
            if (FAILED(hr)) return fail(hr == kNotFound ? AudioCapture::Error::NoMicrophone : errorOf(hr), hr);
        } else {
            hr = enumerator->GetDevice(reinterpret_cast<LPCWSTR>(m_deviceId.utf16()), device.out());
            DWORD state = 0;
            if (SUCCEEDED(hr)) hr = device->GetState(&state);
            if (FAILED(hr) || state != DEVICE_STATE_ACTIVE) return fail(AudioCapture::Error::DeviceGone, hr);
        }

        // Lo pedido: float32 mono 16 kHz, convertido por Windows.
        Com<IAudioClient> client;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.outVoid());
        if (FAILED(hr)) return fail(errorOf(hr), hr);
        WAVEFORMATEX wanted = {};
        wanted.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wanted.nChannels = 1;
        wanted.nSamplesPerSec = AudioCapture::kSampleRate;
        wanted.wBitsPerSample = 32;
        wanted.nBlockAlign = 4;
        wanted.nAvgBytesPerSec = AudioCapture::kSampleRate * 4;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                                    | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                kBufferHns, 0, &wanted, nullptr);
        std::unique_ptr<AudioCapture::Converter> converter;
        int channels = 1;
        const AudioCapture::Error first = errorOf(hr);
        if (FAILED(hr) && (first == AudioCapture::Error::UnsupportedFormat || first == AudioCapture::Error::Other)) {
            // Respaldo: el formato de mezcla del dispositivo (float32 en Windows moderno), convertido acá. Un
            // cliente nuevo: el que fallo al inicializarse no se reusa.
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.outVoid());
            WAVEFORMATEX *mix = nullptr;
            if (SUCCEEDED(hr)) hr = client->GetMixFormat(&mix);
            if (FAILED(hr)) return fail(errorOf(hr), hr);
            if (!isFloat32(mix)) {
                CoTaskMemFree(mix);
                return fail(AudioCapture::Error::UnsupportedFormat, AUDCLNT_E_UNSUPPORTED_FORMAT);
            }
            channels = mix->nChannels;
            converter = std::make_unique<AudioCapture::Converter>(int(mix->nSamplesPerSec), channels);
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, kBufferHns, 0, mix, nullptr);
            CoTaskMemFree(mix);
        }
        if (FAILED(hr)) return fail(errorOf(hr), hr);

        HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Com<IAudioCaptureClient> reader;
        hr = client->SetEventHandle(ready);
        if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioCaptureClient), reader.outVoid());
        if (SUCCEEDED(hr)) hr = client->Start();
        if (FAILED(hr)) {
            CloseHandle(ready);
            return fail(errorOf(hr), hr);
        }
        const auto started = onStarted;
        post([started]() { started(); });

        QVector<float> pending;
        const auto read = [&]() {
            UINT32 packet = 0;
            HRESULT result = S_OK;
            while (SUCCEEDED(result = reader->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                result = reader->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(result)) break;
                const bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
                if (converter) {
                    const QVector<float> zeros = silent ? QVector<float>(int(frames) * channels, 0.0f) : QVector<float>();
                    converter->push(silent ? zeros.constData() : reinterpret_cast<const float *>(data), int(frames), &pending);
                } else if (silent) {
                    pending.resize(pending.size() + int(frames), 0.0f);
                } else {
                    const auto *samples = reinterpret_cast<const float *>(data);
                    pending.append(QVector<float>(samples, samples + frames));
                }
                reader->ReleaseBuffer(frames);
            }
            return result;
        };
        const auto emitBlocks = [&](bool all) {
            while (pending.size() >= kBlockSamples || (all && !pending.isEmpty())) {
                const int size = qMin(int(pending.size()), kBlockSamples);
                QVector<float> block = pending.mid(0, size);
                pending.remove(0, size);
                const float level = AudioCapture::levelOf(block);
                const auto handler = onBlock;
                post([handler, block, level]() { handler(block, level); });
            }
        };

        HANDLE waits[2] = {m_stop, ready};
        for (;;) {
            const DWORD woke = WaitForMultipleObjects(2, waits, FALSE, 2000);
            if (woke == WAIT_OBJECT_0) break;
            if (woke == WAIT_FAILED) {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
            // Evento o espera vencida: leer igual (un dispositivo desconectado deja de avisar y lo dice acá).
            hr = read();
            if (FAILED(hr)) break;
            emitBlocks(false);
        }
        client->Stop();
        if (SUCCEEDED(hr)) {
            read(); // lo que quedaba en el búfer al soltar: el final de la última palabra
            emitBlocks(true);
        }
        CloseHandle(ready);
        if (FAILED(hr)) fail(errorOf(hr), hr);
    }

    const QString m_deviceId;
    AudioCapture *const m_owner;
    HANDLE m_stop;
};

} // namespace

struct AudioCapture::Private
{
    std::unique_ptr<CaptureThread> thread;
    quint64 generation = 0;
};

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

AudioCapture::~AudioCapture()
{
    // Sin señales desde el destructor: lo que quedaba en cola se descarta con el objeto.
    ++d->generation;
    d->thread.reset(); // pide parar y espera
}

QList<AudioCapture::Device> AudioCapture::devices()
{
    QList<Device> list;
    Com<IMMDeviceEnumerator> enumerator;
    Com<IMMDeviceCollection> collection;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                enumerator.outVoid()))
        || FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, collection.out()))) {
        return list;
    }
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        Com<IMMDevice> device;
        LPWSTR id = nullptr;
        if (FAILED(collection->Item(i, device.out())) || FAILED(device->GetId(&id))) continue;
        Device entry;
        entry.id = QString::fromWCharArray(id);
        CoTaskMemFree(id);
        Com<IPropertyStore> store;
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.out())) && SUCCEEDED(store->GetValue(kFriendlyName, &name))
            && name.vt == VT_LPWSTR) {
            entry.name = QString::fromWCharArray(name.pwszVal);
        }
        PropVariantClear(&name);
        list.append(entry);
    }
    return list;
}

bool AudioCapture::start(const QString &deviceId)
{
    if (AutomatedRun::active()) {
        qInfo() << "[dictation] (automatizada) el microfono no se abre";
        return false;
    }
    if (d->thread) {
        return false;
    }
    const quint64 generation = ++d->generation;
    d->thread = std::make_unique<CaptureThread>(deviceId, this);
    d->thread->onStarted = [this, generation]() {
        if (generation == d->generation && d->thread) {
            qInfo() << "[dictation] Microfono abierto";
            emit started();
        }
    };
    d->thread->onBlock = [this, generation](const QVector<float> &block, float value) {
        if (generation != d->generation) return;
        emit samples(block);
        emit level(value);
    };
    d->thread->onFailed = [this, generation](Error error, long code) {
        if (generation != d->generation) return;
        if (d->thread) {
            d->thread->wait();
            d->thread.reset();
        }
        qWarning() << "[dictation] Microfono:" << error << QStringLiteral("0x%1").arg(quint32(code), 8, 16, QLatin1Char('0'));
        emit failed(error, errorText(error, code));
    };
    d->thread->start(QThread::HighestPriority);
    qInfo() << "[dictation] Abriendo el microfono" << (deviceId.isEmpty() ? QStringLiteral("por defecto") : deviceId);
    return true;
}

void AudioCapture::stop()
{
    if (!d->thread) {
        return;
    }
    d->thread->requestStop();
    d->thread->wait();
    d->thread.reset();
    // Los últimos bloques (y un error de último momento) salen antes de volver: quien llama stop() ya tiene
    // todo el audio.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    qInfo() << "[dictation] Microfono cerrado";
}

bool AudioCapture::isRunning() const
{
    return d->thread != nullptr;
}
