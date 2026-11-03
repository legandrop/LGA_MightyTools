#include "modules/dictation/DictationProtocol.h"

#include <QtEndian>

#include <cmath>
#include <cstring>

namespace {

const QByteArray kMagic = QByteArrayLiteral("DCT1");
constexpr int kHeader = 4 + 4 + 1; // magic + largo + tipo

void putU32(QByteArray *out, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    out->append(bytes, 4);
}

void putText(QByteArray *out, const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    putU32(out, quint32(utf8.size()));
    out->append(utf8);
}

// Lectura del cuerpo de un mensaje: cualquier campo que no entra deja `ok` en false.
struct Reader
{
    const QByteArray &body;
    int pos = 0;
    bool ok = true;

    quint32 u32()
    {
        if (pos + 4 > body.size()) {
            ok = false;
            return 0;
        }
        const quint32 value = qFromLittleEndian<quint32>(body.constData() + pos);
        pos += 4;
        return value;
    }
    QString text()
    {
        const quint32 size = u32();
        if (!ok || size > quint32(body.size() - pos)) {
            ok = false;
            return {};
        }
        const QString value = DictationProtocol::cleanText(body.mid(pos, int(size)));
        pos += int(size);
        return value;
    }
};

bool known(quint8 type)
{
    return (type >= 1 && type <= 4) || (type >= 16 && type <= 19);
}

} // namespace

namespace DictationProtocol {

float loudestFrameDb(const float *pcm, size_t count)
{
    constexpr size_t kFrame = kSampleRate * 30 / 1000;
    double loudest = 0.0;
    for (size_t start = 0; start < count; start += kFrame) {
        const size_t end = qMin(count, start + kFrame);
        double sum = 0.0;
        for (size_t i = start; i < end; ++i) {
            sum += double(pcm[i]) * double(pcm[i]);
        }
        loudest = qMax(loudest, sum / double(end - start));
    }
    return loudest > 1e-12 ? float(10.0 * std::log10(loudest)) : -120.0f;
}

} // namespace DictationProtocol

namespace DictationProtocol {

QByteArray encode(const Message &message)
{
    QByteArray body;
    switch (message.type) {
    case Type::Begin:
        putU32(&body, message.session);
        putText(&body, message.language);
        putText(&body, message.prompt);
        putU32(&body, quint32(message.vocabulary.size()));
        for (const QString &term : message.vocabulary) {
            putText(&body, term);
        }
        break;
    case Type::Audio:
        // float32 little endian: el formato de la memoria en x86 y en ARM.
        body.resize(int(message.samples.size() * qsizetype(sizeof(float))));
        if (!message.samples.isEmpty()) {
            std::memcpy(body.data(), message.samples.constData(), size_t(body.size()));
        }
        break;
    case Type::End:
    case Type::Cancel:
        putU32(&body, message.session);
        break;
    case Type::Ready:
        putU32(&body, message.passMs);
        putText(&body, message.text);
        break;
    case Type::Partial:
    case Type::Final:
        putU32(&body, message.session);
        putU32(&body, message.audioMs);
        putU32(&body, message.passMs);
        putText(&body, message.text);
        break;
    case Type::Error:
        putU32(&body, message.session);
        putText(&body, message.text);
        break;
    }
    QByteArray frame = kMagic;
    putU32(&frame, quint32(body.size()));
    frame.append(char(message.type));
    frame.append(body);
    return frame;
}

QList<Message> decode(QByteArray *buffer)
{
    QList<Message> messages;
    for (;;) {
        // Resincronizar: lo que hay antes de "DCT1" no es del protocolo.
        const int start = buffer->indexOf(kMagic);
        if (start < 0) {
            // Se guardan los ultimos 3 bytes por si son el principio de un "DCT1" cortado.
            buffer->remove(0, qMax(0, int(buffer->size()) - 3));
            return messages;
        }
        buffer->remove(0, start);
        if (buffer->size() < kHeader) {
            return messages;
        }
        const quint32 size = qFromLittleEndian<quint32>(buffer->constData() + 4);
        const quint8 type = quint8(buffer->at(8));
        if (size > kMaxBody || !known(type)) {
            buffer->remove(0, 1); // no era un mensaje: seguir buscando
            continue;
        }
        if (buffer->size() < kHeader + int(size)) {
            return messages;
        }
        const QByteArray body = buffer->mid(kHeader, int(size));
        buffer->remove(0, kHeader + int(size));

        Message message;
        message.type = Type(type);
        Reader in{body};
        switch (message.type) {
        case Type::Begin: {
            message.session = in.u32();
            message.language = in.text();
            message.prompt = in.text();
            const quint32 count = in.u32();
            for (quint32 i = 0; in.ok && i < count; ++i) {
                message.vocabulary << in.text();
            }
            break;
        }
        case Type::Audio:
            if (body.size() % int(sizeof(float)) != 0) {
                in.ok = false;
                break;
            }
            message.samples.resize(body.size() / int(sizeof(float)));
            if (!body.isEmpty()) {
                std::memcpy(message.samples.data(), body.constData(), size_t(body.size()));
            }
            break;
        case Type::End:
        case Type::Cancel:
            message.session = in.u32();
            break;
        case Type::Ready:
            message.passMs = in.u32();
            message.text = in.text();
            break;
        case Type::Partial:
        case Type::Final:
            message.session = in.u32();
            message.audioMs = in.u32();
            message.passMs = in.u32();
            message.text = in.text();
            break;
        case Type::Error:
            message.session = in.u32();
            message.text = in.text();
            break;
        }
        if (in.ok) {
            messages << message;
        }
    }
}

QString cleanText(const QByteArray &utf8)
{
    QString text = QString::fromUtf8(utf8);
    text.remove(QChar(QChar::ReplacementCharacter));
    return text;
}

} // namespace DictationProtocol
