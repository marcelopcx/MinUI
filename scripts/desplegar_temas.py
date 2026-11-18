#!/usr/bin/env python3
"""Atajo desde minui/: delega en instalador/desplegar_temas.py."""
import runpy
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent.parent / "instalador" / "desplegar_temas.py"
if not SCRIPT.is_file():
    raise SystemExit(f"No encontrado: {SCRIPT}")
sys.argv[0] = str(SCRIPT)
runpy.run_path(str(SCRIPT), run_name="__main__")
