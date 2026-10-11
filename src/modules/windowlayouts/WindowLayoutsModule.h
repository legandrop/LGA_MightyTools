#ifndef MIGHTYTOOLS_WINDOWLAYOUTSMODULE_H
#define MIGHTYTOOLS_WINDOWLAYOUTSMODULE_H

#include "app/Module.h"
#include "modules/windowlayouts/AreaPicker.h"
#include "modules/windowlayouts/WindowLayoutsState.h"
#include "ui/HelpSection.h"

#include <QPointer>
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
    bool isPaused() const override { return m_state.paused(); }
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;
    QWidget *createCaptureWidget(const QString &state, QWidget *parent) override;

    const WindowLayoutsState &layoutsState() const { return m_state; }
    QString lastError() const { return m_lastError; }
    void setPaused(bool paused);
    QString validateShortcut(int editingId, const Shortcut &shortcut) const;
    bool saveLayout(WindowLayout layout, QString *error = nullptr);
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
    QSet<int> m_failedIds;
    QPointer<AreaPicker> m_picker;
    QString m_lastError;
    QString m_captureState;
};

ModuleDescriptor windowLayoutsDescriptor();
HelpSection windowLayoutsHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_WINDOWLAYOUTSMODULE_H
