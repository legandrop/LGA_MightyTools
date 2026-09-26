#ifndef MIGHTYTOOLS_DISKSPACEMODULE_H
#define MIGHTYTOOLS_DISKSPACEMODULE_H

#include "app/Module.h"
#include "modules/diskspace/DiskSpace.h"
#include "ui/HelpSection.h"

#include <QPointer>

class DiskCard;
class DiskMonitor;
class DiskState;

// Disk Space (id estable "diskSpace"): vigila los discos locales que elige el usuario y avisa cuando
// uno baja de su umbral. Portado de Nuke Shortcuts v2.06 sin cambios de logica.
//
// Todo lo que consume nace en start() y muere en stop(): el DiskMonitor (su timer y la lectura de
// los discos). El estado (DiskState) vive con el objeto del modulo; el panel, solo mientras se ve.
class DiskSpaceModule : public Module
{
    Q_OBJECT

public:
    explicit DiskSpaceModule(ModuleContext &context);
    ~DiskSpaceModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    void fillTrayMenu(QMenu *menu) override;
    QStringList trayTooltipLines() const override;

    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;
    QWidget *createCaptureWidget(const QString &state, QWidget *parent) override;

    DiskState *state() const { return m_state; }

private:
    void notifyLowSpace(const DriveInfo &drive, const DiskWatch &watch);

    DiskState *m_state = nullptr;
    DiskMonitor *m_monitor = nullptr;
    QPointer<QWidget> m_panel;
    QPointer<DiskCard> m_card;
};

ModuleDescriptor diskSpaceDescriptor();
HelpSection diskSpaceHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_DISKSPACEMODULE_H
