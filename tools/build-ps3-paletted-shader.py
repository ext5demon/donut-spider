#!/usr/bin/env python3
"""Rebuild the checked-in paletted shader with PSL1GHT cgcomp + NVIDIA Cg 3.1.

Put cgcomp on PATH and libCg.so on LD_LIBRARY_PATH. The package build uses the
checked-in binary, so shader tools are only needed when modifying paletted.fcg.
"""
from pathlib import Path
import subprocess

folder = Path(__file__).resolve().parent.parent / "src" / "ps3"
subprocess.run(["cgcomp", "-f", str(folder / "paletted.fcg"), str(folder / "paletted.fpo")], check=True)
blob = (folder / "paletted.fpo").read_bytes()
assert blob[:2] == b"FP" and b"uFogColor\0" in blob and b"uPaletteV\0" in blob
rows = ["  " + ", ".join(f"0x{byte:02x}" for byte in blob[i:i + 12]) for i in range(0, len(blob), 12)]
(folder / "paletted_fpo.c").write_text(
    "/* Generated from paletted.fcg with PSL1GHT cgcomp and NVIDIA Cg 3.1. */\n"
    "unsigned char paletted_fpo[] = {\n" + ",\n".join(rows) +
    f"\n}};\nunsigned int paletted_fpo_len = {len(blob)};\n", newline="\n")
print(f"Built and embedded {len(blob)} bytes.")
