#ifndef MIGHTYTOOLS_WHATSAPPSYSTEM_H
#define MIGHTYTOOLS_WHATSAPPSYSTEM_H

#include <QList>
#include <QString>
#include <QStringList>

// Donde abrir un chat de WhatsApp (WhatsApp Chat, D-78). Solo lectura salvo open().
//
//  - App: la app de WhatsApp instalada (mac: la que abre `whatsapp://`; Windows: la de la Store, que
//    registra el protocolo). Se abre con `whatsapp://send?phone=<digitos>`: el chat sale directo, nuevo o
//    existente, sin pasar por el navegador.
//  - WebApp: WhatsApp Web instalado como app desde Chrome, Brave o Edge (mac: el .app de
//    `~/Applications/<Navegador> Apps.localized`; Windows: el acceso directo `<navegador>_proxy.exe
//    --app-id=...` del menu Inicio). Se abre con el navegador y `--app-id` +
//    `--app-launch-url-for-shortcuts-menu-item=https://web.whatsapp.com/send?phone=...`.
//  - Browser: el navegador por defecto con la misma URL (siempre existe; no se lista).
//
// Nunca wa.me: pasa por una pagina intermedia y despues el navegador pide permiso para abrir WhatsApp.
namespace WhatsAppSystem {

enum class Kind { App, WebApp, Browser };

struct Target
{
    Kind kind = Kind::Browser;
    QString name;        ///< «WhatsApp», «WhatsApp Web»
    QString browser;     ///< WebApp: «Brave», «Chrome», «Edge», «Safari»
    bool running = false;
    QString path;        ///< App: el .app o el exe; WebApp: el acceso directo o el .app de la app web
    QString launcher;    ///< WebApp: el ejecutable del navegador (o su _proxy.exe)
    QString appId;       ///< WebApp Chromium: el --app-id
    QString profile;     ///< WebApp Chromium: el --profile-directory (vacio = el ultimo usado)
};

// Lo instalado ahora: la app (si esta) primero y despues las apps web. Lee el disco y los procesos; no
// guarda nada. En la Mac de Lega tarda pocos ms (un puñado de Info.plist).
QList<Target> detect();

// La URL de WhatsApp Web para un numero (la usan la app web y el navegador).
QString webUrl(const QString &digits);
// La del protocolo de la app.
QString appUrl(const QString &digits);

// Abre el chat. Nunca en una corrida automatizada (lo frena el modulo antes de llamar).
bool open(const Target &target, const QString &digits, QString *error);

// «WhatsApp Web (Brave)»: para la UI y el log.
QString describe(const Target &target);

} // namespace WhatsAppSystem

#endif // MIGHTYTOOLS_WHATSAPPSYSTEM_H
