#ifndef MIGHTYTOOLS_NUKESHORTCUTSPANEL_H
#define MIGHTYTOOLS_NUKESHORTCUTSPANEL_H

#include "modules/nukeshortcuts/NukeShortcutsState.h"

#include <QWidget>

#include <functional>

class Chip;
class QLabel;
class QPushButton;
class ShortcutRow;
class SpotThumbnail;
class StatusCard;

// Panel de Nuke Shortcuts (canvas, seccion 2): las tres tarjetas de la ventana de Nuke Shortcuts
// v2.06, sin la de la app (paso a General) ni la de discos (paso a Disk Space).
//  1. Estado: activos / en pausa / un atajo tomado / falta el permiso (mac).
//  2. Shortcuts: las dos acciones, cada una con sus teclas y el lapiz para cambiarlas.
//  3. Dope Sheet position: el punto guardado sobre la captura del layout de Nuke y "Calibrate...".
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

signals:
    void calibrateRequested();
    void accessibilityRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class Status { On, Paused, ShortcutTaken, NeedsPermission };
    Status currentStatus() const;
    void onStatusButtonClicked();

    NukeShortcutsState *m_state = nullptr;
    bool m_showPermission = false;

    StatusCard *m_statusCard = nullptr;
    ShortcutRow *m_addKeyframeRow = nullptr;
    ShortcutRow *m_frameRow = nullptr;
    Chip *m_spotChip = nullptr;
    SpotThumbnail *m_spotThumb = nullptr;
    QLabel *m_spotValue = nullptr;
    QLabel *m_spotCaption = nullptr;
    QPushButton *m_calibrateButton = nullptr;
};

#endif // MIGHTYTOOLS_NUKESHORTCUTSPANEL_H
