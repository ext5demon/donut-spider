#!/usr/bin/env python3
"""Rebuild color_effect_fpo.c with PSL1GHT cgcomp and NVIDIA Cg 3.1.

Set PATH for cgcomp and LD_LIBRARY_PATH for libCg.so. Release builds use the
checked-in C array and do not require shader compilation tools.
"""
from pathlib import Path
import subprocess

folder = Path(__file__).resolve().parent.parent / "src" / "ps3"
binary = folder / "color_effect.fpo"
subprocess.run(["cgcomp", "-f", str(folder / "color_effect.fcg"), str(binary)], check=True)
blob = binary.read_bytes()
assert blob[:2] == b"FP" and b"uEffect\0" in blob and b"uPaletteV\0" in blob
rows = ["  " + ", ".join(f"0x{byte:02x}" for byte in blob[i:i + 12]) for i in range(0, len(blob), 12)]
(folder / "color_effect_fpo.c").write_text(
    "/* Generated from color_effect.fcg with PSL1GHT cgcomp and NVIDIA Cg 3.1. */\n"
    "unsigned char color_effect_fpo[]={\n" + ",\n".join(rows) +
    f"\n}};\nunsigned int color_effect_fpo_len={len(blob)};\n", newline="\n")
print(f"Embedded {len(blob)} bytes.")
