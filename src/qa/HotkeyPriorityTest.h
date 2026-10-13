#ifndef MIGHTYTOOLS_HOTKEYPRIORITYTEST_H
#define MIGHTYTOOLS_HOTKEYPRIORITYTEST_H

#include <QString>
#include <functional>

namespace HotkeyPriorityTest {
// Recorre el receptor real con eventos en memoria: no instala hooks ni inyecta teclas.
void run(const std::function<void(bool, const QString &)> &check);
}

#endif
