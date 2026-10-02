#include "modules/nukeshortcuts/NukeShortcutsState.h"
#include "core/I18n.h"

#include "app/ModuleContext.h"

#include <QDebug>

namespace {

// Claves de la seccion [nukeShortcuts]. "paused" reemplaza al "enabled" de Nuke Shortcuts: el
// prendido de la herramienta es [modules]/nukeShortcuts/enabled.
const QString kPaused = QStringLiteral("paused");
const QString kSpotX = QStringLiteral("dopeSheet/x");
const QString kSpotY = QStringLiteral("dopeSheet/y");

bool validFraction(double value)
{
    return value >= 0.0 && value <= 1.0;
}

} // namespace

QString NukeShortcutsState::shortcutKey(ShortcutAction action)
{
    return action == ShortcutAction::AddKeyframe ? QStringLiteral("shortcuts/addKeyframe")
                                                 : QStringLiteral("shortcuts/frameDopeSheet");
}

NukeShortcutsState::NukeShortcutsState(ModuleContext *context, QObject *parent)
    : QObject(parent)
    , m_context(context)
{
    if (!m_context) {
        return;
    }
    m_paused = m_context->value(kPaused, false).toBool();

    // Un atajo ilegible en el .ini (editado a mano, o de una version futura) vuelve al de fabrica
    // en vez de dejar la accion sin atajo.
    const Shortcut addKeyframe = Shortcut::fromPortableString(m_context->value(shortcutKey(ShortcutAction::AddKeyframe)).toString());
    if (addKeyframe.isValid()) {
        m_addKeyframe = addKeyframe;
    }
    const Shortcut frame = Shortcut::fromPortableString(m_context->value(shortcutKey(ShortcutAction::FrameDopeSheet)).toString());
    if (frame.isValid()) {
        m_frameDopeSheet = frame;
    }

    bool okX = false;
    bool okY = false;
    const double x = m_context->value(kSpotX).toDouble(&okX);
    const double y = m_context->value(kSpotY).toDouble(&okY);
    if (okX && okY && validFraction(x) && validFraction(y)) {
        m_hasDopeSheetSpot = true;
        m_dopeSheetSpot = QPointF(x, y);
    }
    qInfo() << "[NukeShortcuts] Cargado:" << (m_paused ? "en pausa" : "activos")
            << "| Add keyframe:" << m_addKeyframe.toPortableString()
            << "| Frame Dope Sheet:" << m_frameDopeSheet.toPortableString()
            << "| punto del Dope Sheet:" << (m_hasDopeSheetSpot ? QStringLiteral("%1, %2").arg(x).arg(y) : QStringLiteral("sin calibrar"));
}

Shortcut NukeShortcutsState::shortcut(ShortcutAction action) const
{
    return action == ShortcutAction::AddKeyframe ? m_addKeyframe : m_frameDopeSheet;
}

NukeShortcutsState::Registration NukeShortcutsState::registration(ShortcutAction action) const
{
    return action == ShortcutAction::AddKeyframe ? m_addKeyframeRegistration : m_frameRegistration;
}

QString NukeShortcutsState::conflictWith(ShortcutAction action) const
{
    return action == ShortcutAction::AddKeyframe ? m_addKeyframeConflict : m_frameConflict;
}

QString NukeShortcutsState::actionTitle(ShortcutAction action)
{
    return action == ShortcutAction::AddKeyframe ? I18n::tr("Add keyframe") : I18n::tr("Frame Dope Sheet");
}

void NukeShortcutsState::writeValue(const QString &key, const QVariant &value)
{
    if (m_context) {
        m_context->setValue(key, value);
    }
}

void NukeShortcutsState::setPaused(bool paused)
{
    if (m_paused == paused) {
        return;
    }
    m_paused = paused;
    writeValue(kPaused, paused);
    emit changed();
}

void NukeShortcutsState::setShortcut(ShortcutAction action, const Shortcut &shortcut)
{
    Shortcut &target = action == ShortcutAction::AddKeyframe ? m_addKeyframe : m_frameDopeSheet;
    if (!shortcut.isValid() || target == shortcut) {
        return;
    }
    target = shortcut;
    writeValue(shortcutKey(action), shortcut.toPortableString());
    emit changed();
}

void NukeShortcutsState::setDopeSheetSpot(const QPointF &fraction)
{
    if (!validFraction(fraction.x()) || !validFraction(fraction.y())) {
        return;
    }
    if (m_hasDopeSheetSpot && m_dopeSheetSpot == fraction) {
        return;
    }
    m_hasDopeSheetSpot = true;
    m_dopeSheetSpot = fraction;
    writeValue(kSpotX, fraction.x());
    writeValue(kSpotY, fraction.y());
    emit changed();
}

void NukeShortcutsState::setNukeInFront(bool inFront)
{
    if (m_nukeInFront == inFront) {
        return;
    }
    m_nukeInFront = inFront;
    emit changed();
}

void NukeShortcutsState::setPluginInstalled(bool installed)
{
    if (m_pluginInstalled == installed) {
        return;
    }
    m_pluginInstalled = installed;
    emit changed();
}

void NukeShortcutsState::setPluginFramesDopeSheet(bool frames)
{
    if (m_pluginFramesDopeSheet == frames) {
        return;
    }
    m_pluginFramesDopeSheet = frames;
    emit changed();
}

void NukeShortcutsState::setRegistration(ShortcutAction action, Registration registration, const QString &conflictWith)
{
    Registration &target = action == ShortcutAction::AddKeyframe ? m_addKeyframeRegistration : m_frameRegistration;
    QString &conflict = action == ShortcutAction::AddKeyframe ? m_addKeyframeConflict : m_frameConflict;
    if (target == registration && conflict == conflictWith) {
        return;
    }
    target = registration;
    conflict = conflictWith;
    emit changed();
}

void NukeShortcutsState::setAccessibilityGranted(bool granted)
{
    if (m_accessibilityGranted == granted) {
        return;
    }
    m_accessibilityGranted = granted;
    emit changed();
}
