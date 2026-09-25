#ifndef MIGHTYTOOLS_COMAPARTMENT_H
#define MIGHTYTOOLS_COMAPARTMENT_H

// COM en modo apartment, una sola vez por proceso, en main() y antes de construir cualquier modulo
// (plan 4.4): Shell COM y UI Automation (Folder Switch) lo necesitan en el hilo de la UI. En macOS
// no hace nada. Vive lo que vive el objeto.
class ComApartment
{
public:
    ComApartment();
    ~ComApartment();
    ComApartment(const ComApartment &) = delete;
    ComApartment &operator=(const ComApartment &) = delete;

    bool initialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};

#endif // MIGHTYTOOLS_COMAPARTMENT_H
