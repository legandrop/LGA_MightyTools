#ifndef MIGHTYTOOLS_MODULE_H
#define MIGHTYTOOLS_MODULE_H

#include "core/Shortcut.h"

#include <QList>
#include <QObject>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <memory>

class ModuleContext;
class QMenu;
class QWidget;

// Contrato de una herramienta de Mighty Tools (plan, 4.2 y 4.3).
//
// Cada herramienta tiene dos partes:
//  - ModuleDescriptor: datos y funciones ESTATICOS (id, titulo, descripcion, icono, plataformas, la
//    fabrica y los pasos directos). Es lo UNICO que existe de una herramienta apagada: con eso el
//    host dibuja su fila, su interruptor y su panel de apagado.
//  - Module: el objeto vivo. ModuleHost lo construye al prender la herramienta y lo destruye al
//    apagarla. Todo lo que la herramienta toma (atajos, hooks, timers, sockets, COM, caches, panel)
//    cuelga de el y se suelta en stop() o en su destructor.
//
// Un modulo no toca a otro: lo compartido pasa por ModuleContext.
//
// Vida (la garantiza el host):
//  - Prender: create() -> start(). El panel se pide recien cuando el usuario elige la herramienta.
//  - Apagar o salir: stop(); el panel con deleteLater; el modulo con release() de su unique_ptr y
//    deleteLater; el contexto (no es QObject) se borra al recibir destroyed() del modulo, asi sigue
//    vivo mientras corre el destructor del modulo. Con deleteLater un modal del modulo abierto en la
//    pila (QFileDialog, exec() de un popup) no queda apuntando a un objeto muerto.
//  - stop() cierra las ventanas de nivel superior que el modulo haya abierto con window() de padre
//    (carteles no modales, popups); las lambdas que las tocan usan QPointer.
//  - El destructor del contexto es la red de seguridad: suelta los atajos y sus declaraciones y
//    devuelve la ventana si el modulo la habia escondido (hideWindowTemporarily()).
//  - El modulo se construye SIEMPRE sin padre (lo posee un unique_ptr del host).
//  - Las acciones que agrega en fillTrayMenu() se conectan con el modulo como contexto, asi se
//    desconectan solas al borrarlo. El host rearma el menu en aboutToShow y al borrar un modulo,
//    nunca con el menu abierto; el modulo solo agrega entradas, nunca vacia el menu.
//
// Captura (--ui-shot, --ui-probe): el host construye el modulo con captureMode() = true y NO llama
// a start(). El arnes fija un estado con applyCaptureState() y pide el panel o createCaptureWidget().

// Tono del estado de una herramienta: el punto de su fila, el chip y el menu de la bandeja.
enum class ModuleTone {
    Off,       ///< apagada (lo pone el host, un modulo vivo nunca lo devuelve)
    Active,    ///< prendida y funcionando (verde)
    Paused,    ///< prendida pero en pausa por el usuario (gris)
    Attention, ///< necesita algo del usuario: sin calibrar, sin permiso, disco bajo... (ambar)
    Error,     ///< algo fallo: un atajo tomado por otra app (rojo)
};

struct ModuleStatus
{
    ModuleTone tone = ModuleTone::Active;
    QString text; ///< linea corta de la fila de la lista: "On · Nuke in front", "D: is low"
};

enum ModulePlatform {
    PlatformWindows = 0x1,
    PlatformMac = 0x2,
};

// Lectura de la seccion [<id>] de settings.ini sin construir el modulo (pasos directos, atajos de
// herramientas apagadas). Solo lectura.
using SettingsReader = std::function<QVariant(const QString &key, const QVariant &defaultValue)>;

// Aviso que el host muestra en el panel de una herramienta APAGADA cuando el sistema todavia le
// manda cosas (D-05, D-09): "Windows still sends links here" con "Remove as browser".
struct ModuleOffNotice
{
    bool visible = false;
    QString title;
    QString caption;
    QString actionText; ///< vacio = sin boton; el boton llama a releaseSystem() del descriptor
};

// Una entrada del sistema: un .nk o una URL (plan 4.5).
enum class ExternalResult {
    NotMine, ///< no era de esta herramienta
    Done,    ///< atendida (con exito o con su aviso de error ya mostrado)
    Pending, ///< sigue trabajando (socket, cuenta regresiva): avisara con finished()
};

struct ExternalRequest
{
    QString argument;
    bool moduleEnabled = false; ///< apagada: paso directo (D-05), sin reglas ni bridge
    bool resident = false;      ///< true: app residente (mac); false: modo corto de Windows
    bool dryRun = false;        ///< corrida automatizada: solo loguea lo que haria, no abre ni lanza
    SettingsReader value;       ///< su seccion de settings.ini
    // Obligatorio si se devolvio Pending: el modo corto corre app.exec() hasta este llamado y sale
    // con ese codigo (con un tope de seguridad de 30 s, pasado el cual sale con 1). En la app
    // residente el host nunca sale por esto. Lo que sigue trabajando cuelga de qApp y se borra solo
    // (deleteLater) al llamar a finished.
    std::function<void(int exitCode)> finished;
};

