#!/usr/bin/osascript -l JavaScript

// Genera el fondo dark del DMG de instalacion. CERO dependencias: dibuja con AppKit a
// traves del puente ObjC de JXA, que viene con macOS. Antes esto era un script Python con
// Pillow, o sea un wheel compilado que cada app derivada tenia que instalar solo para
// poder cambiar el titulo por el nombre de su app.
//
//   osascript -l JavaScript tools/macos/make_dmg_background.js \
//       resources/dmg/dmg_background.tiff "LGA Mighty Tools" \
//       "Drag to install into your Applications folder" "LGA Mighty Tools" "README.txt"
//
// Las restricciones de Finder que definen este diseno, todas MEDIDAS en macOS 26:
//
// 1. Finder NO estira el background: mas alla de la imagen rellena en BLANCO, incluso con
//    el sistema en Dark Mode. Por eso el lienzo es gigante (3000x1900 pt) y de color plano:
//    agrandar la ventana revela mas del mismo dark en vez de una costura. Es lo que se ve
//    mal en casi todos los DMG dark que andan dando vueltas.
//
// 2. Con background propio, Finder pinta los nombres en NEGRO. Pasa igual con un color
//    solido que con una imagen, y no hay ninguna clave en el .DS_Store para cambiarlo. Por
//    eso va una placa clara debajo de cada label: sin ella los nombres son ilegibles sobre
//    el fondo oscuro.
//
// 3. El background se mapea 1 pixel = 1 punto, asi que en Retina se ve blando. Se emite un
//    TIFF con dos representaciones (1x y 2x) via `tiffutil -cathidpicheck`, que es lo mismo
//    que tiene adentro el .background.tiff de las apps que se ven bien.
//
// 4. Finder IGNORA el tamano de ventana guardado y abre con el suyo: ~920x436, o sea unos
//    377 pt de contenido. Todo el layout tiene que entrar ahi o el usuario no lo ve. Es lo
//    que limita el tamano de los iconos cuando hay dos filas.

ObjC.import('Cocoa');
ObjC.import('stdlib');

// --- Geometria, en PUNTOS. Tiene que coincidir con create_dmg.sh -----------------------
// El ancho NO es el de la ventana: Finder ignora el tamano guardado y abre con el que se le
// canta —hereda el de la ventana de Finder que ya estaba abierta, y solo con todo minimizado
// usa su default de ~920—. Como el fondo se ancla arriba-IZQUIERDA, disenar contra 920 deja
// la composicion corrida a la derecha y CORTADA en cualquier ventana mas angosta, que es el
// caso normal.
//
// Criterio robado de OnyX, que aguanta cualquier tamano: la composicion se disena contra una
// ventana CHICA (~620 pt) y ocupa poco ancho. En una ventana normal queda centrada; en una
// muy ancha queda hacia la izquierda, que se ve bien; y en una angosta no se corta nada.
var WIN_W = 620, WIN_H = 420;          // ancho de REFERENCIA del diseno, no el de la ventana
var CANVAS_W = 3000, CANVAS_H = 1900;  // lienzo gigante (ver restriccion 1)

// TODO se alinea contra UN SOLO eje: titulo, subtitulo, flecha y el par de iconos. Las dos
// columnas se definen como CX -/+ COL_DX, asi la simetria de la flecha sale por
// construccion y no depende de que dos numeros escritos a mano coincidan.
var CX = WIN_W / 2;
var COL_DX = 125;
var ICON_SIZE = 96;                    // tiene que coincidir con el icon_size del .DS_Store

var APP_X = CX - COL_DX;
var APPS_X = CX + COL_DX;

// Distancia del centro del icono al centro de la CAJA DE TEXTO del label. Medido contra
// capturas reales, no estimado: con +20 la placa quedaba 2.5 pt mas abajo que la caja, y el
// texto se veia pegado al borde de arriba.
//
// Como se mide: el aire ENCIMA del texto es constante entre labels (~3.5 pt, el tope de las
// mayusculas) y el de abajo varia (6.5 pt en un nombre con descendentes, 9 pt en "README.txt",
// que no tiene ninguno). Esa asimetria es la senal de que la placa esta corrida, no de que el
// texto este mal: si estuviera centrada, el label sin descendentes quedaria simetrico.
var LABEL_DY = ICON_SIZE / 2 + 17.5;

// La composicion va pegada arriba a proposito: la barra de titulo de la ventana ya hace de
// aire por encima, y lo que se agradece es el margen de abajo.
var TITLE_Y = 30, SUBTITLE_Y = 60;
var ROW1_Y = 132;                      // app y Applications
var ROW2_Y = 268;                      // README.txt, DEBAJO de la app (misma columna)
var ROW1_LABEL_Y = ROW1_Y + LABEL_DY;
var ROW2_LABEL_Y = ROW2_Y + LABEL_DY;

