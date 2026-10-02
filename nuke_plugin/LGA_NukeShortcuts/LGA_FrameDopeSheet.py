"""
LGA_FrameDopeSheet | Lega

Atajo "Frame Dope Sheet" de LGA Mighty Tools resuelto DENTRO de Nuke: selecciona todas las keys del
Dope Sheet y las encuadra (lo que hacen Ctrl+A y F con el mouse sobre el Dope Sheet), sin mover el
puntero, sin click y sin calibrar. Lo registra LGA_NukeShortcuts.

Como funciona (sondas en Nuke 17, 2026-10-02):
- El area de keys es un widget de clase `Dope_Sheet` (adentro del panel `DopeSheet.1`) con una ventana
  OpenGL nativa incrustada (`Foundry::UI::GLWindow`, por un QWindowContainer). Las teclas reales le
  llegan a esa ventana.
- Nuke manda las teclas al panel donde ENTRO el mouse, no al que tiene el foco: con el foco puesto en
  el Dope Sheet, las teclas igual iban al Node Graph. Por eso se le manda a la ventana un Enter y un
  movimiento falsos (el puntero real no se mueve), despues Ctrl+A y F, y un Leave al final. Sin el
  Enter no anda, aunque sendEvent lo devuelva como no aceptado.
- Anda con los modificadores del atajo todavia apretados: no hace falta esperar a que se suelten.
- Con el panel escondido detras de otra pestana no hace nada. Para saberlo se mira el widget: la
  ventana GL sigue diciendo que esta visible.
"""

try:  # PySide6 (Nuke 16+)
    from PySide6 import QtCore, QtGui, QtWidgets
except ImportError:  # PySide2 (Nuke 15)
    from PySide2 import QtCore, QtGui, QtWidgets

Qt = QtCore.Qt


def _dope_sheets():
    """Los widgets `Dope_Sheet` a la vista (puede haber mas de uno si hay paneles flotantes)."""
    return [w for w in QtWidgets.QApplication.allWidgets()
            if w.metaObject().className() == "Dope_Sheet" and w.isVisible()]


def _pick(dope_sheets):
    """El que tiene el puntero encima; si ninguno, el primero."""
    pos = QtGui.QCursor.pos()
    for w in dope_sheets:
        if w.rect().contains(w.mapFromGlobal(pos)):
            return w
    return dope_sheets[0] if dope_sheets else None


def _gl_window(dope):
    """La ventana GL incrustada: la que ocupa el mismo lugar y tamano que el contenedor del widget."""
    container = None
    for child in dope.findChildren(QtWidgets.QWidget):
        if child.metaObject().className() == "QWindowContainer" and child.isVisible():
            container = child
            break
    if container is None:
        return None
    origin = container.mapToGlobal(QtCore.QPoint(0, 0))
    # Con escalado fraccionario la ventana nativa redondea distinto que el widget: un par de pixeles.
    near = lambda a, b: abs(a - b) <= 2
    for win in QtGui.QGuiApplication.allWindows():
        if win.isTopLevel() or not win.isVisible():
            continue
        pos = win.mapToGlobal(QtCore.QPoint(0, 0))
        if (near(pos.x(), origin.x()) and near(pos.y(), origin.y()) and near(win.width(), container.width())
                and near(win.height(), container.height())):
            return win
    return None


def frame_dope_sheet():
    dope = _pick(_dope_sheets())
    if dope is None:
        return  # Dope Sheet cerrado o escondido: no se hace nada (Lega, 2026-10-02)
    win = _gl_window(dope)
    if win is None:
        print("LGA_FrameDopeSheet: no se encontro la ventana del Dope Sheet")
        return
    local = QtCore.QPointF(win.width() / 2.0, win.height() / 2.0)
    glob = QtCore.QPointF(win.mapToGlobal(local.toPoint()))
    send = QtCore.QCoreApplication.sendEvent
    send(win, QtGui.QEnterEvent(local, local, glob))
    send(win, QtGui.QMouseEvent(QtCore.QEvent.MouseMove, local, glob, Qt.NoButton, Qt.NoButton, Qt.NoModifier))
    for key, mods, text in ((Qt.Key_A, Qt.ControlModifier, "\x01"), (Qt.Key_F, Qt.NoModifier, "f")):
        for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
            send(win, QtGui.QKeyEvent(kind, key, mods, text))
    send(win, QtCore.QEvent(QtCore.QEvent.Leave))
