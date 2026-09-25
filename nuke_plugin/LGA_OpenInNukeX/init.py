"""
______________________________________________________________________________________

  LGA_OpenInNukeX v1.84 | Lega
  Initializes a server in NukeX to handle external commands via port 54325

  v1.84 - paste_clipboard solo confirma exito si creo y conecto LGA_ContactSheet
  v1.70 - paste_clipboard: conecta Reads a LGA_ContactSheet y Viewer al resultado
  v1.69 - Agrega comando paste_clipboard para pegar nodos sin cerrar el proyecto
  v1.68 - logging de la apertura del script
______________________________________________________________________________________

"""

import nuke
import socket
import threading
import time
import logging
import queue
from logging.handlers import QueueHandler, QueueListener
import os
import sys
import traceback
import inspect
import importlib.util

# ---------------------------------------------------------------------------
# Logging flags  (alineados al patron de LGA_ToolPack / Python-Startup)
# ---------------------------------------------------------------------------
DEBUG = True
DEBUG_CONSOLE = False
DEBUG_LOG = True
DETAILED_OPEN_LOGGING = False

# ---------------------------------------------------------------------------
# Logging internals
# ---------------------------------------------------------------------------
current_conn = None
script_start_time = None
debug_log_listener = None
_debug_file_handler = None
_logging_lock = threading.Lock()
force_sync_logging = False

TOOL_NAME = "OpenInNukeX"
LOG_FILENAME = f"debugPy_{TOOL_NAME}.log"


class RelativeTimeFormatter(logging.Formatter):
    def format(self, record):
        global script_start_time
        if script_start_time is None:
            script_start_time = record.created
        relative_time = record.created - script_start_time
        record.relative_time = f"{relative_time:.3f}s"
        return super().format(record)


def _is_valid_plugin_root(path):
    if not path:
        return False

    try:
        root = os.path.abspath(path)
        if not os.path.isdir(root):
            return False

        expected_files = [
            os.path.join(root, "init.py"),
            os.path.join(root, "LGA_QtAdapter_OpenInNukeX.py"),
        ]
        return all(os.path.exists(candidate) for candidate in expected_files)
    except Exception:
        return False


def _get_plugin_root():
    candidates = []

    try:
        file_value = globals().get("__file__")
        if file_value:
            candidates.append(os.path.dirname(os.path.abspath(file_value)))
    except Exception:
        pass

    try:
        source_file = inspect.getsourcefile(_get_plugin_root)
        if source_file:
            candidates.append(os.path.dirname(os.path.abspath(source_file)))
    except Exception:
        pass

    try:
        spec = importlib.util.find_spec("LGA_QtAdapter_OpenInNukeX")
        if spec and spec.origin:
            candidates.append(os.path.dirname(os.path.abspath(spec.origin)))
    except Exception:
        pass

    try:
        for plugin_path in nuke.pluginPath():
            if os.path.basename(os.path.normpath(plugin_path)) == "LGA_OpenInNukeX":
                candidates.append(plugin_path)
    except Exception:
        pass

    for candidate in candidates:
        if _is_valid_plugin_root(candidate):
            return os.path.abspath(candidate)

    return os.path.dirname(os.path.abspath(__file__))


def _get_log_file_path():
    return os.path.join(_get_plugin_root(), "logs", LOG_FILENAME)


def _emergency_trace(message):
    """Escritura síncrona mínima para diagnosticar bloqueos o crashes."""
    if not DETAILED_OPEN_LOGGING:
        return

    try:
        trace_path = os.path.join(_get_plugin_root(), "logs", "emergency_trace.log")
        os.makedirs(os.path.dirname(trace_path), exist_ok=True)
        with open(trace_path, "a", encoding="utf-8") as handle:
            handle.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} | {message}\n")
            handle.flush()
            os.fsync(handle.fileno())
    except Exception:
        pass


def _get_relative_time_string():
    global script_start_time
    if script_start_time is None:
        script_start_time = time.time()
    return f"{time.time() - script_start_time:.3f}s"


