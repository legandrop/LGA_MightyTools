// UserChoiceLatest: hash de asociaciones de archivos de Windows 11. Ver licencia (MIT, PS-SFTA y
// DefaultApps) y organizacion en UserChoiceLatest.h.
//
// El empaquetado (encodeHashString) y el hash (hashBytes) son una traduccion linea por linea del
// original, con sus rarezas incluidas: el objetivo es dar EXACTAMENTE el mismo resultado que el
// helper, no corregirlo. Las reglas de C# que cambian el resultado y que aca se imitan a mano:
//   - los accesos fuera de rango tiran excepcion (aca: Fault -> HashError);
//   - "1L << n" y "x << n" en long usan n & 63;
//   - la aritmetica int/long es modular (unchecked): aca se hace en enteros sin signo;
//   - comparar int con uint promueve ambos a long: aca se compara en qint64.

#include "modules/openinnukex/win/UserChoiceLatest.h"

#include <QChar>
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QThread>

#include <windows.h>
#include <sddl.h>
#include <shlobj.h>

#include <cstring>
#include <memory>
#include <vector>

namespace UserChoiceLatest {

namespace {

// ─── Tablas del empaquetado ─────────────────────────────────────────────────

// Largo en bits de un byte (indice 0..255).
const quint8 kLut1[256] = {
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};

// Largo en bits (0..32) -> selector inicial.
const quint8 kLut2[33] = {
    0x00, 0x02, 0x06, 0x07, 0x0A, 0x18, 0x19, 0x1D,
    0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D,
    0x1D, 0x1D, 0xF5, 0xF5, 0xFA, 0xFA, 0xFA, 0xFA,
    0xFA, 0xFA, 0xFA, 0xFA, 0xFA, 0xFB, 0xFB, 0xFB,
    0xFB,
};

// Selector -> cantidad de caracteres que entran en la palabra.
const quint8 kLut3[252] = {
    0xA8, 0x70, 0x6F, 0x53, 0x38, 0x20, 0x1C, 0x13,
    0x13, 0x12, 0x12, 0x12, 0x10, 0x0F, 0x0F, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B,
    0x0B, 0x0B, 0x0B, 0x0A, 0x0A, 0x0A, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x02, 0x01,
};

// Selector x posicion -> ancho en bits del caracter en esa posicion (168 por selector).
const quint8 kLut4[252 * 168] = {
#include "UserChoiceLatestLut4.inc"
};

// Caracteres restantes -> selector de la rama de cola.
const quint8 kLut5[176] = {
    0x00, 0xFB, 0xFA, 0xF5, 0xB4, 0x6D, 0x4C, 0x35,
    0x2B, 0x1E, 0x1B, 0x17, 0x15, 0x13, 0x0F, 0x0D,
    0x0C, 0x0C, 0x09, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x02,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Orden de los seis campos segun el indice del MachineId. Campos:
// 0 extension, 1 SID, 2 ProgID, 3 marca de tiempo, 4 cadena de validacion, 5 MachineId.
const int kFieldOrders[3][6] = {
    {4, 0, 3, 5, 2, 1},
    {4, 3, 0, 1, 5, 2},
    {1, 3, 4, 0, 5, 2},
};

// Final de cada cadena de validacion y MD5 (hex) de la cadena completa en minusculas (UTF-16).
struct ValidationKey {
    const char *suffix;
    const char *md5;
};
const ValidationKey kValidationKeys[3] = {
    {"{3822b7ca-c2f4-4889-b8cc-4ce39a8fb81c}", "b00567862f7d98e19852d440a015240c"},
    {"{d185e0a1-e265-4724-aa21-3a17b038d72e}", "25109076282e50ca419f1fdd0066a1a1"},
    {"{97b6bcf4-c367-4577-95be-73bd3053a5e0}", "467c6722ee84ac9b86a33fd34e62378b"},
};
constexpr int kValidationChars = 83;

const char16_t kUserExperienceFixed[] =
    u"User Choice set via Windows User Experience {D18B6DD5-6124-4341-9318-804003BAFA0B}";
const char16_t kUserExperiencePrefix[] = u"User Choice set via Windows User Experience";

const wchar_t kFileExtsPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\";

// ─── Acceso con verificacion de rango ───────────────────────────────────────

struct Fault {
    HashError error;
};

quint8 lut(const quint8 *table, qint64 size, qint64 index)
{
    if (index < 0 || index >= size) {
        throw Fault{HashError::IndexOutOfRange};
    }
    return table[index];
}

quint8 lut1(qint64 i) { return lut(kLut1, 256, i); }
quint8 lut2(qint64 i) { return lut(kLut2, 33, i); }
quint8 lut3(qint64 i) { return lut(kLut3, 252, i); }
quint8 lut4(qint64 i) { return lut(kLut4, 252 * 168, i); }
quint8 lut5(qint64 i) { return lut(kLut5, 176, i); }

// "1L << n" de C#: el corrimiento usa n & 63 y el resultado es long con signo.
qint64 oneShl(quint8 n)
{
    return static_cast<qint64>(quint64(1) << (n & 63));
}

// "(long)x << n" de C#, modular.
quint64 shl64(quint64 value, int n)
{
    return value << (n & 63);
}

// BitConverter.ToInt32 con sus mismas excepciones.
qint32 readInt32(const QByteArray &bytes, qint64 index)
{
    const qint64 size = bytes.size();
    if (index < 0 || index >= size) {
        throw Fault{HashError::ArgumentOutOfRange};
    }
    if (index > size - 4) {
        throw Fault{HashError::Argument};
    }
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData()) + index;
    const quint32 v = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) |
                      (quint32(p[3]) << 24);
    return static_cast<qint32>(v);
}

void appendInt32(QByteArray *out, quint32 v)
{
    out->append(char(v & 0xFF));
    out->append(char((v >> 8) & 0xFF));
    out->append(char((v >> 16) & 0xFF));
    out->append(char((v >> 24) & 0xFF));
}

// ShiftRight de PS-SFTA: para negativos, (x >> n) ^ 0xFFFF0000 truncado a 32 bits.
qint32 shiftRight(qint32 value, int count)
{
    if (value < 0) {
        return static_cast<qint32>(static_cast<quint32>(value >> count) ^ 0xFFFF0000u);
    }
    return value >> count;
}

// GetByteLength del original: cantidad de unidades de 16 bits antes del primer par en cero.
qint64 byteLength(const QByteArray &bytes)
{
    const qint64 size = bytes.size();
    auto at = [&](qint64 i) -> uchar {
        if (i < 0 || i >= size) {
            throw Fault{HashError::IndexOutOfRange};
        }
        return static_cast<uchar>(bytes.at(i));
    };
    qint64 n = 0;
    if (at(0) != 0) {
        n = -1;
        do {
            ++n;
        } while (at(n * 2) != 0 || at(n * 2 + 1) != 0);
    }
    return n;
}

QByteArray utf16Bytes(const QString &text)
{
    QByteArray out;
    out.reserve(text.size() * 2);
    for (const QChar c : text) {
        out.append(char(c.unicode() & 0xFF));
        out.append(char(c.unicode() >> 8));
    }
    return out;
}

// Decodifica UTF-16LE como Encoding.Unicode: cada sustituto suelto pasa a U+FFFD.
QString decodeUtf16(const uchar *p, qint64 units)
{
    QString out;
    out.reserve(int(units));
    for (qint64 i = 0; i < units; ++i) {
        const char16_t c = char16_t(p[i * 2] | (p[i * 2 + 1] << 8));
        if (QChar::isHighSurrogate(c)) {
            if (i + 1 < units) {
                const char16_t d = char16_t(p[(i + 1) * 2] | (p[(i + 1) * 2 + 1] << 8));
                if (QChar::isLowSurrogate(d)) {
                    out.append(QChar(c));
                    out.append(QChar(d));
                    ++i;
                    continue;
                }
            }
            out.append(QChar(0xFFFD));
        } else if (QChar::isLowSurrogate(c)) {
            out.append(QChar(0xFFFD));
        } else {
            out.append(QChar(c));
        }
    }
    return out;
}

QString systemDirectory()
{
    wchar_t buffer[MAX_PATH] = {};
    const UINT len = GetSystemDirectoryW(buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return QStringLiteral("C:\\Windows\\System32");
    }
    return QString::fromWCharArray(buffer, int(len));
}

QByteArray readWholeFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

// ─── Registro (solo lo que usa la escritura) ───────────────────────────────

struct RegKey {
    HKEY h = nullptr;
    ~RegKey()
    {
        if (h) {
            RegCloseKey(h);
        }
    }
};

LONG setString(HKEY key, const wchar_t *name, const QString &value)
{
    const std::wstring w = value.toStdWString();
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(w.c_str()),
                          DWORD((w.size() + 1) * sizeof(wchar_t)));
}

