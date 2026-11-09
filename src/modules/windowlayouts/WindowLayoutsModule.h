#ifndef MIGHTYTOOLS_WINDOWLAYOUTSMODULE_H
#define MIGHTYTOOLS_WINDOWLAYOUTSMODULE_H

#include "app/Module.h"
#include "modules/windowlayouts/AreaPicker.h"
#include "modules/windowlayouts/WindowLayoutsState.h"
#include "ui/HelpSection.h"

#include <QDateTime>
#include <QPointer>
#include <QHash>
#include <QSet>

class AdminWindowHelper;
class ModuleHotkeys;
class WindowLayoutsPanel;

class WindowLayoutsModule : public Module
{
    Q_OBJECT

public:
    explicit WindowLayoutsModule(ModuleContext &context);
    ~WindowLayoutsModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;
    QWidget *createCaptureWidget(const QString &state, QWidget *parent) override;

    const WindowLayoutsState &layoutsState() const { return m_state; }
    // Lo que el usuario tiene que arreglar, y pone la herramienta en rojo: el ayudante de administrador no
    // arranco (Windows) o falta el permiso de Accesibilidad (macOS; se mira en vivo, sin pedirlo).
    QString lastError() const;
    // El ultimo atajo que no pudo hacer nada (el escritorio al frente, una ventana minimizada). No es un
    // problema de la herramienta: va como nota amarilla con su hora y se borra con el proximo atajo que funcione.
    QString attemptNote() const { return m_attemptNote; }
    QDateTime attemptTime() const { return m_attemptTime; }
    void dismissAttempt();
    // Publica para el self-test; la llaman el atajo y el ayudante de administrador.
    void setAttempt(const QString &note);
    // El error del selector de area, para el editor que lo abrio.
    QString pickerError() const { return m_pickerError; }
    void clearPickerError() { m_pickerError.clear(); }
    // Solo Windows: un ayudante elevado mueve tambien las ventanas de apps abiertas como administrador.
    bool adminWindowsEnabled() const;
    void setAdminWindowsEnabled(bool enabled);
    bool adminHelperActive() const;
    int registrationFailureCount() const { return m_registrationErrors.size(); }
    int externalConflictCount() const { return m_externalConflicts.size(); }
    QString registrationError(int id) const { return m_registrationErrors.value(id); }
    bool hasExternalConflict(int id) const { return m_externalConflicts.contains(id); }
    QString validateShortcut(int editingId, const Shortcut &shortcut, bool enabled = true) const;
    bool saveLayout(WindowLayout layout, QString *error = nullptr);
    bool setLayoutEnabled(int id, bool enabled);
    bool removeLayout(int id);
    void openAreaPicker(AreaPicker::Mode mode, std::function<void(const QRectF &)> chosen);

signals:
    void layoutsChanged();

private:
    void registerLayouts();
    void onHotkey(int id);
    void syncAdminHelper();

    WindowLayoutsState m_state;
    bool m_started = false;
    ModuleHotkeys *m_hotkeys = nullptr;
    QSet<int> m_declaredIds;
    QHash<int, QString> m_registrationErrors;
    QSet<int> m_externalConflicts;
    QPointer<AreaPicker> m_picker;
    QString m_attemptNote;
    QDateTime m_attemptTime;
    QString m_pickerError;
    QString m_adminError;
    bool m_adminFailed = false;
    bool m_permissionShown = false; ///< el ultimo estado avisado del permiso de macOS
    QString m_captureState;
#ifdef Q_OS_WIN
    AdminWindowHelper *m_adminHelper = nullptr;
#endif
};

ModuleDescriptor windowLayoutsDescriptor();
HelpSection windowLayoutsHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_WINDOWLAYOUTSMODULE_H