// La flecha va centrada en CX y su largo se DERIVA de la separacion entre columnas, para
// que el aire contra cada icono siga siendo el mismo si se cambia COL_DX o el tamano de
// icono. Escrito como una constante suelta, cualquier ajuste de ancho la dejaba flotando.
var ARROW_PAD = 40;
var ARROW_HALF = COL_DX - ICON_SIZE / 2 - ARROW_PAD;

// Las placas se MIDEN contra el texto de cada label en vez de tener un ancho fijo: con un
// ancho fijo, un nombre corto como "Applications" queda flotando en una mancha blanca enorme
// y uno largo se sale. El texto se mide con la misma fuente y el mismo cuerpo que usa Finder
// (`systemFontOfSize(TEXT_SIZE)`, el mismo valor que va al `.DS_Store`), asi que la placa
// calza. `TEXT_SIZE` tiene que coincidir con el `text_size` de create_dmg.sh.
var TEXT_SIZE = 13;
var PLATE_PAD_X = 16;                  // aire a cada lado del texto
var PLATE_PAD_Y = 4;                   // aire arriba y abajo

var BG = [18, 18, 22];
var TITLE_COL = [245, 245, 250];
var SUB_COL = [138, 138, 148];
var PLATE_COL = [226, 226, 234];
// ---------------------------------------------------------------------------------------

function rect(x, y, w, h) {
    return { origin: { x: x, y: y }, size: { width: w, height: h } };
}

function color(rgb, alpha) {
    return $.NSColor.colorWithCalibratedRedGreenBlueAlpha(
        rgb[0] / 255, rgb[1] / 255, rgb[2] / 255, alpha === undefined ? 1.0 : alpha);
}

// AppKit dibuja con el origen ABAJO a la izquierda y toda la geometria de arriba esta
// escrita de arriba hacia abajo (que es como la piensa Finder). Esta funcion es el unico
// lugar donde se invierte; si se hace suelto en cada llamada, un solo olvido descoloca un
// elemento y cuesta verlo.
function flipY(scale, yTop) {
    return CANVAS_H * scale - yTop * scale;
}

function drawCenteredText(str, cx, cyTop, scale, fontSize, rgb) {
    var font = $.NSFont.systemFontOfSize(fontSize * scale);
    var para = $.NSMutableParagraphStyle.alloc.init;
    para.alignment = 1;  // NSTextAlignmentCenter
    // Las claves van como literales y no como $.NSFontAttributeName: el puente ObjC de JXA
    // no expone esas constantes, y al llegar undefined el error que tira ("Expected
    // NSString type for attribute key") no dice ni de lejos que el problema es ese. Los
    // valores reales de las constantes son estos y no cambian.
    // Los arrays van envueltos en $(): un array JS suelto NO se convierte a NSArray aca y
    // el error que sale ("Expected NSString type for attribute key") apunta a cualquier
    // lado menos a eso.
    var attrs = $.NSDictionary.dictionaryWithObjectsForKeys(
        $([font, color(rgb), para]),
        $([$('NSFont'), $('NSColor'), $('NSParagraphStyle')]));
    var ns = $(str);
    var size = ns.sizeWithAttributes(attrs);
    var y = flipY(scale, cyTop) - size.height / 2;
    ns.drawInRectWithAttributes(rect(cx * scale - size.width / 2, y, size.width, size.height), attrs);
}

// Mide el ancho/alto que ocupa un texto con la fuente del sistema, para dimensionar su placa.
function measureText(str, scale, fontSize) {
    var font = $.NSFont.systemFontOfSize(fontSize * scale);
    var attrs = $.NSDictionary.dictionaryWithObjectsForKeys($([font]), $([$('NSFont')]));
    var size = $(str).sizeWithAttributes(attrs);
    return { w: size.width, h: size.height };
}