QStringList valueNames(HKEY root, const std::wstring &path)
{
    QStringList out;
    RegKey key;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key.h) != ERROR_SUCCESS) {
        return out;
    }
    for (DWORD i = 0;; ++i) {
        wchar_t name[16384];
        DWORD len = 16384;
        const LONG rc = RegEnumValueW(key.h, i, name, &len, nullptr, nullptr, nullptr, nullptr);
        if (rc != ERROR_SUCCESS) {
            break;
        }
        out << QString::fromWCharArray(name, int(len));
    }
    return out;
}

QStringList subKeyNames(HKEY root, const std::wstring &path)
{
    QStringList out;
    RegKey key;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key.h) != ERROR_SUCCESS) {
        return out;
    }
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        DWORD len = 256;
        const LONG rc = RegEnumKeyExW(key.h, i, name, &len, nullptr, nullptr, nullptr, nullptr);
        if (rc != ERROR_SUCCESS) {
            break;
        }
        out << QString::fromWCharArray(name, int(len));
    }
    return out;
}

// Ultima escritura de la clave (FILETIME UTC, 100 ns). 0 si falla.
quint64 lastWriteTime(HKEY key)
{
    FILETIME ft{};
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr, &ft) != ERROR_SUCCESS) {
        return 0;
    }
    return (quint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// Valor REG_SZ de una clave abierta. Vacio si no existe.
QString readStringValue(HKEY key, const wchar_t *name)
{
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        return QString();
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(buffer.data()),
                         &size) != ERROR_SUCCESS) {
        return QString();
    }
    return QString::fromWCharArray(buffer.data());
}

// Valor REG_SZ por ruta bajo HKCU. Vacio si no existe.
QString readUserString(const std::wstring &path, const wchar_t *name)
{
    RegKey key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key.h) != ERROR_SUCCESS) {
        return QString();
    }
    return readStringValue(key.h, name);
}

} // namespace

// ─── 1. Funciones puras ─────────────────────────────────────────────────────

QString hashErrorName(HashError error)
{
    switch (error) {
    case HashError::None:
        return QString();
    case HashError::IndexOutOfRange:
        return QStringLiteral("IndexOutOfRangeException");
    case HashError::ArgumentOutOfRange:
        return QStringLiteral("ArgumentOutOfRangeException");
    case HashError::Argument:
        return QStringLiteral("ArgumentException");
    case HashError::RepeatedWord:
        return QStringLiteral("RepeatedWord");
    case HashError::EmptyInput:
        return QStringLiteral("EmptyInput");
    }
    return QString();
}

QString hexFileTime(quint64 fileTime)
{
    return QStringLiteral("%1%2")
        .arg(quint32(fileTime >> 32), 8, 16, QLatin1Char('0'))
        .arg(quint32(fileTime & 0xFFFFFFFFu), 8, 16, QLatin1Char('0'));
}

QString trimMachineId(const QString &rawMachineId)
{
    // TrimStart('{').TrimEnd('}'): quita todas las llaves de cada punta.
    int begin = 0;
    int end = rawMachineId.size();
    while (begin < end && rawMachineId.at(begin) == QLatin1Char('{')) {
        ++begin;
    }
    while (end > begin && rawMachineId.at(end - 1) == QLatin1Char('}')) {
        --end;
    }
    return rawMachineId.mid(begin, end - begin);
}

int validationIndex(const QString &machineId)
{
    if (machineId.isEmpty()) {
        return 0;
    }
    return int(quint32(machineId.at(machineId.size() - 1).unicode()) % 3u);
}

QString buildHashString(const QString &machineId, const QString &userSid,
                        const QString &extension, const QString &progId, quint64 fileTime,
                        const ValidationStrings &validation)
{
    const int index = validationIndex(machineId);
    const QString fields[6] = {
        extension, userSid, progId, hexFileTime(fileTime), validation[size_t(index)], machineId,
    };
    QString out;
    for (int i = 0; i < 6; ++i) {
        out += fields[kFieldOrders[index][i]];
    }
    return out;
}

QString toLowerInvariant(const QString &text)
{
    QString out = text;
    const int n = out.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = out.at(i);
        if (c.isHighSurrogate() && i + 1 < n && out.at(i + 1).isLowSurrogate()) {
            const char32_t cp = QChar::surrogateToUcs4(c, out.at(i + 1));
            const char32_t low = QChar::toLower(cp);
            // Solo se acepta un reemplazo que conserve el largo en UTF-16.
            if (QChar::requiresSurrogates(low)) {
                out[i] = QChar(QChar::highSurrogate(low));
                out[i + 1] = QChar(QChar::lowSurrogate(low));
            }
            ++i;
            continue;
        }
        const char32_t low = QChar::toLower(char32_t(c.unicode()));
        if (!QChar::requiresSurrogates(low)) {
            out[i] = QChar(char16_t(low));
        }
    }
    return out;
}

