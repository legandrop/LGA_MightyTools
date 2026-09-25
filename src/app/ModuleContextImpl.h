#ifndef MIGHTYTOOLS_MODULECONTEXTIMPL_H
#define MIGHTYTOOLS_MODULECONTEXTIMPL_H

#include "app/ModuleContext.h"

class ModuleHost;
class ModuleHotkeysImpl;

// El contexto que el host le da a cada modulo vivo. Se borra despues del modulo (ModuleHost lo
// borra al recibir destroyed()). Su destructor es la red de seguridad del contrato: suelta los
// atajos y sus declaraciones, el inyector y devuelve la ventana si el modulo la habia escondido.
class ModuleContextImpl : public ModuleContext
{
public:
    ModuleContextImpl(ModuleHost *host, const QString &moduleId, const QString &moduleTitle, int index);
    ~ModuleContextImpl() override;

    QString moduleId() const override { return m_id; }
    QString moduleTitle() const override { return m_title; }

    QVariant value(const QString &key, const QVariant &defaultValue = {}) const override;
    void setValue(const QString &key, const QVariant &value) override;
    void removeValue(const QString &key) override;

    bool captureMode() const override;
    bool dryRunInput() const override;
    bool automatedRun() const override;
    bool persistentRegistrationAllowed() const override;

    ModuleHotkeys *hotkeys() override;
    InputInjector *injector() override;
    ForegroundWatcher *foreground() override;

    void notify(const QString &title, const QString &body, NoticeIcon icon, int msecs) override;

    void showPanel() override;
    void hideWindowTemporarily() override;
    void restoreWindow() override;
    QWidget *window() const override;

    // La regla de persistentRegistrationAllowed(), expuesta para el self-test.
    static bool persistentAllowed(bool automatedRun, bool buildTree) { return !automatedRun && !buildTree; }

private:
    QString key(const QString &key) const;

    ModuleHost *m_host = nullptr;
    QString m_id;
    QString m_title;
    int m_index = 0;
    ModuleHotkeysImpl *m_hotkeys = nullptr;
    InputInjector *m_injector = nullptr;
    ForegroundWatcher *m_foreground = nullptr;
    bool m_windowHidden = false;
    bool m_windowWasVisible = false;
};

#endif // MIGHTYTOOLS_MODULECONTEXTIMPL_H
