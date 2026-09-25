#ifndef MIGHTYTOOLS_TRAYMENU_H
#define MIGHTYTOOLS_TRAYMENU_H

#include <QIcon>
#include <QList>
#include <QStringList>

class AppState;
class QAction;
class QMenu;

// Acciones del menu de la bandeja (Windows) o de la barra de menu (macOS, donde el menu es nativo y
// el QSS no le llega).
struct TrayMenuActions
{
    QAction *header = nullptr;    // "Nuke Shortcuts · On|Paused", deshabilitado
    QAction *toggle = nullptr;    // Pause shortcuts / Resume shortcuts
    // Un disco bajo su umbral: una linea por disco entre estos dos separadores ("D: is low · 42 GB
    // free"). Sin discos bajos, el separador de arriba se oculta y no queda ninguna linea.
    QAction *diskSeparator = nullptr;
    QAction *mainSeparator = nullptr;
    QList<QAction *> diskWarnings;
    QAction *settings = nullptr;
    QAction *calibrate = nullptr;
    QAction *updates = nullptr;   // solo Windows
    QAction *quit = nullptr;
};

// Arma el menu (lo usan TrayController y la captura de QA, para que sean el mismo menu).
TrayMenuActions buildTrayMenu(QMenu *menu);
void refreshTrayMenu(const TrayMenuActions &actions, bool enabled);
// Rearma las lineas de discos bajos. Las acciones nuevas quedan en actions.diskWarnings: quien las
// conecta es TrayController (abren Settings).
// "D: is low · 42 GB free", una por disco vigilado bajo su umbral.
QStringList diskWarningLines(const AppState &state);
void refreshTrayDiskWarnings(QMenu *menu, TrayMenuActions &actions, const QStringList &lines);

// Icono de la bandeja: los dos keys con el desregistro ajustado al pixel, atenuado en pausa.
QIcon trayIcon(bool paused);

#endif // MIGHTYTOOLS_TRAYMENU_H
