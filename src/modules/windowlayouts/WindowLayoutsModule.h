#ifndef MIGHTYTOOLS_WINDOWLAYOUTSMODULE_H
#define MIGHTYTOOLS_WINDOWLAYOUTSMODULE_H

#include "app/Module.h"
#include "modules/windowlayouts/AreaPicker.h"
#include "modules/windowlayouts/WindowLayoutsState.h"
#include "ui/HelpSection.h"

#include <QPointer>
#include <QHash>
#include <QSet>

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
    QString lastError() const { return m_lastError; }
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

    WindowLayoutsState m_state;
    bool m_started = false;
    ModuleHotkeys *m_hotkeys = nullptr;
    QSet<int> m_declaredIds;
    QHash<int, QString> m_registrationErrors;
    QSet<int> m_externalConflicts;
    QPointer<AreaPicker> m_picker;
    QString m_lastError;
    QString m_captureState;
};

ModuleDescriptor windowLayoutsDescriptor();
HelpSection windowLayoutsHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_WINDOWLAYOUTSMODULE_H
