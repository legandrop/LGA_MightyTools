#ifndef MIGHTYTOOLS_HOSTSERVICES_H
#define MIGHTYTOOLS_HOSTSERVICES_H

#include "app/ModuleContext.h"

#include <QString>

class QWidget;

// Lo que el host le presta a los contextos de los modulos y que vive en la app (la bandeja y la
// ventana): notificaciones y la ventana principal. Lo implementa AppController. Sin
// implementacion (self-test, medicion) el contexto solo loguea.
class HostServices
{
public:
    virtual ~HostServices() = default;

    // Notificacion del sistema. El click abre la ventana en el panel de `moduleId`.
    virtual void notify(const QString &moduleId, const QString &title, const QString &body,
                        ModuleContext::NoticeIcon icon, int msecs) = 0;
    // Aviso con desplegable. Sin implementacion propia, el aviso simple.
    virtual void notifyWithChoice(const QString &moduleId, const QString &title, const QString &body,
                                  const NoticeChoice &choice)
    {
        Q_UNUSED(choice);
        notify(moduleId, title, body, ModuleContext::NoticeIcon::Warning, 0);
    }
    virtual void showPanel(const QString &moduleId) = 0;
    // Esconder la ventana y devolverla. hideWindow devuelve si estaba visible (para restaurarla).
    virtual bool hideWindow() = 0;
    virtual void showWindow() = 0;
    virtual QWidget *window() const = 0;
};

#endif // MIGHTYTOOLS_HOSTSERVICES_H
