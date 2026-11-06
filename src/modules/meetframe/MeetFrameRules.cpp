#include "modules/meetframe/MeetFrameRules.h"

#include <QDir>
#include <QFileInfo>

namespace MeetFrameRules {

namespace {

QStringList lower(const QStringList &list)
{
    QStringList out;
    for (const QString &item : list) out << item.toLower();
    return out;
}

} // namespace

QList<Group> groups()
{
    // Los nombres de proceso salen de las apps instaladas (WhatsApp de la Store corre como WhatsApp.Root.exe;
    // la version vieja, como WhatsApp.exe). Los ids de mac, de su Info.plist.
#if defined(Q_OS_MACOS)
    const QStringList messaging = {QStringLiteral("net.whatsapp.WhatsApp"), QStringLiteral("ru.keepcoder.Telegram"),
                                   QStringLiteral("org.telegram.desktop"), QStringLiteral("org.whispersystems.signal-desktop"),
                                   QStringLiteral("com.tinyspeck.slackmacgap"), QStringLiteral("com.hnc.Discord"),
                                   QStringLiteral("com.apple.MobileSMS"), QStringLiteral("com.facebook.archon.developerID"),
                                   QStringLiteral("com.tencent.xinWeChat"), QStringLiteral("jp.naver.line.mac"),
                                   QStringLiteral("com.viber.osx")};
    const QStringList mail = {QStringLiteral("com.apple.mail"), QStringLiteral("com.microsoft.Outlook"),
                              QStringLiteral("org.mozilla.thunderbird"), QStringLiteral("com.readdle.smartemail-Mac"),
                              QStringLiteral("com.readdle.SparkDesktop")};
    const QStringList passwords = {QStringLiteral("com.1password.1password"), QStringLiteral("com.bitwarden.desktop"),
                                   QStringLiteral("org.keepassxc.keepassxc"), QStringLiteral("com.apple.Passwords"),
                                   QStringLiteral("com.nordsec.nordpass"), QStringLiteral("in.sinew.Enpass-Desktop")};
#else
    const QStringList messaging = {QStringLiteral("WhatsApp.exe"), QStringLiteral("WhatsApp.Root.exe"),
                                   QStringLiteral("Telegram.exe"), QStringLiteral("Signal.exe"), QStringLiteral("slack.exe"),
                                   QStringLiteral("Discord.exe"), QStringLiteral("Messenger.exe"), QStringLiteral("WeChat.exe"),
                                   QStringLiteral("Weixin.exe"), QStringLiteral("LINE.exe"), QStringLiteral("Viber.exe")};
    const QStringList mail = {QStringLiteral("OUTLOOK.EXE"), QStringLiteral("olk.exe"), QStringLiteral("HxOutlook.exe"),
                              QStringLiteral("thunderbird.exe"), QStringLiteral("MailClient.exe"), QStringLiteral("Spark Desktop.exe"),
                              QStringLiteral("Spark.exe")};
    const QStringList passwords = {QStringLiteral("1Password.exe"), QStringLiteral("Bitwarden.exe"), QStringLiteral("KeePass.exe"),
                                   QStringLiteral("KeePassXC.exe"), QStringLiteral("Dashlane.exe"), QStringLiteral("NordPass.exe"),
                                   QStringLiteral("Enpass.exe")};
#endif
    return {
        {QStringLiteral("messaging"), true, lower(messaging), {QStringLiteral("whatsapp"), QStringLiteral("telegram")}},
        {QStringLiteral("mail"), false, lower(mail), {QStringLiteral("gmail"), QStringLiteral("outlook")}},
        {QStringLiteral("passwords"), true, lower(passwords), {}},
    };
}

QSet<QString> browsers()
{
#if defined(Q_OS_MACOS)
    const QStringList list = {QStringLiteral("com.google.Chrome"), QStringLiteral("com.microsoft.edgemac"),
                              QStringLiteral("org.mozilla.firefox"), QStringLiteral("com.brave.Browser"),
                              QStringLiteral("com.apple.Safari"), QStringLiteral("com.operasoftware.Opera"),
                              QStringLiteral("com.vivaldi.Vivaldi"), QStringLiteral("company.thebrowser.Browser")};
#else
    const QStringList list = {QStringLiteral("chrome.exe"), QStringLiteral("msedge.exe"), QStringLiteral("firefox.exe"),
                              QStringLiteral("brave.exe"), QStringLiteral("opera.exe"), QStringLiteral("vivaldi.exe"),
                              QStringLiteral("arc.exe"), QStringLiteral("zen.exe")};
#endif
    const QStringList low = lower(list);
    return QSet<QString>(low.begin(), low.end());
}

QStringList notificationApps()
{
#if defined(Q_OS_MACOS)
    return {QStringLiteral("com.apple.notificationcenterui")};
#else
    // Los avisos (toasts) y el centro de notificaciones de Windows 10 y 11 son ventanas de ShellExperienceHost;
    // en Windows 11 24H2 y siguientes parte de esa interfaz paso a ShellHost.
    return {QStringLiteral("shellexperiencehost.exe"), QStringLiteral("shellhost.exe")};
#endif
}

bool hides(const WindowInfo &window, const Rules &rules)
{
    if (window.ignored || window.desktop) {
        return false;
    }
    if (rules.apps.contains(window.app)) {
        return true;
    }
    if (rules.hideNotifications && notificationApps().contains(window.app)) {
        return true;
    }
    if (!rules.browserTitles.isEmpty() && rules.browsers.contains(window.app)) {
        const QString title = window.title.toLower();
        for (const QString &word : rules.browserTitles) {
            if (title.contains(word)) return true;
        }
    }
    return false;
}

Mask computeMask(const QList<WindowInfo> &topFirst, const QRect &region, const Rules &rules)
{
    Mask mask;
    if (region.isEmpty()) {
        return mask;
    }
    QRegion covered;
    for (const WindowInfo &window : topFirst) {
        if (window.desktop) {
            break; // el escritorio tapa todo lo que esta debajo de el
        }
        if (window.ignored) {
            continue;
        }
        const QRect part = window.rect.intersected(region);
        if (part.isEmpty()) {
            continue;
        }
        if (window.forceMask) {
            mask.region += QRegion(part).subtracted(covered);
            continue; // no cuenta como que tapa: lo de abajo se ve en pantalla y por eso se tapa con ella
        }
        if (hides(window, rules)) {
            const QRegion visible = QRegion(part).subtracted(covered);
            if (!visible.isEmpty()) {
                mask.region += visible;
                ++mask.hiddenWindows;
            }
        }
        if (window.covering) {
            covered += part;
        }
    }
    if (rules.hideDesktop) {
        mask.region += QRegion(region).subtracted(covered);
    }
    mask.region &= QRegion(region);
    return mask;
}

Rules build(const QSet<QString> &enabledGroups, const QStringList &customApps, bool hideDesktop, bool hideNotifications)
{
    Rules rules;
    for (const Group &group : groups()) {
        if (!enabledGroups.contains(group.id)) continue;
        for (const QString &app : group.apps) rules.apps.insert(app);
        rules.browserTitles << group.browserTitles;
    }
    for (const QString &app : customApps) {
        const QString key = appKey(app);
        if (!key.isEmpty()) rules.apps.insert(key);
    }
    rules.browserTitles.removeDuplicates();
    rules.browsers = browsers();
    rules.hideDesktop = hideDesktop;
    rules.hideNotifications = hideNotifications;
    return rules;
}

QString appKey(const QString &text)
{
    QString value = text.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
        value = value.mid(1, value.size() - 2).trimmed();
    }
    if (value.isEmpty()) {
        return {};
    }
#if defined(Q_OS_MACOS)
    // Un bundle id: letras, digitos, puntos y guiones, con al menos un punto.
    if (value.contains(QLatin1Char('/')) || !value.contains(QLatin1Char('.'))) return {};
    for (const QChar c : value) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('.') && c != QLatin1Char('-') && c != QLatin1Char('_')) return {};
    }
    return value.toLower();
#else
    const QString name = QFileInfo(QDir::fromNativeSeparators(value)).fileName();
    if (!name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) || name.size() <= 4) return {};
    static const QString kForbidden = QStringLiteral("<>:\"|?*");
    for (const QChar c : name) {
        if (kForbidden.contains(c)) return {};
    }
    return name.toLower();
#endif
}

} // namespace MeetFrameRules
