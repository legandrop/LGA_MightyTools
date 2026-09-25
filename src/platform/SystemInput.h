#ifndef MIGHTYTOOLS_SYSTEMINPUT_H
#define MIGHTYTOOLS_SYSTEMINPUT_H

// Consultas sueltas al sistema que no son de una clase: estado fisico del mouse y de Esc (para el
// calibrador, que espera un click sobre OTRA app sin capturarlo) y el permiso de Accesibilidad de
// macOS. Una implementacion por plataforma (platform/win/SystemInputWin.cpp,
// platform/mac/SystemInputMac.cpp).
namespace SystemInput {

// El boton principal del mouse esta apretado ahora (respeta los botones invertidos de Windows).
bool primaryButtonDown();
// La tecla Esc esta apretada ahora.
bool escapeDown();

// macOS: si la app tiene el permiso de Accesibilidad, que CGEventPost necesita para mandar clicks
// y teclas a otra app. `prompt` = true le pide a macOS que muestre su cartel para darlo.
// Windows: siempre true.
bool accessibilityTrusted(bool prompt);
// Si esta plataforma pide ese permiso (para mostrar u ocultar la fila en la ventana).
bool needsAccessibilityPermission();

} // namespace SystemInput

#endif // MIGHTYTOOLS_SYSTEMINPUT_H