EncodeResult encodeHashString(const QString &text, EncodeTrace *trace)
{
    EncodeResult result;
    EncodeTrace localTrace;
    EncodeTrace &tr = trace ? *trace : localTrace;
    tr = EncodeTrace();

    const qint64 inputSize = text.size();
    std::vector<quint32> input(static_cast<size_t>(inputSize));
    for (qint64 i = 0; i < inputSize; ++i) {
        input[size_t(i)] = text.at(int(i)).unicode();
    }
    auto in = [&](qint64 i) -> quint32 {
        if (i < 0 || i >= inputSize) {
            throw Fault{HashError::IndexOutOfRange};
        }
        return input[size_t(i)];
    };

    const int hashLength = int(inputSize);
    QByteArray words((hashLength + 1) * 16, '\0');
    const int halfLength = (hashLength + 1) / 8 + 1;
    const int roundLength = ((hashLength + 1) / 8) * 8;
    int hashLength2 = roundLength;
    int index = 0;     // indexIdkHelp: proximo caracter a empaquetar
    int wordIndex = 0; // indexGrr: proxima palabra

    auto writeWord = [&](quint64 value) {
        for (int i = 0; i < 8; ++i) {
            const qint64 pos = qint64(wordIndex) * 8 + i;
            if (pos < 0 || pos >= words.size()) {
                throw Fault{HashError::IndexOutOfRange};
            }
            words[int(pos)] = char((value >> (8 * i)) & 0xFF);
        }
    };

    try {
        do {
            const quint32 ch = in(index);
            quint32 bits = 0;
            const quint32 third = ch >> 16;
            if (third == 0) {
                const quint32 second = ch >> 8;
                bits = (second == 0) ? lut1(ch) : quint32(lut1(second)) + 8;
            } else {
                const quint32 fourth = ch >> 24;
                bits = (fourth == 0) ? quint32(lut1(third)) + 16 : quint32(lut1(fourth)) + 24;
            }
            qint64 selector = lut2(bits);
            quint32 count = lut3(selector);

            if (qint64(hashLength2) < qint64(count)) {
                // Rama de cola.
                int sel = hashLength - index;
                int next = 0;
                while (index < hashLength) {
                    sel = lut5(sel);
                    qint64 row = qint64(sel * 0xa8);
                    qint64 k = 0;
                    for (;;) {
                        k = 0;
                        do {
                            count = lut3(sel);
                            const qint64 limit = oneShl(lut4(row + k));
                            const quint32 c = in(k + index);
                            if (limit <= qint64(c)) {
                                break;
                            }
                            next = int(k) + 1;
                            k = qint64(quint32(next));
                        } while (qint64(next) < qint64(count));
                        if (k != qint64(count)) {
                            do {
                                sel = sel + 1;
                                row = qint64(sel) * 0xa8;
                                const qint64 limit = oneShl(lut4(row + k));
                                const quint32 c = in(k + index);
                                if (qint64(c) < limit) {
                                    break;
                                }
                            } while (k < qint64(lut3(sel)));
                            continue;
                        }
                        break;
                    }
                    quint64 acc = 0;
                    int shift = 0x38;
                    k = 0;
                    do {
                        next = int(k) + 1;
                        shift = shift - lut4(k + qint64(sel) * 0xa8);
                        acc = acc + shl64(quint64(qint64(in(k + index))), shift);
                        k = qint64(quint32(next));
                    } while (qint64(next) < qint64(count));
                    quint64 word = (quint64(qint64(sel)) << 0x38) + acc;
                    ++tr.tailWords;
                    if (hashLength + 1 == roundLength) {
                        // Ajuste del original para este caso de borde.
                        const quint16 low = quint16(word & 0xFFFF);
                        word = word >> 16;
                        word += low;
                        ++tr.hackfixWords;
                    }
                    // El original no avanza wordIndex en esta rama.
                    writeWord(word);
                    index = index + lut3(sel);
                    if (hashLength2 < 8) {
                        hashLength2 = 0;
                    } else {
                        hashLength2 -= 8;
                    }
                }
            } else {
                // Rama principal.
                qint64 row = qint64(quint8(selector)) * 0xa8;
                qint64 k = 0;
                for (;;) {
                    k = 0;
                    do {
                        count = lut3(selector);
                        const qint64 limit = oneShl(lut4(row + k));
                        const quint32 c = in(k + index);
                        if (limit <= qint64(c)) {
                            break;
                        }
                        ++k;
                    } while (k < qint64(count));
                    if (k != qint64(count)) {
                        do {
                            selector = selector + 1;
                            row = selector * 0xa8;
                            const qint64 limit = oneShl(lut4(row + k));
                            const quint32 c = in(k + index);
                            if (qint64(c) < limit) {
                                break;
                            }
                        } while (k < qint64(lut3(selector)));
                        continue;
                    }
                    break;
                }
                quint64 acc = 0;
                int shift = 0x38;
                k = 0;
                quint32 next = 0;
                do {
                    next = quint32(k) + 1;
                    shift = shift - lut4(selector * 0xa8 + k);
                    acc = acc + shl64(quint64(qint64(in(k + index))), shift);
                    k = next;
                } while (next < count);
                const quint64 word = (quint64(selector) << 0x38) + acc;
                writeWord(word);
                ++tr.normalWords;
                hashLength2 -= 8;
                ++wordIndex;
                index = index + lut3(selector);
            }
        } while (index < hashLength);
    } catch (const Fault &fault) {
        result.error = fault.error;
        return result;
    }

    // Cabecera: 2 bytes bajos del largo y 2 bytes bajos de halfLength.
    QByteArray out;
    out.reserve(4 + words.size());
    out.append(char(hashLength & 0xFF));
    out.append(char((hashLength >> 8) & 0xFF));
    out.append(char(halfLength & 0xFF));
    out.append(char((halfLength >> 8) & 0xFF));
    out.append(words);
    result.bytes = out;
    return result;
}

