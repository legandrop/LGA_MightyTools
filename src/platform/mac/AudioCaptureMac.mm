#include "platform/AudioCapture.h"

#include "core/AutomatedRun.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>

#include <memory>
#include <mutex>
#include <vector>

// Micrófono del dictado en macOS: AVAudioEngine con un tap en la entrada. El formato del micrófono
// (48 kHz, uno o dos canales, float) se lleva a 16 kHz mono con AudioCapture::Converter, en el hilo de audio
// del sistema, y los bloques de ~50 ms se mandan al hilo de la UI.
//
// Permiso: el de Micrófono de Privacidad y seguridad (TCC). La primera vez macOS pregunta (el texto es
// NSMicrophoneUsageDescription del Info.plist); mientras pregunta no se graba nada, y si el usuario dice que
// sí, la grabación arranca recién en el próximo dictado (el atajo ya se soltó). Si dijo que no, failed()
// con AccessDenied y el texto que lleva a Ajustes.
//
// El motor de audio existe SOLO entre start() y stop(): al cerrarlo macOS apaga el punto naranja del
// micrófono. En una corrida automatizada start() no abre nada (core/AutomatedRun.h).

namespace {

constexpr int kBlockSamples = AudioCapture::kSampleRate / 20; // 50 ms por bloque emitido

QString cfString(CFStringRef value)
{
    return value ? QString::fromCFString(value) : QString();
}

QString deviceString(AudioObjectID device, AudioObjectPropertySelector selector)
{
    AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) != noErr || !value) {
        return QString();
    }
    const QString text = cfString(value);
    CFRelease(value);
    return text;
}

bool hasInput(AudioObjectID device)
{
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    return AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) == noErr && size > 0;
}

std::vector<AudioObjectID> allDevices()
{
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr || size == 0) {
        return {};
    }
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, ids.data()) != noErr) {
        return {};
    }
    ids.resize(size / sizeof(AudioObjectID));
    return ids;
}

AudioObjectID deviceByUid(const QString &uid)
{
    for (const AudioObjectID device : allDevices()) {
        if (hasInput(device) && deviceString(device, kAudioDevicePropertyDeviceUID) == uid) {
            return device;
        }
    }
    return kAudioObjectUnknown;
}

// Lo que comparten el hilo de audio (el tap) y el de la UI. Todo lo del tap, incluido encolar los bloques,
// pasa con el mutex tomado y solo si `closed` esta apagado: cuando closeEngine() lo prende, ya no puede
// llegar nada mas (AVAudioEngine no garantiza que removeTapOnBus espere a un bloque en curso), y lo que
// quedo en `pending` es todo el resto del audio.
struct Shared
{
    std::mutex mutex;
    bool closed = false;
    AudioCapture *owner = nullptr; // solo para encolar: lo encolado se descarta si el objeto se destruye
    quint64 generation = 0;
    std::unique_ptr<AudioCapture::Converter> converter;
    double converterRate = 0.0;
    int converterChannels = 0;
    QVector<float> pending;
    std::vector<float> interleaved;
};

} // namespace

struct AudioCapture::Private
{
    AVAudioEngine *engine = nil;
    id observer = nil;
    std::shared_ptr<Shared> shared;
    quint64 generation = 0;
    bool running = false;
    bool waitingPermission = false;
    int restarts = 0; // rearranques por un cambio de configuracion (auriculares Bluetooth) en este dictado

    void closeEngine()
    {
        if (shared) {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->closed = true;
        }
        if (observer) {
            [[NSNotificationCenter defaultCenter] removeObserver:observer];
            [observer release];
            observer = nil;
        }
        if (engine) {
            @try {
                [engine.inputNode removeTapOnBus:0];
                [engine stop];
            } @catch (NSException *) {
            }
            [engine release];
            engine = nil;
        }
    }

