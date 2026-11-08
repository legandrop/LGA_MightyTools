#include "ui/Palette.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include "core/I18n.h"
#include <algorithm>
#include <cmath>

namespace Palette {

namespace {

// Cada color de fabrica de Theme.h y de la hoja de estilo en un solo grupo. Los grupos con byValue=false
// (titulos, rotulos del indice, elegido, interruptores) se piden por nombre (@color(rol) o @color(rol:#HEX)
// en las hojas, mapIn en el codigo que pinta), porque sus valores de fabrica son los mismos de otro grupo;
// sus «members» solo son la lista de tonos del editor. Los ids, los bases que coinciden y el orden son los
// de LGA GateBouncer: sus paletas (resources/palettes) se leen igual aca. El acento tiene de base el magenta
// del boton principal (kPrimary), el equivalente del violeta de GateBouncer.
const QVector<Role> kRoles = {
    {"desk", "Mesa: fondo exterior, barra de título, índice y menús", "#0D0D0F", {"#0D0D0F", "#0F0F12", "#131317", "#1B1B20"}},
    {"sheet", "Hoja: fondo de cada página, de los diálogos y de los campos", "#17171B", {"#17171B", "#15151A", "#19191E"}},
    {"card", "Tarjetas, filas, tooltip y hover", "#1E1E23", {"#1E1E23", "#1C1C21", "#222227", "#22222A", "#26262C", "#26262D"}},
    {"control", "Botones, teclas y divisores", "#2A2A31", {"#2A2A31", "#24242A", "#33333B"}},
    {"border", "Bordes", "#2E2E36",
     {"#25252B", "#2E2E36", "#2F2F37", "#333333", "#3A3A43", "#3B3B44", "#4A4A55"}},
    {"title", "Títulos (páginas, tarjetas, diálogos, barra)", "#ECE9E2", {"#ECE9E2"}, false},
    {"section", "Rótulos del índice (TOOLS)", "#8C8B93", {"#8C8B93"}, false},
    {"ink1", "Texto blanco: valores, nombres, botones", "#E3E1DA",
     {"#ECE9E2", "#F3F0E9", "#E8E8E8", "#E3E1DA", "#DCDAD3", "#CFCDD4"}},
    {"ink2", "Texto común: párrafos y descripciones", "#C4C3C9", {"#C4C3C9", "#A9A8AF", "#A6A5AC", "#A3A2A9"}},
    {"ink3", "Texto gris: notas, metas, íconos", "#8C8B93", {"#9C9BA2", "#8C8B93", "#85848C"}},
    {"ink4", "Texto apagado: deshabilitado, herramientas apagadas", "#6E6D75", {"#77767E", "#6E6D75", "#5E5D66"}},
    // gbTones: el equivalente de cada tono magenta en el violeta de GateBouncer (botón principal, su borde, hover y
    // apretado, link y su hover, texto sobre el botón, selección de texto, fila elegida y botón deshabilitado).
    {"accent", "Acento: links, foco, selección y botón principal", "#B4306E",
     {"#B4306E", "#CB4686", "#C33B7C", "#9E2860", "#F070AE", "#F7A3CA", "#FFF3F9", "#9A7387", "#5C1F3F", "#4A2338",
      "#3A1A2B", "#2B1424"},
     true,
     "#443A91",
     {{"#B4306E", "#443A91"}, {"#CB4686", "#5243A8"}, {"#C33B7C", "#5446B4"}, {"#9E2860", "#3B3380"},
      {"#F070AE", "#9D8FE0"}, {"#F7A3CA", "#C5BBF4"}, {"#FFF3F9", "#DDDBEE"}, {"#9A7387", "#7B7499"},
      {"#5C1F3F", "#2A2552"}, {"#4A2338", "#3A3466"}, {"#3A1A2B", "#1D1B2C"}, {"#2B1424", "#25223B"}}},
    // La base de «elegido» no es un color que se use: es la referencia que deja a sus tonos (mas oscuros que los
    // de GateBouncer respecto del magenta) a la misma distancia que alla, asi las paletas dan el mismo fondo.
    {"chosen", "Elegido en selectores (Zoom, GB | %, filtros, versión de Nuke)", "#682344",
     {"#4A1D3D", "#3A1730", "#CB4686", "#FFF3F9"},
     false,
     "#443A91",
     {{"#3A1730", "#2A2552"}, {"#4A1D3D", "#352E66"}, {"#CB4686", "#5243A8"}, {"#FFF3F9", "#DDDBEE"}}},
    {"switch", "Interruptores encendidos", "#4CC9EE", {"#4CC9EE", "#2A97BC", "#0D3542"}, false},
    {"ok", "Funciona: prendido, todo bien, calibrado", "#4CC9EE", {"#4CC9EE", "#24586A", "#0D2A33"}},
    {"warn", "Pide atención", "#F2CB3F", {"#F2CB3F", "#BFA03A", "#8C7731", "#5A4E26", "#1C1606"}},
    {"allow", "Permitir (en GateBouncer; Mighty Tools no lo usa)", "#5ACB8B", {"#5ACB8B"}},
    {"error", "Error", "#F0645C", {"#F0645C", "#6B2E31", "#5E2A2C", "#45222A", "#361B1D", "#2E1719"}},
};

QString norm(const QString &hex) { return hex.trimmed().toUpper(); }

const QHash<QString, QString> &memberRole() {
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> t;
        for (const auto &r : kRoles)
            if (r.byValue)
                for (const auto &m : r.members) t.insert(norm(m), QString::fromLatin1(r.id));
        return t;
    }();
    return table;
}

Data &state() {
    static Data d = factory();
    return d;
}

QHash<QString, QColor> &cache() {
    static QHash<QString, QColor> c;
    return c;
}

bool &factoryFlag() {
    static bool f = true;
    return f;
}

// El tono de fábrica «m» relativo al base de fábrica «b», puesto junto al base nuevo «n»: corre el tono, escala la
// saturación y conserva dónde cae la luz entre el base y el blanco (o el negro).
QColor derive(const QColor &m, const QColor &b, const QColor &n) {
    if (b == n) return m;
    float mh, ms, ml, bh, bs, bl, nh, ns, nl;
    m.getHslF(&mh, &ms, &ml);
    b.getHslF(&bh, &bs, &bl);
    n.getHslF(&nh, &ns, &nl);
    float h;
    if (nh < 0) h = mh < 0 ? 0 : mh;             // base nuevo gris puro: el tono no importa (saturación 0)
    else if (mh < 0 || bh < 0) h = nh;           // sin tono de partida: el del base nuevo
    else h = std::fmod(mh + (nh - bh) + 2.0f, 1.0f);
    float s = bs > 0.02f ? ms * ns / bs : ms + (ns - bs);
    s = std::clamp(s, 0.0f, 1.0f);
    float l;
    if (ml >= bl) l = bl >= 0.999f ? nl : nl + (ml - bl) * (1.0f - nl) / (1.0f - bl);
    else l = bl <= 0.001f ? nl : nl * ml / bl;
    l = std::clamp(l, 0.0f, 1.0f);
    return QColor::fromHslF(h, s, l);
}

// La paleta embebida que se aplico por ultima vez (applyPreset); cualquier otro cambio la borra.
QString &presetFlag() {
    static QString id;
    return id;
}

void changed() {
    presetFlag().clear();
    cache().clear();
    bool f = state().overrides.isEmpty();
    for (const auto &r : kRoles)
        if (state().bases.contains(r.id) && state().bases.value(r.id) != QColor(r.base)) f = false;
    factoryFlag() = f;
    emit Notifier::instance()->changed();
}

} // namespace

