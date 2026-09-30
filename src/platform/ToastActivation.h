#ifndef MIGHTYTOOLS_TOASTACTIVATION_H
#define MIGHTYTOOLS_TOASTACTIVATION_H

#include <QHash>
#include <QString>

#include <functional>

// Lo que vuelve de un aviso del sistema cuando el usuario hace click en el cuerpo o en un boton.
//
// Windows (platform/win/ToastActivationWin.cpp): la app se anota ante Windows en HKCU con un AUMID
// propio (Software\Classes\AppUserModelId\LGA_MightyTools, con nombre, icono y CustomActivator) y un
// servidor COM local (Software\Classes\CLSID\{...}\LocalServer32 = este exe). Windows entrega el
// click a INotificationActivationCallback::Activate: los argumentos del boton y lo elegido en el
// desplegable. Si la app no corre, Windows la lanza con -Embedding. Es el mismo registro que usan
// Windows App SDK y el Community Toolkit para apps sin paquete; sin .NET ni WinRT compilado.
//
// macOS (platform/mac/ToastActivationMac.cpp): sin activaciones todavia (osascript no tiene
// acciones); todo responde que no.
//
// Registro limpio: lo escribe solo la primera notificacion real (nunca una corrida automatizada), y
// lo borra --uninstall-cleanup si es de este exe, o de un LGA_MightyTools.exe que ya no existe.
namespace ToastActivation {

struct Activation
{
    QString arguments;              ///< los del cuerpo (launch) o los del boton
    QHash<QString, QString> inputs; ///< id del <input> -> id de la opcion elegida
};
using Handler = std::function<void(const Activation &)>;

// AUMID de los avisos de la app.
QString appUserModelId();
// Si esta plataforma entrega activaciones.
bool supported();

// Deja la anotacion apuntando a ESTE exe, con `iconPath` como icono del encabezado. Idempotente:
// solo escribe lo que falta o apunta a otro exe. `detail` dice que hizo o por que fallo.
bool ensureRegistered(const QString &iconPath, QString *detail = nullptr);
// Solo lectura: la anotacion existe y apunta a este exe.
bool registeredForThisExe();

// Atiende las activaciones (registra el servidor COM en este proceso; una sola vez). Las que llegan
// antes de setHandler() quedan guardadas: con la app lanzada por Windows, el click puede llegar
// antes de que existan la bandeja y los modulos.
void listen();
// Entrega lo guardado y todo lo que llegue despues, siempre en el hilo de la UI.
void setHandler(Handler handler);
void stopListening();

// --uninstall-cleanup: borra la anotacion si es de este exe o de un LGA_MightyTools.exe que ya no
// existe. La de otro exe que existe no se toca. true si no quedo nada propio.
bool removeIfOwned(QString *detail);

// El argumento con el que Windows lanza el servidor COM ("-Embedding", "/Embedding") o el nuestro
// ("--toast-activated"): no es un archivo ni un link.
bool isActivationArgument(const QString &argument);

} // namespace ToastActivation

#endif // MIGHTYTOOLS_TOASTACTIVATION_H