    // El tap con el formato que tenga ahora la entrada (format:nil = el de su salida, que siempre coincide).
    // False si AVAudioEngine lo rechaza (lanza NSException, no devuelve error).
    bool installTap()
    {
        std::shared_ptr<Shared> state = shared;
        AVAudioInputNode *input = engine.inputNode;
        @try {
            [input installTapOnBus:0 bufferSize:4096 format:nil block:^(AVAudioPCMBuffer *buffer, AVAudioTime *) {
                const int frames = int(buffer.frameLength);
                const int channels = int(buffer.format.channelCount);
                float *const *data = buffer.floatChannelData;
                if (frames <= 0 || channels <= 0 || !data) return;
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->closed) return;
                const double rate = buffer.format.sampleRate;
                if (!state->converter || state->converterRate != rate || state->converterChannels != channels) {
                    state->converter = std::make_unique<AudioCapture::Converter>(int(rate + 0.5), channels);
                    state->converterRate = rate;
                    state->converterChannels = channels;
                }
                state->interleaved.resize(size_t(frames) * size_t(channels));
                const bool interleaved = buffer.format.isInterleaved;
                for (int frame = 0; frame < frames; ++frame) {
                    for (int channel = 0; channel < channels; ++channel) {
                        state->interleaved[size_t(frame * channels + channel)] =
                            interleaved ? data[0][frame * channels + channel] : data[channel][frame];
                    }
                }
                state->converter->push(state->interleaved.data(), frames, &state->pending);
                AudioCapture *owner = state->owner;
                const quint64 generation = state->generation;
                while (state->pending.size() >= kBlockSamples) {
                    const QVector<float> block = state->pending.mid(0, kBlockSamples);
                    state->pending.remove(0, kBlockSamples);
                    const float value = AudioCapture::levelOf(block);
                    QMetaObject::invokeMethod(owner, [owner, generation, block, value]() {
                        if (generation != owner->d->generation) return;
                        emit owner->samples(block);
                        emit owner->level(value);
                    }, Qt::QueuedConnection);
                }
            }];
            return true;
        } @catch (NSException *exception) {
            qWarning() << "[dictation] Microfono: AVAudioEngine rechazo el tap:"
                       << QString::fromNSString(exception.reason ?: @"?");
            return false;
        }
    }

    // prepare + start, sin dejar escapar una NSException. 0 si arranco; si no, el codigo del error.
    long startEngine()
    {
        @try {
            [engine prepare];
            NSError *error = nil;
            if ([engine startAndReturnError:&error]) return 0;
            return error ? long(error.code) : -1;
        } @catch (NSException *) {
            return -1;
        }
    }
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
    @autoreleasepool {
        d->closeEngine();
    }
}

