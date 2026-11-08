#ifndef MIGHTYTOOLS_PALETTE_H
#define MIGHTYTOOLS_PALETTE_H

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

// Paletas de color (las mismas de LGA GateBouncer). Los colores del diseno siguen escritos en Theme.h y en
// la hoja de estilo con sus valores de fabrica; cada uno pertenece a un grupo («rol») y se recalcula a partir
// del color base de ese grupo: conserva su distancia en tono, saturacion y luz respecto del base de fabrica.
// Asi un solo color por grupo mueve todos sus tonos (fondo, hover, borde, texto encima) y cada tono se puede
// ajustar aparte si hace falta. Con la paleta del codigo (factory()) todo queda como esta escrito.
namespace Palette {

struct Role {
    const char *id;
    const char *label;   ///< para el editor de desarrollo
    const char *base;    ///< color base de fabrica
    QStringList members; ///< los colores de fabrica que siguen a este grupo
    bool byValue = true; ///< false: sus tonos se piden por nombre (@color(rol:#HEX), mapIn), no por valor
};

const QVector<Role> &roles();
const Role *role(const QString &id);

struct Data {
    QString name;
    bool liked = false;
    QHash<QString, QColor> bases;     ///< rol -> color base elegido (falta = el de fabrica)
    QHash<QString, QColor> overrides; ///< "rol/#RRGGBB" -> color fijo para ese tono
};

// Los colores tal cual estan en el codigo (Theme.h). No es la paleta que ve el usuario de fabrica: esa es la
// primera de presetIds(), que se aplica al arrancar.
Data factory();
const Data &current();
void apply(const Data &data); ///< reemplaza la paleta y avisa
void setBase(const QString &roleId, const QColor &color);
void setOverride(const QString &roleId, const QString &hex, const QColor &color); ///< invalido = sin ajuste
QColor base(const QString &roleId);
bool isFactory();

// El color actual de un color de fabrica. «mapIn» lo calcula dentro de un grupo dado (el interruptor usa el
// mismo cian que «funciona» pero tiene su propio grupo); «map» busca el grupo del color y deja igual el que no
// es de ninguno.
QColor map(const QString &hex);
QColor mapIn(const QString &roleId, const QString &hex);
QString hex(const QString &factoryHex); ///< map() como #RRGGBB

// Una hoja de estilo con los colores actuales: cada #RRGGBB y rgba(r,g,b,a) de fabrica, «@color(rol)» (el
// base del grupo) y «@color(rol:#HEX)» (ese tono de fabrica calculado dentro del grupo).
QString remapSheet(const QString &sheet);

// Las paletas que ofrece General, embebidas en :/palettes/<id>.json, en el orden del desplegable; la primera
// es la de fabrica. La elegida la guarda AppController en settings.ini (app/palette).
QStringList presetIds();
QString defaultPreset();
QString presetName(const QString &id); ///< el nombre visible, traducido
bool applyPreset(const QString &id);
QString currentPreset(); ///< la ultima aplicada con applyPreset (vacio si se aplico otra cosa)

bool load(const QString &path, Data *out, QString *error = nullptr);
bool save(const QString &path, const Data &data, QString *error = nullptr);

class Notifier final : public QObject
{
    Q_OBJECT

public:
    static Notifier *instance();

signals:
    void changed();
};

} // namespace Palette

#endif // MIGHTYTOOLS_PALETTE_H