struct ModuleDescriptor
{
    QString id;             ///< estable para siempre: clave de su seccion en settings.ini
    QString title;          ///< "Nuke Shortcuts"
    QString description;    ///< una oracion, la del encabezado del panel y del primer arranque
    QStringList offBullets; ///< lo que hace al prenderse, para el panel de apagado
    int platforms = PlatformWindows | PlatformMac;

    // Icono de interfaz, monocromo y vectorial (regla de UI: nada de PNG en la interfaz).
    std::function<void(QPainter &painter, const QRectF &rect, const QColor &color)> paintIcon;

    // Construye el modulo sin prenderlo: el host llama start() despues (salvo en captura).
    std::function<std::unique_ptr<class Module>(ModuleContext &context)> create;

    // ---- Opcionales

    // Los atajos que la herramienta tiene configurados, leidos de su seccion aunque este apagada:
    // asi el grabador de otra herramienta avisa el choque antes de que las dos esten prendidas.
    std::function<QList<Shortcut>(const SettingsReader &value)> configuredShortcuts;

    // Aviso del panel de apagado. `captureState` vacio = leer el sistema; si no, el estado de prueba
    // de la captura (sin leer nada).
    std::function<ModuleOffNotice(const QString &captureState)> offNotice;
    // Suelta lo que la herramienta dejo en el sistema (asociacion .nk, registro como navegador) y es
    // de ESTE exe por contenido (el comando apunta a este exe); lo de otra copia o de otra app queda.
    // La usan el boton del aviso de apagado y --uninstall-cleanup. Nunca corre en una corrida
    // automatizada, salvo el self-test sobre un hive privado (qa/RegistryHiveTest).
    std::function<bool(QString *error)> releaseSystem;

    // Decide, SIN efectos, si una entrada del sistema es de esta herramienta. Con esto main elige
    // el modo corto antes de la instancia unica.
    std::function<bool(const QString &argument)> claimsExternal;
    // Atiende la entrada sin construir el modulo: modo corto de Windows (prendida o apagada) y
    // herramienta apagada en la app residente de mac.
    std::function<ExternalResult(const ExternalRequest &request)> runExternal;

    // --self-test: la logica del modulo sin pantalla, con sus casos negativos. `check(ok, que)`.
    std::function<void(const std::function<void(bool ok, const QString &what)> &check)> selfTest;
    // --simulate-action <accion>: la secuencia real en modo solo loguear (inyector, ruteo de un
    // link, envio de un .nk). Devuelve el codigo de salida; los nombres los documenta cada modulo.
    std::function<int(const QString &action, const QStringList &args)> simulateAction;
};

class Module : public QObject
{
    Q_OBJECT

public:
    explicit Module(ModuleContext &context) : QObject(nullptr), m_context(context) {}
    ~Module() override = default;

    // Idempotentes. stop() suelta todo lo que el modulo tomo; despues el host lo destruye.
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual ModuleStatus status() const = 0;

    // El panel de la derecha. El host lo pide recien cuando el usuario elige la herramienta y lo
    // destruye al apagarla (el panel es hijo de `parent`, el host se queda con el puntero).
    virtual QWidget *createPanel(QWidget *parent) = 0;

    // Sus entradas en el menu de la bandeja, dentro de la seccion con su nombre (D-15). Se llama cada
    // vez que se arma el menu. Sin entradas, la seccion no aparece.
    virtual void fillTrayMenu(QMenu *menu) { Q_UNUSED(menu); }

    // Lineas extra del tooltip del icono de la bandeja ("D: 42 GB free").
    virtual QStringList trayTooltipLines() const { return {}; }

    // True si la herramienta esta "en pausa" por el usuario (el icono de la bandeja se atenua cuando
    // todas las prendidas lo estan, o no hay ninguna prendida).
    virtual bool isPaused() const { return false; }

    // Una entrada del sistema con la herramienta prendida en la app residente (mac: QFileOpenEvent).
    // Misma semantica que ModuleDescriptor::runExternal.
    virtual ExternalResult handleExternal(const ExternalRequest &request) { Q_UNUSED(request); return ExternalResult::NotMine; }

    // QA (--ui-shot). Los estados que sabe dibujar el modulo construido en modo captura; fijar uno
    // antes de pedir el panel (false si no existe: el arnes lo reporta como error, nunca lo
    // reemplaza por otro). createCaptureWidget() arma lo que NO es el panel (dialogo, popup, burbuja,
    // menu) sin exec() ni show(); nullptr cuando el estado es del panel.
    virtual QStringList captureStates() const { return {}; }
    virtual bool applyCaptureState(const QString &state) { Q_UNUSED(state); return false; }
    virtual QWidget *createCaptureWidget(const QString &state, QWidget *parent) { Q_UNUSED(state); Q_UNUSED(parent); return nullptr; }

signals:
    // Cambio lo que devuelve status(), trayTooltipLines() o isPaused(): el host redibuja la fila,
    // el encabezado del panel y la bandeja.
    void statusChanged();

protected:
    ModuleContext &context() const { return m_context; }

private:
    ModuleContext &m_context;
};

#endif // MIGHTYTOOLS_MODULE_H