QList<AudioCapture::Device> AudioCapture::devices()
{
    QList<Device> list;
    for (const AudioObjectID device : allDevices()) {
        if (!hasInput(device)) continue;
        Device entry;
        entry.id = deviceString(device, kAudioDevicePropertyDeviceUID);
        entry.name = deviceString(device, kAudioObjectPropertyName);
        if (entry.id.isEmpty()) continue;
        if (entry.name.isEmpty()) entry.name = entry.id;
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
    if (d->running) {
        return false;
    }
    const quint64 generation = ++d->generation;
    const QPointer<AudioCapture> self(this);
    const auto fail = [self, generation](Error error, long code) {
        // Siempre encolado: failed() llega despues de que start() volvio, como en Windows.
        QMetaObject::invokeMethod(self, [self, generation, error, code]() {
            if (!self || generation != self->d->generation) return;
            @autoreleasepool {
                self->d->closeEngine();
            }
            self->d->running = false;
            qWarning() << "[dictation] Microfono:" << error << code;
            emit self->failed(error, errorText(error, code));
        }, Qt::QueuedConnection);
    };

    if (d->waitingPermission) {
        // El cartel de macOS sigue abierto: se repite el mismo aviso, sin volver a preguntar.
        d->running = true;
        fail(Error::PermissionAsked, 0);
        return true;
    }
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status == AVAuthorizationStatusNotDetermined) {
        // macOS muestra su pregunta; este dictado no graba. La respuesta vale para el proximo.
        d->waitingPermission = true;
        qInfo() << "[dictation] Pidiendo el permiso de microfono";
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
            QMetaObject::invokeMethod(self, [self, granted]() {
                if (!self) return;
                self->d->waitingPermission = false;
                qInfo() << "[dictation] Permiso de microfono:" << (granted ? "concedido" : "negado");
            }, Qt::QueuedConnection);
        }];
        fail(Error::PermissionAsked, 0);
        d->running = true; // hasta que llegue el failed() encolado
        return true;
    }
    if (status != AVAuthorizationStatusAuthorized) {
        d->running = true;
        fail(Error::AccessDenied, 0);
        return true;
    }

    @autoreleasepool {
        if (allDevices().empty() || devices().isEmpty()) {
            d->running = true;
            fail(Error::NoMicrophone, 0);
            return true;
        }
        d->engine = [[AVAudioEngine alloc] init];
        AVAudioInputNode *input = d->engine.inputNode;
        if (!deviceId.isEmpty()) {
            AudioObjectID device = deviceByUid(deviceId);
            if (device == kAudioObjectUnknown) {
                d->running = true;
                fail(Error::DeviceGone, 0);
                return true;
            }
            const OSStatus set = AudioUnitSetProperty(input.audioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                                      kAudioUnitScope_Global, 0, &device, sizeof(device));
            if (set != noErr) {
                d->running = true;
                fail(Error::Other, long(set));
                return true;
            }
        }
        AVAudioFormat *hardware = [input inputFormatForBus:0];
        if (!hardware || hardware.sampleRate <= 0.0 || hardware.channelCount == 0) {
            d->running = true;
            fail(Error::NoMicrophone, 0);
            return true;
        }

        auto shared = std::make_shared<Shared>();
        shared->owner = this;
        shared->generation = generation;
        d->shared = shared;
        d->restarts = 0;
        if (!d->installTap()) {
            d->running = true;
            fail(Error::UnsupportedFormat, 0);
            return true;
        }

        // Se desconecto el microfono, cambio el de por defecto o unos auriculares Bluetooth cambiaron de perfil al
        // abrir su microfono: el motor de audio se detiene solo. Se rearma una vez con el formato nuevo; si no
        // anda, el dictado se corta (DeviceGone). El aviso llega en cualquier hilo: se pasa al de la UI.
        AudioCapture *owner = this;
        d->observer = [[[NSNotificationCenter defaultCenter]
            addObserverForName:AVAudioEngineConfigurationChangeNotification
                        object:d->engine
                         queue:nil
                    usingBlock:^(NSNotification *) {
                        std::lock_guard<std::mutex> lock(shared->mutex);
                        if (shared->closed) return;
                        QMetaObject::invokeMethod(owner, [owner, generation]() {
                            Private *p = owner->d.get();
                            if (generation != p->generation || !p->running || !p->engine || p->engine.isRunning) return;
                            bool restarted = false;
                            if (p->restarts < 1) {
                                ++p->restarts;
                                @autoreleasepool {
                                    @try {
                                        [p->engine.inputNode removeTapOnBus:0];
                                    } @catch (NSException *) {
                                    }
                                    restarted = p->installTap() && p->startEngine() == 0;
                                }
                                qInfo() << "[dictation] Microfono: cambio la configuracion de audio;"
                                        << (restarted ? "rearmado" : "no se pudo rearmar");
                            }
                            if (restarted) return;
                            ++p->generation;
                            @autoreleasepool {
                                p->closeEngine();
                            }
                            p->running = false;
                            qWarning() << "[dictation] Microfono: se corto (cambio la configuracion de audio)";
                            emit owner->failed(Error::DeviceGone, errorText(Error::DeviceGone));
                        }, Qt::QueuedConnection);
                    }] retain];

        const long code = d->startEngine();
        if (code != 0) {
            d->running = true;
            fail(code == kAudioHardwareIllegalOperationError ? Error::DeviceInUse : Error::Other, code);
            return true;
        }
    }
    d->running = true;
    qInfo() << "[dictation] Microfono abierto" << (deviceId.isEmpty() ? QStringLiteral("por defecto") : deviceId);
    return true;
}

void AudioCapture::stop()
{
    if (!d->running) {
        return;
    }
    d->running = false;
    @autoreleasepool {
        d->closeEngine();
    }
    // Lo que quedo en el conversor (menos de un bloque) sale ahora; los bloques ya encolados, antes de volver:
    // quien llama stop() ya tiene todo el audio.
    QVector<float> rest;
    if (d->shared) {
        std::lock_guard<std::mutex> lock(d->shared->mutex);
        rest.swap(d->shared->pending);
    }
    d->shared.reset();
    // Los bloques ya encolados y un error de ultimo momento (permiso, sin microfono) salen ahora, como en Windows.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    if (!rest.isEmpty()) {
        emit samples(rest);
        emit level(levelOf(rest));
    }
    // Un bloque que el hilo de audio mande despues de cerrar ya no corresponde a este dictado.
    ++d->generation;
    qInfo() << "[dictation] Microfono cerrado";
}

bool AudioCapture::isRunning() const
{
    return d->running;
}
