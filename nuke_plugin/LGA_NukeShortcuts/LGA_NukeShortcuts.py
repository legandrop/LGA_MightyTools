"""
LGA_NukeShortcuts | Lega

Plugin de Nuke de la herramienta Nuke Shortcuts de LGA Mighty Tools. Se instala en
`<.nuke>/LGA_NukeShortcuts/` desde el panel de Nuke Shortcuts, con su propia linea en el init.py de la
.nuke. Funciona en cualquier Nuke con interfaz (Nuke, NukeX, Nuke Studio, Indie).

Registra DENTRO de Nuke los dos atajos de la herramienta, que antes eran secuencias de teclas y clicks
mandadas desde la app:
- "Add keyframe" (LGA_KeyframeToggle): pone o saca la key del knob bajo el mouse.
- "Frame Dope Sheet" (LGA_FrameDopeSheet): selecciona y encuadra todas las keys del Dope Sheet.

- Los atajos y el prendido salen del settings.ini de Mighty Tools ([modules] nukeShortcuts/enabled,
  [nukeShortcuts] paused y shortcuts/<accion>). Un QFileSystemWatcher sobre esa carpeta los vuelve a
  aplicar cuando cambian, sin timers.
- Mientras un atajo esta activo, este Nuke deja una marca `<carpeta de la accion>/<pid>` en la carpeta de
  Mighty Tools: con esa marca, Mighty Tools no registra ese atajo global para este proceso y no manda
  su secuencia. Cada accion tiene su carpeta: un plugin viejo que solo sabia del keyframe no apaga el
  macro del Dope Sheet. Sin plugin, Mighty Tools sigue como antes.
"""

import atexit
import ctypes
import os
import re
import sys

import nuke

try:  # PySide6 (Nuke 16+)
    from PySide6 import QtCore
except ImportError:  # PySide2 (Nuke 15)
    from PySide2 import QtCore

import LGA_FrameDopeSheet
import LGA_KeyframeToggle

MENU_PARENT = "Edit/LGA Mighty Tools"

# Cada accion: item del menu, clave del settings.ini, atajo de fabrica (el de Shortcut::default* de la
# app), carpeta de marcas y funcion. La carpeta del keyframe es la de la version 1.00 del plugin.
_ACTIONS = (
    {
        "item": "Add Keyframe",
        "key": "addKeyframe",
        "default": "Ctrl+Shift+D",
        "markers": "nuke_set_key",
        "run": LGA_KeyframeToggle.toggle_key,
    },
    {
        "item": "Frame Dope Sheet",
        "key": "frameDopeSheet",
        "default": "Ctrl+Alt+Shift+D",
        "markers": "nuke_frame_dope",
        "run": LGA_FrameDopeSheet.frame_dope_sheet,
    },
)
ACTIONS = _ACTIONS

_state = {"watcher": None, "timer": None, "applied": {}}


# ---------------------------------------------------------------------------
# Configuracion de Mighty Tools
# ---------------------------------------------------------------------------
def _settings_dir():
    if sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.environ.get("APPDATA", "")
    return os.path.join(base, "LGA", "LGA_MightyTools") if base else ""


def _as_bool(value, default):
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in ("true", "1")


def _read_config():
    """(activo, {clave: atajo en formato portable de Qt}). Solo lectura: QSettings no escribe si no se
    le pide."""
    path = os.path.join(_settings_dir(), "settings.ini")
    if not os.path.isfile(path):
        return False, {}
    settings = QtCore.QSettings(path, QtCore.QSettings.IniFormat)
    enabled = _as_bool(settings.value("modules/nukeShortcuts/enabled"), False)
    paused = _as_bool(settings.value("nukeShortcuts/paused"), False)
    shortcuts = {}
    for action in ACTIONS:
        value = settings.value("nukeShortcuts/shortcuts/" + action["key"])
        shortcuts[action["key"]] = str(value).strip() if value else action["default"]
    return enabled and not paused, shortcuts


_MODIFIERS = ("ctrl", "shift", "alt", "meta")
_KEY = re.compile(r"^([A-Z0-9]|F([1-9]|1[0-2]))$")


def _nuke_shortcut(portable, default):
    """'Ctrl+Shift+D' -> 'ctrl+shift+D'. En mac 'Ctrl' es Cmd para Qt y para Nuke. Las mismas teclas
    que acepta la app (Shortcut::isSupportedKey); un texto que no valida se reemplaza por el de
    fabrica, que es lo que la app muestra en ese caso."""
    parts = [p.strip() for p in portable.split("+") if p.strip()]
    mods = [p.lower() for p in parts[:-1]]
    key = parts[-1].upper() if parts else ""
    if not _KEY.match(key) or any(m not in _MODIFIERS for m in mods) or len(set(mods)) != len(mods):
        if portable != default:
            return _nuke_shortcut(default, default)
        return ""
    return "+".join(mods + [key])


