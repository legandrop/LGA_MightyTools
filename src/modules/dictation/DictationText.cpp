#include "modules/dictation/DictationText.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {

QStringList words(const QString &text)
{
    return text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

// El punto y los "..." del final de una pasada en vivo no son confiables: Whisper los pone en cada pausa
// y los saca cuando sigue la frase.
QString withoutClosingDots(QString text)
{
    text = text.trimmed();
    while (text.endsWith(QLatin1Char('.')) || text.endsWith(QChar(0x2026))) {
        text.chop(1);
        text = text.trimmed();
    }
    return text;
}

} // namespace

namespace DictationText {

QString Split::display() const
{
    if (confirmed.isEmpty() || provisional.isEmpty()) {
        return confirmed + provisional;
    }
    return confirmed + QLatin1Char(' ') + provisional;
}

QString normalizedWord(const QString &word)
{
    QString out;
    out.reserve(word.size());
    for (const QChar c : word) {
        if (c.isLetterOrNumber()) {
            out += c.toLower();
        }
    }
    return out;
}

bool echoesContext(const QString &hypothesis, const QStringList &vocabulary, const QString &prompt)
{
    QSet<QString> context = {QStringLiteral("glossary")};
    for (const QString &word : words(prompt + QLatin1Char(' ') + vocabulary.join(QLatin1Char(' ')))) {
        context.insert(normalizedWord(word));
    }
    for (const QString &word : words(hypothesis)) {
        const QString normalized = normalizedWord(word);
        if (!normalized.isEmpty() && !context.contains(normalized)) {
            return false;
        }
    }
    return true;
}

Split stabilize(const QString &confirmed, const QString &previous, const QString &next)
{
    const QStringList kept = words(confirmed);
    const QStringList before = words(withoutClosingDots(previous));
    const QStringList now = words(withoutClosingDots(next));

    int common = 0;
    while (common < before.size() && common < now.size()
           && normalizedWord(before.at(common)) == normalizedWord(now.at(common))) {
        ++common;
    }
    // La ultima palabra de la pasada nunca se confirma: puede estar cortada a la mitad ("la burla" ->
    // "laburo") y su puntuacion todavia no se sabe.
    common = qMin(common, int(now.size()) - 1);

    QStringList out = kept;
    for (int i = int(kept.size()); i < common; ++i) {
        out << now.at(i);
    }
    Split split;
    split.confirmed = out.join(QLatin1Char(' '));
    split.provisional = now.mid(out.size()).join(QLatin1Char(' '));
    return split;
}

QString applyReplacements(const QString &text, const Replacements &replacements)
{
    QString out = text;
    for (const auto &pair : replacements) {
        const QString from = pair.first.simplified();
        if (from.isEmpty()) {
            continue;
        }
        QString pattern = QRegularExpression::escape(from);
        pattern.replace(QStringLiteral("\\ "), QStringLiteral("\\s+"));
        // Palabra o frase completa: sin una letra o un numero pegados de ningun lado (\b solo ve ASCII).
        const QRegularExpression re(QStringLiteral("(?<![\\p{L}\\p{N}])") + pattern + QStringLiteral("(?![\\p{L}\\p{N}])"),
                                    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        // Armado a mano y no con QString::replace: ahi un "\1" de Y seria una referencia, no texto.
        QString result;
        qsizetype last = 0;
        QRegularExpressionMatchIterator it = re.globalMatch(out);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            result += out.mid(last, match.capturedStart() - last);
            result += pair.second;
            last = match.capturedEnd();
        }
        result += out.mid(last);
        out = result;
    }
    return out;
}

} // namespace DictationText
