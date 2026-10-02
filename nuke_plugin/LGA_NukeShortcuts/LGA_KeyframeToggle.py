"""
LGA_KeyframeToggle | Lega

Plugin de Nuke de la herramienta Nuke Shortcuts de LGA Mighty Tools. Se instala en
`<.nuke>/LGA_NukeShortcuts/` desde el panel de Nuke Shortcuts, con su propia linea en el init.py de la
.nuke. Funciona en cualquier Nuke con interfaz (Nuke, NukeX, Nuke Studio, Indie).

Atajo "Add keyframe" de LGA Mighty Tools resuelto DENTRO de Nuke: pone una key en el frame actual
del knob que esta bajo el mouse (o, si el mouse no esta sobre un knob, del que tiene el foco), y si
ya hay una key en ese frame, la borra. Reemplaza a la secuencia de clic derecho + flecha + Enter,
que fallaba con el campo del knob en foco (el clic derecho abre el menu de texto del campo).

- El atajo y el prendido salen del settings.ini de Mighty Tools ([modules] nukeShortcuts/enabled,
  [nukeShortcuts] paused y shortcuts/addKeyframe). Un QFileSystemWatcher sobre esa carpeta vuelve
  a aplicarlos cuando cambian, sin timers.
- Mientras el atajo esta activo, este Nuke deja una marca `nuke_set_key/<pid>` en la carpeta de
  Mighty Tools: con esa marca, Mighty Tools no registra el atajo global para este proceso y no
  corre su secuencia de teclas. Sin plugin (o con uno viejo), Mighty Tools sigue como antes.
- Knob con un solo campo: la key va a todos los canales y el knob queda colapsado. Knob abierto en
  un campo por canal: solo el canal del campo del mouse o del foco.
"""

import atexit
import ctypes
import os
import re
import sys

import nuke

try:  # PySide6 (Nuke 16+)
    from PySide6 import QtCore, QtGui, QtWidgets
except ImportError:  # PySide2 (Nuke 15)
    from PySide2 import QtCore, QtGui, QtWidgets

MENU_PARENT = "Edit/LGA Mighty Tools"
MENU_ITEM = "Add Keyframe"
MENU_PATH = MENU_PARENT + "/" + MENU_ITEM
DEFAULT_SHORTCUT = "Ctrl+Shift+D"
MARKER_DIR_NAME = "nuke_set_key"

_state = {"watcher": None, "timer": None, "applied": None}


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
    """(activo, atajo en formato portable de Qt). Solo lectura: QSettings no escribe si no se le pide."""
    path = os.path.join(_settings_dir(), "settings.ini")
    if not os.path.isfile(path):
        return False, ""
    settings = QtCore.QSettings(path, QtCore.QSettings.IniFormat)
    enabled = _as_bool(settings.value("modules/nukeShortcuts/enabled"), False)
    paused = _as_bool(settings.value("nukeShortcuts/paused"), False)
    shortcut = settings.value("nukeShortcuts/shortcuts/addKeyframe")
    shortcut = str(shortcut).strip() if shortcut else DEFAULT_SHORTCUT
    return enabled and not paused, shortcut


_MODIFIERS = ("ctrl", "shift", "alt", "meta")
_KEY = re.compile(r"^([A-Z0-9]|F([1-9]|1[0-2]))$")


def _nuke_shortcut(portable):
    """'Ctrl+Shift+D' -> 'ctrl+shift+D'. En mac 'Ctrl' es Cmd para Qt y para Nuke. Las mismas teclas
    que acepta la app (Shortcut::isSupportedKey); un texto que no valida se reemplaza por el de
    fabrica, que es lo que la app muestra en ese caso."""
    parts = [p.strip() for p in portable.split("+") if p.strip()]
    mods = [p.lower() for p in parts[:-1]]
    key = parts[-1].upper() if parts else ""
    if not _KEY.match(key) or any(m not in _MODIFIERS for m in mods) or len(set(mods)) != len(mods):
        if portable != DEFAULT_SHORTCUT:
            return _nuke_shortcut(DEFAULT_SHORTCUT)
        return ""
    return "+".join(mods + [key])


# ---------------------------------------------------------------------------
# Marca para Mighty Tools
# ---------------------------------------------------------------------------
def _marker_path():
    base = _settings_dir()
    return os.path.join(base, MARKER_DIR_NAME, str(os.getpid())) if base else ""


def _write_marker(shortcut):
    path = _marker_path()
    if not path:
        return
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(shortcut + "\n")
    except OSError as e:
        print("LGA_KeyframeToggle: no se pudo escribir la marca:", e)


def _remove_marker():
    path = _marker_path()
    if path and os.path.isfile(path):
        try:
            os.remove(path)
        except OSError:
            pass


def _pid_alive(pid):
    if sys.platform == "win32":
        # os.kill en Windows TERMINA el proceso: se pregunta por OpenProcess + GetExitCodeProcess.
        kernel32 = ctypes.windll.kernel32
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
    folder = os.path.join(base, MARKER_DIR_NAME) if base else ""
    if not folder or not os.path.isdir(folder):
        return
    for name in os.listdir(folder):
        if name.isdigit() and int(name) != os.getpid() and not _pid_alive(int(name)):
            try:
                os.remove(os.path.join(folder, name))
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Knob bajo el mouse o en foco
# ---------------------------------------------------------------------------
def _panel_root(widget):
    w = widget
    while w is not None and not w.objectName().startswith("NodePanel_"):
        w = w.parentWidget()
    return w


