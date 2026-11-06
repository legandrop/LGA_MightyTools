#ifndef MIGHTYTOOLS_MEETFRAMESYSTEM_H
#define MIGHTYTOOLS_MEETFRAMESYSTEM_H

#include "modules/meetframe/MeetFrameRules.h"

#include <QColor>
#include <QIcon>
#include <QList>
#include <QMargins>
#include <QObject>
#include <QRect>
#include <QString>

#include <memory>

class QWidget;

// Meet Frame: la captura de una region y la ventana que se comparte. Una implementacion por plataforma:
//  - Windows (platform/win/MeetFrameSystemWin.cpp): captura del monitor con DXGI Desktop Duplication (GDI si
//    no se puede), las ventanas en orden Z para saber que tapar, y una ventana Win32 propia al fondo de todo
//    («Meet Frame») que es la que se elige en Teams, Meet o Zoom. El marco no sale en ninguna captura
//    (WDA_EXCLUDEFROMCAPTURE).
//  - macOS (platform/mac/MeetFrameSystemMac.mm): ScreenCaptureKit con un filtro que deja afuera las apps a
//    tapar, el Centro de notificaciones, el escritorio y la propia app; la ventana compartida es una ventana Qt.
//
// Nada de esto corre en una corrida automatizada: el modulo nunca crea una sesion ahi.
namespace MeetFrameSystem {

struct Options
{
    MeetFrameRules::Rules rules;
    QColor fill;         ///< el color liso con que se tapa (Windows; en mac lo tapado simplemente no se dibuja)
    bool showCursor = true;
    int fps = 30;
    bool paused = false; ///< la ventana compartida se queda con la ultima imagen
};

class Session : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~Session() override = default;

    // La region en las coordenadas que devuelve captureRegion().
    virtual void setRegion(const QRect &region) = 0;
    virtual void setOptions(const Options &options) = 0;
    // Ventanas propias que no cuentan (el marco y sus menus): no salen en la captura.
    virtual void setIgnoredWindows(const QList<QWidget *> &windows) = 0;
    virtual int hiddenWindows() const = 0;
    // Vacio si todo anda; si no, el motivo para la UI (en ingles, por I18n::tr).
    virtual QString error() const = 0;
    // Para el log: "dxgi", "gdi", "screencapturekit".
    virtual QString engine() const = 0;

signals:
    // Cambio hiddenWindows() o error().
    void changed();
    // El usuario cerro la ventana compartida (desde la barra de tareas): se esconde el marco.
    void closeRequested();
};

// Crea la ventana compartida con ese titulo y arranca la captura. nullptr con `error` si no se pudo.
std::unique_ptr<Session> start(const QString &windowTitle, const QRect &region, const Options &options, QString *error);

// La region a capturar: el rectangulo de la ventana del marco menos sus margenes (en pixeles logicos de Qt),
// en las coordenadas que usa la captura (Windows: pixeles fisicos del escritorio virtual; mac: puntos).
QRect captureRegion(QWidget *frame, const QMargins &margins);
// Ese tamaño en pixeles reales (lo que recibe la reunion), para mostrarlo en la barra del marco.
QSize pixelSize(QWidget *frame, const QRect &region);

// El marco: no toma foco y no sale en ninguna captura (tampoco en la de pantalla completa de la reunion).
// Con la ventana nativa ya creada y antes de mostrarla.
void prepareFrame(QWidget *frame);
// Un menu o popup del marco: no sale en ninguna captura.
void excludeFromCapture(QWidget *window);

// mac: permiso de Grabacion de pantalla. Windows: siempre true.
bool hasCapturePermission();
// Pide el permiso (mac: el cartel del sistema la primera vez) o abre los Ajustes donde se da.
void requestCapturePermission();

// Las apps con ventana abierta, para el menu «Add app»: id (exe o bundle id), nombre visible y ruta.
struct RunningApp
{
    QString id;
    QString name;
    QString path;
};
QList<RunningApp> runningApps();
// El id de una app elegida con Browse… (Windows: el .exe; mac: el bundle id del .app). Vacio si no sirve.
QString appIdForPath(const QString &path);

// Solo lectura, para --simulate-action meetFrame:windows: las ventanas de nivel superior como las ve la
// captura (Windows); vacio en mac.
QList<MeetFrameRules::WindowInfo> listWindows(const QRect &region);

} // namespace MeetFrameSystem

#endif // MIGHTYTOOLS_MEETFRAMESYSTEM_H
