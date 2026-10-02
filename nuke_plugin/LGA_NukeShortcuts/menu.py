# Nuke corre este archivo solo con interfaz, despues del init.py de la carpeta.
try:
    import LGA_KeyframeToggle

    LGA_KeyframeToggle.setup()
except Exception as e:
    print("LGA_KeyframeToggle: no se pudo cargar:", e)
