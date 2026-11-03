#include "platform/AppPreferences.h"

#include <QByteArray>
#include <QFile>
#include <QVariantList>

#include <CoreFoundation/CoreFoundation.h>

#include <vector>

// Un .plist se lee como archivo (QFile) y CoreFoundation solo lo convierte desde los bytes: nunca
// CFPreferences, que sincroniza y puede escribir el dominio del otro programa.

namespace {

constexpr qint64 kMaxPlistBytes = 8 * 1024 * 1024;
constexpr int kMaxDepth = 16;

QVariant toVariant(CFTypeRef value, int depth)
{
    if (!value || depth > kMaxDepth) {
        return QVariant();
    }
    const CFTypeID type = CFGetTypeID(value);
    if (type == CFStringGetTypeID()) {
        return QString::fromCFString(static_cast<CFStringRef>(value));
    }
    if (type == CFBooleanGetTypeID()) {
        return bool(CFBooleanGetValue(static_cast<CFBooleanRef>(value)));
    }
    if (type == CFNumberGetTypeID()) {
        const auto number = static_cast<CFNumberRef>(value);
        if (CFNumberIsFloatType(number)) {
            double real = 0;
            CFNumberGetValue(number, kCFNumberDoubleType, &real);
            return real;
        }
        long long integer = 0;
        CFNumberGetValue(number, kCFNumberLongLongType, &integer);
        return qlonglong(integer);
    }
    if (type == CFDataGetTypeID()) {
        return QByteArray::fromCFData(static_cast<CFDataRef>(value));
    }
    if (type == CFArrayGetTypeID()) {
        const auto array = static_cast<CFArrayRef>(value);
        QVariantList list;
        const CFIndex count = CFArrayGetCount(array);
        for (CFIndex i = 0; i < count; ++i) {
            list.append(toVariant(CFArrayGetValueAtIndex(array, i), depth + 1));
        }
        return list;
    }
    if (type == CFDictionaryGetTypeID()) {
        const auto dictionary = static_cast<CFDictionaryRef>(value);
        const CFIndex count = CFDictionaryGetCount(dictionary);
        std::vector<const void *> keys(static_cast<size_t>(count));
        std::vector<const void *> values(static_cast<size_t>(count));
        if (count > 0) {
            CFDictionaryGetKeysAndValues(dictionary, keys.data(), values.data());
        }
        QVariantMap map;
        for (CFIndex i = 0; i < count; ++i) {
            const auto key = static_cast<CFTypeRef>(keys[size_t(i)]);
            if (key && CFGetTypeID(key) == CFStringGetTypeID()) {
                map.insert(QString::fromCFString(static_cast<CFStringRef>(key)), toVariant(static_cast<CFTypeRef>(values[size_t(i)]), depth + 1));
            }
        }
        return map;
    }
    return QVariant();
}

} // namespace

namespace AppPreferences {

QString userRegistryString(const QString &, const QString &)
{
    return QString();
}

QStringList userRegistrySubKeys(const QString &)
{
    return {};
}

QVariantMap readPlist(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxPlistBytes) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    file.close();
    CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(bytes.constData()), CFIndex(bytes.size()));
    if (!data) {
        return {};
    }
    CFPropertyListRef list = CFPropertyListCreateWithData(nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);
    CFRelease(data);
    if (!list) {
        return {};
    }
    QVariantMap result;
    if (CFGetTypeID(list) == CFDictionaryGetTypeID()) {
        result = toVariant(list, 0).toMap();
    }
    CFRelease(list);
    return result;
}

} // namespace AppPreferences
