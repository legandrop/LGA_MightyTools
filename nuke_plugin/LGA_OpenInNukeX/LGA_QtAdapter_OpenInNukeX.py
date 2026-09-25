"""
Compatibilidad Qt para LGA_OpenInNukeX - Nuke 15/16.
"""

from typing import Optional

try:  # PySide6 primero (Nuke 16)
    from PySide6 import QtWidgets, QtGui, QtCore
    from PySide6.QtGui import QAction, QGuiApplication
    from PySide6.QtCore import Qt, QTimer
    from PySide6.QtWidgets import QApplication

    PYSIDE_VER = 6
except ImportError:  # PySide2 (Nuke 15)
    from PySide2 import QtWidgets, QtGui, QtCore
    from PySide2.QtCore import Qt, QTimer

    try:
        from PySide2.QtGui import QAction  # Qt5 a veces lo expone aqui
    except ImportError:
        from PySide2.QtWidgets import QAction  # fallback QtWidgets
    from PySide2.QtGui import QGuiApplication
    from PySide2.QtWidgets import QApplication

    PYSIDE_VER = 2


def horizontal_advance(metrics: QtGui.QFontMetrics, text: str) -> int:
    """
    Ancho de texto compatible (Qt6 usa horizontalAdvance).
    """
    if hasattr(metrics, "horizontalAdvance"):
        return metrics.horizontalAdvance(text)
    return metrics.width(text)


def primary_screen_geometry(pos: Optional[QtCore.QPoint] = None) -> QtCore.QRect:
    """
    Geometry del monitor principal o del monitor bajo pos.
    """
    app = QtWidgets.QApplication.instance()
    if app is None:
        return QtCore.QRect(0, 0, 1920, 1080)

    screen = None
    if pos is not None and hasattr(QGuiApplication, "screenAt"):
        screen = QGuiApplication.screenAt(pos)
    if screen is None:
        screen = QGuiApplication.primaryScreen()
    geo = screen.availableGeometry() if screen else QtCore.QRect(0, 0, 1920, 1080)
    return geo


def set_layout_margin(layout: QtWidgets.QLayout, margin: int) -> None:
    """
    Establecer margen de layout compatible Qt5/Qt6.
    En Qt6 usa setContentsMargins, en Qt5 usa setMargin.
    """
    if hasattr(layout, "setContentsMargins"):
        layout.setContentsMargins(margin, margin, margin, margin)
    else:
        layout.setMargin(margin)


def get_screen_geometry() -> QtCore.QRect:
    """Get screen geometry with Qt6/Qt5 compatibility"""
    app = QApplication.instance()
    if app is None:
        return QtCore.QRect(0, 0, 1920, 1080)

    screen = app.primaryScreen() if app else QGuiApplication.primaryScreen()
    return screen.availableGeometry() if screen else QtCore.QRect(0, 0, 1920, 1080)


def get_screen_at(pos: QtCore.QPoint) -> QtCore.QRect:
    """Get screen at position with Qt6/Qt5 compatibility"""
    if hasattr(QGuiApplication, "screenAt"):
        screen = QGuiApplication.screenAt(pos)
        return (
            screen.geometry() if screen else QGuiApplication.primaryScreen().geometry()
        )
    else:
        # Fallback for older versions - approximate with primary screen
        return QGuiApplication.primaryScreen().geometry()


__all__ = [
    "QtWidgets",
    "QtGui",
    "QtCore",
    "QAction",
    "QGuiApplication",
    "Qt",
    "QApplication",
    "QTimer",
    "PYSIDE_VER",
    "horizontal_advance",
    "primary_screen_geometry",
    "set_layout_margin",
    "get_screen_geometry",
    "get_screen_at",
]