HashResult hashBytes(const QByteArray &bytes, int lengthBase)
{
    HashResult result;
    const QByteArray md5 = QCryptographicHash::hash(bytes, QCryptographicHash::Md5);

    const bool condition = ((lengthBase & 4) <= 1);
    const int length = (condition ? 1 : 0) + (lengthBase >> 2) - 1;
    if (length <= 1) {
        return result; // el original devuelve "" sin error
    }

    try {
        qint64 pdata = 0;
        quint32 cache = 0;
        quint32 out1 = 0;
        quint32 out2 = 0;

        // Primera pasada.
        quint32 h1 = (quint32(readInt32(md5, 0)) | 1u) + 0x69FB0000u;
        quint32 h2 = (quint32(readInt32(md5, 4)) | 1u) + 0x13DB0000u;
        qint32 counter = shiftRight(length - 2, 1) + 1;
        while (counter > 0) {
            const quint32 m0 = quint32(readInt32(bytes, pdata)) + out1;
            const quint32 m1 = quint32(readInt32(bytes, pdata + 4));
            pdata += 8;
            const quint32 m3 = h1 * m0 - 0x10FA9605u * quint32(shiftRight(qint32(m0), 16));
            const quint32 m4 = 0x79F8A395u * m3 + 0x689B6B9Fu * quint32(shiftRight(qint32(m3), 16));
            const quint32 m5 = 0xEA970001u * m4 - 0x3C101569u * quint32(shiftRight(qint32(m4), 16));
            const quint32 m6 = m5 + m1;
            const quint32 m10 = h2 * m6 - 0x3CE8EC25u * quint32(shiftRight(qint32(m6), 16));
            const quint32 m11 = 0x59C3AF2Du * m10 - 0x2232E0F1u * quint32(shiftRight(qint32(m10), 16));
            out1 = 0x1EC90001u * m11 + 0x35BD1EC9u * quint32(shiftRight(qint32(m11), 16));
            const quint32 m8 = cache + m5;
            out2 = out1 + m8;
            cache = out2;
            --counter;
        }
        const quint32 pass1a = out1;
        const quint32 pass1b = out2;

        // Segunda pasada.
        cache = 0;
        out1 = 0;
        pdata = 0;
        h1 = quint32(readInt32(md5, 0)) | 1u;
        h2 = quint32(readInt32(md5, 4)) | 1u;
        counter = shiftRight(length - 2, 1) + 1;
        while (counter > 0) {
            const quint32 m0 = quint32(readInt32(bytes, pdata)) + out1;
            pdata += 8;
            const quint32 m1 = m0 * h1;
            const quint32 m2 = 0xB1110000u * m1 - 0x30674EEFu * quint32(shiftRight(qint32(m1), 16));
            const quint32 m3 = 0x5B9F0000u * m2 - 0x78F7A461u * quint32(shiftRight(qint32(m2), 16));
            const quint32 m4 = 0x12CEB96Du * quint32(shiftRight(qint32(m3), 16)) - 0x46930000u * m3;
            const quint32 m5 = 0x1D830000u * m4 + 0x257E1D83u * quint32(shiftRight(qint32(m4), 16));
            const quint32 m6 = h2 * (m5 + quint32(readInt32(bytes, pdata - 4)));
            const quint32 m7 = 0x16F50000u * m6 - 0x5D8BE90Bu * quint32(shiftRight(qint32(m6), 16));
            const quint32 m8 = 0x96FF0000u * m7 - 0x2C7C6901u * quint32(shiftRight(qint32(m7), 16));
            const quint32 m9 = 0x2B890000u * m8 + 0x7C932B89u * quint32(shiftRight(qint32(m8), 16));
            out1 = 0x9F690000u * m9 - 0x405B6097u * quint32(shiftRight(qint32(m9), 16));
            out2 = out1 + cache + m5;
            cache = out2;
            --counter;
        }

        QByteArray base;
        appendInt32(&base, out1 ^ pass1a);
        appendInt32(&base, out2 ^ pass1b);
        result.hash = QString::fromLatin1(base.toBase64());
    } catch (const Fault &fault) {
        result.error = fault.error;
    }
    return result;
}

HashResult legacyHashOfString(const QString &text)
{
    // El texto base ya trae el "\0" final (buildLegacyBaseInfo): los bytes son su UTF-16 y
    // lengthBase es exactamente ese largo en bytes, como PS-SFTA.
    return hashBytes(utf16Bytes(text), text.size() * 2);
}

HashResult legacyHashOfStringHelperCompatible(const QString &text)
{
    // Igual que el helper: los bytes NO llevan un terminador extra, pero lengthBase suma 2. Con
    // largos % 4 == 3 lee fuera del bloque y falla (ArgumentException).
    return hashBytes(utf16Bytes(text), text.size() * 2 + 2);
}

quint64 truncateToMinute(quint64 fileTime)
{
    return fileTime - fileTime % 600000000ull;
}

quint64 truncateToMillisecond(quint64 fileTime)
{
    return fileTime - fileTime % 10000ull;
}

bool isPlainAscii(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    for (const QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7E) {
            return false;
        }
    }
    return true;
}

QString fileVersionFromBytes(const QByteArray &peBytes)
{
    // VS_FIXEDFILEINFO: firma 0xFEEF04BD, version de estructura, y despues FileVersionMS/LS.
    static const uchar kSignature[4] = {0xBD, 0x04, 0xEF, 0xFE};
    const auto *data = reinterpret_cast<const uchar *>(peBytes.constData());
    const qint64 size = peBytes.size();
    for (qint64 pos = 0; pos + 16 <= size; pos += 4) {
        if (memcmp(data + pos, kSignature, 4) != 0) {
            continue;
        }
        auto dword = [&](qint64 at) {
            return quint32(data[at]) | (quint32(data[at + 1]) << 8) | (quint32(data[at + 2]) << 16) |
                   (quint32(data[at + 3]) << 24);
        };
        const quint32 ms = dword(pos + 8);
        const quint32 ls = dword(pos + 12);
        return QStringLiteral("%1.%2.%3.%4")
            .arg(ms >> 16)
            .arg(ms & 0xFFFF)
            .arg(ls >> 16)
            .arg(ls & 0xFFFF);
    }
    return QString();
}

HashResult latestHashOfString(const QString &hashString)
{
    HashResult result;
    const EncodeResult encoded = encodeHashString(toLowerInvariant(hashString));
    if (!encoded.ok()) {
        result.error = encoded.error;
        return result;
    }
    // El helper mide el largo util sobre una copia de (largo + 8) bytes del empaquetado.
    const QByteArray window = encoded.bytes.left(hashString.size() + 8);
    qint64 units = 0;
    try {
        units = byteLength(window);
    } catch (const Fault &fault) {
        result.error = fault.error;
        return result;
    }
    const int hashByteLen = int(units * 2) + 2;
    if (hashByteLen > encoded.bytes.size()) {
        result.error = HashError::ArgumentOutOfRange;
        return result;
    }
    return hashBytes(encoded.bytes.left(hashByteLen), hashByteLen);
}

HashResult computeLatestHash(const QString &machineId, const QString &userSid,
                             const QString &extension, const QString &progId, quint64 fileTime,
                             const ValidationStrings &validation)
{
    return latestHashOfString(
        buildHashString(machineId, userSid, extension, progId, fileTime, validation));
}

