#!/usr/bin/env python3
"""Transcode one GameMaker video into Donut Spider's low-memory BSV stream.

The resulting files are placed beside the source by default:
  clip.mp4.bsv      indexed sequence of independently compressed JPEG frames
  clip.mp4.bsv.ogg  streaming audio track

The runner keeps only one decoded frame and one GPU surface resident.
"""

from __future__ import annotations

import argparse
from fractions import Fraction
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def probe(ffprobe: str, source: Path) -> tuple[int, int, Fraction, int]:
    result = subprocess.run(
        [
            ffprobe,
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height,avg_frame_rate,duration:format=duration",
            "-of",
            "json",
            str(source),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    payload = json.loads(result.stdout)
    stream = payload["streams"][0]
    width = int(stream["width"])
    height = int(stream["height"])
    fps = Fraction(stream["avg_frame_rate"])
    duration = float(stream.get("duration") or payload["format"]["duration"])
    return width, height, fps, max(1, round(duration * 1000.0))


def pack_frames(frame_paths: list[Path], output: Path, width: int, height: int, fps: Fraction, duration_ms: int) -> int:
    temporary_output = output.with_suffix(output.suffix + ".tmp")
    entries: list[tuple[int, int]] = []
    with temporary_output.open("w+b") as handle:
        handle.write(
            struct.pack(
                "<4s7I",
                b"BSV1",
                width,
                height,
                fps.numerator,
                fps.denominator,
                len(frame_paths),
                duration_ms,
                0,
            )
        )
        handle.write(b"\0" * (len(frame_paths) * 8))
        for frame_path in frame_paths:
            frame = frame_path.read_bytes()
            offset = handle.tell()
            handle.write(frame)
            entries.append((offset, len(frame)))

        handle.seek(32)
        for offset, size in entries:
            if offset > 0xFFFFFFFF or size > 0xFFFFFFFF:
                raise ValueError("BSV1 supports files smaller than 4 GiB")
            handle.write(struct.pack("<II", offset, size))

    os.replace(temporary_output, output)
    return output.stat().st_size


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--jpeg-quality", type=int, default=4, choices=range(2, 16), metavar="2..15")
    args = parser.parse_args()

    source = args.source.resolve()
    if not source.is_file():
        raise FileNotFoundError(source)
    output = args.output.resolve() if args.output else Path(str(source) + ".bsv")
    output.parent.mkdir(parents=True, exist_ok=True)
    audio_output = Path(str(output) + ".ogg")

    width, height, fps, duration_ms = probe(args.ffprobe, source)
    with tempfile.TemporaryDirectory(prefix="spider_donut-video-") as temp_name:
        temp_dir = Path(temp_name)
        frame_pattern = temp_dir / "frame_%08d.jpg"
        run(
            [
                args.ffmpeg,
                "-hide_banner",
                "-loglevel",
                "warning",
                "-y",
                "-i",
                str(source),
                "-map",
                "0:v:0",
                "-an",
                "-fps_mode",
                "passthrough",
                "-c:v",
                "mjpeg",
                "-q:v",
                str(args.jpeg_quality),
                str(frame_pattern),
            ]
        )
        frames = sorted(temp_dir.glob("frame_*.jpg"))
        if not frames:
            raise RuntimeError("FFmpeg produced no video frames")
        stream_size = pack_frames(frames, output, width, height, fps, duration_ms)

    run(
        [
            args.ffmpeg,
            "-hide_banner",
            "-loglevel",
            "warning",
            "-y",
            "-i",
            str(source),
            "-map",
            "0:a:0",
            "-vn",
            "-c:a",
            "libvorbis",
            "-q:a",
            "4",
            str(audio_output),
        ]
    )

    print(
        f"Wrote {output} ({len(frames)} frames, {width}x{height}, "
        f"{float(fps):.3f} fps, {duration_ms / 1000.0:.3f}s, {stream_size / 1048576.0:.2f} MiB)"
    )
    print(f"Wrote {audio_output} ({audio_output.stat().st_size / 1048576.0:.2f} MiB)")


if __name__ == "__main__":
    main()
