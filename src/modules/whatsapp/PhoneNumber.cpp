#include "modules/whatsapp/PhoneNumber.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace PhoneNumber {

namespace {

// Los separadores que la gente usa dentro de un telefono: espacio (tambien el duro), guiones (tambien los
// tipograficos), punto y parentesis.
bool isSeparator(QChar c)
{
    return c == QLatin1Char(' ') || c == QChar(0x00A0) || c == QLatin1Char('-') || c == QChar(0x2010) || c == QChar(0x2011) || c == QChar(0x2012)
           || c == QChar(0x2013) || c == QLatin1Char('.') || c == QLatin1Char('(') || c == QLatin1Char(')');
}

// Lo que pegado ANTES de un numero dice que es parte de otra cosa: una palabra, una URL, un monto, un mail.
bool blocksBefore(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('/') || c == QLatin1Char('=') || c == QLatin1Char('_')
           || c == QLatin1Char('@') || c == QLatin1Char('#') || c == QLatin1Char('$') || c == QChar(0x20AC)
           || c == QLatin1Char('&') || c == QLatin1Char('%') || c == QLatin1Char('\\');
}

// Lo mismo DESPUES: «12345678abc», una fecha «11/11», una hora «14:30», un mail.
bool blocksAfter(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('/') || c == QLatin1Char(':') || c == QLatin1Char('@')
           || c == QLatin1Char('_') || c == QLatin1Char('%') || c == QLatin1Char('\\');
}

QString onlyDigits(const QString &text)
{
    QString digits;
    for (QChar c : text) {
        if (c.isDigit()) digits.append(QChar(QLatin1Char('0' + c.digitValue())));
    }
    return digits;
}

// CUIT / CUIL: 11 digitos, prefijo de persona o empresa y digito verificador valido.
bool looksLikeCuit(const QString &digits)
{
    if (digits.size() != 11) return false;
    static const QSet<QString> prefixes{QStringLiteral("20"), QStringLiteral("23"), QStringLiteral("24"), QStringLiteral("27"),
                                        QStringLiteral("30"), QStringLiteral("33"), QStringLiteral("34")};
    if (!prefixes.contains(digits.left(2))) return false;
    static const int weights[10] = {5, 4, 3, 2, 7, 6, 5, 4, 3, 2};
    int sum = 0;
    for (int i = 0; i < 10; ++i) sum += digits.at(i).digitValue() * weights[i];
    int check = 11 - (sum % 11);
    if (check == 11) check = 0;
    if (check == 10) check = 9;
    return check == digits.at(10).digitValue();
}

// Formas que no son telefonos aunque tengan 8 o mas digitos.
bool looksLikeOtherThing(const QString &token, const QString &digits, bool international, const QString &countryId)
{
    static const QRegularExpression date(QStringLiteral("^(\\d{4}[-.]\\d{1,2}[-.]\\d{1,2}|\\d{1,2}[-.]\\d{1,2}[-.]\\d{4})$"));
    static const QRegularExpression ip(QStringLiteral("^\\d{1,3}(\\.\\d{1,3}){3}$"));
    static const QRegularExpression dni(QStringLiteral("^\\d{1,2}\\.\\d{3}\\.\\d{3}$"));
    static const QRegularExpression cuit(QStringLiteral("^\\d{2}-\\d{8}-\\d$"));
    if (date.match(token).hasMatch() || ip.match(token).hasMatch() || dni.match(token).hasMatch()) return true;
    if (cuit.match(token).hasMatch()) return true;
    // Sin guiones, el verificador del CUIT coincide por azar con 1 de cada 11 numeros: solo en Argentina.
    return !international && countryId == QLatin1String("AR") && looksLikeCuit(digits);
}

// El numero nacional argentino (area + numero, sin el 0 y sin el 15): 10 digitos, o vacio con el motivo.
QString argentinaNational(QString n, QString *problem)
{
    if (n.startsWith(QLatin1Char('0'))) n = n.mid(1);
    if (n.size() == 12) {
        // El 15 de los celulares, justo despues del area: 11 (2 digitos) o un area de 3 o 4 que empieza con 2 o 3.
        int at = -1;
        if (n.startsWith(QLatin1String("11"))) {
            if (n.mid(2, 2) == QLatin1String("15")) at = 2;
        } else if (n.at(0) == QLatin1Char('2') || n.at(0) == QLatin1Char('3')) {
            if (n.mid(3, 2) == QLatin1String("15")) at = 3;
            else if (n.mid(4, 2) == QLatin1String("15")) at = 4;
        }
        if (at > 0) n = n.left(at) + n.mid(at + 2);
    }
    if (n.size() == 10 && (n.startsWith(QLatin1String("11")) || n.at(0) == QLatin1Char('2') || n.at(0) == QLatin1Char('3'))) {
        return n;
    }
    if (problem) {
        // «4567-8901» o «15-4567-8901»: un numero local sin el area.
        const bool local = n.size() == 8 || (n.size() == 10 && n.startsWith(QLatin1String("15")));
        *problem = local ? QStringLiteral("area") : QStringLiteral("argentina");
    }
    return {};
}

// Lo que sigue al 54: con o sin el 9, con o sin el 0, con o sin el 15. Siempre devuelve el 549 (D-78: un
// numero argentino en WhatsApp es casi siempre un celular).
QString argentinaInternational(QString rest, QString *problem)
{
    if (rest.startsWith(QLatin1Char('9')) && (rest.size() == 11 || rest.size() == 13)) rest = rest.mid(1);
    const QString national = argentinaNational(rest, problem);
    return national.isEmpty() ? QString() : QStringLiteral("549") + national;
}

QString international(const QString &digits, QString *problem)
{
    if (digits.startsWith(QLatin1String("54"))) {
        return argentinaInternational(digits.mid(2), problem);
    }
    // Ningun codigo de pais empieza con 0: «0001-00012345» (una factura) no es un 00 internacional.
    if (digits.size() < 8 || digits.size() > 15 || digits.startsWith(QLatin1Char('0'))) {
        if (problem) *problem = QStringLiteral("incomplete");
        return {};
    }
    return digits;
}

// Un tramo de texto con forma de telefono, partido en palabras (por los espacios). Si el tramo entero no da un
// numero, se prueban sus pedazos: «1) 11 4567-8901», «DNI 30123456 11 4567 8901», «11 4567 8901 24hs».
struct Token
{
    QStringList words;
};

bool isSpace(QChar c)
{
    return c == QLatin1Char(' ') || c == QChar(0x00A0);
}

QList<Token> tokens(const QString &text)
{
    QList<Token> found;
    const int size = text.size();
    int i = 0;
    while (i < size) {
        const QChar c = text.at(i);
        const bool starts = c.isDigit() || ((c == QLatin1Char('+') || c == QLatin1Char('(')) && i + 1 < size
                                            && (text.at(i + 1).isDigit() || text.at(i + 1) == QLatin1Char('+')
                                                || text.at(i + 1) == QLatin1Char(' ')));
        if (!starts) {
            ++i;
            continue;
        }
        const bool blocked = i > 0 && blocksBefore(text.at(i - 1));
        int end = i + 1;
        int separators = 0;
        while (end < size) {
            const QChar e = text.at(end);
            if (e.isDigit()) {
                separators = 0;
            } else if (isSeparator(e) || (e == QLatin1Char('+') && end == i + 1)) {
                if (++separators > 3) break;
            } else {
                break;
            }
            ++end;
        }
        int last = end;
        while (last > i && !text.at(last - 1).isDigit()) --last;
        const bool blockedAfter = end < size && blocksAfter(text.at(end)) && last == end;
        QStringList words;
        QString word;
        for (int k = i; k < last; ++k) {
            if (isSpace(text.at(k))) {
                if (!word.isEmpty()) words.append(word);
                word.clear();
            } else {
                word.append(text.at(k));
            }
        }
        if (!word.isEmpty()) words.append(word);
        i = end;
        // La palabra pegada a una letra, a una URL o a un monto es de otra cosa; el resto del tramo puede servir.
        if (blocked && !words.isEmpty()) words.removeFirst();
        if (blockedAfter && !words.isEmpty()) words.removeLast();
        if (!words.isEmpty()) found.append({words});
    }
    return found;
}

// Un candidato: sus digitos (sin el 00 internacional) y si es internacional; vacio si no tiene forma de telefono.
QString candidateDigits(const QString &text, const QString &countryId)
{
    QString body = text;
    while (body.startsWith(QLatin1Char('('))) body = body.mid(1);
    const QString digits = onlyDigits(body);
    const bool plus = body.startsWith(QLatin1Char('+'));
    const bool doubleZero = !plus && digits.startsWith(QLatin1String("00")) && digits.size() > 10;
    const QString clean = doubleZero ? digits.mid(2) : digits;
    if (clean.size() < 8 || clean.size() > 15) return {};
    if (looksLikeOtherThing(text, clean, plus || doubleZero, countryId)) return {};
    return clean;
}

} // namespace

