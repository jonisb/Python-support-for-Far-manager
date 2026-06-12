"""
py_handler.far.py - Registers the 'py:' prefix and routes to RunScript.
"""

import ctypes
import os

import far_api


class Plugin:
    title = "PythonFar Command Handler"
    description = "Routes py: commands to Python scripts"
    author = "PythonFar"
    version = (1, 0, 0, 0)

    def get_plugin_info(self):
        return {
            "title": self.title,
            "description": self.description,
            "author": self.author,
            "version": ".".join(str(v) for v in self.version),
            "flags": far_api.PF_FULLCMDLINE,
            "command_prefix": "py",
        }

    def set_startup_info(self, psi_ptr):
        pass

    def _resolve_script(self, name):
        if os.path.isabs(name):
            return name
        if not name.endswith(".py"):
            name += ".py"
        # Try relative to this plugin's directory first
        here = os.path.dirname(__file__)
        candidate = os.path.join(here, name)
        if os.path.isfile(candidate):
            return candidate
        # Try CWD
        candidate = os.path.join(os.getcwd(), name)
        if os.path.isfile(candidate):
            return candidate
        # Try adapter python dir (sys.path[0])
        try:
            import sys
            for p in sys.path:
                candidate = os.path.join(p, name)
                if os.path.isfile(candidate):
                    return candidate
        except Exception:
            pass
        return None

    def OpenW(self, info):
        if info.OpenFrom != far_api.OPEN_COMMANDLINE:
            return None
        if not info.Data:
            return None

        cmd_info = ctypes.cast(
            info.Data, ctypes.POINTER(far_api.OpenCommandLineInfo)
        ).contents
        raw = cmd_info.CommandLine or ""
        colon = raw.find(":")
        if colon < 0:
            return None

        script_name = raw[colon + 1:].strip()
        if not script_name or ".." in script_name:
            return None

        script_path = self._resolve_script(script_name)
        if not script_path:
            return None

        try:
            adapter = ctypes.CDLL("PythonFar.adapter.dll")
            run = adapter.PythonFar_RunScript
            run.restype = ctypes.c_bool
            run(ctypes.c_wchar_p(script_path))
            import datetime
            with open(os.path.join(os.environ["TEMP"], "pythonfar_handler.log"), "a") as f:
                f.write(f"{datetime.datetime.now()}: ran {script_path}\n")
        except Exception:
            import traceback
            with open(os.path.join(os.environ["TEMP"], "pythonfar_handler.log"), "a") as f:
                f.write(traceback.format_exc())
