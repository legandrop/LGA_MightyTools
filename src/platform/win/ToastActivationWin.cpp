#include "platform/ToastActivation.h"

#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QMetaObject>

#include <windows.h>
#include <objbase.h>

namespace {

const QString kAumid = QStringLiteral("LGA_MightyTools");
const QString kDisplayName = QStringLiteral("LGA Mighty Tools");
const QString kExeName = QStringLiteral("LGA_MightyTools.exe");
// CLSID propio del activador. Fijo: es la clave que Windows busca en CustomActivator.
const QString kClsidText = QStringLiteral("{3AB03416-FB8F-436A-872F-5FB73C97F32C}");
const CLSID kClsid = {0x3ab03416, 0xfb8f, 0x436a, {0x87, 0x2f, 0x5f, 0xb7, 0x3c, 0x97, 0xf3, 0x2c}};
// IID de INotificationActivationCallback (notificationactivationcallback.h, que MinGW no trae).
const IID kIidActivationCallback = {0x53e31837, 0x6600, 0x4a81, {0x93, 0x95, 0x75, 0xcf, 0xfe, 0x74, 0x6f, 0x94}};

const QString kAumidParent = QStringLiteral("Software\\Classes\\AppUserModelId");
const QString kAumidKey = kAumidParent + QStringLiteral("\\") + kAumid;
const QString kClsidKey = QStringLiteral("Software\\Classes\\CLSID\\") + kClsidText;
const QString kServerKey = kClsidKey + QStringLiteral("\\LocalServer32");

// Declaracion a mano de la interfaz, con la misma disposicion que el SDK de Windows.
struct NotificationUserInputData
{
    LPCWSTR Key;
    LPCWSTR Value;
};

struct INotificationActivationCallback : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Activate(LPCWSTR appUserModelId, LPCWSTR invokedArgs,
                                               const NotificationUserInputData *data, ULONG count) = 0;
};

struct State
{
    bool listening = false;
    DWORD cookie = 0;
    ToastActivation::Handler handler;
    QList<ToastActivation::Activation> pending;
};

State &state()
{
    static State s;
    return s;
}

void deliver(const ToastActivation::Activation &activation)
{
    State &s = state();
    if (s.handler) {
        s.handler(activation);
    } else {
        s.pending.append(activation);
    }
}

QString fromWide(LPCWSTR text)
{
    return text ? QString::fromWCharArray(text) : QString();
}

class Activator : public INotificationActivationCallback
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (!object) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIidActivationCallback)) {
            *object = static_cast<INotificationActivationCallback *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&m_refs)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) {
            delete this;
        }
        return ULONG(refs);
    }

    HRESULT STDMETHODCALLTYPE Activate(LPCWSTR, LPCWSTR invokedArgs, const NotificationUserInputData *data,
                                       ULONG count) override
    {
        ToastActivation::Activation activation;
        activation.arguments = fromWide(invokedArgs);
        for (ULONG i = 0; data && i < count; ++i) {
            activation.inputs.insert(fromWide(data[i].Key), fromWide(data[i].Value));
        }
        qInfo().noquote() << QStringLiteral("[ToastActivation] Click en un aviso: '%1', %2 valores elegidos")
                                 .arg(activation.arguments)
                                 .arg(activation.inputs.size());
        // Se devuelve enseguida al sistema: la app atiende el click en su propio ciclo de eventos.
        if (QCoreApplication *app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, [activation]() { deliver(activation); }, Qt::QueuedConnection);
        }
        return S_OK;
    }

private:
    LONG m_refs = 1;
};

// La fabrica vive lo que el proceso: CoRegisterClassObject la sostiene hasta CoRevokeClassObject.
class Factory : public IClassFactory
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (!object) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *object = static_cast<IClassFactory *>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid, void **object) override
    {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        auto *activator = new Activator();
        const HRESULT hr = activator->QueryInterface(riid, object);
        activator->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

Factory g_factory;

// Si el valor ya es ese, no escribe (el registro no se toca en cada aviso).
bool ensureValue(const QString &key, const QString &name, const QString &value, int *written)
{
    if (RegistryHelper::readString(HKEY_CURRENT_USER, key, name) == value) {
        return true;
    }
    ++*written;
    return RegistryHelper::writeString(HKEY_CURRENT_USER, key, name, value);
}

QString serverCommand()
{
    return QStringLiteral("\"%1\" --toast-activated").arg(RegistryHelper::ownExePath());
}

} // namespace

