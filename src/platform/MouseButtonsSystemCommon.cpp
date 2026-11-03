#include "platform/MouseButtonsSystem.h"

namespace MouseButtonsSystem {

QList<Step> pressSteps(const Rule &rule)
{
    QList<Step> steps;
    const struct
    {
        Qt::KeyboardModifier modifier;
        int key;
    } modifiers[] = {{Qt::ControlModifier, Qt::Key_Control},
                     {Qt::AltModifier, Qt::Key_Alt},
                     {Qt::ShiftModifier, Qt::Key_Shift},
                     {Qt::MetaModifier, Qt::Key_Meta}};
    for (const auto &each : modifiers) {
        if (rule.keys.modifiers & each.modifier) steps.append({Step::KeyDown, each.key});
    }
    if (rule.keys.isValid()) steps.append({Step::KeyDown, rule.keys.key});
    if (rule.holdLeft) steps.append({Step::LeftDown, 0});
    return steps;
}

Machine::Machine(const QList<Rule> &rules)
    : m_rules(rules)
{
}

const Rule *Machine::ruleFor(Button button) const
{
    for (const Rule &rule : m_rules) {
        if (rule.button == button) return &rule;
    }
    return nullptr;
}

QList<Step> Machine::releaseOf(const QList<Step> &pressed)
{
    QList<Step> steps;
    for (auto it = pressed.crbegin(); it != pressed.crend(); ++it) {
        steps.append({it->kind == Step::LeftDown ? Step::LeftUp : Step::KeyUp, it->key});
    }
    return steps;
}

Result Machine::handle(const Event &event)
{
    const Rule *rule = ruleFor(event.button);
    if (!rule || event.ownInjected) {
        return {};
    }
    const int slot = int(event.button);
    if (event.down) {
        // Segunda apretada sin haber visto soltar: lo apretado sigue como esta y la proxima soltada lo suelta.
        if (m_held[slot]) return {true, {}};
        if (!event.overTarget) return {};
        m_held[slot] = true;
        m_pressed[slot] = pressSteps(*rule);
        return {true, m_pressed[slot]};
    }
    // Soltar sin haber tomado la apretada (fue sobre otra app, o antes del gancho): es de la app que la recibio.
    if (!m_held[slot]) return {};
    m_held[slot] = false;
    const QList<Step> steps = releaseOf(m_pressed[slot]);
    m_pressed[slot].clear();
    return {true, steps};
}

QList<Step> Machine::releaseAll()
{
    QList<Step> steps;
    for (int slot = 0; slot < kButtonCount; ++slot) {
        if (!m_held[slot]) continue;
        steps += releaseOf(m_pressed[slot]);
        m_pressed[slot].clear();
        m_held[slot] = false;
    }
    return steps;
}

bool Machine::holding() const
{
    for (const bool held : m_held) {
        if (held) return true;
    }
    return false;
}

} // namespace MouseButtonsSystem
