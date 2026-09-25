#include "platform/ComApartment.h"

#include <objbase.h>

ComApartment::ComApartment()
{
    // S_FALSE (ya estaba inicializado en este hilo con el mismo modo) tambien cuenta y pide su
    // CoUninitialize. RPC_E_CHANGED_MODE: otro lo inicializo en otro modo; no se toca.
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_initialized = SUCCEEDED(result);
}

ComApartment::~ComApartment()
{
    if (m_initialized) {
        CoUninitialize();
    }
}
