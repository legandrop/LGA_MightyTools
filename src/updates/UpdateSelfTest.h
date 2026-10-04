#ifndef MIGHTYTOOLS_UPDATESELFTEST_H
#define MIGHTYTOOLS_UPDATESELFTEST_H

#include <QString>

#include <functional>

// Casos del updater para --self-test: la lectura del manifiesto con JSON de ejemplo (todas las
// plataformas) y, en macOS, el script que reemplaza el bundle, corrido de verdad sobre bundles de
// mentira dentro de una carpeta de pruebas propia.
namespace UpdateSelfTest {

void run(const std::function<void(bool ok, const QString &what)> &check);

} // namespace UpdateSelfTest

#endif // MIGHTYTOOLS_UPDATESELFTEST_H
