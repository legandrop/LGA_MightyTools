#ifndef MIGHTYTOOLS_WHATSAPPMODULE_H
#define MIGHTYTOOLS_WHATSAPPMODULE_H

#include "app/Module.h"
#include "modules/whatsapp/PhoneNumber.h"
#include "platform/WhatsAppSystem.h"
#include "ui/HelpSection.h"

#include <QDateTime>
#include <QElapsedTimer>

class ModuleHotkeys;

// WhatsApp Chat (D-78): un telefono copiado abre su chat de WhatsApp. Dos caminos:
//  - Ctrl+V / ⌘V en la ventana de Mighty Tools, en cualquier pagina (Module::handleWindowPaste);
//  - un atajo global que lee el portapapeles desde cualquier app.
// Lee el numero (PhoneNumber), elige donde abrirlo (la app, la app web o el navegador; WhatsAppSystem),
// lo abre y cierra la ventana. Si no hay un numero claro, deja una nota amarilla con la hora (y, desde el
// atajo, un aviso del sistema). No guarda ningun numero; en el log va solo el largo y el pais.
//
// Sin timers ni nada de fondo: la deteccion se hace al abrir un chat y al mostrar el panel.
class WhatsAppModule : public Module
{
    Q_OBJECT

public:
    enum { OpenShortcut = 1 };
    enum class Source { Paste, Shortcut, Button };

    explicit WhatsAppModule(ModuleContext &context);
    ~WhatsAppModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    bool handleWindowPaste(const QString &text) override;
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;

    // ---- Ajustes
    QString countryId() const;
    void setCountry(const QString &id);
    QString openIn() const; ///< «auto», «app», «webapp», «browser»
    void setOpenIn(const QString &mode);
    static Shortcut defaultShortcut();
    Shortcut shortcut() const;
    QString validateShortcut(const Shortcut &keys) const;
    bool setShortcut(const Shortcut &keys, QString *error);
    QString shortcutError() const { return m_shortcutError; }

    // ---- Destinos: lo detectado la ultima vez (refreshTargets() vuelve a mirar; en captura, los de prueba).
    const QList<WhatsAppSystem::Target> &targets() const { return m_targets; }
    void refreshTargets(bool force = false);
    bool targetsKnown() const { return m_targetsKnown; }
    // La eleccion, sin efectos: «auto» = la abierta (app antes que app web), la app, la app web, el navegador.
    static bool choose(const QList<WhatsAppSystem::Target> &targets, const QString &mode, WhatsAppSystem::Target *out,
                       QString *problem);

    // ---- La accion: lee `text`, abre el chat y cierra la ventana. False si no pudo (deja la nota).
    bool openText(const QString &text, Source source);
    void openClipboard(Source source);
    QString attemptNote() const { return m_attemptNote; }
    QDateTime attemptTime() const { return m_attemptTime; }
    void dismissAttempt();
    // Lo que se abrio la ultima vez (o se habria abierto en una corrida automatizada): para el self-test.
    QString lastOpened() const { return m_lastOpened; }

    // El texto para la UI de un resultado que no es un numero.
    static QString resultProblem(const PhoneNumber::Result &result);
    static QString countryName(const QString &id);

signals:
    void changed();

private:
    void registerShortcut();
    void unregisterShortcut();
    void setAttempt(const QString &note);

    bool m_started = false;
    ModuleHotkeys *m_hotkeys = nullptr;
    QString m_shortcutError;
    QList<WhatsAppSystem::Target> m_targets;
    bool m_targetsKnown = false;
    QElapsedTimer m_targetsAge;
    QString m_attemptNote;
    QDateTime m_attemptTime;
    QString m_lastOpened;
};

ModuleDescriptor whatsAppDescriptor();
HelpSection whatsAppHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_WHATSAPPMODULE_H