# ---------------------------------------------------------------------------
# Marcas para Mighty Tools
# ---------------------------------------------------------------------------
def _marker_path(folder):
    base = _settings_dir()
    return os.path.join(base, folder, str(os.getpid())) if base else ""


def _write_marker(folder, shortcut):
    path = _marker_path(folder)
    if not path:
        return
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(shortcut + "\n")
    except OSError as e:
        print("LGA_NukeShortcuts: no se pudo escribir la marca:", e)


def _remove_marker(folder):
    path = _marker_path(folder)
    if path and os.path.isfile(path):
        try:
            os.remove(path)
        except OSError:
            pass


def _remove_all_markers():
    for action in ACTIONS:
        _remove_marker(action["markers"])


def _pid_alive(pid):
    if sys.platform == "win32":
        # os.kill en Windows TERMINA el proceso: se pregunta por OpenProcess + GetExitCodeProcess.
        kernel32 = ctypes.WinDLL("kernel32")  # propio: no se tocan los argtypes del windll compartido
        kernel32.OpenProcess.restype = ctypes.c_void_p
        kernel32.GetExitCodeProcess.argtypes = (ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong))
        kernel32.CloseHandle.argtypes = (ctypes.c_void_p,)
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        ok = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        return bool(ok) and code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except OSError:
        return True
    return True


def _sweep_markers():
    """Borra las marcas de los Nuke que se cerraron sin pasar por atexit (un crash)."""
    base = _settings_dir()
    if not base:
        return
    for action in ACTIONS:
        folder = os.path.join(base, action["markers"])
        if not os.path.isdir(folder):
            continue
        for name in os.listdir(folder):
            if name.isdigit() and int(name) != os.getpid() and not _pid_alive(int(name)):
                try:
                    os.remove(os.path.join(folder, name))
                except OSError:
                    pass


# ---------------------------------------------------------------------------
# Registro de los atajos
# ---------------------------------------------------------------------------
def _apply():
    active, shortcuts = _read_config()
    menu = nuke.menu("Nuke")
    for action in ACTIONS:
        portable = shortcuts.get(action["key"], action["default"])
        shortcut = _nuke_shortcut(portable, action["default"]) if active else ""
        if _state["applied"].get(action["key"]) == shortcut:
            # La carpeta de la app pudo borrarse con Nuke abierto (desinstalar y reinstalar): sin la
            # marca, la app volveria a registrar el atajo global y a mandar su macro.
            path = _marker_path(action["markers"])
            if shortcut and path and not os.path.isfile(path):
                _write_marker(action["markers"], portable)
            continue
        # Se saca siempre antes: un atajo nuevo no tiene que convivir con el anterior.
        parent = menu.findItem(MENU_PARENT)
        if parent is not None and parent.findItem(action["item"]) is not None:
            parent.removeItem(action["item"])
        if shortcut:
            menu.addCommand(MENU_PARENT + "/" + action["item"], action["run"], shortcut)
            _write_marker(action["markers"], portable)
        else:
            _remove_marker(action["markers"])
        _state["applied"][action["key"]] = shortcut


def _watch_settings_dir():
    """Vigila la carpeta de la app; si todavia no existe, la de LGA, hasta que aparezca."""
    watcher = _state["watcher"]
    folder = _settings_dir()
    if watcher is None or not folder:
        return
    wanted = folder if os.path.isdir(folder) else os.path.dirname(folder)
    current = watcher.directories()
    if wanted in current or not os.path.isdir(wanted):
        return
    if current:
        watcher.removePaths(current)
    watcher.addPath(wanted)


def _schedule_apply(*_):
    _watch_settings_dir()
    # QSettings guarda con un archivo temporal y un rename: llegan varios avisos seguidos.
    timer = _state["timer"]
    if timer is not None:
        timer.start(300)


def setup():
    if not nuke.GUI or _state["watcher"] is not None:
        return
    folder = _settings_dir()
    if not folder:
        return
    timer = QtCore.QTimer()
    timer.setSingleShot(True)
    timer.timeout.connect(_apply)
    _state["timer"] = timer
    watcher = QtCore.QFileSystemWatcher()
    watcher.directoryChanged.connect(_schedule_apply)
    _state["watcher"] = watcher
    _watch_settings_dir()
    atexit.register(_remove_all_markers)
    _sweep_markers()
    _apply()


if __name__ == "__main__":
    # Prueba desde el Script Editor, con la carpeta del plugin en el path de Nuke:
    # exec(open(<este archivo>).read())
    setup()
