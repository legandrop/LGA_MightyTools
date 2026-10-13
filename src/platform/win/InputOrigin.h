#ifndef MIGHTYTOOLS_INPUTORIGIN_H
#define MIGHTYTOOLS_INPUTORIGIN_H

#include <windows.h>

// Distinguir nuestra inyeccion de las macros externas sin ignorar todo LLKHF_INJECTED.
inline constexpr ULONG_PTR kMightyToolsInputTag = 0x4c47414d;

#endif
