#ifndef MIGHTYTOOLS_APPTOGGLESTATE_H
#define MIGHTYTOOLS_APPTOGGLESTATE_H

#include "core/Shortcut.h"
#include "platform/AppToggleSystem.h"

#include <QList>
#include <QString>

class ModuleContext;

// Una app con su atajo. Se guarda la ruta del exe (mac: del .app), nunca un acceso directo: el
// instalador de OBS 33 le cambio el nombre al suyo y el script viejo dejo de abrirlo.
struct AppEntry
{
    int id = 0;
    QString name;
    QString path;
    QString arguments;
    QString workingDir; ///< solo Windows; vacio = la carpeta de la app
    Shortcut shortcut{Qt::NoModifier, 0};
    bool enabled = true;

    AppToggleSystem::Target target() const { return {path, arguments, workingDir}; }
};

// La lista de [appToggle] apps= en settings.ini, en JSON como Window Layouts.
class AppToggleState
{
public:
    static constexpr int kMaxApps = 99;
    explicit AppToggleState(ModuleContext *context = nullptr);

    const QList<AppEntry> &apps() const { return m_apps; }
    // Asigna el id a una entrada nueva (0). False si no es valida, si su atajo ya lo tiene otra o si no
    // queda lugar.
    bool upsert(AppEntry &entry);
    bool remove(int id);
    void setCaptureApps(const QList<AppEntry> &apps);

    // Ruta absoluta a un .exe (Windows) o a un .app (mac). No mira el disco.
    static bool validPath(const QString &path);
    // Sin espacios ni comillas en los extremos: «Copiar como ruta» de Windows 11 la pega entre comillas.
    static QString cleanPath(const QString &text);
    static bool valid(const AppEntry &entry);
    static QList<AppEntry> fromJson(const QString &text);
    static QString toJson(const QList<AppEntry> &apps);

private:
    void save();
    ModuleContext *m_context = nullptr;
    QList<AppEntry> m_apps;
};

#endif // MIGHTYTOOLS_APPTOGGLESTATE_H