EncodeResult encodeHashStringWindows(const QString &text)
{
    EncodeResult result;
    const int n = text.size();
    if (n == 0) {
        result.error = HashError::EmptyInput;
        return result;
    }

    std::vector<quint64> words;
    try {
        int index = 0;
        while (index < n) {
            const quint32 first = text.at(index).unicode();
            const quint32 bits = (first >> 8) == 0 ? lut1(first) : quint32(lut1(first >> 8)) + 8;
            qint64 selector = lut2(bits);
            const int remaining = n - index;
            if (remaining < lut3(selector)) {
                // Cola: selector que empaqueta exactamente los caracteres que quedan.
                selector = lut5(remaining);
            }
            // Primer selector (desde ahi hacia arriba) en el que entran todos sus caracteres.
            for (;;) {
                const int count = lut3(selector);
                bool fits = true;
                for (int k = 0; k < count; ++k) {
                    const quint32 c = text.at(index + k).unicode();
                    if (quint64(c) >= (quint64(1) << (lut4(selector * 0xa8 + k) & 63))) {
                        fits = false;
                        break;
                    }
                }
                if (fits) {
                    break;
                }
                ++selector;
            }
            const int count = lut3(selector);
            quint64 word = quint64(selector) << 56;
            int shift = 56;
            for (int k = 0; k < count; ++k) {
                shift -= lut4(selector * 0xa8 + k);
                word += shl64(text.at(index + k).unicode(), shift);
            }
            words.push_back(word);
            index += count;
        }
    } catch (const Fault &fault) {
        result.error = fault.error;
        return result;
    }

    // Windows reemplaza una palabra repetida por una referencia hacia atras (selector 0xFF) cuyo
    // formato no esta reproducido: en ese caso no se puede dar el mismo resultado.
    for (size_t i = 0; i < words.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (words[i] == words[j]) {
                result.error = HashError::RepeatedWord;
                return result;
            }
        }
    }

    // Unidades de 16 bits: cabecera (largo, palabras + 1) y cada palabra sin sus unidades en cero.
    std::vector<quint16> units;
    units.reserve(2 + words.size() * 4);
    units.push_back(quint16(n & 0xFFFF));
    units.push_back(quint16((words.size() + 1) & 0xFFFF));
    for (const quint64 word : words) {
        for (int j = 0; j < 4; ++j) {
            const quint16 unit = quint16((word >> (16 * j)) & 0xFFFF);
            if (unit != 0) {
                units.push_back(unit);
            }
        }
    }
    // Tope del bufer de Windows: largo + 32 unidades.
    if (units.size() > size_t(n) + 32) {
        units.resize(size_t(n) + 32);
    }

    QByteArray out;
    out.reserve(int(units.size()) * 2);
    for (const quint16 unit : units) {
        if (unit == 0) {
            break; // el hash cubre hasta el primer cero (solo puede ser la cabecera)
        }
        out.append(char(unit & 0xFF));
        out.append(char(unit >> 8));
    }
    result.bytes = out;
    return result;
}

HashResult latestHashOfStringWindows(const QString &hashString)
{
    HashResult result;
    const EncodeResult encoded = encodeHashStringWindows(toLowerInvariant(hashString));
    if (!encoded.ok()) {
        result.error = encoded.error;
        return result;
    }
    QByteArray bytes = encoded.bytes;
    bytes.append('\0');
    bytes.append('\0');
    return hashBytes(bytes, int(bytes.size()));
}

HashResult computeLatestHash(const QString &machineId, const QString &userSid,
                             const QString &extension, const QString &progId, quint64 fileTime,
                             const ValidationStrings &validation, HashMode mode)
{
    const QString text = buildHashString(machineId, userSid, extension, progId, fileTime, validation);
    return mode == HashMode::Windows ? latestHashOfStringWindows(text) : latestHashOfString(text);
}

void runSelfTestVectors(const std::function<void(bool ok, const QString &what)> &check)
{
    // Datos sinteticos (SID, MachineId y cadenas de validacion ficticios). Los hashes esperados
    // salen de la funcion de empaquetado de Windows.Internal.OpenWithHost.dll (nuevo) y de
    // GetHash de PS-SFTA con el terminador incluido (viejo), calculados aparte.
    const ValidationStrings validation = {
        QStringLiteral("Copyright (C) Example Corp. Test vector {11111111-2222-4333-8444-555555555555}"),
        QStringLiteral("Copyright (C) Example Corp. Test vector {66666666-7777-4888-9999-AAAAAAAAAAAA}"),
        QStringLiteral("Copyright (C) Example Corp. Test vector {BBBBBBBB-CCCC-4DDD-AEEE-FFFFFFFFFFFF}"),
    };
    struct LatestVector {
        const char *machineId;
        const char *sid;
        const char *extension;
        const char *progId;
        quint64 fileTime;
        const char *hash;
    };
    static const LatestVector kLatest[] = {
        {"0F6C2E11-9A3B-4C55-B2DE-77AA01C3E4F0", "S-1-5-21-1111111111-2222222222-3333333333-1001",
         ".nk", "LGA.NukeScript.1", 0x01dd4cd8ff528d30ull, "6eYIZYEOBIU="},
        {"0F6C2E11-9A3B-4C55-B2DE-77AA01C3E4F1", "S-1-5-21-1111111111-2222222222-3333333333-1001",
         ".mov", "LGA.MightyTools.URL", 0x01db5be019ba4000ull, "a6DT7WVvCeQ="},
        {"0F6C2E11-9A3B-4C55-B2DE-77AA01C3E4F2", "S-1-5-21-4444444444-555555555-666666666-500",
         ".exr", "LGA.NukeScript.1", 0x01de1b6f22644510ull, "8gX5zuWgOAo="},
        {"a1b2c3d4-5e6f-4a7b-8c9d-0e1f2a3b4c5e", "S-1-5-21-4444444444-555555555-666666666-1002",
         ".nk", "Applications\\LGA_MightyTools.exe", 0x01db5be019ba18f0ull, "09gn02p+rCY="},
    };
    for (const LatestVector &v : kLatest) {
        const HashResult r = computeLatestHash(QLatin1String(v.machineId), QLatin1String(v.sid),
                                               QLatin1String(v.extension), QLatin1String(v.progId),
                                               v.fileTime, validation, HashMode::Windows);
        check(r.ok() && r.hash == QLatin1String(v.hash),
              QStringLiteral("UserChoiceLatest %1 %2 -> %3 (dio %4)")
                  .arg(QLatin1String(v.extension), QLatin1String(v.progId), QLatin1String(v.hash),
                       r.ok() ? r.hash : hashErrorName(r.error)));
    }

    const QString experience = QString::fromUtf16(kUserExperienceFixed);
    const QString legacyTime = hexFileTime(0x01dd4cd8f51dca00ull);
    struct LegacyVector {
        const char *extension;
        const char *sid;
        const char *progId;
        const char *hash;
    };
    // Cubren los cuatro restos del largo % 4 (el ultimo es el caso en que fallaba el helper).
    static const LegacyVector kLegacy[] = {
        {".nk", "S-1-5-21-1111111111-2222222222-3333333333-1001", "LGA.NukeScript.1", "1CxWZrwmg+M="},
        {".nk", "S-1-5-21-1111111111-2222222222-3333333333-1001", "LGA.NukeScript.12", "kBcnobDi0l0="},
        {".mov", "S-1-5-21-1111111111-2222222222-3333333333-1001", "LGA.NukeScript.1", "6K2kb5HgxoE="},
        {".exr", "S-1-5-21-4444444444-555555555-666666666-500", "LGA.MightyTools.URL", "XTUL6RMeDfM="},
        {".nk", "S-1-5-21-4444444444-555555555-666666666-500", "LGA.MightyTools.URL", "j81OXuDbL0M="},
        {".nk", "S-1-5-21-1111111111-2222222222-3333333333-1001", "LGA.NukeScript.123", "ZNA45AmKaXM="},
        {".nk", "S-1-5-21-1111111111-2222222222-3333333333-1001", "LGA.NukeScript.1234", "JmX5qXfo8c0="},
    };
    for (const LegacyVector &v : kLegacy) {
        const QString base = buildLegacyBaseInfo(QLatin1String(v.extension), QLatin1String(v.sid),
                                                 QLatin1String(v.progId), legacyTime, experience);
        const HashResult r = legacyHashOfString(base);
        check(r.ok() && r.hash == QLatin1String(v.hash),
              QStringLiteral("UserChoice %1 %2 (largo mod 4 = %3) -> %4 (dio %5)")
                  .arg(QLatin1String(v.extension), QLatin1String(v.progId))
                  .arg(base.size() % 4)
                  .arg(QLatin1String(v.hash), r.ok() ? r.hash : hashErrorName(r.error)));
    }

    // Casos negativos: tienen que detectarse.
    {
        const LatestVector &v = kLatest[0];
        const HashResult r = computeLatestHash(QLatin1String(v.machineId), QLatin1String(v.sid),
                                               QLatin1String(v.extension), QLatin1String(v.progId),
                                               v.fileTime + 10000, validation, HashMode::Windows);
        check(r.ok() && r.hash != QLatin1String(v.hash),
              QStringLiteral("negativo: 1 ms mas en la marca cambia el hash de UserChoiceLatest"));
    }
    {
        const LegacyVector &v = kLegacy[6];
        const QString base = buildLegacyBaseInfo(QLatin1String(v.extension), QLatin1String(v.sid),
                                                 QLatin1String(v.progId), legacyTime, experience);
        const HashResult r = legacyHashOfStringHelperCompatible(base);
        check(!r.ok(), QStringLiteral("negativo: el calculo del helper falla con largo mod 4 = 3"));
    }
    {
        const EncodeResult r = encodeHashStringWindows(QString(64, QLatin1Char('a')));
        check(r.error == HashError::RepeatedWord,
              QStringLiteral("negativo: palabras repetidas se rechazan (RepeatedWord)"));
    }
    check(!isPlainAscii(QStringLiteral(".nñ")) && isPlainAscii(QStringLiteral("LGA.NukeScript.1")),
          QStringLiteral("negativo: extensiones y ProgIDs no ASCII se rechazan"));
}

