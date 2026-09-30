#include "platform/ToastActivation.h"

namespace ToastActivation {

bool isActivationArgument(const QString &argument)
{
    return argument.compare(QLatin1String("-Embedding"), Qt::CaseInsensitive) == 0
           || argument.compare(QLatin1String("/Embedding"), Qt::CaseInsensitive) == 0
           || argument == QLatin1String("--toast-activated");
}

} // namespace ToastActivation
