#ifndef MIGHTYTOOLS_MEETFRAMERULES_H
#define MIGHTYTOOLS_MEETFRAMERULES_H

#include <QList>
#include <QRect>
#include <QRegion>
#include <QSet>
#include <QString>
#include <QStringList>

// Meet Frame: que se tapa en lo que se comparte. Logica pura, sin leer el sistema: la usan la captura de
// Windows (sobre la lista de ventanas en orden Z), la de mac (para armar el filtro de ScreenCaptureKit) y el
// --self-test.
//
// Una app se reconoce por el nombre del exe en Windows (sin distinguir mayusculas, en cualquier carpeta) y por
// el bundle id en mac. Las apps que viven en el navegador (WhatsApp Web) se reconocen por palabras del titulo
// de la ventana del navegador: se tapa la ventana entera mientras esa pestaña esta al frente.
namespace MeetFrameRules {

// Un grupo de apps listo para prender y apagar en el panel.
struct Group
{
    QString id;                 ///< estable: la clave de settings es "group_<id>"
    bool defaultOn = false;
    QStringList apps;           ///< exes (Windows) o bundle ids (mac) de ESTA plataforma, en minusculas
    QStringList browserTitles;  ///< palabras del titulo de una ventana de navegador, en minusculas
};

QList<Group> groups();
// Los exes o bundle ids de los navegadores de esta plataforma, en minusculas.
QSet<QString> browsers();
// El proceso que muestra los avisos del sistema (Windows: ShellExperienceHost.exe; mac: el Centro de
// notificaciones), en minusculas.
QStringList notificationApps();

// Lo que se tapa, ya resuelto desde los ajustes.
struct Rules
{
    QSet<QString> apps;         ///< en minusculas
    QStringList browserTitles;  ///< en minusculas
    QSet<QString> browsers;     ///< en minusculas
    bool hideDesktop = true;
    bool hideNotifications = true;

    bool operator==(const Rules &other) const
    {
        return apps == other.apps && browserTitles == other.browserTitles && browsers == other.browsers
            && hideDesktop == other.hideDesktop && hideNotifications == other.hideNotifications;
    }
    bool operator!=(const Rules &other) const { return !(*this == other); }
};

// Una ventana de nivel superior, en coordenadas nativas, tal como la ve la captura.
struct WindowInfo
{
    QRect rect;            ///< el marco visible (sin los bordes invisibles de Windows)
    QString app;           ///< exe o bundle id, en minusculas
    QString className;     ///< Windows: la clase de la ventana
    QString title;         ///< solo se lee para los navegadores
    bool covering = true;  ///< opaca: tapa lo que hay debajo (las transparentes o excluidas de captura, no)
    bool desktop = false;  ///< el escritorio (Windows: Progman / WorkerW): lo que sigue abajo no se ve
    bool forceMask = false;///< la ventana compartida misma, si queda a la vista: se tapa siempre (si no, espejo infinito)
    bool ignored = false;  ///< el marco de Meet Frame: no sale en la captura
};

// True si esta ventana se tapa.
bool hides(const WindowInfo &window, const Rules &rules);

struct Mask
{
    QRegion region;   ///< lo que se tapa, en coordenadas nativas
    int hiddenWindows = 0;
};

// Recorre las ventanas de arriba hacia abajo: de cada ventana a tapar se tapa la parte que se ve (lo que no
// tapa ninguna ventana opaca de arriba). Lo que no cubre ninguna ventana es el escritorio. Ante la duda tapa
// de mas, nunca de menos: una ventana que no se sabe si es opaca no cuenta como que tapa.
Mask computeMask(const QList<WindowInfo> &topFirst, const QRect &region, const Rules &rules);

// Ajustes guardados -> reglas. `customApps` son los exes / bundle ids que agrego el usuario.
Rules build(const QSet<QString> &enabledGroups, const QStringList &customApps, bool hideDesktop, bool hideNotifications);

// El exe (Windows) o el id de una entrada escrita o elegida, en minusculas; vacio si no sirve.
QString appKey(const QString &text);

} // namespace MeetFrameRules

#endif // MIGHTYTOOLS_MEETFRAMERULES_H