def _write_log_line_sync(message, caller_frame):
    """Escribe una línea directamente al log principal con flush+fsync."""
    try:
        log_path = _get_log_file_path()
        os.makedirs(os.path.dirname(log_path), exist_ok=True)

        module_name = os.path.splitext(
            os.path.basename(caller_frame.f_code.co_filename)
        )[0]
        func_name = caller_frame.f_code.co_name
        line = f"[{_get_relative_time_string()}] [{module_name}::{func_name}] {message}\n"

        with open(log_path, "a", encoding="utf-8") as handle:
            handle.write(line)
            handle.flush()
            os.fsync(handle.fileno())
    except Exception:
        pass


def _critical_log(message):
    """Log síncrono para la ruta crítica de apertura de scripts."""
    if not DETAILED_OPEN_LOGGING:
        return

    _write_log_line_sync(message, sys._getframe(1))


def _reset_log():
    """Detiene el listener, vacía el log, y reinicia todo el pipeline de logging."""
    global script_start_time, debug_log_listener, _debug_file_handler, debug_logger
    script_start_time = None

    with _logging_lock:
        if debug_log_listener:
            try:
                debug_log_listener.stop()
            except Exception:
                pass
            debug_log_listener = None

        if _debug_file_handler:
            try:
                _debug_file_handler.close()
            except Exception:
                pass
            _debug_file_handler = None

        logger = logging.getLogger(f"{TOOL_NAME.lower()}_logger")
        if logger.handlers:
            logger.handlers.clear()

        log_file_path = _get_log_file_path()
        try:
            with open(log_file_path, "w", encoding="utf-8") as f:
                f.write(f"Fecha: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        except Exception:
            pass

        _debug_file_handler = logging.FileHandler(log_file_path, encoding="utf-8")
        _debug_file_handler.setLevel(logging.DEBUG)
        _debug_file_handler.setFormatter(
            RelativeTimeFormatter(
                "[%(relative_time)s] [%(module)s::%(funcName)s] %(message)s"
            )
        )

        log_queue = queue.Queue()
        queue_handler = QueueHandler(log_queue)
        queue_handler.setLevel(logging.DEBUG)
        logger.addHandler(queue_handler)

        debug_log_listener = QueueListener(
            log_queue, _debug_file_handler, respect_handler_level=True
        )
        debug_log_listener.daemon = True
        debug_log_listener.start()

        debug_logger = logger


def setup_debug_logging():
    """Configura logging asincrono a archivo con encabezado de fecha/hora."""
    global debug_log_listener, _debug_file_handler

    with _logging_lock:
        log_file_path = _get_log_file_path()
        os.makedirs(os.path.dirname(log_file_path), exist_ok=True)

        try:
            with open(log_file_path, "w", encoding="utf-8") as f:
                f.write(f"Fecha: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        except Exception as e:
            _emergency_trace(f"setup_debug_logging initialization failed: {e}")
            print(f"Warning: No se pudo inicializar el archivo de log: {e}")

        logger = logging.getLogger(f"{TOOL_NAME.lower()}_logger")
        logger.setLevel(logging.DEBUG)
        logger.propagate = False

        if logger.handlers:
            logger.handlers.clear()

        _debug_file_handler = logging.FileHandler(log_file_path, encoding="utf-8")
        _debug_file_handler.setLevel(logging.DEBUG)
        _debug_file_handler.setFormatter(
            RelativeTimeFormatter(
                "[%(relative_time)s] [%(module)s::%(funcName)s] %(message)s"
            )
        )

        log_queue = queue.Queue()
        queue_handler = QueueHandler(log_queue)
        queue_handler.setLevel(logging.DEBUG)
        logger.addHandler(queue_handler)

        if debug_log_listener:
            try:
                debug_log_listener.stop()
            except Exception:
                pass

        debug_log_listener = QueueListener(
            log_queue, _debug_file_handler, respect_handler_level=True
        )
        debug_log_listener.daemon = True
        debug_log_listener.start()

        return logger


debug_logger = setup_debug_logging()


def debug_print(*message, level="info"):
    """Logging con soporte de niveles, archivo y consola opcionales."""
    global script_start_time

    if not DEBUG:
        return

    msg = " ".join(str(arg) for arg in message)

    if DEBUG_LOG:
        if DETAILED_OPEN_LOGGING and force_sync_logging:
            _write_log_line_sync(msg, sys._getframe(1))
        else:
            if script_start_time is None:
                script_start_time = time.time()
            log_method = getattr(debug_logger, level, debug_logger.info)
            try:
                log_method(msg, stacklevel=2)
            except TypeError:
                log_method(msg)

    if DEBUG_CONSOLE:
        print(f"[{_get_relative_time_string()}] {msg}")


def _collect_nuke_environment():
    """Captura un snapshot del entorno de Nuke para diagnosticar crashes."""
    info = []
    try:
        info.append(f"Nuke version: {nuke.env.get('NukeVersionString', '?')}")
        info.append(
            f"NukeX: {nuke.env.get('nukex', '?')}, Studio: {nuke.env.get('studio', '?')}"
        )
        info.append(f"Python: {sys.version}")
        info.append(f"Platform: {sys.platform}")
        info.append(f"PID: {os.getpid()}")
        info.append(f"Threads activos: {threading.active_count()}")
        info.append(f"Thread actual: {threading.current_thread().name}")
    except Exception as e:
        info.append(f"Error capturando entorno base: {e}")

    try:
        root = nuke.root()
        info.append(f"Script actual: {root.name()}")
        info.append(f"Frame range: {root.firstFrame()}-{root.lastFrame()}")
        info.append(f"Proyecto modificado: {root.modified()}")
    except Exception as e:
        info.append(f"Error capturando info de root: {e}")

    try:
        all_nodes = nuke.allNodes()
        info.append(f"Total nodos: {len(all_nodes)}")
        node_types = {}
        for n in all_nodes:
            cls = n.Class()
            node_types[cls] = node_types.get(cls, 0) + 1
        top5 = sorted(node_types.items(), key=lambda x: -x[1])[:5]
        info.append(f"Top nodos: {top5}")
    except Exception as e:
        info.append(f"Error capturando nodos: {e}")

    try:
        import psutil

        proc = psutil.Process(os.getpid())
        mem = proc.memory_info()
        info.append(f"Memoria RSS: {mem.rss / (1024*1024):.1f} MB")
        info.append(f"Memoria VMS: {mem.vms / (1024*1024):.1f} MB")
        info.append(f"CPU%: {proc.cpu_percent(interval=0)}")
    except ImportError:
        pass
    except Exception as e:
        info.append(f"Error capturando memoria: {e}")

    return info


def _collect_nuke_callbacks():
    """Captura callbacks registrados en Nuke que podrían ejecutarse post-apertura."""
    info = []
    try:
        callback_types = [
            "onScriptLoad", "onScriptSave", "onScriptClose",
            "onCreate", "onDestroy", "onUserCreate",
            "knobChanged", "updateUI", "autolabel",
        ]
        for cb_name in callback_types:
            try:
                cbs = nuke.callbacks.get(cb_name, [])
                if cbs:
                    for cb_func, cb_args, cb_kwargs, cb_node_class in cbs:
                        func_name = getattr(cb_func, "__name__", str(cb_func))
                        func_module = getattr(cb_func, "__module__", "?")
                        info.append(
                            f"{cb_name}: {func_module}.{func_name} "
                            f"(nodeClass={cb_node_class})"
                        )
            except Exception as e:
                info.append(f"{cb_name}: error enumerando - {e}")
    except AttributeError:
        try:
            for attr in ["onScriptLoads", "onCreateNodes", "onDestroys",
                         "onScriptCloses", "onScriptSaves", "knobChangeds",
                         "updateUIs", "autolabels", "onUserCreates"]:
                cb_list = getattr(nuke, attr, None)
                if cb_list:
                    info.append(f"{attr}: {len(cb_list)} callback(s) registrado(s)")
                    for cb_entry in cb_list:
                        cb_func = cb_entry[0] if isinstance(cb_entry, tuple) else cb_entry
                        func_name = getattr(cb_func, "__name__", str(cb_func))
                        func_module = getattr(cb_func, "__module__", "?")
                        node_class = cb_entry[2] if isinstance(cb_entry, tuple) and len(cb_entry) > 2 else "*"
                        info.append(
                            f"  -> {func_module}.{func_name} (nodeClass={node_class})"
                        )
        except Exception as e:
            info.append(f"Error enumerando callbacks (fallback): {e}")
    except Exception as e:
        info.append(f"Error capturando callbacks: {e}")

    if not info:
        info.append("No se detectaron callbacks registrados (o API no accesible)")
    return info


def _log_file_info(filepath):
    """Loguea metadata del archivo que se va a abrir."""
    info = []
    try:
        if os.path.exists(filepath):
            stat = os.stat(filepath)
            info.append(f"Archivo existe: True")
            info.append(f"Tamaño: {stat.st_size / 1024:.1f} KB")
            info.append(
                f"Última modificación: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(stat.st_mtime))}"
            )
            info.append(f"Path absoluto: {os.path.abspath(filepath)}")
            info.append(f"Path normalizado: {os.path.normpath(filepath)}")
            info.append(f"Es symlink: {os.path.islink(filepath)}")
        else:
            info.append(f"Archivo existe: FALSE - {filepath}")
    except Exception as e:
        info.append(f"Error capturando info del archivo: {e}")
    return info


if nuke.env["nukex"] and not nuke.env["studio"]:
    debug_print("=== OpenInNukeX: DETECTADO NUKEX - INICIANDO SERVIDOR ===")
    debug_print(f"Versión de Nuke: {nuke.env['NukeVersionString']}")
    debug_print(f"NukeX: {nuke.env['nukex']}, Studio: {nuke.env['studio']}")
    debug_print(f"Python: {sys.version}")
    debug_print(f"PID: {os.getpid()}")
    debug_print(f"Log file: {_get_log_file_path()}")

    def handle_client(conn):
        debug_print("=== NUEVA CONEXIÓN RECIBIDA ===")
        debug_print(f"Thread: {threading.current_thread().name}")
        with conn:
            try:
                debug_print("Recibiendo datos del cliente...")
                data = conn.recv(1024).decode()
                debug_print(f"Datos recibidos: '{data}'")

                if data:
                    if data == "ping":
                        debug_print("Comando PING recibido")
                        if not nuke.env["studio"]:
                            debug_print("Respondiendo PONG (NukeX)")
                            conn.sendall("pong".encode())
                        else:
                            debug_print("PING desde NukeStudio - no respondiendo")
                            print("Received ping from NukeStudio, not responding.")
                    elif data == "paste_clipboard":
                        debug_print("Comando PASTE_CLIPBOARD recibido")
                        _flush_log()

                        def _paste_clipboard_in_main_thread():
                            return paste_clipboard_with_logging()

                        result = nuke.executeInMainThreadWithResult(
                            _paste_clipboard_in_main_thread
                        )
                        debug_print(
                            f"Resultado paste_clipboard en main thread: {result}"
                        )
                        response = "Clipboard pasted successfully\n"
                        conn.sendall(response.encode())
                        debug_print("Respuesta enviada exitosamente")
                    else:
                        debug_print(f"Procesando comando: '{data}'")
                        try:
                            command, filepath = data.split("||")
                            debug_print(
                                f"Comando parseado: '{command}', Archivo: '{filepath}'"
                            )

                            if command == "run_script":
                                debug_print(
                                    f"Ejecutando script en main thread: {filepath}"
                                )
                                _critical_log(
                                    f"[CRITICAL] before executeInMainThreadWithResult filepath='{filepath}'"
                                )
                                _emergency_trace(
                                    f"handle_client before executeInMainThreadWithResult: {filepath}"
                                )

                                def _run_script_in_main_thread():
                                    _critical_log(
                                        f"[CRITICAL] main thread lambda entered filepath='{filepath}'"
                                    )
                                    _emergency_trace(
                                        f"main thread lambda entered: {filepath}"
                                    )
                                    return run_script_with_logging(filepath)

                                result = nuke.executeInMainThreadWithResult(
                                    _run_script_in_main_thread
                                )
                                _emergency_trace(
                                    f"executeInMainThreadWithResult returned: {result}"
                                )
                                _critical_log(
                                    f"[CRITICAL] executeInMainThreadWithResult returned result='{result}'"
                                )
                                debug_print(
                                    f"Resultado ejecución en main thread: {result}"
                                )
                                response = "Script executed successfully\n"
                            else:
                                debug_print(
                                    f"Comando desconocido: '{command}'", level="warning"
                                )
                                response = f"Received command: {data}\n"
                        except ValueError as parse_error:
                            debug_print(
                                f"ERROR parseando comando: {parse_error}", level="error"
                            )
                            response = f"Error parsing command: {data}\n"

                        debug_print("Enviando respuesta al cliente...")
                        conn.sendall(response.encode())
                        debug_print("Respuesta enviada exitosamente")
            except Exception as e:
                debug_print(f"ERROR CRÍTICO en handle_client: {e}", level="error")
                debug_print(
                    f"Traceback completo: {traceback.format_exc()}", level="error"
                )
                try:
                    response = f"Error: {str(e)}\n"
                    conn.sendall(response.encode())
                except Exception as send_error:
                    debug_print(
                        f"ERROR enviando respuesta de error: {send_error}",
                        level="error",
                    )
            finally:
                debug_print("Cerrando conexión...")
                conn.close()
                debug_print("Conexión cerrada")

    def _flush_log():
        """Fuerza escritura a disco esperando que la cola se vacíe y flusheando el FileHandler."""
        try:
            if debug_log_listener and hasattr(debug_log_listener, "queue"):
                deadline = time.time() + 0.5
                while not debug_log_listener.queue.empty() and time.time() < deadline:
                    time.sleep(0.005)
            if _debug_file_handler:
                _debug_file_handler.flush()
                if hasattr(_debug_file_handler, "stream"):
                    os.fsync(_debug_file_handler.stream.fileno())
        except Exception:
            pass

    def paste_clipboard_with_logging():
        """Pega el contenido del clipboard en el proyecto actual sin cerrar ni abrir scripts."""
        debug_print("=== INICIANDO PASTE DESDE CLIPBOARD ===")
        debug_print("--- SNAPSHOT ENTORNO PRE-PASTE ---")
        for line in _collect_nuke_environment():
            debug_print(f"  {line}")

        try:
            # Deselect all so selectedNodes() post-paste da exactamente los nodos nuevos
            for n in nuke.allNodes():
                n.setSelected(False)

            before_nodes = len(nuke.allNodes())
            debug_print(f"Nodos antes del paste: {before_nodes}")
            _flush_log()

            pasted = nuke.nodePaste("%clipboard%")
            after_nodes = len(nuke.allNodes())
            debug_print(f"Resultado nodePaste: {pasted}")
            debug_print(f"Nodos despues del paste: {after_nodes}")
            debug_print(f"Nodos agregados estimados: {after_nodes - before_nodes}")

            # Capturar los Read nodes mientras siguen seleccionados
            pasted_nodes = nuke.selectedNodes()
            read_nodes = [n for n in pasted_nodes if n.Class() == "Read"]
            debug_print(f"Read nodes capturados para ContactSheet: {[n.name() for n in read_nodes]}")
            _flush_log()

            # --- Cargar y conectar LGA_ContactSheet ---
            cs_path = os.path.expanduser(
                "~/.nuke/LGA_NodePack/LGizmos/Other/LGA_ContactSheet.nk"
            ).replace("\\", "/")
            debug_print(f"Buscando LGA_ContactSheet en: {cs_path}")

            if not read_nodes:
                raise RuntimeError(
                    "The clipboard did not create Read nodes; the Contact Sheet cannot be built."
                )
            if not os.path.exists(cs_path):
                raise RuntimeError(f"LGA_ContactSheet.nk was not found: {cs_path}")

            # Deselect antes de pegar el template para identificarlo limpio
            for n in nuke.allNodes():
                n.setSelected(False)

            nuke.nodePaste(cs_path)
            cs_pasted = nuke.selectedNodes()
            contact_sheet = next(
                (n for n in cs_pasted if n.Class() == "Group" and "ContactSheet" in n.name()),
                None
            )

            if contact_sheet is None:
                raise RuntimeError(
                    "LGA_ContactSheet.nk did not create the LGA_ContactSheet group."
                )

            debug_print(f"LGA_ContactSheet encontrado: {contact_sheet.name()}")

            # Conectar cada Read al input correspondiente del grupo
            for i, read_node in enumerate(read_nodes):
                contact_sheet.setInput(i, read_node)
                debug_print(f"  Input {i} -> {read_node.name()}")

            # Posicionar el ContactSheet debajo de los Reads
            max_ypos = max(n.ypos() for n in read_nodes)
            avg_xpos = sum(n.xpos() for n in read_nodes) // len(read_nodes)
            contact_sheet.setXYpos(avg_xpos, max_ypos + 200)
            debug_print(f"  Posicion ContactSheet: x={avg_xpos} y={max_ypos + 200}")

            # Conectar el primer Viewer existente al ContactSheet
            viewers = nuke.allNodes("Viewer")
            if viewers:
                viewer = viewers[0]
                viewer.setInput(0, contact_sheet)
                debug_print(f"  Viewer '{viewer.name()}' conectado al ContactSheet")

            _flush_log()
            activate_nuke_window_with_logging()
            debug_print("=== PASTE DESDE CLIPBOARD COMPLETADO ===")
            return True
        except Exception as e:
            debug_print(f"ERROR en paste_clipboard: {e}", level="error")
            debug_print(f"Traceback completo: {traceback.format_exc()}", level="error")
            _flush_log()
            raise

    def run_script_with_logging(filepath):
        global force_sync_logging
        _emergency_trace(f"run_script_with_logging entered: {filepath}")
        force_sync_logging = DETAILED_OPEN_LOGGING
        _reset_log()
        _emergency_trace("run_script_with_logging after _reset_log")
        _critical_log(f"[CRITICAL] run_script_with_logging entered filepath='{filepath}'")
        debug_print(f"=== INICIANDO APERTURA DE SCRIPT ===")
        debug_print(f"Filepath solicitado: {filepath}")

        # --- Snapshot del entorno ANTES de tocar nada ---
        debug_print("--- SNAPSHOT ENTORNO PRE-OPERACIÓN ---")
        for line in _collect_nuke_environment():
            debug_print(f"  {line}")

        debug_print("--- INFO ARCHIVO A ABRIR ---")
        for line in _log_file_info(filepath):
            debug_print(f"  {line}")

        try:
            _emergency_trace("run_script_with_logging before phase 1")
            _critical_log("[CRITICAL] phase 1 start")
            debug_print("--- FASE 1: VERIFICAR ESTADO INICIAL ---")
            initial_modified = nuke.root().modified()
            initial_script = nuke.root().name()
            debug_print(f"Script actual: {initial_script}")
            debug_print(f"Proyecto modificado: {initial_modified}")

            debug_print("--- FASE 2: CERRANDO SCRIPT ACTUAL ---")
            _emergency_trace("before nuke.scriptClose")
            _critical_log("[CRITICAL] before nuke.scriptClose")
            _flush_log()
            nuke.scriptClose()
            _emergency_trace("after nuke.scriptClose")
            _critical_log("[CRITICAL] after nuke.scriptClose")

            after_close_modified = nuke.root().modified()
            after_close_script = nuke.root().name()
            debug_print(
                f"Estado post-cierre: modified={after_close_modified}, script={after_close_script}"
            )

            if nuke.root().modified() == initial_modified and initial_modified:
                debug_print(
                    "CIERRE CANCELADO POR EL USUARIO - proyecto sigue modificado",
                    level="warning",
                )
                return False
            else:
                debug_print("Proyecto cerrado exitosamente")

            debug_print("--- FASE 3: ABRIENDO NUEVO SCRIPT ---")
            debug_print(f"Llamando nuke.scriptOpen('{filepath}')...")
            _emergency_trace(f"before registering load tracker: {filepath}")
            _critical_log(f"[CRITICAL] before registering load trackers filepath='{filepath}'")

            _node_load_counter = [0]
            _script_load_counter = [0]

            _risky_classes = frozenset([
                "Group", "Gizmo", "LiveGroup", "Precomp",
                "DeepExpression", "Expression",
            ])

            def _on_node_created_during_load():
                try:
                    n = nuke.thisNode()
                    _node_load_counter[0] += 1
                    node_name = n.name()
                    node_class = n.Class()
                    _critical_log(
                        f"[LOAD #{_node_load_counter[0]}] {node_class} '{node_name}'"
                    )
                    debug_print(
                        f"  [LOAD #{_node_load_counter[0]}] "
                        f"{node_class} '{node_name}'"
                    )
                    if (
                        _node_load_counter[0] % 50 == 0
                        or node_class in _risky_classes
                    ):
                        _flush_log()
                except Exception as cb_err:
                    debug_print(
                        f"  [LOAD] Error en callback onCreate: {cb_err}",
                        level="warning",
                    )

            def _on_script_loaded():
                try:
                    _script_load_counter[0] += 1
                    _critical_log(
                        f"[SCRIPT LOAD #{_script_load_counter[0]}] root='{nuke.root().name()}'"
                    )
                    debug_print(
                        f"  [SCRIPT LOAD #{_script_load_counter[0]}] "
                        f"root='{nuke.root().name()}'"
                    )
                    _flush_log()
                except Exception as cb_err:
                    debug_print(
                        f"  [SCRIPT LOAD] Error en callback onScriptLoad: {cb_err}",
                        level="warning",
                    )

            if DETAILED_OPEN_LOGGING:
                debug_print("Registrando tracker de carga de nodos...")
                nuke.addOnCreate(_on_node_created_during_load)
                nuke.addOnScriptLoad(_on_script_loaded)
                _emergency_trace("after nuke.addOnCreate")
                _critical_log("[CRITICAL] after registering load trackers")
            _flush_log()

            try:
                _emergency_trace(f"before nuke.scriptOpen: {filepath}")
                _critical_log(f"[CRITICAL] before nuke.scriptOpen filepath='{filepath}'")
                nuke.scriptOpen(filepath)
                _emergency_trace("after nuke.scriptOpen")
                _critical_log("[CRITICAL] after nuke.scriptOpen")
            finally:
                if DETAILED_OPEN_LOGGING:
                    _emergency_trace("entering scriptOpen finally")
                    _critical_log("[CRITICAL] entering scriptOpen finally")
                    nuke.removeOnCreate(_on_node_created_during_load)
                    nuke.removeOnScriptLoad(_on_script_loaded)
                    _emergency_trace(
                        "trackers removed, total nodes: "
                        f"{_node_load_counter[0]}, onScriptLoad: {_script_load_counter[0]}"
                    )
                    _critical_log(
                        "[CRITICAL] trackers removed "
                        f"total_nodes={_node_load_counter[0]} onScriptLoad={_script_load_counter[0]}"
                    )
                    debug_print(
                        "Trackers removidos. "
                        f"Total nodos cargados: {_node_load_counter[0]}. "
                        f"onScriptLoad: {_script_load_counter[0]}"
                    )

            debug_print("nuke.scriptOpen() completado sin excepción")

            debug_print("--- SNAPSHOT ENTORNO POST-APERTURA ---")
            for line in _collect_nuke_environment():
                debug_print(f"  {line}")

            debug_print("--- CALLBACKS REGISTRADOS EN SCRIPT ---")
            for line in _collect_nuke_callbacks():
                debug_print(f"  {line}")
            _flush_log()

            debug_print("--- FASE 4: ACTIVANDO VENTANA ---")
            _flush_log()
            activate_nuke_window_with_logging()

            debug_print("=== SCRIPT ABIERTO EXITOSAMENTE ===")
            _emergency_trace("run_script_with_logging returning True")
            _critical_log("[CRITICAL] run_script_with_logging returning True")
            return True

        except Exception as e:
            _critical_log(f"[CRITICAL] run_script_with_logging exception: {e}")
            debug_print(f"ERROR CRÍTICO en run_script_with_logging: {e}", level="error")
            debug_print(f"Traceback completo: {traceback.format_exc()}", level="error")
            debug_print("--- SNAPSHOT ENTORNO POST-ERROR ---", level="error")
            try:
                for line in _collect_nuke_environment():
                    debug_print(f"  {line}", level="error")
            except Exception:
                debug_print("No se pudo capturar entorno post-error", level="error")
            raise
        finally:
            force_sync_logging = False

    def activate_nuke_window_with_logging():
        try:
            debug_print("Importando QApplication desde QtAdapter...")
            _flush_log()
            from LGA_QtAdapter_OpenInNukeX import QApplication

            debug_print("Import exitoso, obteniendo instancia de QApplication...")
            _flush_log()
            app_instance = QApplication.instance()

            if app_instance:
                debug_print("QApplication encontrada, buscando ventana activa...")
                _flush_log()
                active_window = app_instance.activeWindow()

                if active_window:
                    debug_print(
                        f"Ventana activa encontrada: {active_window.objectName()}, "
                        f"clase: {active_window.__class__.__name__}, "
                        f"visible: {active_window.isVisible()}"
                    )
                    _flush_log()
                    debug_print("Llamando raise_() en ventana activa...")
                    _flush_log()
                    active_window.raise_()
                    debug_print("raise_() completado, llamando activateWindow()...")
                    _flush_log()
                    active_window.activateWindow()
                    debug_print("Ventana activada exitosamente")
                    _flush_log()
                else:
                    debug_print("No hay ventana activa, enumerando topLevelWidgets...")
                    _flush_log()
                    all_widgets = app_instance.topLevelWidgets()
                    debug_print(f"Total topLevelWidgets: {len(all_widgets)}")
                    main_window = None
                    for i, widget in enumerate(all_widgets):
                        w_name = widget.objectName() if hasattr(widget, "objectName") else "?"
                        w_class = widget.__class__.__name__
                        w_visible = widget.isVisible()
                        w_is_window = widget.isWindow()
                        debug_print(
                            f"  Widget[{i}]: name='{w_name}', class={w_class}, "
                            f"isWindow={w_is_window}, visible={w_visible}"
                        )
                        if w_is_window and w_visible and main_window is None:
                            main_window = widget
                            debug_print(f"  -> Seleccionada como ventana principal")

                    _flush_log()
                    if main_window:
                        debug_print(
                            f"Activando ventana principal: {main_window.objectName()}"
                        )
                        _flush_log()
                        debug_print("Llamando raise_()...")
                        _flush_log()
                        main_window.raise_()
                        debug_print("raise_() completado, llamando activateWindow()...")
                        _flush_log()
                        main_window.activateWindow()
                        debug_print("Ventana principal activada exitosamente")
                        _flush_log()
                    else:
                        debug_print(
                            "No se pudo encontrar ventana de Nuke para activar",
                            level="warning",
                        )
                        _flush_log()
            else:
                debug_print(
                    "No se pudo obtener la instancia de QApplication", level="warning"
                )
                _flush_log()

        except Exception as e:
            debug_print(f"ERROR en activación de ventana: {e}", level="error")
            debug_print(
                f"Traceback activación ventana: {traceback.format_exc()}", level="error"
            )
            _flush_log()

    def nuke_server(port=54325):
        debug_print(f"=== INICIANDO SERVIDOR OpenInNukeX EN PUERTO {port} ===")
        host = "localhost"
        try:
            debug_print("Creando socket TCP...")
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            debug_print("Socket creado exitosamente")

            debug_print(f"Intentando bind al puerto {port}...")
            s.bind((host, port))
            debug_print(f"Bind exitoso al puerto {port}")

            debug_print("Poniendo socket en modo escucha...")
            s.listen(1)
            debug_print(f"OpenInNukeX activo y escuchando en puerto {port}")
            print(f"OpenInNukeX active on port {port}")

            while True:
                debug_print("Esperando conexiones entrantes...")
                conn, addr = s.accept()
                debug_print(f"Conexión aceptada desde {addr}")

                debug_print("Creando thread para manejar cliente...")
                client_thread = threading.Thread(target=handle_client, args=(conn,))
                client_thread.daemon = True
                client_thread.start()
                debug_print(f"Thread cliente iniciado: {client_thread.name}")

        except socket.error as e:
            debug_print(f"ERROR de socket en puerto {port}: {e}", level="error")
            print(f"Error al vincular al puerto {port}: {e}")
            s.close()
            return
        except Exception as e:
            debug_print(f"ERROR CRÍTICO al iniciar servidor: {e}", level="error")
            debug_print(f"Traceback servidor: {traceback.format_exc()}", level="error")
            try:
                s.close()
            except Exception:
                pass
            return

    debug_print("Creando thread principal del servidor...")
    thread = threading.Thread(target=nuke_server, args=(54325,))
    thread.daemon = True
    thread.name = "OpenInNukeX_Server"
    debug_print("Iniciando thread del servidor...")
    thread.start()
    debug_print("Thread del servidor iniciado exitosamente")
else:
    debug_print("=== OpenInNukeX: NO ES NUKEX - SERVIDOR INACTIVO ===")
    print("OpenInNukeX inactive. This is not NukeX.")


def cleanup_logging():
    global debug_log_listener
    if not debug_log_listener:
        return
    try:
        debug_log_listener.stop()
    except Exception:
        pass
    finally:
        debug_log_listener = None


try:
    import atexit

    atexit.register(cleanup_logging)
    debug_print("Cleanup de logging registrado con atexit")
except Exception as e:
    debug_print(f"No se pudo registrar cleanup: {e}", level="warning")
