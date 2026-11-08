#ifndef MIGHTYTOOLS_THEME_H
#define MIGHTYTOOLS_THEME_H

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;

// Tokens visuales de la app y la hoja de estilo global. Lenguaje «Pliego» (D-73): cada herramienta es
// una hoja mas clara sobre la mesa negra, y las tres tintas del logo tienen significado:
//   cian     = funciona (prendido, todo bien, calibrado),
//   magenta  = lo que hace el usuario (boton principal, links, ayuda abierta, foco y seleccion),
//   amarillo = pide atencion (en tinta llena, como «Update available»),
//   rojo     = error.
// Todo color o medida de la UI sale de aca; los widgets solo eligen objectName y propiedades.
namespace Theme {

// Fondos: la mesa (ventana, barra de titulo e indice) y la hoja (la pagina de cada herramienta).
inline constexpr const char *kDesk = "#0D0D0F";
inline constexpr const char *kSheet = "#17171B";
// Fondo de las paginas y de las ventanas de contenido (limpieza de Disk Space, dialogos). Es la hoja;
// los modulos lo usan tambien como fondo hundido de miniaturas.
inline constexpr const char *kWindow = kSheet;
inline constexpr const char *kTitleBar = kDesk;
inline constexpr const char *kCard = "#1E1E23";
inline constexpr const char *kCardBorder = "#25252B";
// Relleno de botones, teclas y el riel de las barras.
inline constexpr const char *kTile = "#2A2A31";
inline constexpr const char *kField = "#15151A";
inline constexpr const char *kDialog = kSheet;

// Bordes
inline constexpr const char *kBorder = "#2E2E36";
inline constexpr const char *kFieldBorder = "#2F2F37";
inline constexpr const char *kDivider = "#2A2A31";
// Renglon de un textarea rayado (sobre el fondo de campo).
inline constexpr const char *kTextareaRuleLine = "#1C1C21";

// Aviso de Disk Space (la tarjeta flotante del recordatorio).
inline constexpr const char *kReminderGradientStart = "#22222A";
inline constexpr const char *kReminderGradientEnd = "#1B1B20";
inline constexpr const char *kReminderBorder = "#33333B";
inline constexpr const char *kReminderFieldBorder = "#3A3A43";
inline constexpr const char *kReminderButtonBorder = "#3A3A43";
inline constexpr const char *kReminderText = "#F3F0E9";
inline constexpr const char *kReminderBody = "#C4C3C9";
inline constexpr const char *kReminderUnit = "#A6A5AC";
inline constexpr const char *kReminderHeaderIcon = "#9C9BA2";
inline constexpr const char *kReminderButton = "rgba(255,255,255,8)";
inline constexpr const char *kReminderButtonHover = "#33333B";

// Texto, de mas claro a mas apagado. Ningun texto chico baja de kTextFaint (contraste 4.5:1 sobre la hoja).
inline constexpr const char *kTextBright = "#ECE9E2";  ///< titulos
inline constexpr const char *kTextStrong = "#E3E1DA";  ///< nombres, valores, botones
inline constexpr const char *kText = "#C4C3C9";        ///< cuerpo
inline constexpr const char *kTextMuted = "#A6A5AC";   ///< descripciones
inline constexpr const char *kTextCaption = "#9C9BA2"; ///< notas y textos secundarios
inline constexpr const char *kTextFaint = "#8C8B93";   ///< metas: "Last check", "3 steps"
inline constexpr const char *kTextPlaceholder = "#5E5D66";
inline constexpr const char *kIcon = "#9C9BA2";        ///< glifos de los botones de icono
// Magenta: el foco, lo que se esta grabando, la seleccion y los links. kAccent y kLink son la misma tinta.
inline constexpr const char *kAccent = "#F070AE";
inline constexpr const char *kLink = "#F070AE";
inline constexpr const char *kLinkHover = "#F7A3CA";
// Seleccion de texto en un campo.
inline constexpr const char *kSelectionBg = "#5C1F3F";

// Estados
inline constexpr const char *kOk = "#4CC9EE";    ///< cian: funciona
inline constexpr const char *kWarn = "#F2CB3F";  ///< amarillo: pide atencion
inline constexpr const char *kError = "#F0645C"; ///< rojo: error
inline constexpr const char *kWarnInk = "#1C1606";    ///< texto sobre la tinta amarilla llena
inline constexpr const char *kWarnBorder = "#5A4E26"; ///< borde de una tarjeta que pide atencion
inline constexpr const char *kErrCardBorder = "#5E2A2C";
// Â«What changedÂ» de la limpieza: las carpetas que mas crecieron van en amarillo (pide atencion), de mas a menos
// intenso; lo que bajo va en cian (kOk), como en la lista.
inline constexpr const char *kGrowthStrong = kWarn;
inline constexpr const char *kGrowthMid = "#BFA03A";
inline constexpr const char *kGrowthSoft = "#8C7731";
// Barra de uso de un disco: lo ocupado y la marca del umbral (la misma en un disco bajo).
inline constexpr const char *kBarFill = "#8C8B93";
inline constexpr const char *kWarnMark = "#ECE9E2";
// Fondos y bordes de los estados: el cartel de "Freed..." y el boton de borrar definitivo.
inline constexpr const char *kOkBg = "#0D2A33";
inline constexpr const char *kOkBorder = "#24586A";
inline constexpr const char *kErrBg = "#361B1D";
inline constexpr const char *kErrBorder = "#6B2E31";
// Casilla sin tildar. La tildada es magenta (kPrimary / kPrimaryBorder / kTextOnAccent): la eleccion del usuario.
inline constexpr const char *kCheckOffBg = "#15151A";
inline constexpr const char *kCheckOffBorder = "#3A3A43";

// Indice lateral (mesa) y filas de listas.
inline constexpr const char *kNavHover = "#131317";       ///< ficha del indice con el mouse encima
inline constexpr const char *kListSelected = "#2B1424";   ///< fila elegida de una lista (seleccion: magenta)
inline constexpr const char *kListHover = "#222227";      ///< fila de una lista con el mouse encima
inline constexpr const char *kToolIconOn = "#CFCDD4";     ///< icono de una herramienta prendida
inline constexpr const char *kToolIconOff = "#6E6D75";    ///< icono de una apagada
inline constexpr const char *kToolIconSel = "#ECE9E2";    ///< icono de la ficha elegida
inline constexpr const char *kStatusOk = kOk;             ///< linea de estado de una fila que funciona
inline constexpr const char *kDotPaused = "#77767E";
inline constexpr const char *kDotOffBorder = "#4A4A55";
// Fondo y borde de las miniaturas de pantalla (zonas de Window Layouts y su selector).
inline constexpr const char *kModIconBg = "#15151A";
inline constexpr const char *kModIconBorder = "#2F2F37";
inline constexpr const char *kPlatBorder = "#3A3A43";
inline constexpr const char *kMiniCard = "#19191E";       ///< tarjetas de la bienvenida
// Boton principal (magenta: la accion del usuario).
inline constexpr const char *kPrimary = "#B4306E";
inline constexpr const char *kPrimaryBorder = "#CB4686";
inline constexpr const char *kPrimaryHover = "#C33B7C";
inline constexpr const char *kTextOnAccent = "#FFF3F9";   ///< texto y trazos sobre el magenta
// Interruptor: apagado gris, prendido cian (funciona). El riel apagado se despega de la tarjeta (#1E1E23) y
// de la mesa por el relleno y por un borde mas claro.
inline constexpr const char *kSwitchOff = "#26262D";
inline constexpr const char *kSwitchOffBorder = "#4A4A55";
inline constexpr const char *kSwitchKnobOff = "#77767E";
inline constexpr const char *kSwitchOn = "#0D3542";
inline constexpr const char *kSwitchOnBorder = "#2A97BC";
inline constexpr const char *kSwitchKnobOn = "#4CC9EE";
inline constexpr const char *kBorderStrong = "#3A3A43";   ///< borde de botones, menus y lo punteado
// Hover de fila generico (fuera del indice): la fila resaltada del popup de carpetas recientes.
inline constexpr const char *kRowHover = "#26262D";
// Tecla (`.kc` del diseno): borde de 1 y borde inferior grueso mas oscuro.
inline constexpr const char *kKeyBorder = "#3B3B44";
inline constexpr const char *kKeyBottom = "#0F0F12";
// El «?» de la barra con la ayuda abierta.
inline constexpr const char *kHelpActiveBg = "#2B1424";
// Tooltip propio.
inline constexpr const char *kTooltipBg = "#26262C";
inline constexpr const char *kTooltipBorder = "#3A3A43";

// "Elegido": un boton entre varios que queda marcado sin ser la accion principal (el segmento GB|%, el
// zoom de General, la version de Nuke elegida en Open in NukeX, los filtros de la limpieza). Elegir es
// una accion del usuario: magenta tenue con borde magenta y texto claro (D-73).
inline constexpr const char *kChosenBg = "#3A1730";
inline constexpr const char *kChosenHover = "#4A1D3D";
inline constexpr const char *kChosenBorder = "#CB4686";
inline constexpr const char *kChosenText = "#FFF3F9";

inline QColor color(const char *hex) { return QColor(QLatin1String(hex)); }

// Fuente de interfaz (Inter) en pixeles, con peso opcional.
QFont uiFont(qreal pixelSize, int weight = QFont::Normal);

// Fuente de titulos: Bricolage Grotesque si esta cargada; si no, Inter SemiBold (el peso pedido se
// recorta a 600, la app no trae Inter Bold).
QFont titleFont(qreal pixelSize, int weight = QFont::Bold);
// La familia y el peso con que titleFont() resuelve `weight` (para la hoja de estilo).
QString titleFamily();
int titleWeight(int weight);

// Carga Inter (y Bricolage si viene), estilo Fusion, paleta oscura y hoja de estilo global.
void apply(QApplication &app);

QString styleSheet();

// Tamano de fuente del diseno (px, puede ser 13.5) en puntos, como lo escribe la hoja de estilo.
QString fontSize(qreal px);

} // namespace Theme

#endif // MIGHTYTOOLS_THEME_H
