#ifndef MIGHTYTOOLS_THEME_H
#define MIGHTYTOOLS_THEME_H

#include <QColor>
#include <QFont>
#include <QString>

class QApplication;

// Tokens visuales de la app (tema oscuro LGA) y la hoja de estilo global. Copia del Theme de
// LGA_FolderSwitch, que a su vez recorta el de LGA_VideoDownloader: mismos valores, para que las
// apps se vean de la misma familia. Todo color o medida de la UI sale de aca; los widgets solo
// eligen objectName y propiedades.
namespace Theme {

// Fondos
inline constexpr const char *kWindow = "#161616";
inline constexpr const char *kTitleBar = "#101010";
inline constexpr const char *kCard = "#1d1d1d";
inline constexpr const char *kTile = "#242424";
inline constexpr const char *kField = "#1a1a1a";
inline constexpr const char *kDialog = "#1E1E1E";

// Bordes
inline constexpr const char *kBorder = "#303030";
inline constexpr const char *kFieldBorder = "#2f2f2f";
inline constexpr const char *kDivider = "#262626";

// Texto
inline constexpr const char *kText = "#B2B2B2";
inline constexpr const char *kTextStrong = "#CCCCCC";
inline constexpr const char *kTextBright = "#E6E6E6";
inline constexpr const char *kTextMuted = "#8f8f8f";
inline constexpr const char *kTextCaption = "#7b7b7b";
inline constexpr const char *kTextFaint = "#6f6f6f";
inline constexpr const char *kTextPlaceholder = "#555555";
inline constexpr const char *kIcon = "#6a6a6a";
inline constexpr const char *kAccent = "#774dcb";
inline constexpr const char *kLink = "#9D8FE0";

// Estados
inline constexpr const char *kOk = "#a8d86a";
inline constexpr const char *kWarn = "#d4a437";
inline constexpr const char *kError = "#e8836f";
// Barra de uso de un disco: lo ocupado, y la marca del umbral cuando el disco esta bajo.
inline constexpr const char *kBarFill = "#4e4e4e";
inline constexpr const char *kWarnMark = "#e6c56b";

// Ventana de herramientas (forma A del canvas): barra lateral, filas, encabezado del panel.
inline constexpr const char *kSide = "#131313";
inline constexpr const char *kSideHover = "#1b1b1b";
inline constexpr const char *kSideSelected = "#212027";
inline constexpr const char *kToolIconOn = "#b7aef0";   ///< icono de una herramienta prendida
inline constexpr const char *kToolIconOff = "#5a5a5a";  ///< icono en la fila de una apagada
inline constexpr const char *kStatusOk = "#8fb866";     ///< linea de estado verde de una fila
inline constexpr const char *kDotPaused = "#555555";
inline constexpr const char *kDotOffBorder = "#4a4a4a";
inline constexpr const char *kModIconBg = "#1f1d27";
inline constexpr const char *kModIconBorder = "#2e2a40";
inline constexpr const char *kPlatBorder = "#2c2c2c";
inline constexpr const char *kMiniCard = "#191919";     ///< tarjetas de la bienvenida
inline constexpr const char *kAboutName = "#9f84d6";
// Interruptor
inline constexpr const char *kPrimary = "#443a91";
inline constexpr const char *kPrimaryBorder = "#5243a8";
inline constexpr const char *kSwitchOff = "#2d2d33";
inline constexpr const char *kSwitchOffBorder = "#3a3a44";
inline constexpr const char *kSwitchKnobOff = "#7a7a80";
inline constexpr const char *kSwitchKnobOn = "#DDDBEE";
// Hover de fila generico (fuera de la barra lateral, que tiene el suyo, kSideHover): la fila
// resaltada del popup de carpetas recientes de Folder Switch.
inline constexpr const char *kRowHover = "#2a2a2a";

inline QColor color(const char *hex) { return QColor(QLatin1String(hex)); }

// Fuente de interfaz (Inter) en pixeles, con peso opcional.
QFont uiFont(qreal pixelSize, int weight = QFont::Normal);

// Carga Inter embebida, estilo Fusion, paleta oscura y hoja de estilo global.
void apply(QApplication &app);

QString styleSheet();

// Tamano de fuente del diseno (px, puede ser 13.5) en puntos, como lo escribe la hoja de estilo.
QString fontSize(qreal px);

} // namespace Theme

#endif // MIGHTYTOOLS_THEME_H
