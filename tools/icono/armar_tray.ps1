# Arma el icono de la bandeja: los dos keys con el desregistro CMY ajustado al pixel, un PNG por tamano.
#
# Uso (desde la raiz del repo):
#   powershell -ExecutionPolicy Bypass -File tools\icono\armar_tray.ps1
#
# Herramienta de desarrollo, no la usa la app. Solo necesita Windows PowerShell (System.Drawing), sin
# Python. Escribe resources/icons/tray/tray_<n>.png para 16, 20, 24, 32, 40 y 48 px (escalas 100-300 %).
#
# Por que no se reduce el icono de la app: a 16 px sus planchas quedan corridas menos de un pixel y el
# suavizado las vuelve un halo finito y borroso. Aca cada plancha se corre en pasos de round(n / 16) px:
# amarillo un paso a la izquierda, magenta uno arriba, cian medio paso abajo a la derecha (la misma
# formula del tray de FolderSwitch). El cuerpo es la triple interseccion en #262626, igual sobre barra
# clara y oscura: sobre la oscura el cuerpo se funde y la forma la dibuja el borde de color.
# La geometria es la silueta F de armar_icono.py (dos rombos redondeados en un lienzo de 1024), con los
# rombos mas juntos y el chico algo mas grande: la F es ancha y chata, y a 16 px solo ocupaba 14 x 11.
# Asi entra mas grande en el cuadro (16 x 14) sin dejar de leerse como dos keys.
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$salida = Join-Path $raiz 'resources\icons\tray'

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class ArmarTray {
    const int SS = 16;  // supersampling: mascaras binarias que despues se promedian
    static readonly int[] Y = { 255, 241, 0 }, M = { 235, 0, 139 }, C = { 0, 173, 238 };
    const double Negro = 38;  // las tintas se levantan a un negro de salida de 38, como el app-icon

    // Rombo con las esquinas redondeadas por una cuadratica, como poligono_plano de armar_icono.py.
    static List<double[]> Rombo(double cx, double cy, double r, double radio) {
        var v = new[] { new[] { cx, cy - r }, new[] { cx + r, cy }, new[] { cx, cy + r }, new[] { cx - r, cy } };
        var pts = new List<double[]>();
        for (int i = 0; i < 4; i++) {
            double[] p0 = v[(i + 3) % 4], p1 = v[i], p2 = v[(i + 1) % 4];
            double l1 = Math.Sqrt(Math.Pow(p0[0] - p1[0], 2) + Math.Pow(p0[1] - p1[1], 2));
            double l2 = Math.Sqrt(Math.Pow(p2[0] - p1[0], 2) + Math.Pow(p2[1] - p1[1], 2));
            double rr = Math.Min(radio, Math.Min(l1 / 2.2, l2 / 2.2));
            double ax = p1[0] + (p0[0] - p1[0]) / l1 * rr, ay = p1[1] + (p0[1] - p1[1]) / l1 * rr;
            double bx = p1[0] + (p2[0] - p1[0]) / l2 * rr, by = p1[1] + (p2[1] - p1[1]) / l2 * rr;
            for (int k = 0; k <= 16; k++) {
                double t = k / 16.0;
                pts.Add(new[] { (1 - t) * (1 - t) * ax + 2 * (1 - t) * t * p1[0] + t * t * bx,
                                (1 - t) * (1 - t) * ay + 2 * (1 - t) * t * p1[1] + t * t * by });
            }
        }
        return pts;
    }

    // En el app-icon: Rombo(420, 512, 300, 60) y Rombo(735, 512, 185, 60).
    static readonly List<double[]>[] Silueta = { Rombo(450, 512, 300, 60), Rombo(680, 512, 200, 60) };

    // Mascara de la silueta en el lienzo de n*SS: escala al lado del icono y corre (dx, dy) subpixeles.
    static bool[] Mascara(int n, double escala, double dx, double dy) {
        int W = n * SS;
        using (var bmp = new Bitmap(W, W, PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(bmp)) {
            g.SmoothingMode = SmoothingMode.None;
            g.Clear(Color.Black);
            double k = W / 1024.0 * escala, c = 512;
            foreach (var poly in Silueta) {
                var pts = new PointF[poly.Count];
                for (int i = 0; i < poly.Count; i++)
                    pts[i] = new PointF((float)((poly[i][0] - c) * k + W / 2.0 + dx), (float)((poly[i][1] - c) * k + W / 2.0 + dy));
                g.FillPolygon(Brushes.White, pts);
            }
            var d = bmp.LockBits(new Rectangle(0, 0, W, W), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var buf = new byte[W * W * 4];
            Marshal.Copy(d.Scan0, buf, 0, buf.Length);
            bmp.UnlockBits(d);
            var m = new bool[W * W];
            for (int i = 0; i < m.Length; i++) m[i] = buf[i * 4] > 127;
            return m;
        }
    }

    static byte[] Componer(int n, double escala, int[] corrimiento) {
        int paso = (int)Math.Round(n / 16.0, MidpointRounding.ToEven);
        double p = paso * SS, sx = corrimiento[0] * SS, sy = corrimiento[1] * SS;
        var mY = Mascara(n, escala, sx - p, sy);
        var mM = Mascara(n, escala, sx, sy - p);
        var mC = Mascara(n, escala, sx + p / 2, sy + p / 2);
        int W = n * SS;
        var px = new byte[n * n * 4];
        for (int py = 0; py < n; py++)
        for (int qx = 0; qx < n; qx++) {
            double r = 0, gr = 0, b = 0, a = 0;
            for (int j = 0; j < SS; j++)
            for (int i = 0; i < SS; i++) {
                int q = (py * SS + j) * W + qx * SS + i;
                bool y = mY[q], m = mM[q], c = mC[q];
                if (!(y || m || c)) continue;
                double cr = 255, cg = 255, cb = 255;
                if (y) { cr *= Y[0] / 255.0; cg *= Y[1] / 255.0; cb *= Y[2] / 255.0; }
                if (m) { cr *= M[0] / 255.0; cg *= M[1] / 255.0; cb *= M[2] / 255.0; }
                if (c) { cr *= C[0] / 255.0; cg *= C[1] / 255.0; cb *= C[2] / 255.0; }
                // Levantar a negro 38: la triple interseccion (el cuerpo) queda #262626.
                cr = Negro + cr * (255 - Negro) / 255; cg = Negro + cg * (255 - Negro) / 255; cb = Negro + cb * (255 - Negro) / 255;
                if (y && m && c) { cr = cg = cb = Negro; }
                r += cr; gr += cg; b += cb; a += 1;
            }
            int o = (py * n + qx) * 4;
            if (a > 0) {
                px[o + 2] = (byte)Math.Round(r / a); px[o + 1] = (byte)Math.Round(gr / a);
                px[o] = (byte)Math.Round(b / a); px[o + 3] = (byte)Math.Round(a * 255.0 / (SS * SS));
            }
        }
        return px;
    }

    // Arma el icono centrado: compone una vez, mide la caja con contenido y la centra en pixeles enteros
    // (un corrimiento fraccionario ensuciaria el borde).
    public static void Icono(int n, double escala, string ruta) {
        var prueba = Componer(n, escala, new[] { 0, 0 });
        int x0 = n, x1 = -1, y0 = n, y1 = -1;
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++)
            if (prueba[(y * n + x) * 4 + 3] > 0) { x0 = Math.Min(x0, x); x1 = Math.Max(x1, x); y0 = Math.Min(y0, y); y1 = Math.Max(y1, y); }
        int dx = (int)Math.Floor((n - (x0 + x1 + 1)) / 2.0 + 0.5), dy = (int)Math.Floor((n - (y0 + y1 + 1)) / 2.0 + 0.5);
        var px = Componer(n, escala, new[] { dx, dy });
        using (var bmp = new Bitmap(n, n, PixelFormat.Format32bppArgb)) {
            var d = bmp.LockBits(new Rectangle(0, 0, n, n), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(px, 0, d.Scan0, px.Length);
            bmp.UnlockBits(d);
            bmp.Save(ruta, ImageFormat.Png);
        }
    }
}
'@

# Escala de la silueta dentro del cuadro: con los colores, el ancho llega justo al lado del icono.
$escala = 1.30
New-Item -ItemType Directory -Force $salida | Out-Null
foreach ($n in 16, 20, 24, 32, 40, 48) {
    [ArmarTray]::Icono($n, $escala, (Join-Path $salida "tray_$n.png"))
}
Get-ChildItem $salida -Filter 'tray_*.png' | ForEach-Object { $_.Name }
