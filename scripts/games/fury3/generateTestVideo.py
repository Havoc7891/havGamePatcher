# SPDX-License-Identifier: MIT

"""Generate a small owned uncompressed checkerboard AVI with silent PCM audio."""

import argparse
from pathlib import Path
import struct
import hashlib
import json


def Chunk(tag, data):
    return tag + struct.pack("<I", len(data)) + data + b"\0" * (len(data) & 1)


def List(kind, data):
    return Chunk(b"LIST", kind + data)


def Stream(kind, handler, scale, rate, length, buffer, sample, rectangle, formatData):
    header = struct.pack(
        "<4s4sIHHIIIIIIIIhhhh",
        kind,
        handler,
        0,
        0,
        0,
        0,
        scale,
        rate,
        0,
        length,
        buffer,
        0xFFFFFFFF,
        sample,
        *rectangle,
    )
    return List(b"strl", Chunk(b"strh", header) + Chunk(b"strf", formatData))


def Generate(target):
    width, height, fps, frames = 160, 120, 8, 24
    frameSize = width * height * 3
    audioPerFrame = 8000 // fps * 2
    main = struct.pack(
        "<14I",
        1000000 // fps,
        frameSize * fps + 16000,
        0,
        0x110,
        frames,
        0,
        2,
        frameSize,
        width,
        height,
        0,
        0,
        0,
        0,
    )
    videoFormat = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, frameSize, 0, 0, 0, 0)
    audioFormat = struct.pack("<HHIIHH", 1, 1, 8000, 16000, 2, 16)
    headers = List(
        b"hdrl",
        Chunk(b"avih", main)
        + Stream(b"vids", b"DIB ", 1, fps, frames, frameSize, 0, (0, 0, width, height), videoFormat)
        + Stream(
            b"auds",
            b"\0" * 4,
            2,
            16000,
            8000 * frames // fps,
            audioPerFrame,
            2,
            (0, 0, 0, 0),
            audioFormat,
        ),
    )
    data, index = bytearray(), bytearray()
    for frame in range(frames):
        pixels = bytearray()
        for y in range(height - 1, -1, -1):
            for x in range(width):
                if x < 5 or x >= width - 5 or y < 5 or y >= height - 5:
                    rgb = (255, 0, 255)
                elif x < width // 2:
                    rgb = (255, 32, 32) if y < height // 2 else (32, 32, 255)
                else:
                    rgb = (32, 255, 32) if y < height // 2 else (255, 255, 32)
                if ((x // 10) + (y // 10) + frame) % 2:
                    rgb = tuple(c // 2 for c in rgb)
                pixels.extend(reversed(rgb))
        for tag, payload in [(b"00db", pixels), (b"01wb", bytes(audioPerFrame))]:
            index.extend(struct.pack("<4sIII", tag, 0x10, len(data) + 4, len(payload)))
            data.extend(Chunk(tag, payload))
    avi = Chunk(b"RIFF", b"AVI " + headers + List(b"movi", data) + Chunk(b"idx1", index))
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(avi)
    target.with_suffix(".json").write_text(
        json.dumps(
            {
                "format": "OwnedSyntheticAvi",
                "width": width,
                "height": height,
                "frames": frames,
                "fps": fps,
                "audio": "silent mono 16-bit PCM at 8000Hz",
                "sha256": hashlib.sha256(avi).hexdigest(),
            },
            indent=2,
        )
        + "\n"
    )
    print("Generated owned checkerboard AVI with silent audio")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    Generate(parser.parse_args().output)