const QVector<Role> &roles() { return kRoles; }

const Role *role(const QString &id) {
    for (const auto &r : kRoles)
        if (id == QLatin1String(r.id)) return &r;
    return nullptr;
}

// Los colores tal cual estan escritos en Theme.h y la hoja de estilo. No es la paleta que ve el usuario de fabrica:
// esa es la primera de presetIds() (Violet), que aplica AppController al arrancar.
Data factory() {
    Data d;
    d.name = QStringLiteral("Base del código (Theme.h)");
    for (const auto &r : kRoles) d.bases.insert(r.id, QColor(r.base));
    return d;
}

const Data &current() { return state(); }

void apply(const Data &data) {
    Data d = factory();
    d.name = data.name;
    d.liked = data.liked;
    for (auto it = data.bases.cbegin(); it != data.bases.cend(); ++it)
        if (role(it.key()) && it.value().isValid()) d.bases.insert(it.key(), it.value());
    d.overrides = data.overrides;
    state() = d;
    changed();
}

void setBase(const QString &roleId, const QColor &color) {
    if (!role(roleId) || !color.isValid()) return;
    state().bases.insert(roleId, color);
    changed();
}

void setOverride(const QString &roleId, const QString &hex, const QColor &color) {
    const QString key = roleId + '/' + norm(hex);
    if (color.isValid()) state().overrides.insert(key, color);
    else state().overrides.remove(key);
    changed();
}