QString buildLegacyBaseInfo(const QString &extension, const QString &userSid,
                            const QString &progId, const QString &hexDateTime,
                            const QString &userExperience)
{
    return toLowerInvariant(extension + userSid + progId + hexDateTime + userExperience +
                            QChar(u'\0'));
}

int findValidationStrings(const QByteArray &dllBytes, ValidationStrings *out)
{
    static const uchar kPattern[8] = {0x43, 0x00, 0x6F, 0x00, 0x70, 0x00, 0x79, 0x00}; // "Copy"
    const auto *data = reinterpret_cast<const uchar *>(dllBytes.constData());
    const qint64 size = dllBytes.size();
    ValidationStrings found;
    for (qint64 pos = 0; pos + 8 <= size; ++pos) {
        if (memcmp(data + pos, kPattern, 8) != 0) {
            continue;
        }
        const qint64 units = qMin<qint64>(kValidationChars, (size - pos) / 2);
        const QString candidate = decodeUtf16(data + pos, units);
        const QString lower = toLowerInvariant(candidate);
        for (int i = 0; i < 3; ++i) {
            if (!lower.endsWith(QLatin1String(kValidationKeys[i].suffix))) {
                continue;
            }
            const QByteArray md5 =
                QCryptographicHash::hash(utf16Bytes(lower), QCryptographicHash::Md5).toHex();
            if (md5 == kValidationKeys[i].md5) {
                // Como el original: si aparece mas de una vez, gana la ultima.
                found[size_t(i)] = candidate;
                break;
            }
        }
    }
    int count = 0;
    for (const QString &s : found) {
        if (!s.isEmpty()) {
            ++count;
        }
    }
    if (out) {
        *out = found;
    }
    return count;
}

QString findUserExperience(const QByteArray &shell32Bytes)
{
    const QString fixed = QString::fromUtf16(kUserExperienceFixed);
    const QString prefix = QString::fromUtf16(kUserExperiencePrefix);
    const QByteArray pattern = utf16Bytes(prefix);
    const auto *data = reinterpret_cast<const uchar *>(shell32Bytes.constData());
    const qint64 size = shell32Bytes.size();
    // El original decodifica el archivo entero como UTF-16: solo cuentan posiciones pares.
    for (qint64 pos = 0; pos + pattern.size() <= size; pos += 2) {
        if (memcmp(data + pos, pattern.constData(), size_t(pattern.size())) != 0) {
            continue;
        }
        const qint64 units = fixed.size();
        if (pos + units * 2 > size) {
            return fixed;
        }
        return decodeUtf16(data + pos, units);
    }
    return fixed;
}

// ─── 2. Lectura del sistema ─────────────────────────────────────────────────

QString currentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return QString();
    }
    std::unique_ptr<void, decltype(&CloseHandle)> guard(token, &CloseHandle);

    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) {
        return QString();
    }
    std::vector<BYTE> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        return QString();
    }
    const auto *user = reinterpret_cast<const TOKEN_USER *>(buffer.data());
    LPWSTR sidText = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sidText)) {
        return QString();
    }
    const QString sid = QString::fromWCharArray(sidText);
    LocalFree(sidText);
    return sid;
}

QString readMachineIdRaw()
{
    RegKey key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\SQMClient", 0,
                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.h) != ERROR_SUCCESS) {
        return QString();
    }
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key.h, L"MachineId", nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        return QString();
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key.h, L"MachineId", nullptr, &type,
                         reinterpret_cast<BYTE *>(buffer.data()), &size) != ERROR_SUCCESS) {
        return QString();
    }
    return QString::fromWCharArray(buffer.data());
}

bool readValidationStrings(ValidationStrings *out, QString *error)
{
    const QString path = systemDirectory() + QStringLiteral("\\Windows.Internal.OpenWithHost.dll");
    const QByteArray bytes = readWholeFile(path);
    if (bytes.isEmpty()) {
        if (error) {
            *error = QStringLiteral("No se pudo leer %1").arg(path);
        }
        return false;
    }
    const int found = findValidationStrings(bytes, out);
    if (found != 3) {
        if (error) {
            *error = QStringLiteral("Se encontraron %1 de 3 cadenas de validacion en %2")
                         .arg(found)
                         .arg(path);
        }
        return false;
    }
    return true;
}

QString readUserExperience()
{
    return findUserExperience(readWholeFile(systemDirectory() + QStringLiteral("\\shell32.dll")));
}

bool isLatestHashActive(const QString &userSid)
{
    if (userSid.isEmpty()) {
        return false;
    }
    const std::wstring path =
        (QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SystemProtectedUserData\\") +
         userSid + QStringLiteral("\\AnyoneRead\\AppDefaults"))
            .toStdWString();
    RegKey key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY,
                      &key.h) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    BYTE data[64] = {};
    DWORD size = sizeof(data) - sizeof(wchar_t);
    if (RegQueryValueExW(key.h, L"HashVersion", nullptr, &type, data, &size) != ERROR_SUCCESS) {
        return false;
    }
    // En Windows 11 es REG_DWORD; se acepta tambien texto por si alguna version lo guarda asi.
    if (type == REG_DWORD && size == sizeof(DWORD)) {
        DWORD value = 0;
        memcpy(&value, data, sizeof(DWORD));
        return value == 1;
    }
    if (type == REG_SZ) {
        return QString::fromWCharArray(reinterpret_cast<const wchar_t *>(data)).trimmed() ==
               QLatin1String("1");
    }
    return false;
}

