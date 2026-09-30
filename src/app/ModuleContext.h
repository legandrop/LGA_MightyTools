#ifndef MIGHTYTOOLS_MODULECONTEXT_H
#define MIGHTYTOOLS_MODULECONTEXT_H

#include "core/Shortcut.h"

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVariant>

class ForegroundWatcher;
class InputInjector;
class QWidget;

// Atajos globales de UN modulo. Todos los modulos comparten un solo HotkeyService del proceso (plan
// 4.4): el host le asigna a cada modulo su rango de ids, crea el servicio con el primer modulo que
// lo pide y lo destruye cuando ya ningun modulo prendido lo usa.
//
// Los ids que ve el modulo son locales (1, 2...). "Declarar" un atajo es anunciar la combinacion
// configurada aunque no este registrada en este momento (Nuke Shortcuts solo registra con Nuke al
// frente). Politica de choques entre herramientas:
//  - Gana la que declaro primero. declare() devuelve el titulo de la otra herramienta si ya la tenia
//    (y entonces no la declara); vacio si quedo declarada.
//  - registerHotkey() rechaza una combinacion declarada por otra herramienta, sin llamar al sistema.
//  - El grabador pregunta declaredByOtherModule() ANTES de probe(): "Already used by Folder Switch."
//    en vez de "taken by another app" (RegisterHotKey tambien rechaza duplicados del mismo proceso).
//    declaredByOtherModule() cuenta tambien los atajos configurados de herramientas apagadas
//    (ModuleDescriptor::configuredShortcuts); declare() compite SOLO contra las prendidas, asi
//    prender una herramienta nunca la deja sin atajo por una apagada.
//
// En una corrida automatizada (ModuleContext::automatedRun()) esta clase NO llama a
// RegisterHotKey / RegisterEventHotKey: cuenta los registros y responde como si el sistema los
// aceptara. Asi la medicion "atajos en 0 despues de stop()" sigue valiendo sin tocar el sistema.
class ModuleHotkeys : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~ModuleHotkeys() override = default;

    virtual bool registerHotkey(int localId, const Shortcut &shortcut) = 0;
    virtual void unregisterHotkey(int localId) = 0;
    virtual void unregisterAll() = 0;
    virtual bool isRegistered(int localId) const = 0;

    // Si el sistema acepta la combinacion AHORA (otra app puede tenerla).
    virtual bool probe(const Shortcut &shortcut) = 0;
    // Le devuelve la combinacion a la app del frente (ver HotkeyService::passThrough).
    virtual void passThrough(const Shortcut &shortcut) = 0;

    // Combinacion configurada de una accion de este modulo; un Shortcut invalido la retira.
    virtual QString declare(int localId, const Shortcut &shortcut) = 0;
    // Titulo de la OTRA herramienta que ya tiene `shortcut` (prendida o apagada), o vacio.
    virtual QString declaredByOtherModule(const Shortcut &shortcut) const = 0;

signals:
    void activated(int localId);
};

// Un aviso con un desplegable y un boton al lado (Windows; en mac sale el aviso simple). Elegir una
// opcion y apretar el boton llega al modulo como Module::noticeAction(action, key, id elegido).
struct NoticeChoice
{
    QString key;                            ///< que aviso es (la raiz de un disco); uno nuevo con la misma key reemplaza al anterior
    QString label;                          ///< "Remind me again in"
    QList<QPair<QString, QString>> options; ///< id, texto
    QString defaultId;                      ///< el preseleccionado
    QString button;                         ///< "Remind me"
    QString action;                         ///< lo que recibe noticeAction ("snooze")
    bool persistent = false;                ///< queda en pantalla hasta que se elige algo
};

// Lo que el host le da a un modulo vivo. Vive mientras vive el modulo (se borra despues que el).
class ModuleContext
{
public:
    enum class NoticeIcon { Info, Warning, Critical };

    virtual ~ModuleContext() = default;

    virtual QString moduleId() const = 0;
    virtual QString moduleTitle() const = 0;

    // Settings de la seccion del modulo en settings.ini ([<id>]). En modo captura quedan en memoria
    // y nunca tocan el disco.
    virtual QVariant value(const QString &key, const QVariant &defaultValue = {}) const = 0;
    virtual void setValue(const QString &key, const QVariant &value) = 0;
    virtual void removeValue(const QString &key) = 0;

    // Captura (--ui-shot, --ui-probe): el modulo no lee el sistema, no registra nada, no arranca
    // timers ni hooks y muestra solo el estado de prueba que le fije el arnes.
    virtual bool captureMode() const = 0;
    // Inyeccion en modo solo loguear (--dry-run-input o dryRunInput=true).
    virtual bool dryRunInput() const = 0;
    // True en TODA corrida automatizada (--self-test, --ui-shot, --ui-probe, --simulate-action, la
    // medicion de consumo): los atajos son de mentira (ModuleHotkeys cuenta y no llama al sistema),
    // no se abren links, no se lanzan procesos y no se escribe nada del sistema. Solo se loguea lo que
    // se haria. La copia de build\ que usa Lega para probar NO es automatizada: funciona de verdad.
    virtual bool automatedRun() const = 0;
    // False en corrida automatizada Y en un arbol de build: lo que queda escrito en el sistema
    // apuntando a ESTE exe sin que el usuario lo pida (reescribir ProgIDs al arrancar, la clave Run,
    // el registro LGA). Una accion explicita del usuario (Apply, Make Default) desde un build si se
    // hace: la decide automatedRun().
    virtual bool persistentRegistrationAllowed() const = 0;

    // Servicios compartidos, perezosos: se crean con el primer pedido.
    virtual ModuleHotkeys *hotkeys() = 0;
    virtual InputInjector *injector() = 0;
    // Observador de "que ventana esta al frente" (plan 4.4): una sola instancia por proceso, creada
    // con el primer modulo prendido que la pide y destruida cuando ya ninguno la usa (refcount). En
    // corrida automatizada, sin hook real salvo que la corrida pida observar (HostOptions::
    // observeForeground; ver ForegroundWatcher.h). La usan Folder Switch y Nuke Shortcuts: con los
    // dos prendidos hay un solo hook.
    virtual ForegroundWatcher *foreground() = 0;

    // Notificacion del sistema; el click abre la ventana en el panel de este modulo.
    virtual void notify(const QString &title, const QString &body, NoticeIcon icon, int msecs) = 0;
    // Aviso con desplegable (NoticeChoice). Sin implementacion propia, el aviso simple.
    virtual void notifyWithChoice(const QString &title, const QString &body, const NoticeChoice &choice)
    {
        Q_UNUSED(choice);
        notify(title, body, NoticeIcon::Warning, 0);
    }

    // La ventana principal: abrirla en este modulo, u ocultarla y devolverla (el calibrador la
    // esconde para no tapar Nuke). window() sirve de padre para los dialogos del modulo.
    virtual void showPanel() = 0;
    virtual void hideWindowTemporarily() = 0;
    virtual void restoreWindow() = 0;
    virtual QWidget *window() const = 0;
};

#endif // MIGHTYTOOLS_MODULECONTEXT_H
