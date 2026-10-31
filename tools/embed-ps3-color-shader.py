#!/usr/bin/env python3
"""Rebuild the colour-effect and ripple programs with PSL1GHT cgcomp and NVIDIA Cg 3.1.

Set PATH for cgcomp and LD_LIBRARY_PATH for libCg.so. Release builds use the
checked-in C array and do not require shader compilation tools.
"""
from pathlib import Path
import subprocess

folder = Path(__file__).resolve().parent.parent / "src" / "ps3"
for name, markers in [("color_effect",[b"uEffect\0",b"uPaletteV\0"]),
                      ("ripple",[b"uRippleCenter\0",b"uRippleRadius\0"])]:
    binary = folder / (name + ".fpo")
    subprocess.run(["cgcomp", "-f", str(folder / (name + ".fcg")), str(binary)], check=True)
    blob = binary.read_bytes()
    assert blob[:2] == b"FP" and all(marker in blob for marker in markers)
    rows = ["  " + ", ".join(f"0x{byte:02x}" for byte in blob[i:i + 12]) for i in range(0, len(blob), 12)]
    (folder / (name + "_fpo.c")).write_text(
        f"/* Generated from {name}.fcg with PSL1GHT cgcomp and NVIDIA Cg 3.1. */\n"
        f"unsigned char {name}_fpo[]={{\n" + ",\n".join(rows) +
        f"\n}};\nunsigned int {name}_fpo_len={len(blob)};\n", newline="\n")
    print(f"Embedded {name}: {len(blob)} bytes.")