quint64 currentMinuteFileTime()
{
    // DateTime(now local, segundos 0).ToFileTime(): hora local -> UTC.
    SYSTEMTIME local{};
    GetLocalTime(&local);
    local.wSecond = 0;
    local.wMilliseconds = 0;
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc)) {
        GetSystemTime(&utc);
        utc.wSecond = 0;
        utc.wMilliseconds = 0;
    }
    FILETIME ft{};
    if (!SystemTimeToFileTime(&utc, &ft)) {
        return 0;
    }
    return (quint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}


QString readOpenWithHostVersion()
{
    return fileVersionFromBytes(
        readWholeFile(systemDirectory() + QStringLiteral("\\Windows.Internal.OpenWithHost.dll")));
}

// ─── 3. Escritura ───────────────────────────────────────────────────────────

namespace {

struct LatestWrite {
    bool ok = false;
    QString hash;
    quint64 stamp = 0;
    int attempts = 0;
    QString reason;
    bool touched = false; ///< se llego a escribir el ProgId (la eleccion anterior ya no esta intacta)
};

// UserChoiceLatest\ProgId\ProgId = progId y UserChoiceLatest\Hash = hash nuevo calculado con la
// marca de esa escritura (truncada al milisegundo). Si el hash no se puede calcular para esa
// marca, se vuelve a escribir el ProgId (marca nueva) y se reintenta.
LatestWrite writeLatest(HKEY latest, HKEY progKey, const QString &progId, const QString &machineId,
                        const QString &sid, const QString &extension,
                        const ValidationStrings &validation, int maxAttempts)
{
    LatestWrite w;
    HashResult hash;
    const int attempts = qMax(1, maxAttempts);
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        w.attempts = attempt;
        if (attempt > 1) {
            QThread::msleep(2); // la precision que cuenta es el milisegundo
        }
        const LONG rc = setString(progKey, L"ProgId", progId);
        if (rc != ERROR_SUCCESS) {
            w.reason = QStringLiteral("No se pudo escribir UserChoiceLatest\\ProgId (rc=%1)").arg(rc);
            return w;
        }
        w.touched = true;
        w.stamp = truncateToMillisecond(lastWriteTime(progKey));
        if (w.stamp == 0) {
            w.reason = QStringLiteral("No se pudo leer la ultima escritura de UserChoiceLatest\\ProgId");
            return w;
        }
        hash = computeLatestHash(machineId, sid, extension, progId, w.stamp, validation,
                                 HashMode::Windows);
        if (hash.ok() && !hash.hash.isEmpty()) {
            break;
        }
    }
    if (!hash.ok() || hash.hash.isEmpty()) {
        w.reason = QStringLiteral("Hash de UserChoiceLatest no calculable para %1 tras %2 intentos (%3)")
                       .arg(progId)
                       .arg(w.attempts)
                       .arg(hashErrorName(hash.error));
        return w;
    }
    const LONG rc = setString(latest, L"Hash", hash.hash);
    if (rc != ERROR_SUCCESS) {
        w.reason = QStringLiteral("No se pudo escribir UserChoiceLatest\\Hash (rc=%1)").arg(rc);
        return w;
    }
    w.ok = true;
    w.hash = hash.hash;
    return w;
}

struct LegacyWrite {
    bool ok = false;
    QString hash;
    QString reason;
};

// UserChoice clasico: borrar la clave, escribir ProgId y el Hash calculado con la ultima escritura
// de la clave truncada al minuto (lo que usa Windows). Si al terminar la clave quedo con una
// marca de otro minuto, se recalcula y se vuelve a escribir.
LegacyWrite writeLegacy(const std::wstring &choicePath, const QString &extension,
                        const QString &sid, const QString &progId, const QString &userExperience)
{
    LegacyWrite w;
    RegDeleteKeyW(HKEY_CURRENT_USER, choicePath.c_str());
    RegKey key;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, choicePath.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, &key.h,
                              nullptr);
    if (rc != ERROR_SUCCESS) {
        w.reason = QStringLiteral("No se pudo crear UserChoice (rc=%1)").arg(rc);
        return w;
    }
    rc = setString(key.h, L"ProgId", progId);
    if (rc != ERROR_SUCCESS) {
        w.reason = QStringLiteral("No se pudo escribir UserChoice\\ProgId (rc=%1)").arg(rc);
        return w;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        const quint64 minute = truncateToMinute(lastWriteTime(key.h));
        if (minute == 0) {
            w.reason = QStringLiteral("No se pudo leer la ultima escritura de UserChoice");
            return w;
        }
        const HashResult hash = legacyHashOfString(
            buildLegacyBaseInfo(extension, sid, progId, hexFileTime(minute), userExperience));
        if (!hash.ok() || hash.hash.isEmpty()) {
            w.reason = QStringLiteral("Hash de UserChoice no calculable (%1)").arg(hashErrorName(hash.error));
            return w;
        }
        rc = setString(key.h, L"Hash", hash.hash);
        if (rc != ERROR_SUCCESS) {
            w.reason = QStringLiteral("No se pudo escribir UserChoice\\Hash (rc=%1)").arg(rc);
            return w;
        }
        if (truncateToMinute(lastWriteTime(key.h)) == minute) {
            w.ok = true;
            w.hash = hash.hash;
            return w;
        }
    }
    w.reason = QStringLiteral("La marca de UserChoice cambio de minuto en cada intento");
    return w;
}

void notifyShell()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace

