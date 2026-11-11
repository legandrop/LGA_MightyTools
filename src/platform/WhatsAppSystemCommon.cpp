#include "platform/WhatsAppSystem.h"

#include "core/I18n.h"

namespace WhatsAppSystem {

QString webUrl(const QString &digits)
{
    return QStringLiteral("https://web.whatsapp.com/send?phone=%1").arg(digits);
}

QString appUrl(const QString &digits)
{
    return QStringLiteral("whatsapp://send?phone=%1").arg(digits);
}

QString describe(const Target &target)
{
    switch (target.kind) {
    case Kind::App:
        return target.name.isEmpty() ? QStringLiteral("WhatsApp") : target.name;
    case Kind::WebApp:
        return target.browser.isEmpty() ? target.name : QStringLiteral("%1 (%2)").arg(target.name, target.browser);
    case Kind::Browser:
        break;
    }
    return I18n::tr("WhatsApp Web in the browser");
}

} // namespace WhatsAppSystem
