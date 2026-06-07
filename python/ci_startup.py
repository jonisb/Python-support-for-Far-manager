"""ci_startup.py - Smoke test: write log and exit Far."""

import ctypes
import datetime
import os
import traceback

LOG_PATH = os.path.join(os.environ.get("TEMP", r"C:\temp"), "pythonfar_ci.log")

try:
    with open(LOG_PATH, "w", encoding="utf-8") as f:
        f.write(f"PythonFar CI startup OK\n")
        f.write(f"Time: {datetime.datetime.now()}\n")
        f.write(f"CWD: {os.getcwd()}\n")

    LOADER_GUID = ctypes.c_char_p(
        bytes.fromhex("82ea0e04f31cc1438caaa8125990b0c8")
    )
    adapter = ctypes.CDLL("PythonFar.adapter.dll")
    adv_control = adapter.PythonFar_AdvControl
    adv_control.restype = ctypes.c_int64
    adv_control(LOADER_GUID, 23, 0, None)  # ACTL_QUIT=23

except Exception:
    with open(LOG_PATH, "w", encoding="utf-8") as f:
        f.write(f"PythonFar CI startup FAILED\n")
        f.write(traceback.format_exc())
