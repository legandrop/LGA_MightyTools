#ifndef MIGHTYTOOLS_NUKESHORTCUTSSTATE_H
#define MIGHTYTOOLS_NUKESHORTCUTSSTATE_H

#include "core/Shortcut.h"

#include <QObject>
#include <QPointF>
#include <QString>

class ModuleContext;

// Las dos acciones que la herramienta sabe hacer sobre Nuke.
enum class ShortcutAction { AddKeyframe, FrameDopeSheet };

// Estado de Nuke Shortcuts que ve el usuario: la UNICA fuente de verdad de la herramienta (la parte
// de atajos del AppState de Nuke Shortcuts). La tarjeta de estado, la fila de la lista y las
// entradas de la bandeja leen de aca y escriben aca.
//
// Con un ModuleContext lee y escribe la seccion [nukeShortcuts] de settings.ini. Sin contexto
// (captura, self-test) no toca el disco nunca.
class NukeShortcutsState : public QObject
{
    Q_OBJECT

public:
    // Que paso la ultima vez que se intento registrar el atajo de una accion.
    enum class Registration {
        Idle,       ///< no esta registrado porque no hace falta (Nuke no esta al frente, o en pausa)
        Registered, ///< registrado: la combinacion le llega a la app
        Failed,     ///< rechazado: otra app (o otra herramienta) ya tiene esa combinacion
    };

    explicit NukeShortcutsState(ModuleContext *context, QObject *parent = nullptr);

    // ---- Persistente
    bool paused() const { return m_paused; }
    Shortcut shortcut(ShortcutAction action) const;
    // Punto guardado del Dope Sheet, en fraccion de la ventana principal de Nuke (0..1 en x e y).
    bool hasDopeSheetSpot() const { return m_hasDopeSheetSpot; }
    QPointF dopeSheetSpot() const { return m_dopeSheetSpot; }

    // ---- De esta sesion
    bool nukeInFront() const { return m_nukeInFront; }
    // El plugin de Nuke (`<.nuke>/LGA_NukeShortcuts`, D-41) esta instalado: la herramienta necesita las
    // dos cosas, la app y el plugin (Lega, 2026-10-02). Lo actualiza el modulo.
    bool pluginInstalled() const { return m_pluginInstalled; }
    // Y es una version que hace "Frame Dope Sheet" dentro de Nuke (1.01+, D-42): la calibracion del
    // Dope Sheet no hace falta y no se muestra.
    bool pluginFramesDopeSheet() const { return m_pluginFramesDopeSheet; }
    Registration registration(ShortcutAction action) const;
    // La otra herramienta que declaro primero la combinacion de `action` (vacio = otra app o nadie).
    QString conflictWith(ShortcutAction action) const;
    // Permiso de Accesibilidad de macOS. En Windows siempre true.
    bool accessibilityGranted() const { return m_accessibilityGranted; }

    // Cada setter escribe (si corresponde) y avisa SOLO si el valor cambio.
    void setPaused(bool paused);
    void setShortcut(ShortcutAction action, const Shortcut &shortcut);
    void setDopeSheetSpot(const QPointF &fraction);
    void setNukeInFront(bool inFront);
    void setPluginInstalled(bool installed);
    void setPluginFramesDopeSheet(bool frames);
    void setRegistration(ShortcutAction action, Registration registration, const QString &conflictWith = QString());
    void setAccessibilityGranted(bool granted);

    static QString actionTitle(ShortcutAction action);
    // Claves de la seccion [nukeShortcuts] (las lee tambien el descriptor sin construir el modulo).
    static QString shortcutKey(ShortcutAction action);

signals:
    void changed();

private:
    void writeValue(const QString &key, const QVariant &value);

    ModuleContext *m_context = nullptr;
    bool m_paused = false;
    Shortcut m_addKeyframe = Shortcut::defaultAddKeyframe();
    Shortcut m_frameDopeSheet = Shortcut::defaultFrameDopeSheet();
    bool m_hasDopeSheetSpot = false;
    QPointF m_dopeSheetSpot;

    bool m_nukeInFront = false;
    bool m_pluginInstalled = true;
    bool m_pluginFramesDopeSheet = false;
    Registration m_addKeyframeRegistration = Registration::Idle;
    Registration m_frameRegistration = Registration::Idle;
    QString m_addKeyframeConflict;
    QString m_frameConflict;
    bool m_accessibilityGranted = true;
};

#endif // MIGHTYTOOLS_NUKESHORTCUTSSTATE_H