def _node_of_panel(panel):
    name = panel.objectName()[len("NodePanel_"):]
    node = nuke.toNode(name)
    if node is None:
        # Nodo dentro de un Group: el panel puede llevar solo el nombre corto.
        matches = [n for n in nuke.allNodes(recurseGroups=True) if n.fullName() == name or n.name() == name]
        node = matches[0] if len(matches) == 1 else None
    return node


def _knob_from_widget(widget):
    """(knob, widget del campo o del knob). El widget del knob se llama como el knob; la etiqueta de
    la izquierda es un QLabel suelto, y para ella se busca el knob de la misma fila, a su derecha."""
    if widget is None:
        return None, None
    panel = _panel_root(widget)
    if panel is None:
        return None, None
    node = _node_of_panel(panel)
    if node is None:
        return None, None
    knobs = node.knobs()
    w = widget
    while w is not None and w is not panel:
        if w.objectName() in knobs:
            return knobs[w.objectName()], widget
        w = w.parentWidget()
    pos = QtGui.QCursor.pos()
    best = None
    for child in panel.findChildren(QtWidgets.QWidget):
        name = child.objectName()
        if name not in knobs or not child.isVisible():
            continue
        top_left = child.mapToGlobal(child.rect().topLeft())
        if not (top_left.y() <= pos.y() < top_left.y() + child.height()) or top_left.x() < pos.x():
            continue
        if best is None or top_left.x() < best[0]:
            best = (top_left.x(), name, child)
    if best is not None:
        return knobs[best[1]], best[2]
    return None, None


def _channel_of(knob, field):
    """Si el knob muestra un campo por canal, el indice del campo; si muestra uno solo, None."""
    if not isinstance(field, QtWidgets.QLineEdit):
        return None
    box = field
    while box is not None and box.objectName() != knob.name():
        box = box.parentWidget()
    if box is None:
        return None
    fields = [f for f in box.findChildren(QtWidgets.QLineEdit) if f.isVisible()]
    if len(fields) < 2 or field not in fields or len(fields) != knob.arraySize():
        return None
    fields.sort(key=lambda f: f.mapToGlobal(f.rect().topLeft()).x())
    return fields.index(field)


# ---------------------------------------------------------------------------
# La accion
# ---------------------------------------------------------------------------
def toggle_key():
    knob, widget = _knob_from_widget(QtWidgets.QApplication.widgetAt(QtGui.QCursor.pos()))
    if knob is None:
        knob, widget = _knob_from_widget(QtWidgets.QApplication.focusWidget())
    if knob is None or not isinstance(knob, nuke.Array_Knob):
        return
    frame = nuke.frame()
    channel = _channel_of(knob, widget)
    channels = [channel] if channel is not None else list(range(knob.arraySize()))
    keyed = [i for i in channels if knob.isAnimated(i) and knob.isKeyAt(frame, i)]
    undo = nuke.Undo()
    undo.begin("Delete key" if keyed else "Set key")
    try:
        if keyed:
            for i in keyed:
                value = knob.getValueAt(frame, i)
                knob.removeKeyAt(frame, i)
                anim = knob.animation(i)
                if anim is None or not anim.keys():
                    # Era la unica key: el canal deja de estar animado y conserva el valor.
                    knob.clearAnimated(i)
                    knob.setValue(value, i)
        elif channel is not None:
            knob.setAnimated(channel)
            knob.setValueAt(knob.getValueAt(frame, channel), frame, channel)
        elif knob.singleValue():
            # Un solo valor para todo el knob: Nuke lo deja colapsado, como su "Set key".
            knob.setAnimated()
            knob.setValue(knob.getValueAt(frame, 0))
        else:
            knob.setAnimated()
            for i in range(knob.arraySize()):
                knob.setValueAt(knob.getValueAt(frame, i), frame, i)
    except Exception as e:
        print("LGA_KeyframeToggle: error en %s.%s: %s" % (knob.node().fullName(), knob.name(), e))
    finally:
        undo.end()


# ---------------------------------------------------------------------------
# Registro del atajo
# ---------------------------------------------------------------------------
def _apply():
    active, portable = _read_config()
    shortcut = _nuke_shortcut(portable) if active else ""
    if _state["applied"] == shortcut:
        return
    menu = nuke.menu("Nuke")
    # Se saca siempre antes: un atajo nuevo no tiene que convivir con el anterior.
    parent = menu.findItem(MENU_PARENT)
    if parent is not None and parent.findItem(MENU_ITEM) is not None:
        parent.removeItem(MENU_ITEM)
    if shortcut:
        menu.addCommand(MENU_PATH, toggle_key, shortcut)
        _write_marker(portable)
    else:
        _remove_marker()
    _state["applied"] = shortcut


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
    atexit.register(_remove_marker)
    _sweep_markers()
    _apply()


if __name__ == "__main__":
    # Prueba desde el Script Editor: exec(open(<este archivo>).read())
    setup()
