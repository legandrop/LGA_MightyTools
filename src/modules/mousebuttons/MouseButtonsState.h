#ifndef MIGHTYTOOLS_MOUSEBUTTONSSTATE_H
#define MIGHTYTOOLS_MOUSEBUTTONSSTATE_H

#include "platform/MouseButtonsSystem.h"

#include <QList>
#include <QString>

class ModuleContext;

// Una regla: en la app `app`, mientras se mantiene `button`, se mantienen `keys` y, si `holdLeft`, el boton
// izquierdo. La app se reconoce por el nombre del exe sin distinguir mayusculas, en cualquier carpeta: asi
// sobrevive a que Photoshop cambie de carpeta con cada version. `path` solo sirve para el icono.
struct MouseRule
{
    int id = 0;
    QString app;  ///< "Photoshop.exe"
    QString path; ///< la ruta elegida con Browse..., o vacia
    MouseButtonsSystem::Button button = MouseButtonsSystem::Button::Middle;
    Shortcut keys{Qt::NoModifier, Qt::Key_Space};
    bool holdLeft = true;
    bool enabled = true;

    MouseButtonsSystem::Rule rule() const { return {button, keys, holdLeft}; }
};

// La lista de [mouseButtons] rules= en settings.ini, en JSON como Window Layouts y App Toggle.
class MouseButtonsState
{
public:
    static constexpr int kMaxRules = 99;
    explicit MouseButtonsState(ModuleContext *context = nullptr);

    const QList<MouseRule> &rules() const { return m_rules; }
    // Asigna el id a una regla nueva (0). False si no es valida, si la app ya tiene una regla para ese boton
    // o si no queda lugar.
    bool upsert(MouseRule &rule);
    bool remove(int id);
    void setCaptureRules(const QList<MouseRule> &rules);

    // El nombre del exe de lo que se escribio o se eligio (una ruta o el nombre solo), o vacio si no es un .exe.
    static QString appName(const QString &text);
    static bool sameApp(const QString &a, const QString &b);
    // Lo que el grabador acepta como teclas para mantener: letras, digitos, F1-F12, flechas o Espacio, con o
    // sin modificadores.
    static bool holdKey(int key);
    static Shortcut keysFromString(const QString &text);
    static bool valid(const MouseRule &rule);
    static QList<MouseRule> fromJson(const QString &text);
    static QString toJson(const QList<MouseRule> &rules);

private:
    void save();
    ModuleContext *m_context = nullptr;
    QList<MouseRule> m_rules;
};

#endif // MIGHTYTOOLS_MOUSEBUTTONSSTATE_H
