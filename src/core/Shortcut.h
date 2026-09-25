#ifndef MIGHTYTOOLS_SHORTCUT_H
#define MIGHTYTOOLS_SHORTCUT_H

#include <QString>
#include <QStringList>
#include <Qt>

// Un atajo de teclado: modificadores + una tecla, en la semantica de Qt.
//
// Ojo con macOS: Qt llama `ControlModifier` a la tecla Command y `MetaModifier` a la tecla Control.
// Es justo el mapeo que hace Nuke (Ctrl en Windows = Cmd en mac), asi que "Ctrl+Shift+D" guardado
// en el .ini significa Ctrl+Shift+D en Windows y Cmd+Shift+D en mac sin traducir nada.
struct Shortcut
{
    Qt::KeyboardModifiers modifiers;
    int key = 0; // Qt::Key

    bool isValid() const { return key != 0; }
    bool operator==(const Shortcut &other) const { return modifiers == other.modifiers && key == other.key; }
    bool operator!=(const Shortcut &other) const { return !(*this == other); }

    // Texto portable para el .ini ("Ctrl+Shift+D"). Igual en las dos plataformas.
    QString toPortableString() const;
    static Shortcut fromPortableString(const QString &text);

    // Una caja por tecla, como se dibujan en la ventana: Windows "Ctrl", "Shift", "D"; mac los
    // glifos en el orden de Apple (⌃ ⌥ ⇧ ⌘) y la tecla.
    QStringList displayKeys() const;
    // Una linea para textos corridos: "Ctrl+Shift+D" en Windows, "⌘⇧D" en mac.
    QString displayText() const;

    // Nombre de una tecla sola (sin modificadores), para las cajas.
    static QString keyName(int key);
    // True si la tecla se puede usar como atajo global en las dos plataformas: letras, digitos y
    // F1-F12. Lo demas depende de la distribucion del teclado y no se ofrece.
    static bool isSupportedKey(int key);
    // True si `key` es una tecla modificadora sola (Ctrl, Shift, Alt, Meta...).
    static bool isModifierKey(int key);

    // Los de fabrica: los mismos de la version AutoHotkey.
    static Shortcut defaultAddKeyframe();
    static Shortcut defaultFrameDopeSheet();
};

#endif // MIGHTYTOOLS_SHORTCUT_H