ApplyResult applyAssociation(const QString &extensionIn, const QString &progId, int maxAttempts)
{
    ApplyResult result;
    const QString extension =
        extensionIn.startsWith(QLatin1Char('.')) ? extensionIn : QLatin1Char('.') + extensionIn;
    // Solo ASCII imprimible: las minusculas fuera de ASCII no estan verificadas contra Windows. La
    // extension ademas no puede traer separadores (arma rutas del registro).
    if (extension.size() < 2 || !isPlainAscii(extension) || !isPlainAscii(progId) ||
        extension.contains(QLatin1Char('\\')) || extension.contains(QLatin1Char('/'))) {
        result.reason = QStringLiteral("Extension o ProgID vacios o con caracteres no ASCII: %1 -> %2")
                            .arg(extensionIn, progId);
        return result;
    }

    const QString sid = currentUserSid();
    if (sid.isEmpty()) {
        result.reason = QStringLiteral("No se pudo leer el SID del usuario");
        return result;
    }

    // Todo lo que hace falta leer se lee ANTES de escribir nada: si falta algo, no se toca el
    // registro.
    const QString machineId = trimMachineId(readMachineIdRaw());
    if (machineId.isEmpty()) {
        result.reason = QStringLiteral("No se encontro HKLM\\SOFTWARE\\Microsoft\\SQMClient\\MachineId");
        return result;
    }
    ValidationStrings validation;
    QString validationError;
    if (!readValidationStrings(&validation, &validationError)) {
        result.reason = validationError;
        return result;
    }
    result.dllVersion = readOpenWithHostVersion();
    qInfo() << "[UserChoiceLatest] Windows.Internal.OpenWithHost.dll version" << result.dllVersion;
    const QString userExperience = readUserExperience();
    result.latestActive = isLatestHashActive(sid);

    const std::wstring extPath = std::wstring(kFileExtsPath) + extension.toStdWString();
    const std::wstring latestPath = extPath + L"\\UserChoiceLatest";
    const std::wstring choicePath = extPath + L"\\UserChoice";

    // Eleccion anterior del usuario, para poder volver a ella si esta escritura falla.
    result.previousProgId = readUserString(latestPath + L"\\ProgId", L"ProgId");

    // 1. Toasts falsos: valores DWORD 0 por cada ProgID y aplicacion de la extension.
    {
        const std::wstring classesExt = extension.toStdWString();
        QStringList toastIds;
        for (const QString &name : valueNames(HKEY_CLASSES_ROOT, classesExt + L"\\OpenWithProgids")) {
            toastIds << name + QLatin1Char('_') + extension;
        }
        for (const QString &app : subKeyNames(HKEY_CLASSES_ROOT, classesExt + L"\\OpenWithList")) {
            toastIds << QStringLiteral("Applications\\") + app + QLatin1Char('_') + extension;
        }
        if (!toastIds.isEmpty()) {
            RegKey toasts;
            if (RegOpenKeyExW(HKEY_CURRENT_USER,
                              L"Software\\Microsoft\\Windows\\CurrentVersion\\ApplicationAssociationToasts",
                              0, KEY_SET_VALUE, &toasts.h) != ERROR_SUCCESS) {
                result.warnings << QStringLiteral("No se pudo abrir ApplicationAssociationToasts");
            } else {
                const DWORD zero = 0;
                for (const QString &id : toastIds) {
                    const std::wstring name = id.toStdWString();
                    if (RegSetValueExW(toasts.h, name.c_str(), 0, REG_DWORD,
                                       reinterpret_cast<const BYTE *>(&zero),
                                       sizeof(zero)) != ERROR_SUCCESS) {
                        result.warnings << QStringLiteral("No se pudo escribir el toast %1").arg(id);
                    }
                }
            }
        }
    }

    // 2. UserChoice clasico.
    const LegacyWrite legacy = writeLegacy(choicePath, extension, sid, progId, userExperience);
    if (legacy.ok) {
        result.legacyHash = legacy.hash;
    } else {
        result.warnings << legacy.reason;
    }

    // 3. UserChoiceLatest.
    RegKey latest;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, latestPath.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, &latest.h,
                              nullptr);
    if (rc != ERROR_SUCCESS) {
        result.reason = QStringLiteral("No se pudo crear UserChoiceLatest (rc=%1)").arg(rc);
        notifyShell();
        return result;
    }
    RegKey progKey;
    rc = RegCreateKeyExW(latest.h, L"ProgId", 0, nullptr, REG_OPTION_NON_VOLATILE,
                         KEY_READ | KEY_WRITE, nullptr, &progKey.h, nullptr);
    if (rc != ERROR_SUCCESS) {
        result.reason = QStringLiteral("No se pudo crear UserChoiceLatest\\ProgId (rc=%1)").arg(rc);
        notifyShell();
        return result;
    }

    const LatestWrite written = writeLatest(latest.h, progKey.h, progId, machineId, sid, extension,
                                            validation, maxAttempts);
    result.latestAttempts = written.attempts;
    if (!written.ok) {
        result.reason = written.reason;
        // No perder la eleccion anterior: se vuelve a escribir con su hash recalculado. Si no habia
        // o tampoco se puede, se borran ProgId y Hash para no dejar un Hash huerfano.
        if (!written.touched) {
            // No se llego a escribir nada: la eleccion anterior sigue intacta y valida.
            result.reason += QStringLiteral("; la eleccion anterior no se toco");
            notifyShell();
            return result;
        }
        bool restored = false;
        if (!result.previousProgId.isEmpty() &&
            result.previousProgId.compare(progId, Qt::CaseInsensitive) != 0 &&
            isPlainAscii(result.previousProgId)) {
            const LatestWrite back = writeLatest(latest.h, progKey.h, result.previousProgId,
                                                 machineId, sid, extension, validation, maxAttempts);
            restored = back.ok;
            result.reason += restored
                                 ? QStringLiteral("; se restauro la eleccion anterior (%1)")
                                       .arg(result.previousProgId)
                                 : QStringLiteral("; no se pudo restaurar la eleccion anterior (%1)")
                                       .arg(back.reason);
        }
        if (!restored) {
            RegDeleteValueW(progKey.h, L"ProgId");
            RegDeleteValueW(latest.h, L"Hash");
        }
        result.restoredPrevious = restored;
        notifyShell();
        return result;
    }
    result.latestHash = written.hash;
    result.latestFileTime = written.stamp;

    // 4. Aviso al shell.
    notifyShell();

    // 5. Relectura: si Windows reseteo la eleccion, la clave ya no tiene lo que se escribio.
    QThread::msleep(250);
    {
        const QString readProgId = readUserString(latestPath + L"\\ProgId", L"ProgId");
        const QString readHash = readUserString(latestPath, L"Hash");
        QString recomputed;
        RegKey check;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, (latestPath + L"\\ProgId").c_str(), 0,
                          KEY_QUERY_VALUE, &check.h) == ERROR_SUCCESS) {
            const HashResult h = computeLatestHash(machineId, sid, extension, readProgId,
                                                   truncateToMillisecond(lastWriteTime(check.h)),
                                                   validation, HashMode::Windows);
            recomputed = h.ok() ? h.hash : QString();
        }
        if (readProgId != progId || readHash != written.hash || recomputed != written.hash) {
            result.warnings << QStringLiteral("UserChoiceLatest no quedo como se escribio (ProgId=%1, "
                                              "Hash=%2, hash segun la marca actual=%3): Windows pudo "
                                              "haberlo reseteado")
                                   .arg(readProgId, readHash, recomputed);
        }
        if (legacy.ok) {
            const QString legacyProgId = readUserString(choicePath, L"ProgId");
            const QString legacyHashRead = readUserString(choicePath, L"Hash");
            if (legacyProgId != progId || legacyHashRead != legacy.hash) {
                result.warnings << QStringLiteral("UserChoice no quedo como se escribio (ProgId=%1, "
                                                  "Hash=%2)")
                                       .arg(legacyProgId, legacyHashRead);
            }
        }
    }

    // Sin UserChoiceLatest activo, Windows valida con el UserChoice clasico: si ese no quedo
    // escrito, la asociacion no esta hecha.
    if (!result.latestActive && !legacy.ok) {
        result.reason = QStringLiteral("HashVersion no esta activo y no se pudo escribir UserChoice (%1)")
                            .arg(legacy.reason);
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace UserChoiceLatest
