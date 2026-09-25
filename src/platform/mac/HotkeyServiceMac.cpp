#include "platform/HotkeyService.h"

#include <QDebug>
#include <QHash>
#include <QMetaObject>

#include <Carbon/Carbon.h>

// Atajos globales en macOS con RegisterEventHotKey (Carbon). Es la API que sigue vigente para esto:
// no pide el permiso de Accesibilidad (a diferencia de un CGEventTap) y entrega el evento en el
// hilo principal, por el event loop de la app.
//
// Sin compilar todavia en un Mac: la primera compilacion en macOS la valida.

namespace {

constexpr OSType kSignature = 'LGNS';
constexpr int kProbeId = 99;

UInt32 nativeModifiers(Qt::KeyboardModifiers modifiers)
{
    // En Qt/mac ControlModifier es la tecla Command y MetaModifier la tecla Control.
    UInt32 mods = 0;
    if (modifiers & Qt::ControlModifier) mods |= cmdKey;
    if (modifiers & Qt::MetaModifier) mods |= controlKey;
    if (modifiers & Qt::AltModifier) mods |= optionKey;
    if (modifiers & Qt::ShiftModifier) mods |= shiftKey;
    return mods;
}

// Codigos virtuales de la distribucion ANSI (posicion fisica de la tecla).
UInt32 nativeKey(int key)
{
    static const QHash<int, UInt32> table = {
        {Qt::Key_A, kVK_ANSI_A}, {Qt::Key_B, kVK_ANSI_B}, {Qt::Key_C, kVK_ANSI_C}, {Qt::Key_D, kVK_ANSI_D},
        {Qt::Key_E, kVK_ANSI_E}, {Qt::Key_F, kVK_ANSI_F}, {Qt::Key_G, kVK_ANSI_G}, {Qt::Key_H, kVK_ANSI_H},
        {Qt::Key_I, kVK_ANSI_I}, {Qt::Key_J, kVK_ANSI_J}, {Qt::Key_K, kVK_ANSI_K}, {Qt::Key_L, kVK_ANSI_L},
        {Qt::Key_M, kVK_ANSI_M}, {Qt::Key_N, kVK_ANSI_N}, {Qt::Key_O, kVK_ANSI_O}, {Qt::Key_P, kVK_ANSI_P},
        {Qt::Key_Q, kVK_ANSI_Q}, {Qt::Key_R, kVK_ANSI_R}, {Qt::Key_S, kVK_ANSI_S}, {Qt::Key_T, kVK_ANSI_T},
        {Qt::Key_U, kVK_ANSI_U}, {Qt::Key_V, kVK_ANSI_V}, {Qt::Key_W, kVK_ANSI_W}, {Qt::Key_X, kVK_ANSI_X},
        {Qt::Key_Y, kVK_ANSI_Y}, {Qt::Key_Z, kVK_ANSI_Z},
        {Qt::Key_0, kVK_ANSI_0}, {Qt::Key_1, kVK_ANSI_1}, {Qt::Key_2, kVK_ANSI_2}, {Qt::Key_3, kVK_ANSI_3},
        {Qt::Key_4, kVK_ANSI_4}, {Qt::Key_5, kVK_ANSI_5}, {Qt::Key_6, kVK_ANSI_6}, {Qt::Key_7, kVK_ANSI_7},
        {Qt::Key_8, kVK_ANSI_8}, {Qt::Key_9, kVK_ANSI_9},
        {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2}, {Qt::Key_F3, kVK_F3}, {Qt::Key_F4, kVK_F4},
        {Qt::Key_F5, kVK_F5}, {Qt::Key_F6, kVK_F6}, {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8},
        {Qt::Key_F9, kVK_F9}, {Qt::Key_F10, kVK_F10}, {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12},
    };
    return table.value(key, UINT32_MAX);
}

} // namespace

struct HotkeyService::Private
{
    HotkeyService *q = nullptr;
    EventHandlerRef handler = nullptr;
    QHash<int, EventHotKeyRef> refs;

