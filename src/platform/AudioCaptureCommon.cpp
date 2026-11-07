#include "platform/AudioCapture.h"

#include "core/I18n.h"

#include <QtMath>

#include <cmath>

AudioCapture::Converter::Converter(int sampleRate, int channels)
    : m_step(double(qMax(1, sampleRate)) / kSampleRate)
    , m_channels(qMax(1, channels))
{
}

// shortcut: interpolación lineal sin filtro antialias (48 kHz -> 16 kHz deja pasar algo de 8-24 kHz al
// habla); alcanza para el dictado. Solo corre si Windows no convierte solo; si un micrófono da mala
// transcripción por esto, sumar un pasabajos antes.
void AudioCapture::Converter::push(const float *interleaved, int frames, QVector<float> *out)
{
    for (int frame = 0; frame < frames; ++frame) {
        float sample = 0.0f;
        for (int channel = 0; channel < m_channels; ++channel) {
            sample += interleaved[frame * m_channels + channel];
        }
        sample /= float(m_channels);
        // Las salidas que caen entre la muestra anterior (m_index - 1) y esta (m_index).
        while (m_next <= double(m_index)) {
            const float t = float(m_next - double(m_index - 1));
            out->append(m_previous * (1.0f - t) + sample * t); // con t = 1, la muestra exacta
            m_next += m_step;
        }
        m_previous = sample;
        ++m_index;
    }
}

float AudioCapture::levelOf(const QVector<float> &block)
{
    if (block.isEmpty()) {
        return 0.0f;
    }
    double sum = 0.0;
    for (const float sample : block) {
        sum += double(sample) * double(sample);
    }
    const double rms = std::sqrt(sum / block.size());
    if (rms <= 0.0) {
        return 0.0f;
    }
    return float(qBound(0.0, (20.0 * std::log10(rms) + 60.0) / 60.0, 1.0));
}

QString AudioCapture::errorText(Error error, long systemCode)
{
    switch (error) {
    case Error::None:
        return QString();
    case Error::NoMicrophone:
        return I18n::tr("No microphone found.");
    case Error::AccessDenied:
#if defined(Q_OS_MACOS)
        return I18n::tr("Allow the microphone for LGA Mighty Tools in System Settings > Privacy & Security > Microphone.");
#else
        return I18n::tr("Turn on microphone access for desktop apps in Windows Settings.");
#endif
    case Error::PermissionAsked:
        return I18n::tr("Allow the microphone in the macOS message, then press the shortcut again.");
    case Error::DeviceInUse:
        return I18n::tr("The microphone is in use by another app.");
    case Error::DeviceGone:
        return I18n::tr("The microphone is not connected.");
    case Error::UnsupportedFormat:
    case Error::Other:
        break;
    }
    return I18n::tr("Could not open the microphone (%1).").arg(QStringLiteral("0x%1").arg(quint32(systemCode), 8, 16, QLatin1Char('0')));
}
