# -*- coding: utf-8 -*-
"""Arma el icono de LGA Mighty Tools con el tratamiento de la marca LGA, y guarda las propuestas.

Uso (desde la raiz del repo):
    python tools/icono/armar_icono.py

Herramienta de desarrollo, no la usa la app. Necesita Pillow, numpy y scipy. La de referencia de la
marca es `make_planchas.py` de LGA_IconLab (en la Mac): esta hace lo mismo para esta app sin depender
de ese repo, y las metricas que imprime son una aproximacion a las suyas.

COMO SE ARMA EL COLOR (reglas de la marca, skill de iconos LGA):
  - La silueta se dibuja UNA vez, en un solo color.
  - Tres copias desplazadas: amarillo #fff100, cyan #00adee y magenta #eb008b, en multiply. Rojo,
    verde y azul NO son planchas: salen de donde se pisan dos.
  - El cuerpo es la triple interseccion. Despues todas las tintas se levantan a un negro de salida de
    38 (38 + v * 217/255 por canal), como las de PipeSync: el cuerpo queda #262626.

QUE ESCRIBE:
  resources/icons/Alta/propuestas/   las seis siluetas (SVG de un color) y la hoja con sus renders
  resources/icons/Alta/              la silueta elegida y el maestro desregistrado de 1024
  resources/icons/                   LGA_MightyTools.png (1024, recentrado al 92 %) y el .ico
"""
import math, os, sys

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ALTA = os.path.join(RAIZ, 'resources', 'icons', 'Alta')
PROPUESTAS = os.path.join(ALTA, 'propuestas')

LADO = 1024
SS = 4  # supersampling para el antialias
DESPLAZAMIENTOS = {'y': (-24, -16), 'm': (16, -26), 'c': (26, 18)}
TINTAS = {'y': (255, 241, 0), 'c': (0, 173, 238), 'm': (235, 0, 139)}
ELEGIDA = 'F'


# ---------------------------------------------------------------- geometria

def area(pts):
    return sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1]
               for i in range(len(pts))) / 2


def orientar(pts, horario=True):
    return pts if (area(pts) > 0) == horario else list(reversed(pts))


def esquinas(pts, r):
    """Para cada vertice: (punto de entrada, vertice, punto de salida) del redondeo."""
    n = len(pts)
    out = []
    for i in range(n):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
        v1 = (p0[0] - p1[0], p0[1] - p1[1]); l1 = math.hypot(*v1)
        v2 = (p2[0] - p1[0], p2[1] - p1[1]); l2 = math.hypot(*v2)
        rr = min(r, l1 / 2.2, l2 / 2.2)
        out.append(((p1[0] + v1[0] / l1 * rr, p1[1] + v1[1] / l1 * rr), p1,
                    (p1[0] + v2[0] / l2 * rr, p1[1] + v2[1] / l2 * rr)))
    return out


def path_svg(pts, r):
    e = esquinas(pts, r)
    d = 'M%.1f,%.1f ' % e[0][2]
    for i in range(1, len(e) + 1):
        a, c, b = e[i % len(e)]
        d += 'L%.1f,%.1f Q%.1f,%.1f %.1f,%.1f ' % (a[0], a[1], c[0], c[1], b[0], b[1])
    return d + 'Z'


def poligono_plano(pts, r, pasos=16):
    """El mismo contorno redondeado, aplanado a un poligono para rasterizar."""
    out = []
    for a, c, b in esquinas(pts, r):
        for k in range(pasos + 1):
            t = k / pasos
            out.append(((1 - t) ** 2 * a[0] + 2 * (1 - t) * t * c[0] + t * t * b[0],
                        (1 - t) ** 2 * a[1] + 2 * (1 - t) * t * c[1] + t * t * b[1]))
    return out


def rombo(cx, cy, rx, ry=None):
    ry = ry or rx
    return [(cx, cy - ry), (cx + rx, cy), (cx, cy + ry), (cx - rx, cy)]


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


# Cada opcion: (letra, nombre, solidos, huecos, radio de los solidos, radio de los huecos).
OPCIONES = [
    ('A', 'keyframe', [rombo(512, 512, 395)], [], 80, 0),
    ('B', 'key_rapido', [rombo(512, 512, 395)],
     [[(560, 250), (655, 250), (585, 455), (665, 455), (455, 790), (505, 545), (410, 545)]], 80, 18),
    ('C', 'tecla_key', [[(300, 150), (724, 150), (884, 870), (140, 870)]], [rombo(512, 545, 175)], 90, 26),
    ('D', 'puntero_key', [[(150, 110), (150, 760), (310, 615), (420, 860), (525, 812), (418, 575), (630, 575)],
                          rombo(700, 700, 210)], [], 34, 0),
    ('E', 'key_en_la_pista', [rect(110, 440, 914, 584), rombo(512, 512, 330)], [], 56, 0),
    ('F', 'dos_keys', [rombo(420, 512, 300), rombo(735, 512, 185)], [], 60, 0),
]


# ---------------------------------------------------------------- salidas

def svg_silueta(solidos, huecos, r, hr):
    # Un solo <path>. Los solidos van horarios y los huecos antihorarios: con nonzero se unen los que
    # se pisan y se calan los huecos. En Illustrator conviene Pathfinder > Unir antes de desregistrar.
    d = ' '.join(path_svg(orientar(p, True), r) for p in solidos)
    d += ''.join(' ' + path_svg(orientar(p, False), hr) for p in huecos)
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">\n'
            '<path fill="#262626" fill-rule="nonzero" d="%s"/>\n</svg>\n' % (LADO, LADO, LADO, LADO, d))