function render(scale, title, subtitle, appLabel, readmeLabel) {
    var w = CANVAS_W * scale, h = CANVAS_H * scale;
    var rep = $.NSBitmapImageRep.alloc
        .initWithBitmapDataPlanesPixelsWidePixelsHighBitsPerSampleSamplesPerPixelHasAlphaIsPlanarColorSpaceNameBytesPerRowBitsPerPixel(
            $(), w, h, 8, 4, true, false, $.NSCalibratedRGBColorSpace, 0, 0);

    var ctx = $.NSGraphicsContext.graphicsContextWithBitmapImageRep(rep);
    $.NSGraphicsContext.saveGraphicsState;
    $.NSGraphicsContext.setCurrentContext(ctx);

    color(BG).set;
    $.NSBezierPath.bezierPathWithRect(rect(0, 0, w, h)).fill;

    // SIN glow ni degrade de ningun tipo: el fondo es UN color plano y nada mas.
    //
    // Hubo un glow radial y hubo que sacarlo: `drawInRectRelativeCenterPosition` NO llega al
    // color final en el borde del rect —queda a ~2 niveles del fondo— y eso deja un escalon
    // horizontal a lo ancho de TODO el lienzo, visible como una banda cuando la ventana es
    // grande. Dos niveles parecen nada medidos de a un pixel, pero sobre un area oscura y
    // plana el ojo los lee como una linea.
    //
    // Cualquier cosa que se dibuje sobre el fondo tiene que terminar EXACTAMENTE en el color
    // plano antes de llegar a su propio borde, o reaparece la costura. Con el lienzo gigante
    // que exige Finder (ver restriccion 1) eso es dificil de garantizar, y un fondo plano no
    // tiene el problema por construccion.

    drawCenteredText(title, CX, TITLE_Y, scale, 28, TITLE_COL);
    drawCenteredText(subtitle, CX, SUBTITLE_Y, scale, 13, SUB_COL);

    // Flecha con degrade violeta, CENTRADA en CX y a la altura del centro de los iconos.
    var ax0 = (CX - ARROW_HALF) * scale, ax1 = (CX + ARROW_HALF) * scale;
    var ay = flipY(scale, ROW1_Y);
    var thick = Math.max(1, Math.round(2.5 * scale));
    for (var x = ax0; x < ax1; x++) {
        var tt = (x - ax0) / Math.max(1, ax1 - ax0);
        color([118 + 72 * tt, 86 + 44 * tt, 228 + 20 * tt]).set;
        $.NSBezierPath.bezierPathWithRect(rect(x, ay - thick / 2, 1, thick)).fill;
    }
    var head = 8 * scale;
    var tri = $.NSBezierPath.bezierPath;
    tri.moveToPoint({ x: ax1, y: ay });
    tri.lineToPoint({ x: ax1 - head * 1.8, y: ay - head });
    tri.lineToPoint({ x: ax1 - head * 1.8, y: ay + head });
    tri.closePath;
    color([190, 130, 248]).set;
    tri.fill;

    // Placas claras debajo de cada label (ver restriccion 2), MEDIDAS contra su propio texto.
    var plates = [
        [APP_X, ROW1_LABEL_Y, appLabel],
        [APPS_X, ROW1_LABEL_Y, 'Applications'],
        [APP_X, ROW2_LABEL_Y, readmeLabel]
    ];
    color(PLATE_COL).set;
    for (var p = 0; p < plates.length; p++) {
        var m = measureText(plates[p][2], scale, TEXT_SIZE);
        var pw = m.w + PLATE_PAD_X * 2 * scale;
        var ph = m.h + PLATE_PAD_Y * 2 * scale;
        var pcx = plates[p][0] * scale;
        var pcy = flipY(scale, plates[p][1]);
        $.NSBezierPath.bezierPathWithRoundedRectXRadiusYRadius(
            rect(pcx - pw / 2, pcy - ph / 2, pw, ph), ph / 2, ph / 2).fill;
    }

    $.NSGraphicsContext.restoreGraphicsState;
    return rep;
}

function writePNG(rep, path) {
    // 4 = NSBitmapImageFileTypePNG. La constante simbolica no siempre esta expuesta por el
    // puente, y el valor no cambia desde hace veinte anios.
    var data = rep.representationUsingTypeProperties(4, $());
    if (!data.writeToFileAtomically($(path), true)) {
        throw new Error('No se pudo escribir ' + path);
    }
}

function run(argv) {
    if (argv.length < 1) {
        console.log('Uso: make_dmg_background.js <salida.tiff> [titulo] [subtitulo] [nombre del .app] [nombre del README]');
        $.exit(1);
    }
    var out = argv[0];
    var title = argv.length > 1 ? argv[1] : 'LGA Mighty Tools';
    var subtitle = argv.length > 2 ? argv[2] : 'Drag to install into your Applications folder';
    // Cuarto argumento: el nombre del .app tal cual lo muestra Finder debajo del icono. Sin
    // el, la placa se dimensiona contra el titulo, que suele ser mas largo.
    var appLabel = argv.length > 3 ? argv[3] : title;
    // Quinto: el nombre del archivo de instrucciones (la placa de la segunda fila). Tiene que ser el
    // mismo que lleva el DMG (README_NAME en create_dmg.sh).
    var readmeLabel = argv.length > 4 ? argv[4] : 'README.txt';

    var png1x = out + '.1x.png', png2x = out + '.2x.png';
    writePNG(render(1, title, subtitle, appLabel, readmeLabel), png1x);
    writePNG(render(2, title, subtitle, appLabel, readmeLabel), png2x);

    // -cathidpicheck arma el TIFF multi-representacion que Finder usa como @2x. Comprime,
    // asi que el lienzo gigante termina pesando unos cientos de KB.
    var cmd = 'tiffutil -cathidpicheck ' + q(png1x) + ' ' + q(png2x) + ' -out ' + q(out) +
              ' >/dev/null && rm -f ' + q(png1x) + ' ' + q(png2x);
    if ($.system(cmd) !== 0) {
        throw new Error('tiffutil fallo');
    }

    console.log(out + ' listo — iconos ' + ICON_SIZE + ' pt, ventana ' + WIN_W + 'x' + WIN_H +
                ' pt, lienzo ' + CANVAS_W + 'x' + CANVAS_H + ' pt');
    return 0;
}

function q(s) {
    return "'" + s.replace(/'/g, "'\\''") + "'";
}
