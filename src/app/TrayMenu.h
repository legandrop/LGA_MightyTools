#ifndef MIGHTYTOOLS_TRAYMENU_H
#define MIGHTYTOOLS_TRAYMENU_H

#include <QIcon>
#include <QString>

class ModuleHost;
class QAction;
class QMenu;

// Menu de la bandeja (canvas, seccion 4, D-15): encabezado con el estado general, una seccion por
// herramienta prendida con sus entradas (Module::fillTrayMenu), Settings..., Check for Updates... y
// Quit. En macOS el menu es nativo y el QSS no le llega: las secciones van como titulos
// deshabilitados.
struct TrayMenuActions
{
    QAction *settings = nullptr;
    QAction *updates = nullptr; // solo Windows
    QAction *quit = nullptr;
};

// Vacia y rearma el menu entero. Lo usan AppController (en aboutToShow, nunca con el menu abierto)
// y la captura de QA, asi son el mismo menu.
TrayMenuActions fillTrayMenu(QMenu *menu, ModuleHost *host);

// "Mighty Tools · 3 of 5 on".
QString trayHeaderText(const ModuleHost *host);
// "LGA Mighty Tools" mas las lineas de las herramientas prendidas ("D: 42 GB free").
QString trayTooltip(const ModuleHost *host);

// Icono de la bandeja; atenuado al 40 % con todo apagado o todo en pausa.
QIcon trayIcon(bool dimmed);

#endif // MIGHTYTOOLS_TRAYMENU_H
