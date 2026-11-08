#ifndef MIGHTYTOOLS_APPTOGGLESYSTEM_H
#define MIGHTYTOOLS_APPTOGGLESYSTEM_H

#include <QIcon>
#include <QList>
#include <QString>
#include <QtGlobal>

// App Toggle: abrir una app, traerla al frente o minimizarla con un atajo. La decision es una funcion
// pura (AppToggleSystemCommon.cpp) sobre una foto del sistema; leer el sistema y ejecutar la decision
// tiene una implementacion por plataforma:
//  - Windows (platform/win/AppToggleSystemWin.cpp): procesos por ruta de imagen, ventanas de nivel
//    superior en orden Z, ShellExecuteEx, ShowWindow y SetForegroundWindow.
//  - macOS (platform/mac/AppToggleSystemMac.mm): NSRunningApplication por la ruta del .app y
//    NSWorkspace; actua sobre la app entera (activar u ocultar), no sobre una ventana.
namespace AppToggleSystem {

// Lo que abre la entrada. `workingDir` vacio = la carpeta de la app (solo Windows).
struct Target
{
    QString path;      ///< Windows: el .exe; mac: el .app
    QString arguments; ///< tal cual se escribieron
    QString workingDir;
};

// Una ventana de nivel superior de un proceso de la app (Windows). Las del resto no se miran.
struct Window
{
    quintptr id = 0;
    QString processPath;
    bool visible = false;
    bool minimized = false;
    bool owned = false;
    bool toolWindow = false;
    bool appWindow = false; ///< WS_EX_APPWINDOW: va a la barra de tareas aunque tenga dueno (apps VB6)
    bool cloaked = false;
    QString className;      ///< solo para el log
};

// Foto del sistema para una app, solo lectura.
struct Snapshot
{
    QStringList processPaths; ///< rutas de imagen de los procesos vivos que pueden ser la app
    QList<Window> windows;    ///< Windows: sus ventanas de nivel superior, la primera es la de arriba
    quintptr foreground = 0;  ///< Windows: GA_ROOTOWNER de la ventana del frente
    QString foregroundPath;   ///< ruta del proceso (Windows) o del .app (mac) que esta al frente
    bool wholeApp = false;    ///< mac: se actua sobre la app, sin mirar ventanas
    // Windows: la app es el Explorador. Su proceso siempre corre (es el escritorio y la barra de tareas):
    // `windows` trae solo sus ventanas de carpetas y, sin ninguna, se abre una nueva.
    bool shell = false;
};

enum class Action {
    Launch,   ///< no corre: abrirla
    Nothing,  ///< corre sin ventana que se pueda mostrar (solo en la bandeja): no se hace nada
    Minimize, ///< esta al frente: minimizarla (mac: ocultarla, como Cmd+H)
    Activate, ///< corre sin el foco: traerla al frente (restaurandola si esta minimizada)
};

struct Decision
{
    Action action = Action::Nothing;
    quintptr window = 0;  ///< Windows: la ventana sobre la que se actua
    bool restore = false; ///< Windows: estaba minimizada
    QString reason;       ///< para el log, en castellano
};

// Ruta comparable: separadores unificados, sin "." ni "..", sin barra final y, en Windows, sin
// distinguir mayusculas.
QString normalizedPath(const QString &path);
bool samePath(const QString &a, const QString &b);

// Las que tienen boton en la barra de tareas: visibles o minimizadas, no ocultas por DWM y sin dueno ni
// WS_EX_TOOLWINDOW, o con WS_EX_APPWINDOW. Esta ultima regla es la de la barra de tareas: XYplorer (VB6)
// tiene su ventana principal con un dueno oculto y WS_EX_APPWINDOW.
bool eligible(const Window &window);
// La decision, sin efectos. Windows: la primera ventana elegible en orden Z es la ultima usada.
Decision decide(const QString &appPath, const Snapshot &snapshot);
// Una linea para el log y para --simulate-action.
QString describe(const Decision &decision);

// Lee el sistema (solo lectura): procesos, ventanas y la ventana del frente.
Snapshot snapshot(const QString &appPath);
// Ejecuta la decision. Nunca en una corrida automatizada (ahi el modulo solo loguea).
bool run(const Decision &decision, const Target &target, QString *error);

// Nombre para la entrada nueva: el nombre del producto (Windows: la descripcion del exe, la que muestra el
// Administrador de tareas, o su ProductName; mac: CFBundleDisplayName / CFBundleName). Vacio si no tiene.
QString productName(const QString &appPath);

// El icono de la app, el mismo que muestra el sistema. Nulo si no existe en el disco. En Windows se lee del
// shell directamente: la captura de QA (plataforma offscreen) muestra el mismo que ve el usuario.
QIcon icon(const QString &appPath);

// Una captura (--ui-shot --dpr N) dibuja a una escala que no es la de ninguna pantalla: icon() suma esos
// tamanos. 0 (por defecto) = solo las pantallas.
void setCaptureIconScale(qreal scale);
qreal captureIconScale();

// Una entrada de ejemplo para la primera vez. `shortcut` en texto portable.
struct Suggestion
{
    QString name;
    QString path;
    QString shortcut;
};
// Las entradas de ejemplo, en orden. Windows: Win+E para XYplorer si esta instalado (el atajo que su
// AutoHotkey le daba), si no para el Explorador; despues OBS. mac: OBS. Lee el disco y el registro, nunca
// escribe. OBS va aunque no este instalado: es el ejemplo.
QList<Suggestion> suggestedApps();
#ifdef Q_OS_WIN
// %WINDIR%\explorer.exe
QString explorerPath();
// El Photoshop.exe instalado mas nuevo (Program Files\Adobe\Adobe Photoshop *), o vacio.
QString photoshopPath();
#endif

} // namespace AppToggleSystem

#endif // MIGHTYTOOLS_APPTOGGLESYSTEM_H