    static OSStatus onHotKey(EventHandlerCallRef, EventRef event, void *userData)
    {
        auto *self = static_cast<Private *>(userData);
        EventHotKeyID hotKeyId{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hotKeyId), nullptr,
                              &hotKeyId)
                != noErr
            || hotKeyId.signature != kSignature) {
            return eventNotHandledErr;
        }
        const int id = static_cast<int>(hotKeyId.id);
        if (!self->refs.contains(id)) {
            return eventNotHandledErr;
        }
        // Encolado: la accion corre despues de que Carbon termine de despachar el evento.
        HotkeyService *service = self->q;
        QMetaObject::invokeMethod(service, [service, id]() { emit service->activated(id); }, Qt::QueuedConnection);
        return noErr;
    }
};

HotkeyService::HotkeyService(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    const EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
    InstallApplicationEventHandler(&Private::onHotKey, 1, &spec, d.get(), &d->handler);
}

HotkeyService::~HotkeyService()
{
    unregisterAll();
    if (d->handler) {
        RemoveEventHandler(d->handler);
    }
}

bool HotkeyService::registerHotkey(int id, const Shortcut &shortcut)
{
    unregisterHotkey(id);
    const UInt32 key = nativeKey(shortcut.key);
    if (key == UINT32_MAX) {
        qWarning() << "[HotkeyService] Tecla no soportada:" << shortcut.toPortableString();
        return false;
    }
    EventHotKeyRef ref = nullptr;
    const EventHotKeyID hotKeyId{kSignature, static_cast<UInt32>(id)};
    const OSStatus status = RegisterEventHotKey(key, nativeModifiers(shortcut.modifiers), hotKeyId,
                                                GetApplicationEventTarget(), 0, &ref);
    if (status != noErr || !ref) {
        qWarning() << "[HotkeyService] macOS rechazo" << shortcut.toPortableString() << "status:" << status;
        return false;
    }
    d->refs.insert(id, ref);
    qDebug() << "[HotkeyService] Registrado" << shortcut.toPortableString() << "id" << id;
    return true;
}

void HotkeyService::unregisterHotkey(int id)
{
    const auto it = d->refs.find(id);
    if (it == d->refs.end()) {
        return;
    }
    UnregisterEventHotKey(it.value());
    d->refs.erase(it);
    qDebug() << "[HotkeyService] Liberado id" << id;
}

void HotkeyService::unregisterAll()
{
    const QList<int> ids = d->refs.keys();
    for (const int id : ids) {
        unregisterHotkey(id);
    }
}

bool HotkeyService::isRegistered(int id) const
{
    return d->refs.contains(id);
}

void HotkeyService::passThrough(const Shortcut &shortcut)
{
    const UInt32 key = nativeKey(shortcut.key);
    if (key == UINT32_MAX) {
        return;
    }
    CGEventFlags flags = 0;
    if (shortcut.modifiers & Qt::ControlModifier) flags |= kCGEventFlagMaskCommand;
    if (shortcut.modifiers & Qt::MetaModifier) flags |= kCGEventFlagMaskControl;
    if (shortcut.modifiers & Qt::AltModifier) flags |= kCGEventFlagMaskAlternate;
    if (shortcut.modifiers & Qt::ShiftModifier) flags |= kCGEventFlagMaskShift;
    for (const bool down : {true, false}) {
        CGEventRef event = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(key), down);
        if (event) {
            CGEventSetFlags(event, flags);
            CGEventPost(kCGHIDEventTap, event);
            CFRelease(event);
        }
    }
    qInfo() << "[HotkeyService] Combinacion devuelta a la app del frente:" << shortcut.toPortableString();
}

bool HotkeyService::probe(const Shortcut &shortcut)
{
    const UInt32 key = nativeKey(shortcut.key);
    if (key == UINT32_MAX) {
        return false;
    }
    EventHotKeyRef ref = nullptr;
    const EventHotKeyID hotKeyId{kSignature, static_cast<UInt32>(kProbeId)};
    if (RegisterEventHotKey(key, nativeModifiers(shortcut.modifiers), hotKeyId, GetApplicationEventTarget(), 0, &ref)
            != noErr
        || !ref) {
        return false;
    }
    UnregisterEventHotKey(ref);
    return true;
}
