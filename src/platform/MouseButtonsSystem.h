#ifndef MIGHTYTOOLS_MOUSEBUTTONSSYSTEM_H
#define MIGHTYTOOLS_MOUSEBUTTONSSYSTEM_H

#include "core/Shortcut.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

// Mouse Buttons (solo Windows): mientras se mantiene apretado un boton del mouse sobre una app elegida, se
// mantienen apretadas unas teclas y, si se pide, el boton izquierdo. Lo que se hace con cada evento lo
// decide una maquina de estados pura (MouseButtonsSystemCommon.cpp), que el self-test prueba con
// secuencias falsas. El gancho WH_MOUSE_LL y SendInput viven en platform/win/MouseButtonsSystemWin.cpp.
namespace MouseButtonsSystem {

enum class Button { Middle, Back, Forward }; ///< Back = X1, Forward = X2
constexpr int kButtonCount = 3;

struct Rule
{
    Button button = Button::Middle;
    Shortcut keys{Qt::NoModifier, Qt::Key_Space}; ///< modificadores + una tecla
    bool holdLeft = true;

    bool operator==(const Rule &other) const
    {
        return button == other.button && keys == other.keys && holdLeft == other.holdLeft;
    }
};

// Una tecla o el boton izquierdo, abajo o arriba. `key` es una Qt::Key; los modificadores van como
// Qt::Key_Control, Qt::Key_Alt, Qt::Key_Shift y Qt::Key_Meta (Win).
struct Step
{
    enum Kind { KeyDown, KeyUp, LeftDown, LeftUp };
    Kind kind = KeyDown;
    int key = 0;

    bool operator==(const Step &other) const { return kind == other.kind && key == other.key; }
};

struct Event
{
    Button button = Button::Middle;
    bool down = true;
    bool ownInjected = false; ///< lo mando esta app (marca kMightyToolsInputTag): nunca se toma
    bool overTarget = false;  ///< el puntero esta sobre la app de la regla y esa app esta al frente
};

struct Result
{
    bool swallow = false; ///< el evento no sigue: no le llega a ninguna app
    QList<Step> steps;    ///< lo que hay que inyectar, en orden
};

// Lo que se aprieta, en orden: modificadores (Ctrl, Alt, Shift, Win), la tecla y el boton izquierdo.
QList<Step> pressSteps(const Rule &rule);

// Una por gancho; vive en su hilo. Sin estado global.
class Machine
{
public:
    explicit Machine(const QList<Rule> &rules = {});

    Result handle(const Event &event);
    // Suelta todo lo que esta apretado (cambio de frente, regla apagada, gancho que se va).
    QList<Step> releaseAll();
    bool holding() const;

private:
    const Rule *ruleFor(Button button) const;
    static QList<Step> releaseOf(const QList<Step> &pressed);

    QList<Rule> m_rules;
    QList<Step> m_pressed[kButtonCount];
    bool m_held[kButtonCount] = {};
};

// ---- Windows

// True si la app de `pid` no recibiria la inyeccion: corre como administrador y esta app no (UIPI), o no
// se puede saber. Solo lectura.
bool injectionBlocked(quint32 pid);
// Lo que se mandaria a SendInput por cada paso (tecla virtual, escaneo, marca propia), para el log y
// --simulate-action. No manda nada.
QStringList describeInputs(const QList<Step> &steps);
// Ganchos de mouse instalados ahora en el proceso. Con la herramienta apagada o sin su app al frente, 0.
int liveHooks();

// El gancho de las reglas de UNA app (el proceso `pid`), en su propio hilo con su bucle de mensajes: si la
// ventana de Mighty Tools se traba, el mouse del sistema sigue andando. Destruirlo suelta todo lo que
// quedo apretado. Nunca se instala en una corrida automatizada (devuelve nullptr).
class Hook
{
public:
    static std::unique_ptr<Hook> install(const QList<Rule> &rules, quint32 pid, QString *error);
    ~Hook();
    bool holding() const;

private:
    Hook();
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace MouseButtonsSystem

#endif // MIGHTYTOOLS_MOUSEBUTTONSSYSTEM_H