const QList<Country> &countries()
{
    static const QList<Country> list = {
        {QStringLiteral("AR"), QStringLiteral("Argentina"), QStringLiteral("54"), {10}},
        {QStringLiteral("BO"), QStringLiteral("Bolivia"), QStringLiteral("591"), {8}},
        {QStringLiteral("BR"), QStringLiteral("Brazil"), QStringLiteral("55"), {10, 11}},
        {QStringLiteral("CL"), QStringLiteral("Chile"), QStringLiteral("56"), {9}},
        {QStringLiteral("CO"), QStringLiteral("Colombia"), QStringLiteral("57"), {10}},
        {QStringLiteral("MX"), QStringLiteral("Mexico"), QStringLiteral("52"), {10}},
        {QStringLiteral("PY"), QStringLiteral("Paraguay"), QStringLiteral("595"), {9}},
        {QStringLiteral("PE"), QStringLiteral("Peru"), QStringLiteral("51"), {8, 9}},
        {QStringLiteral("ES"), QStringLiteral("Spain"), QStringLiteral("34"), {9}},
        {QStringLiteral("US"), QStringLiteral("United States and Canada"), QStringLiteral("1"), {10}},
        {QStringLiteral("UY"), QStringLiteral("Uruguay"), QStringLiteral("598"), {8}},
        {QStringLiteral("none"), QStringLiteral("None"), QString(), {}},
    };
    return list;
}