namespace ToastActivation {

QString appUserModelId()
{
    return kAumid;
}

bool supported()
{
    return true;
}

bool ensureRegistered(const QString &iconPath, QString *detail)
{
    int written = 0;
    bool ok = ensureValue(kServerKey, QString(), serverCommand(), &written);
    ok &= ensureValue(kAumidKey, QStringLiteral("DisplayName"), kDisplayName, &written);
    ok &= ensureValue(kAumidKey, QStringLiteral("CustomActivator"), kClsidText, &written);
    if (!iconPath.isEmpty()) {
        ok &= ensureValue(kAumidKey, QStringLiteral("IconUri"), QDir::toNativeSeparators(iconPath), &written);
    }
    if (detail) {
        *detail = !ok ? QStringLiteral("no se pudo escribir en HKCU")
                      : (written > 0 ? QStringLiteral("anotada (%1 valores)").arg(written) : QStringLiteral("ya estaba"));
    }
    return ok;
}

bool registeredForThisExe()
{
    const QString command = RegistryHelper::readString(HKEY_CURRENT_USER, kServerKey);
    return RegistryHelper::commandPointsTo(command, RegistryHelper::ownExePath())
           && RegistryHelper::readString(HKEY_CURRENT_USER, kAumidKey, QStringLiteral("CustomActivator")) == kClsidText;
}

void listen()
{
    State &s = state();
    if (s.listening) {
        return;
    }
    // STA del hilo principal (ComApartment en main): las llamadas entran por la cola de mensajes, que
    // bombea el ciclo de eventos de Qt.
    const HRESULT hr = CoRegisterClassObject(kClsid, &g_factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &s.cookie);
    if (FAILED(hr)) {
        qWarning().noquote() << QStringLiteral("[ToastActivation] No se pudo atender los clicks de los avisos: 0x%1")
                                    .arg(quint32(hr), 8, 16, QLatin1Char('0'));
        return;
    }
    s.listening = true;
    qInfo() << "[ToastActivation] Atendiendo los clicks de los avisos";
}

void setHandler(Handler handler)
{
    State &s = state();
    s.handler = std::move(handler);
    if (!s.handler) {
        return;
    }
    const QList<Activation> pending = s.pending;
    s.pending.clear();
    for (const Activation &activation : pending) {
        s.handler(activation);
    }
}

void stopListening()
{
    State &s = state();
    if (s.listening) {
        CoRevokeClassObject(s.cookie);
        s.listening = false;
        s.cookie = 0;
    }
    s.handler = nullptr;
}

bool removeIfOwned(QString *detail)
{
    const bool hasServer = RegistryHelper::keyExists(HKEY_CURRENT_USER, kClsidKey);
    const bool hasAumid = RegistryHelper::keyExists(HKEY_CURRENT_USER, kAumidKey);
    if (!hasServer && !hasAumid) {
        *detail = QStringLiteral("no habia nada");
        return true;
    }
    const QString command = RegistryHelper::readString(HKEY_CURRENT_USER, kServerKey);
    const QString target = RegistryHelper::commandExecutable(command);
    const bool mine = RegistryHelper::commandPointsTo(command, RegistryHelper::ownExePath());
    // Una copia de build que ya no existe (la excepcion de build de la regla de registro limpio).
    const bool orphan = !target.isEmpty() && !QFileInfo::exists(target)
                        && QFileInfo(target).fileName().compare(kExeName, Qt::CaseInsensitive) == 0;
    if (!command.isEmpty() && !mine && !orphan) {
        *detail = QStringLiteral("apunta a otro exe, no se toca: %1").arg(target);
        return true;
    }
    bool ok = true;
    if (hasServer) {
        ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, kClsidKey);
    }
    if (hasAumid
        && RegistryHelper::readString(HKEY_CURRENT_USER, kAumidKey, QStringLiteral("CustomActivator")) == kClsidText) {
        ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, kAumidKey);
    }
    // Las claves de arriba, solo si quedaron vacias (la primera anotacion pudo haberlas creado).
    RegistryHelper::deleteKeyIfEmpty(HKEY_CURRENT_USER, kAumidParent);
    RegistryHelper::deleteKeyIfEmpty(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\CLSID"));
    *detail = ok ? QStringLiteral("borrada%1").arg(orphan ? QStringLiteral(" (era de una copia que ya no existe)") : QString())
                 : QStringLiteral("no se pudo borrar");
    return ok;
}

} // namespace ToastActivation
