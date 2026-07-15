#!/usr/bin/env python3
"""Scrub local paths from a PS3 ELF."""

from __future__ import annotations

import re
import sys
from pathlib import Path


PRINTABLE_STRING = re.compile(rb"[\x20-\x7e]{4,}\x00")
HOST_HOME_PATTERNS = (
    re.compile(
        rb"[A-Za-z]:" + rb"[\\/]" + rb"(?:Users|Documents and Settings)"
        + rb"[\\/]" + rb"[^\\/\x00]+" + rb"[\\/]"
    ),
    re.compile(rb"/(?:home|Users)/[^/\x00]+/"),
)


def contains_host_home(value: bytes) -> bool:
    return any(pattern.search(value) for pattern in HOST_HOME_PATTERNS)


def neutral_string(value: bytes) -> bytes:
    basename = re.split(rb"[\\/]", value)[-1] or b"path"
    replacement = b"donut-spider/" + basename
    if len(replacement) > len(value):
        replacement = b"donut-spider"
    return replacement + (b"_" * (len(value) - len(replacement)))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: sanitize_binary_paths.py <elf>", file=sys.stderr)
        return 2

    elf_path = Path(sys.argv[1])
    data = bytearray(elf_path.read_bytes())
    sanitized = 0

    for match in list(PRINTABLE_STRING.finditer(data)):
        value = match.group(0)[:-1]
        if not contains_host_home(value):
            continue
        start = match.start()
        data[start : start + len(value)] = neutral_string(value)
        sanitized += 1

    if contains_host_home(data):
        print("error: host home-directory path remains in linked ELF", file=sys.stderr)
        return 1

    temporary_path = elf_path.with_suffix(elf_path.suffix + ".sanitized.tmp")
    temporary_path.write_bytes(data)
    temporary_path.replace(elf_path)
    print(f"Sanitized {sanitized} host build path string(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