const Country *country(const QString &id)
{
    for (const Country &c : countries()) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

QString defaultCountry()
{
    return QStringLiteral("AR");
}

QString normalize(const QString &candidate, const QString &countryId, QString *problem)
{
    QString body = candidate.trimmed();
    while (body.startsWith(QLatin1Char('('))) body = body.mid(1).trimmed();
    // «+44 (0) 20 7946 0958»: el 0 de larga distancia entre parentesis no se marca desde afuera.
    if (body.startsWith(QLatin1Char('+'))) body.remove(QStringLiteral("(0)"));
    const QString digits = onlyDigits(body);
    if (body.startsWith(QLatin1Char('+'))) return international(digits, problem);
    if (digits.startsWith(QLatin1String("00")) && digits.size() > 10) return international(digits.mid(2), problem);

    const Country *c = country(countryId);
    if (!c || c->code.isEmpty()) {
        if (problem) *problem = QStringLiteral("country-code");
        return {};
    }
    if (c->id == QLatin1String("AR")) {
        QString why;
        const QString national = argentinaNational(digits, &why);
        if (!national.isEmpty()) return QStringLiteral("549") + national;
        // Internacional copiado sin el +: «5491145678901».
        if (digits.startsWith(QLatin1String("54"))) {
            const QString full = argentinaInternational(digits.mid(2), nullptr);
            if (!full.isEmpty()) return full;
        }
        if (problem) *problem = why;
        return {};
    }
    QString n = digits;
    if (c->id == QLatin1String("MX")) {
        // Los prefijos viejos de Mexico: 044 / 045 (celular local) y 01 (larga distancia).
        if (n.size() == 13 && (n.startsWith(QLatin1String("044")) || n.startsWith(QLatin1String("045")))) n = n.mid(3);
        else if (n.size() == 12 && n.startsWith(QLatin1String("01"))) n = n.mid(2);
    } else if (c->id == QLatin1String("US")) {
        if (n.size() == 11 && n.startsWith(QLatin1Char('1'))) n = n.mid(1);
    } else if (n.startsWith(QLatin1Char('0'))) {
        n = n.mid(1);
    }
    if (c->lengths.contains(n.size())) return c->code + n;
    if (digits.startsWith(c->code) && c->lengths.contains(digits.size() - c->code.size())) return digits;
    if (problem) *problem = QStringLiteral("incomplete-for");
    return {};
}

Result parse(const QString &text, const QString &countryId)
{
    Result result;
    // Un link de WhatsApp manda: el numero ya viene internacional.
    static const QRegularExpression link(
        QStringLiteral("(?:wa\\.me/|(?:api|web)\\.whatsapp\\.com/send/?\\?(?:[^\\s]*?&)?phone=|whatsapp://send/?\\?(?:[^\\s]*?&)?phone=)"
                       "(?:\\+|%2B)?(\\d{6,15})(?!\\d)"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList linked;
    auto it = link.globalMatch(text);
    while (it.hasNext()) {
        // Lo armo WhatsApp: se respeta tal cual (un WhatsApp Business de un fijo argentino va sin el 9).
        const QString digits = it.next().captured(1);
        if (!linked.contains(digits)) linked.append(digits);
    }
    QStringList numbers;
    QString problem;
    if (!linked.isEmpty()) {
        numbers = linked;
        result.fromLink = true;
    } else {
        for (const Token &token : tokens(text)) {
            // El tramo entero; si no da, sus pedazos de palabras seguidas, de los mas largos a los mas cortos.
            // Una tarjeta (cuatro grupos de 4) no se parte: sus pedazos parecen telefonos.
            const int count = token.words.size();
            const bool card = count == 4 && std::all_of(token.words.cbegin(), token.words.cend(),
                                                        [](const QString &w) { return w.size() == 4; });
            QString chosen;
            QString why;
            for (int length = count; length >= 1 && chosen.isEmpty(); --length) {
                if (length < count && card) break;
                for (int from = 0; from + length <= count && chosen.isEmpty(); ++from) {
                    const QString text = token.words.mid(from, length).join(QLatin1Char(' '));
                    if (candidateDigits(text, countryId).isEmpty()) continue;
                    QString reason;
                    chosen = normalize(text, countryId, &reason);
                    if (chosen.isEmpty() && why.isEmpty()) why = reason;
                }
            }
            if (chosen.isEmpty()) {
                if (problem.isEmpty()) problem = why;
                continue;
            }
            if (!numbers.contains(chosen)) numbers.append(chosen);
        }
    }
    if (numbers.size() == 1) {
        result.status = Status::Ok;
        result.digits = numbers.first();
        result.display = format(result.digits);
    } else if (numbers.size() > 1) {
        result.status = Status::Several;
        for (const QString &digits : numbers) result.found.append(format(digits));
    } else if (!problem.isEmpty()) {
        result.status = Status::Incomplete;
        result.problem = problem;
    }
    return result;
}

QString format(const QString &digits)
{
    if (digits.startsWith(QLatin1String("549")) && digits.size() == 13) {
        const QString n = digits.mid(3);
        if (n.startsWith(QLatin1String("11"))) {
            return QStringLiteral("+54 9 11 %1-%2").arg(n.mid(2, 4), n.right(4));
        }
        // Sin saber si el area es de 3 o de 4 digitos: el numero junto y los ultimos 4 aparte.
        return QStringLiteral("+54 9 %1-%2").arg(n.left(6), n.right(4));
    }
    for (const Country &c : countries()) {
        if (!c.code.isEmpty() && digits.startsWith(c.code) && c.lengths.contains(digits.size() - c.code.size())) {
            return QStringLiteral("+%1 %2").arg(c.code, digits.mid(c.code.size()));
        }
    }
    return QStringLiteral("+") + digits;
}

QString logText(const QString &digits)
{
    return QStringLiteral("%1 digitos, +%2…").arg(digits.size()).arg(digits.left(2));
}

} // namespace PhoneNumber
