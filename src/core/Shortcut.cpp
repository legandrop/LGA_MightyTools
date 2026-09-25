#include "core/Shortcut.h"

#include <QKeySequence>

QString Shortcut::toPortableString() const
{
    if (!isValid()) {
        return QString();
    }
    return QKeySequence(QKeyCombination(modifiers, static_cast<Qt::Key>(key))).toString(QKeySequence::PortableText);
}

Shortcut Shortcut::fromPortableString(const QString &text)
{
    const QKeySequence sequence = QKeySequence::fromString(text.trimmed(), QKeySequence::PortableText);
    Shortcut shortcut;
    if (sequence.count() != 1) {
        return shortcut;
    }
    const QKeyCombination combo = sequence[0];
    if (!isSupportedKey(combo.key())) {
        return shortcut;
    }
    shortcut.modifiers = combo.keyboardModifiers();
    shortcut.key = combo.key();
    return shortcut;
}

QString Shortcut::keyName(int key)
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    }
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        return QString(QChar(key));
    }
    return QKeySequence(key).toString(QKeySequence::NativeText);
}

bool Shortcut::isSupportedKey(int key)
{
    return (key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)
        || (key >= Qt::Key_F1 && key <= Qt::Key_F12);
}

bool Shortcut::isModifierKey(int key)
{
    return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta
        || key == Qt::Key_AltGr || key == Qt::Key_Super_L || key == Qt::Key_Super_R;
}

QStringList Shortcut::displayKeys() const
{
    QStringList keys;
    if (!isValid()) {
        return keys;
    }
#ifdef Q_OS_MACOS
    // Orden de Apple: Control, Option, Shift, Command. MetaModifier es la tecla Control en Qt/mac.
    if (modifiers & Qt::MetaModifier) keys << QStringLiteral("⌃");
    if (modifiers & Qt::AltModifier) keys << QStringLiteral("⌥");
    if (modifiers & Qt::ShiftModifier) keys << QStringLiteral("⇧");
    if (modifiers & Qt::ControlModifier) keys << QStringLiteral("⌘");
#else
    if (modifiers & Qt::ControlModifier) keys << QStringLiteral("Ctrl");
    if (modifiers & Qt::AltModifier) keys << QStringLiteral("Alt");
    if (modifiers & Qt::ShiftModifier) keys << QStringLiteral("Shift");
    if (modifiers & Qt::MetaModifier) keys << QStringLiteral("Win");
#endif
    keys << keyName(key);
    return keys;
}

QString Shortcut::displayText() const
{
#ifdef Q_OS_MACOS
    return displayKeys().join(QString());
#else
    return displayKeys().join(QLatin1Char('+'));
#endif
}

Shortcut Shortcut::defaultAddKeyframe()
{
    Shortcut s;
    s.modifiers = Qt::ControlModifier | Qt::ShiftModifier;
    s.key = Qt::Key_D;
    return s;
}

Shortcut Shortcut::defaultFrameDopeSheet()
{
    Shortcut s;
    s.modifiers = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier;
    s.key = Qt::Key_D;
    return s;
}
