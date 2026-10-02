# Nuke corre este archivo solo con interfaz, despues del init.py de la carpeta.
try:
    import LGA_NukeShortcuts

    LGA_NukeShortcuts.setup()
except Exception as e:
    print("LGA_NukeShortcuts: no se pudo cargar:", e)
