#include "modules/mousebuttons/MouseButtonsState.h"

#include "app/ModuleContext.h"
#include "platform/AppToggleSystem.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSet>

namespace {

const QString kRules = QStringLiteral("rules");
constexpr int kMaxText = 2048;

using MouseButtonsSystem::Button;

QString buttonId(Button button)
{
    switch (button) {
    case Button::Back: return QStringLiteral("back");
    case Button::Forward: return QStringLiteral("forward");
    case Button::Middle: break;
    }
    return QStringLiteral("middle");
}

bool buttonFromId(const QString &id, Button *button)
{
    for (const Button each : {Button::Middle, Button::Back, Button::Forward}) {
        if (buttonId(each) == id) {
            *button = each;
            return true;
        }
    }
    return false;
}

} // namespace

MouseButtonsState::MouseButtonsState(ModuleContext *context)
    : m_context(context)
{
    if (!context) {
        return;
    }
    const QVariant stored = context->value(kRules);
    // Una corrida automatizada o una captura nunca siembran: leen el disco ajeno y escribirian settings.
    if (stored.isValid() || context->captureMode() || context->automatedRun()) {
        m_rules = fromJson(stored.toString());
        return;
    }
    m_rules = {defaultRule(AppToggleSystem::photoshopPath())};
    save();
}

MouseRule MouseButtonsState::defaultRule(const QString &photoshopPath)
{
    // La que reemplaza al perfil de X-Mouse: el boton del medio mueve la imagen de Photoshop como
    // Espacio + arrastrar.
    MouseRule rule;
    rule.id = 1;
    rule.app = QStringLiteral("Photoshop.exe");
    rule.path = photoshopPath;
    rule.button = Button::Middle;
    rule.keys = Shortcut{Qt::NoModifier, Qt::Key_Space};
    rule.holdLeft = true;
    rule.enabled = false;
    return rule;
}

QString MouseButtonsState::appName(const QString &text)
{
    const QString name = QFileInfo(QDir::fromNativeSeparators(text.trimmed())).fileName();
    static const QString forbidden = QStringLiteral("<>:\"|?*");
    for (const QChar c : name) {
        if (forbidden.contains(c) || c.unicode() < 32) return QString();
    }
    return name.size() <= 255 && name.size() > 4 && name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) ? name : QString();
}

bool MouseButtonsState::sameApp(const QString &a, const QString &b)
{
    return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0;
}

bool MouseButtonsState::holdKey(int key)
{
    return Shortcut::isSupportedKey(key) || key == Qt::Key_Space;
}

Shortcut MouseButtonsState::keysFromString(const QString &text)
{
    const QKeySequence sequence = QKeySequence::fromString(text.trimmed(), QKeySequence::PortableText);
    if (sequence.count() != 1 || !holdKey(sequence[0].key())) {
        return Shortcut();
    }
    return {sequence[0].keyboardModifiers(), sequence[0].key()};
}

bool MouseButtonsState::valid(const MouseRule &rule)
{
    const QString path = QDir::fromNativeSeparators(rule.path);
    return rule.id >= 1 && rule.id <= kMaxRules && appName(rule.app) == rule.app && holdKey(rule.keys.key)
        && (rule.path.isEmpty() || (path.size() <= kMaxText && QDir::isAbsolutePath(path) && sameApp(QFileInfo(path).fileName(), rule.app)));
}

bool MouseButtonsState::upsert(MouseRule &rule)
{
    int position = -1;
    if (rule.id != 0) {
        for (int i = 0; i < m_rules.size(); ++i) {
            if (m_rules.at(i).id == rule.id) position = i;
        }
        if (position < 0) return false;
    } else {
        if (m_rules.size() >= kMaxRules) return false;
        QSet<int> used;
        for (const MouseRule &stored : m_rules) used.insert(stored.id);
        for (int id = 1; id <= kMaxRules && !rule.id; ++id) {
            if (!used.contains(id)) rule.id = id;
        }
    }
    rule.app = rule.app.trimmed();
    rule.path = rule.path.trimmed();
    bool clash = false;
    for (const MouseRule &stored : m_rules) {
        clash = clash || (stored.id != rule.id && stored.button == rule.button && sameApp(stored.app, rule.app));
    }
    if (!valid(rule) || clash) {
        if (position < 0) rule.id = 0;
        return false;
    }
    if (position >= 0) {
        m_rules[position] = rule;
    } else {
        m_rules.append(rule);
    }
    save();
    return true;
}

bool MouseButtonsState::remove(int id)
{
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules.at(i).id == id) {
            m_rules.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}

void MouseButtonsState::setCaptureRules(const QList<MouseRule> &rules)
{
    if (!m_context || m_context->captureMode()) {
        m_rules = rules;
    }
}

QList<MouseRule> MouseButtonsState::fromJson(const QString &text)
{
    QList<MouseRule> result;
    if (text.size() > 262144) {
        return result;
    }
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray()) {
        return result;
    }
    QSet<int> seen;
    QSet<QString> pairs; // app + boton: una sola regla por boton en cada app
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        MouseRule rule;
        rule.id = object.value(QStringLiteral("id")).toInt();
        rule.app = object.value(QStringLiteral("app")).toString().trimmed();
        rule.path = object.value(QStringLiteral("path")).toString().trimmed();
        rule.keys = keysFromString(object.value(QStringLiteral("keys")).toString());
        rule.holdLeft = object.value(QStringLiteral("left")).toBool(true);
        rule.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        const QString pair = rule.app.toLower() + QLatin1Char('|') + object.value(QStringLiteral("button")).toString();
        if (!buttonFromId(object.value(QStringLiteral("button")).toString(), &rule.button) || !valid(rule)
            || seen.contains(rule.id) || pairs.contains(pair)) {
            continue;
        }
        result.append(rule);
        seen.insert(rule.id);
        pairs.insert(pair);
        if (result.size() == kMaxRules) break;
    }
    return result;
}

QString MouseButtonsState::toJson(const QList<MouseRule> &rules)
{
    QJsonArray array;
    for (const MouseRule &rule : rules) {
        if (!valid(rule)) continue;
        QJsonObject object;
        object.insert(QStringLiteral("id"), rule.id);
        object.insert(QStringLiteral("app"), rule.app);
        if (!rule.path.isEmpty()) object.insert(QStringLiteral("path"), rule.path);
        object.insert(QStringLiteral("button"), buttonId(rule.button));
        object.insert(QStringLiteral("keys"), rule.keys.toPortableString());
        object.insert(QStringLiteral("left"), rule.holdLeft);
        if (!rule.enabled) object.insert(QStringLiteral("enabled"), false);
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void MouseButtonsState::save()
{
    if (m_context) {
        m_context->setValue(kRules, toJson(m_rules));
    }
}
