#ifndef MIGHTYTOOLS_INPUTINJECTOR_H
#define MIGHTYTOOLS_INPUTINJECTOR_H

#include <QPoint>
#include <QStringList>

// Clicks y teclas simulados. Es la parte delicada de la app: Nuke no expone por API "el knob bajo
// el puntero" ni el Dope Sheet, asi que la unica forma de hacer las dos acciones es imitar al
// usuario. Una implementacion por plataforma:
//  - Windows (platform/win/InputInjectorWin.cpp): SendInput y SetCursorPos.
//  - macOS   (platform/mac/InputInjectorMac.cpp): CGEventPost. Pide el permiso de Accesibilidad.
//
// Coordenadas NATIVAS de pantalla (ver NukeWatcher.h).
//
// dryRun = true no toca el mouse ni el teclado: solo anota cada paso en steps(). Es el modo de los
// arneses de prueba (--simulate-action) y de toda corrida automatizada. steps() se anota SIEMPRE,
// con o sin dryRun, para que el log muestre exactamente lo que se mando.
class InputInjector
{
public:
    enum class Button { Left, Right };
    enum class Key { Down, Return, A, F };

    explicit InputInjector(bool dryRun);

    bool dryRun() const { return m_dryRun; }
    const QStringList &steps() const { return m_steps; }

    QPoint cursorPos() const;
    void moveCursor(const QPoint &nativePoint);
    // Mueve el puntero a `nativePoint` y hace click con `button`.
    void clickAt(Button button, const QPoint &nativePoint);

    // Suelta los modificadores que el usuario todavia tiene apretados del atajo. Sin esto, la `F`
    // del Dope Sheet le llega a Nuke como Ctrl+Alt+Shift+F. La version AutoHotkey hacia lo mismo
    // con {Ctrl Down}{Ctrl Up}{Alt Down}{Alt Up}. En macOS no hace falta: cada evento lleva sus
    // propios modificadores explicitos.
    void releaseModifiers();

    // Aprieta y suelta una tecla. `withPrimary` la manda con Ctrl (Windows) o Cmd (macOS), que es
    // la tecla que usa Nuke para "seleccionar todo".
    void tapKey(Key key, bool withPrimary = false);

    static QString keyName(Key key);

private:
    void note(const QString &step);

    bool m_dryRun = true;
    QStringList m_steps;
};

#endif // MIGHTYTOOLS_INPUTINJECTOR_H
