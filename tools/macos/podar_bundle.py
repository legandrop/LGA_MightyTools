#!/usr/bin/env python3
"""Poda el bundle de macOS despues de macdeployqt y verifica que quede autocontenido.

Uso: python3 tools/macos/podar_bundle.py "deploy/LGA Mighty Tools.app"

macdeployqt (el de Homebrew) copia TODOS los plugins de los modulos enlazados, incluidos los que esta
app no usa (SVG, PDF, teclado virtual, informacion de red) y que arrastran QtQuick, QtQml y glib; y
varios quedan rotos porque no resuelve sus @rpath. Aca:
  1. Se dejan solo los plugins de PLUGINS_KEEP.
  2. Se borran los frameworks y dylibs de Contents/Frameworks que ya nadie referencia (cierre
     transitivo desde el ejecutable y los plugins que quedan).
  3. Se verifica que toda dependencia de todo Mach-O del bundle exista adentro del bundle o en el
     sistema (/System, /usr/lib). Sale con 1 si algo falta o apunta a /opt/homebrew.
Va ANTES de firmar: la firma cubre el contenido.
"""
import os
import shutil
import subprocess
import sys

PLUGINS_KEEP = {
    'platforms': {'libqcocoa.dylib'},
    'styles': {'libqmacstyle.dylib'},
    'imageformats': {'libqico.dylib', 'libqicns.dylib', 'libqjpeg.dylib', 'libqgif.dylib'},
    # Sin un backend de TLS, QNetworkAccessManager no puede hacer HTTPS: el updater no llega ni al
    # manifiesto ("No functional TLS backend was found"). El de macOS es Secure Transport, que usa
    # el framework Security del sistema y no arrastra OpenSSL.
    'tls': {'libqsecuretransportbackend.dylib'},
}


def deps(path):
    out = subprocess.run(['otool', '-L', path], capture_output=True, text=True).stdout.splitlines()[1:]
    return [line.strip().split(' (')[0] for line in out if line.strip()]


def rpaths(path):
    out = subprocess.run(['otool', '-l', path], capture_output=True, text=True).stdout.splitlines()
    result = []
    for i, line in enumerate(out):
        if 'cmd LC_RPATH' in line:
            for nxt in out[i + 1:i + 4]:
                nxt = nxt.strip()
                if nxt.startswith('path '):
                    result.append(nxt.split(' ')[1])
    return result


def resolve(dep, binary, exe_dir, main_rpaths):
    loader = os.path.dirname(binary)
    if dep.startswith('@executable_path/'):
        return [os.path.normpath(os.path.join(exe_dir, dep[len('@executable_path/'):]))]
    if dep.startswith('@loader_path/'):
        return [os.path.normpath(os.path.join(loader, dep[len('@loader_path/'):]))]
    if dep.startswith('@rpath/'):
        rest = dep[len('@rpath/'):]
        cands = []
        # dyld busca @rpath en los LC_RPATH de la imagen y de la cadena que la cargo, que siempre
        # incluye al ejecutable (los plugins de Homebrew traen rpaths que en el bundle no existen).
        for rp in rpaths(binary) + main_rpaths:
            rp = rp.replace('@executable_path', exe_dir).replace('@loader_path', loader)
            cands.append(os.path.normpath(os.path.join(rp, rest)))
        return cands
    return [dep]


def is_system(dep):
    return dep.startswith('/System/') or dep.startswith('/usr/lib/')


def main():
    bundle = os.path.abspath(sys.argv[1])
    contents = os.path.join(bundle, 'Contents')
    exe_dir = os.path.join(contents, 'MacOS')
    plugins = os.path.join(contents, 'PlugIns')
    frameworks = os.path.join(contents, 'Frameworks')

    # 1. Plugins
    for kind in sorted(os.listdir(plugins)):
        kind_dir = os.path.join(plugins, kind)
        keep = PLUGINS_KEEP.get(kind, set())
        for name in sorted(os.listdir(kind_dir)):
            if name not in keep:
                os.remove(os.path.join(kind_dir, name))
        if not os.listdir(kind_dir):
            os.rmdir(kind_dir)

    # 2. Cierre transitivo desde el ejecutable y los plugins
    roots = [os.path.join(exe_dir, n) for n in os.listdir(exe_dir)]
    main_rpaths = [rp for r in roots for rp in rpaths(r)]
    for dirpath, _, files in os.walk(plugins):
        roots += [os.path.join(dirpath, f) for f in files if f.endswith('.dylib')]
    used = set()
    pending = list(roots)
    problems = []
    seen = set()
    while pending:
        binary = os.path.realpath(pending.pop())
        if binary in seen:
            continue
        seen.add(binary)
        for dep in deps(binary):
            if is_system(dep):
                continue
            if dep.startswith('/opt/homebrew') or dep.startswith('/usr/local'):
                problems.append(f'{os.path.relpath(binary, bundle)} -> {dep}')
                continue
            found = [c for c in resolve(dep, binary, exe_dir, main_rpaths) if os.path.exists(c)]
            if not found:
                problems.append(f'{os.path.relpath(binary, bundle)} -> {dep} (no esta en el bundle)')
                continue
            target = os.path.realpath(found[0])
            # Se marcan el nombre pedido (puede ser un symlink, libx.1.dylib) y el archivo real.
            for name in (found[0], target):
                real_dir = os.path.realpath(os.path.dirname(name))
                if real_dir.startswith(os.path.realpath(frameworks)):
                    rel = os.path.relpath(os.path.join(real_dir, os.path.basename(name)),
                                          os.path.realpath(frameworks)).split(os.sep)[0]
                    used.add(rel)
            pending.append(target)

    removed = []
    if os.path.isdir(frameworks):
        for name in sorted(os.listdir(frameworks)):
            if name not in used:
                path = os.path.join(frameworks, name)
                if os.path.isdir(path) and not os.path.islink(path):
                    shutil.rmtree(path)
                else:
                    os.remove(path)
                removed.append(name)

    print(f'Plugins: {sum(len(f) for _, _, f in os.walk(plugins))} | frameworks y dylibs usados: {len(used)} | '
          f'borrados sin uso: {len(removed)}')
    if problems:
        print('ERROR: dependencias que no resuelven adentro del bundle:')
        for p in sorted(set(problems)):
            print('  ' + p)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
