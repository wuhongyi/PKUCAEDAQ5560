#!/usr/bin/env python3
"""Detect stale GUI register definitions when working in the full FPGA repository."""
from pathlib import Path
import sys

gui = Path(__file__).resolve().parents[1]
firmware = gui.parents[1] / "R5560/software/r5560_registers.h"
if not firmware.exists():
    sys.exit("Full FPGA repository not found; compare r5560_registers.h with the target firmware before deployment.")
if gui.joinpath("r5560_registers.h").read_text() != firmware.read_text():
    sys.exit("GUI and FPGA register definitions differ. Regenerate the FPGA register map and update the GUI copy.")
print("GUI register definitions match the FPGA-generated header.")
