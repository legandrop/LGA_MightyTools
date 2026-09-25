#ifndef MIGHTYTOOLS_OPENINNUKEX_MACFILEASSOCIATION_H
#define MIGHTYTOOLS_OPENINNUKEX_MACFILEASSOCIATION_H

#include <QString>
#include <functional>

// Asociacion de `.nk` en macOS, portada de
// `~/.nuke/LGA_OpenInNukeX/QtClient/src/macintegration.{h,mm}` (v1.83). Sin cambios de
// comportamiento respecto del origen: Launch Services (NSWorkspace) es quien decide y muestra su
// propio cartel de confirmacion, que la app no controla.
//
// D-12 (abierta): los usuarios de mac siguen con el cliente viejo hasta la fase 8 de mac, asi que
// este archivo no se ejercita todavia (no hay usuarios de Open in NukeX en mac hoy) pero se porta
// ya para que el modulo compile completo bajo APPLE, coherente con el resto del repo.
namespace MacFileAssociation {

/// Deja a esta app como aplicacion por defecto para los `.nk` (equivalente a
/// `duti -s <bundle-id> .nk all`, pero con la misma API que usa duti por dentro). Asincronica:
/// macOS puede pedirle permiso al usuario antes de cambiar la asociacion. `done` corre en el hilo
/// de UI y recibe el detalle crudo de Launch Services cuando falla (para el log).
void setAsDefaultNkHandler(std::function<void(bool ok, const QString &error)> done);

/// Si esta app YA es el handler por defecto de `.nk` (para no repetir el cartel de permiso de
/// macOS cuando no hay nada que cambiar).
bool isDefaultNkHandler();

} // namespace MacFileAssociation

#endif // MIGHTYTOOLS_OPENINNUKEX_MACFILEASSOCIATION_H
