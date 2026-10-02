#ifndef MIGHTYTOOLS_NUKESHORTCUTSPANEL_H
#define MIGHTYTOOLS_NUKESHORTCUTSPANEL_H

#include "core/NukePlugin.h"
#include "modules/nukeshortcuts/NukeShortcutsState.h"

#include <QWidget>

#include <functional>

class Chip;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class RichLineLabel;
class ShortcutRow;
class SpotThumbnail;
class StatusCard;

// Panel de Nuke Shortcuts (canvas, seccion 2): las tres tarjetas de la ventana de Nuke Shortcuts
// v2.06, sin la de la app (paso a General) ni la de discos (paso a Disk Space).
//  1. Estado: activos / en pausa / un atajo tomado / falta el permiso (mac).
//  2. Shortcuts: las dos acciones, cada una con sus teclas y el lapiz para cambiarlas.
//  3. Nuke plugin: estado e instalacion de `<.nuke>/LGA_NukeShortcuts` (D-41), con el que "Add
//     keyframe" lo resuelve Nuke. Instalar lo hace el modulo (installPluginRequested).
//  4. Dope Sheet position: el punto guardado sobre la captura del layout de Nuke y "Calibrate...".
// Todo lo que muestra sale de NukeShortcutsState; lo que el usuario cambia se escribe ahi.
class NukeShortcutsPanel : public QWidget
{
    Q_OBJECT

public:
    // interactive = false (captura): no conecta nada. showPermission: la tarjeta puede decir
    // "Accessibility access needed" (mac, o la captura en cualquier plataforma).
    NukeShortcutsPanel(NukeShortcutsState *state, bool interactive, bool showPermission, QWidget *parent = nullptr);

    ShortcutRow *shortcutRow(ShortcutAction action) const;
    using Validator = std::function<QString(ShortcutAction, const Shortcut &)>;
    void setValidator(Validator validator);
    void refresh();
    void cancelRecordings();

    // Tarjeta del plugin. refreshPlugin() mira el disco (`<nukeDir>/LGA_NukeShortcuts` y el init.py);
    // las capturas usan showPluginFixture() y nunca leen la .nuke real.
    void setNukeDirectory(const QString &nukeDir);
    QString nukeDirectory() const { return m_nukeDir; }
    void refreshPlugin();
    // Aviso debajo de la carpeta hasta el proximo refresco: error de instalacion (warn) o nota.
    void showPluginMessage(const QString &text, bool warn);
    void showPluginFixture(NukePlugin::ChipState chip, const QString &installedVersion, const QString &nukeDir);

signals:
    void calibrateRequested();
    void accessibilityRequested();
    void installPluginRequested(const QString &nukeDir);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class Status { On, Paused, ShortcutTaken, NeedsPermission, NeedsPlugin };
    Status currentStatus() const;
    void onStatusButtonClicked();
    void onBrowseNukeDirClicked();
    void applyPluginView(NukePlugin::ChipState chip, const QString &installedVersion);

    NukeShortcutsState *m_state = nullptr;
    bool m_showPermission = false;

    StatusCard *m_statusCard = nullptr;
    ShortcutRow *m_addKeyframeRow = nullptr;
    ShortcutRow *m_frameRow = nullptr;
    QFrame *m_spotCard = nullptr;
    Chip *m_spotChip = nullptr;
    SpotThumbnail *m_spotThumb = nullptr;
    QLabel *m_spotValue = nullptr;
    QLabel *m_spotCaption = nullptr;
    QPushButton *m_calibrateButton = nullptr;
    Chip *m_pluginChip = nullptr;
    QLineEdit *m_pluginDirField = nullptr;
    RichLineLabel *m_pluginHint = nullptr;
    QPushButton *m_pluginBrowseButton = nullptr;
    QPushButton *m_pluginInstallButton = nullptr;
    QString m_nukeDir;
    bool m_pluginFixture = false;
};

#endif // MIGHTYTOOLS_NUKESHORTCUTSPANEL_H