QColor base(const QString &roleId) {
    const Role *r = role(roleId);
    if (!r) return {};
    return state().bases.value(roleId, QColor(r->base));
}

bool isFactory() { return factoryFlag(); }

QColor mapIn(const QString &roleId, const QString &hex) {
    const QString h = norm(hex);
    if (isFactory()) return QColor(h);
    const QString key = roleId + '/' + h;
    auto &c = cache();
    if (const auto it = c.constFind(key); it != c.cend()) return it.value();
    QColor out(h);
    if (const Role *r = role(roleId)) {
        const QColor fixed = state().overrides.value(key);
        if (fixed.isValid()) {
            out = fixed;
        } else {
            // Con tabla de equivalencias, se parte del tono de GateBouncer y de su base (ver Role::gbTones).
            QColor from(h), fromBase(r->base);
            if (r->gbBase)
                for (const auto &pair : r->gbTones)
                    if (norm(QLatin1String(pair.first)) == h) {
                        from = QColor(QLatin1String(pair.second));
                        fromBase = QColor(QLatin1String(r->gbBase));
                        break;
                    }
            out = derive(from, fromBase, base(roleId));
        }
    }
    c.insert(key, out);
    return out;
}

QColor map(const QString &hex) {
    const QString h = norm(hex);
    const QString roleId = memberRole().value(h);
    if (roleId.isEmpty()) return QColor(h);
    return mapIn(roleId, h);
}

QString hex(const QString &factoryHex) { return map(factoryHex).name(QColor::HexRgb).toUpper(); }

QString remapSheet(const QString &sheet) {
    // Una sola pasada para «@color(rol)», «@color(rol:#HEX)» y los #RRGGBB sueltos: así el #HEX de adentro de
    // @color(...) no se traduce por su valor, y un color ya traducido no se vuelve a traducir. Un #RRGGBB pegado a
    // una palabra (el objectName de un selector, «QWidget#facade») no es un color, y uno que no es de ningun grupo
    // queda tal cual estaba escrito.
    static const QRegularExpression colorRe(QStringLiteral(
        R"(@color\((\w+)(?::(#[0-9A-Fa-f]{6}))?\)|(?<![0-9A-Za-z_#-])#[0-9A-Fa-f]{6}(?![0-9A-Za-z_-]))"));
    static const QRegularExpression rgbaRe(QStringLiteral(R"(rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,)"));
    const bool factoryNow = isFactory();
    QString out;
    qsizetype last = 0;
    for (auto it = colorRe.globalMatch(sheet); it.hasNext();) {
        const auto m = it.next();
        QString value;
        if (m.captured(0).startsWith('#')) {
            value = factoryNow || !memberRole().contains(norm(m.captured(0))) ? m.captured(0) : hex(m.captured(0));
        } else {
            // @color(rol): el base del grupo; @color(rol:#HEX): ese tono de fábrica calculado dentro del grupo.
            const QColor c = m.captured(2).isEmpty() ? base(m.captured(1)) : mapIn(m.captured(1), m.captured(2));
            value = c.isValid() ? c.name(QColor::HexRgb).toUpper() : QStringLiteral("#FF00FF");
        }
        out += sheet.mid(last, m.capturedStart() - last) + value;
        last = m.capturedEnd();
    }
    out += sheet.mid(last);
    if (factoryNow) return out;
    QString s = out;
    out.clear();
    last = 0;
    for (auto it = rgbaRe.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        const QColor c =
            map(QColor(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()).name(QColor::HexRgb));
        out += s.mid(last, m.capturedStart() - last) +
               QStringLiteral("rgba(%1,%2,%3,").arg(c.red()).arg(c.green()).arg(c.blue());
        last = m.capturedEnd();
    }
    return out + s.mid(last);
}