def mascara(solidos, huecos, r, hr):
    im = Image.new('L', (LADO * SS, LADO * SS), 0)
    dib = ImageDraw.Draw(im)
    for p in solidos:
        dib.polygon([(x * SS, y * SS) for x, y in poligono_plano(p, r)], fill=255)
    for p in huecos:
        dib.polygon([(x * SS, y * SS) for x, y in poligono_plano(p, hr)], fill=0)
    return np.array(im) > 127


def desplazar(m, dx, dy):
    out = np.zeros_like(m)
    dx, dy = dx * SS, dy * SS
    h, w = m.shape
    ys = slice(max(0, dy), min(h, h + dy)); yo = slice(max(0, -dy), min(h, h - dy))
    xs = slice(max(0, dx), min(w, w + dx)); xo = slice(max(0, -dx), min(w, w - dx))
    out[ys, xs] = m[yo, xo]
    return out


def desregistrar(sil):
    """Las tres planchas en multiply, levantadas a negro 38. Devuelve RGBA 1024 y las mascaras."""
    planchas = {k: desplazar(sil, *DESPLAZAMIENTOS[k]) for k in 'ymc'}
    rgb = np.full(sil.shape + (3,), 255.0)
    for k in 'ymc':
        rgb[planchas[k]] *= np.array(TINTAS[k]) / 255.0
    rgb = 38 + rgb * 217 / 255
    alfa = (planchas['y'] | planchas['m'] | planchas['c']).astype(float)
    # Reduccion con alfa premultiplicado: sin esto el borde se aclara con el blanco del fondo.
    h = sil.shape[0] // SS
    pre = (rgb * alfa[..., None]).reshape(h, SS, h, SS, 3).mean(axis=(1, 3))
    a = alfa.reshape(h, SS, h, SS).mean(axis=(1, 3))
    color = np.where(a[..., None] > 0, pre / np.maximum(a[..., None], 1e-6), 0)
    img = np.dstack([np.clip(color, 0, 255), a * 255]).round().astype(np.uint8)
    cuerpo = (planchas['y'] & planchas['m'] & planchas['c'])
    return Image.fromarray(img, 'RGBA'), cuerpo, alfa > 0


def medir(cuerpo, total):
    """Aproximacion a las dos metricas de la marca: cuerpo (% del icono) y separacion (px @1024)."""
    pct = 100.0 * cuerpo.sum() / total.sum()
    borde = total & ~cuerpo
    dist = ndimage.distance_transform_edt(~cuerpo) / SS
    # Mediana de la distancia del color al cuerpo. Medida igual sobre los iconos del pack (en el sitio),
    # da 25-34 px contra los 22-36 que documenta la marca: sirve de aproximacion, no reemplaza a IconLab.
    sep = np.percentile(dist[borde], 50) if borde.any() else 0
    return pct, sep


def recentrar(img, fraccion):
    """Recorta al dibujo y lo centra en un lienzo cuadrado ocupando `fraccion` del lado."""
    x0, y0, x1, y1 = img.getbbox()
    arte = img.crop((x0, y0, x1, y1))
    escala = fraccion * LADO / max(arte.size)
    arte = arte.resize((round(arte.width * escala), round(arte.height * escala)), Image.LANCZOS)
    lienzo = Image.new('RGBA', (LADO, LADO), (0, 0, 0, 0))
    lienzo.paste(arte, ((LADO - arte.width) // 2, (LADO - arte.height) // 2), arte)
    return lienzo


def main():
    os.makedirs(PROPUESTAS, exist_ok=True)
    renders = []
    for letra, nombre, solidos, huecos, r, hr in OPCIONES:
        base = '%s_%s' % (letra, nombre)
        open(os.path.join(PROPUESTAS, base + '.svg'), 'w', encoding='utf-8', newline='\n').write(
            svg_silueta(solidos, huecos, r, hr))
        img, cuerpo, total = desregistrar(mascara(solidos, huecos, r, hr))
        pct, sep = medir(cuerpo, total)
        print('%s %-16s cuerpo %5.1f %%  separacion %4.1f px   (marca: 56-73 %% y 22-36 px)'
              % (letra, nombre, pct, sep))
        renders.append((letra, nombre, img))
        if letra == ELEGIDA:
            open(os.path.join(ALTA, 'LGA_MightyTools_silueta.svg'), 'w', encoding='utf-8',
                 newline='\n').write(svg_silueta(solidos, huecos, r, hr))
            img.save(os.path.join(ALTA, 'LGA_MightyTools_1024.png'))
            win = recentrar(img, 0.92)
            win.save(os.path.join(RAIZ, 'resources', 'icons', 'LGA_MightyTools.png'))
            win.save(os.path.join(RAIZ, 'resources', 'icons', 'LGA_MightyTools.ico'),
                     sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64),
                            (128, 128), (256, 256)])

    # Hoja de las seis sobre gris medio (con cuerpo oscuro, sobre fondo oscuro parecen anillos).
    celda = 300
    hoja = Image.new('RGBA', (celda * 3, celda * 2), (128, 128, 128, 255))
    for i, (letra, nombre, img) in enumerate(renders):
        chico = img.resize((240, 240), Image.LANCZOS)
        hoja.alpha_composite(chico, ((i % 3) * celda + 30, (i // 3) * celda + 20))
        ImageDraw.Draw(hoja).text(((i % 3) * celda + 12, (i // 3) * celda + 272), '%s  %s' % (letra, nombre),
                                  fill=(20, 20, 20, 255))
    hoja.convert('RGB').save(os.path.join(PROPUESTAS, 'propuestas.png'))
    print('listo: propuestas en', os.path.relpath(PROPUESTAS, RAIZ), '| elegida', ELEGIDA)


if __name__ == '__main__':
    sys.exit(main())
