#ifndef MIGHTYTOOLS_DICTATIONQA_H
#define MIGHTYTOOLS_DICTATIONQA_H

#include <QString>
#include <QStringList>

#include <functional>

// Pruebas de la captura, el atajo y la escritura del dictado, con las firmas de ModuleDescriptor::selfTest y
// ::simulateAction para que la herramienta las enchufe tal cual. Ninguna abre el micrófono ni manda teclas.
namespace DictationQa {

void selfTest(const std::function<void(bool ok, const QString &what)> &check);

// "devices": lista los micrófonos (id y nombre) sin abrir ninguno. 2 si no conoce la acción.
int simulateAction(const QString &action, const QStringList &args);

} // namespace DictationQa

#endif // MIGHTYTOOLS_DICTATIONQA_H