QStringList presetIds()
{
    // En el orden del desplegable de General (el de GateBouncer): la primera es la de fabrica y Magenta
    // (default-2) va ultima.
    return {QStringLiteral("default-1"), QStringLiteral("alto-contraste"), QStringLiteral("grafito-ambar"),
            QStringLiteral("niebla"), QStringLiteral("pizarra-turquesa"), QStringLiteral("default-2")};
}

QString defaultPreset()
{
    return presetIds().first();
}

QString presetName(const QString &id)
{
    if (id == QLatin1String("default-1")) {
        return I18n::tr("Violet");
    }
    if (id == QLatin1String("alto-contraste")) {
        return I18n::tr("High contrast");
    }
    if (id == QLatin1String("grafito-ambar")) {
        return I18n::tr("Graphite | amber");
    }
    if (id == QLatin1String("niebla")) {
        return I18n::tr("Mist");
    }
    if (id == QLatin1String("pizarra-turquesa")) {
        return I18n::tr("Warm teal");
    }
    if (id == QLatin1String("default-2")) {
        return I18n::tr("Magenta");
    }
    return id;
}

bool applyPreset(const QString &id)
{
    Data d;
    if (!presetIds().contains(id) || !load(QStringLiteral(":/palettes/%1.json").arg(id), &d)) {
        return false;
    }
    apply(d);
    presetFlag() = id;
    return true;
}

QString currentPreset()
{
    return presetFlag();
}

bool load(const QString &path, Data *out, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        if (error) *error = pe.errorString();
        return false;
    }
    const QJsonObject o = doc.object();
    Data d;
    d.name = o.value("name").toString();
    d.liked = o.value("liked").toBool();
    const QJsonObject colors = o.value("colors").toObject();
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        const QColor c(it.value().toString());
        if (role(it.key()) && c.isValid()) d.bases.insert(it.key(), c);
    }
    const QJsonObject ov = o.value("overrides").toObject();
    for (auto it = ov.begin(); it != ov.end(); ++it) {
        const QColor c(it.value().toString());
        if (c.isValid() && it.key().contains('/')) d.overrides.insert(it.key().section('/', 0, 0) + '/' + norm(it.key().section('/', 1)), c);
    }
    *out = d;
    return true;
}

bool save(const QString &path, const Data &data, QString *error) {
    QJsonObject colors;
    for (const auto &r : kRoles) colors.insert(r.id, data.bases.value(r.id, QColor(r.base)).name(QColor::HexRgb).toUpper());
    QJsonObject ov;
    for (auto it = data.overrides.cbegin(); it != data.overrides.cend(); ++it)
        ov.insert(it.key(), it.value().name(QColor::HexRgb).toUpper());
    QJsonObject o{{"name", data.name}, {"liked", data.liked}, {"colors", colors}};
    if (!ov.isEmpty()) o.insert("overrides", ov);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

Notifier *Notifier::instance() {
    static Notifier n;
    return &n;
}


} // namespace Palette
