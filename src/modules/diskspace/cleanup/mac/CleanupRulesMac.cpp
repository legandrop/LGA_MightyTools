#include "modules/diskspace/cleanup/CleanupRules.h"

// La limpieza no esta habilitada en macOS (platform/SystemPaths.h, cleanupSupported): sin reglas.
namespace CleanupRules {

QList<Cleanup::Category> systemCategories(const Context &context)
{
    Q_UNUSED(context);
    return {};
}

} // namespace CleanupRules
