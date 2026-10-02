"""
LGA_KeyframeToggle | Lega

Atajo "Add keyframe" de LGA Mighty Tools resuelto DENTRO de Nuke: pone una key en el frame actual
del knob que esta bajo el mouse (o, si el mouse no esta sobre un knob, del que tiene el foco), y si
ya hay una key en ese frame, la borra. Reemplaza a la secuencia de clic derecho + flecha + Enter,
que fallaba con el campo del knob en foco (el clic derecho abre el menu de texto del campo).
Lo registra LGA_NukeShortcuts.

- Knob con un solo campo: la key va a todos los canales y el knob queda colapsado. Knob abierto en
  un campo por canal: solo el canal del campo del mouse o del foco.
"""

import nuke

try:  # PySide6 (Nuke 16+)
    from PySide6 import QtGui, QtWidgets
except ImportError:  # PySide2 (Nuke 15)
    from PySide2 import QtGui, QtWidgets


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
