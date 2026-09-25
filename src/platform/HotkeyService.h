#ifndef MIGHTYTOOLS_HOTKEYSERVICE_H
#define MIGHTYTOOLS_HOTKEYSERVICE_H

#include "core/Shortcut.h"

#include <QObject>

#include <memory>

// Atajos globales del sistema. Una implementacion por plataforma:
//  - Windows (platform/win/HotkeyServiceWin.cpp): RegisterHotKey + filtro de eventos nativos.
//  - macOS   (platform/mac/HotkeyServiceMac.cpp): RegisterEventHotKey de Carbon.
//
// Un atajo registrado se come la combinacion en TODAS las apps. Por eso TrayController solo los
// registra mientras Nuke esta al frente y los suelta apenas sale.
class HotkeyService : public QObject
{
    Q_OBJECT

public:
    explicit HotkeyService(QObject *parent = nullptr);
    ~HotkeyService() override;

    // Registra `shortcut` con el identificador `id` (si ya habia uno con ese id, lo reemplaza).
    // False si el sistema lo rechazo: otra app ya tiene esa combinacion.
    bool registerHotkey(int id, const Shortcut &shortcut);
    void unregisterHotkey(int id);
    void unregisterAll();
    bool isRegistered(int id) const;

    // Si el sistema acepta la combinacion AHORA: la registra con un id de prueba y la suelta
    // enseguida. Sirve para rechazar un atajo nuevo que ya tiene otra app.
    bool probe(const Shortcut &shortcut);

    // Le devuelve la combinacion a la app que esta al frente, como si esta app no existiera: manda la
    // tecla con sus modificadores (aprieta los que el usuario ya solto y los suelta despues). Hay que
    // llamarla con el atajo YA liberado, o la tecla simulada lo volveria a disparar.
    void passThrough(const Shortcut &shortcut);

signals:
    void activated(int id);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

#endif // MIGHTYTOOLS_HOTKEYSERVICE_H
