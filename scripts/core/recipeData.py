# SPDX-License-Identifier: MIT

"""Generate minimal binary patch recipes and caller-defined C++ headers."""

from __future__ import annotations

from collections.abc import Mapping
import os
from pathlib import Path
import re
import shutil
import subprocess
import textwrap

PROJECT_ROOT = Path(__file__).absolute().parents[2]


def FindClangFormat() -> str:
    override = os.environ.get("CLANG_FORMAT")
    if override:
        return override

    executable = shutil.which("clang-format") or shutil.which("clang-format.exe")
    if executable:
        return executable

    if os.name == "nt":
        installer = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
        locator = installer / "Microsoft Visual Studio/Installer/vswhere.exe"
        if locator.is_file():
            try:
                result = subprocess.run(
                    [
                        str(locator),
                        "-products",
                        "*",
                        "-utf8",
                        "-find",
                        r"VC\Tools\Llvm\x64\bin\clang-format.exe",
                    ],
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    timeout=15,
                )
                if result.returncode == 0:
                    for candidate in result.stdout.splitlines():
                        if Path(candidate).is_file():
                            return candidate
            except (OSError, subprocess.TimeoutExpired):
                pass

    raise ValueError(
        "clang-format was not found. Add it to PATH or set CLANG_FORMAT to its executable."
    )


def FormatCpp(contents: bytes, filename: str) -> bytes:
    """Format generated C++ before replacing files or comparing a maintained header."""
    executable = FindClangFormat()
    require((PROJECT_ROOT / ".clang-format").is_file(), "The project .clang-format is missing")

    try:
        result = subprocess.run(
            [executable, "--style=file:.clang-format", "--Werror", f"--assume-filename={filename}"],
            input=contents,
            capture_output=True,
            cwd=PROJECT_ROOT,
            timeout=60,
        )
    except subprocess.TimeoutExpired as error:
        raise ValueError("clang-format exceeded its time limit") from error
    except OSError as error:
        raise ValueError("Cannot run clang-format. Check PATH or CLANG_FORMAT.") from error

    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError("clang-format failed" + (": " + detail if detail else ""))
    require(bool(result.stdout.strip()), "clang-format returned an empty header")

    return result.stdout


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def make_recipe(original: bytes, patched: bytes) -> tuple[list[tuple[int, int, int]], bytes, bytes]:
    require(len(patched) > len(original), "Expected appended patch sections")

    spans: list[tuple[int, int, int]] = []
    replacements = bytearray()
    cursor = 0

    while cursor < len(original):
        if original[cursor] == patched[cursor]:
            cursor += 1
            continue

        start = cursor

        while cursor < len(original) and original[cursor] != patched[cursor]:
            cursor += 1

        spans.append((start, len(replacements), cursor - start))
        replacements.extend(patched[start:cursor])

    require(spans and replacements, "Empty patch recipe")

    return spans, bytes(replacements), patched[len(original) :]


def apply_recipe(
    source: bytes, spans: list[tuple[int, int, int]], replacements: bytes, appended: bytes
) -> bytes:
    result = bytearray(source)
    end = 0

    for offset, data_offset, size in spans:
        require(
            size > 0 and offset >= end and offset + size <= len(source),
            "Invalid or overlapping patch span",
        )
        require(
            data_offset >= 0 and data_offset + size <= len(replacements),
            "Replacement bytes outside array",
        )

        result[offset : offset + size] = replacements[data_offset : data_offset + size]
        end = offset + size

    result.extend(appended)

    return bytes(result)


def byte_array(name: str, data: bytes) -> str:
    lines = [f"  inline constexpr std::array<std::uint8_t, {len(data)}> {name}{{{{"]
    columns = 11 if len(data) == 32 else 15

    for offset in range(0, len(data), columns):
        lines.append(
            "      " + ", ".join(f"0x{byte:02x}" for byte in data[offset : offset + columns]) + ","
        )

    lines.append("  }};")

    return "\n".join(lines)


def make_header(
    original_size: int,
    spans: list[tuple[int, int, int]],
    replacements: bytes,
    appended: bytes,
    *,
    namespace: str,
    provenance: str,
    patch_version: int,
    payload_version: int,
    source_hashes: Mapping[str, str],
    patched_sha256: str,
    patched_size: int,
) -> str:
    require(
        re.fullmatch(r"[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*", namespace, re.ASCII) is not None,
        "Invalid C++ namespace",
    )
    require(
        bool(provenance) and "\n" not in provenance and "\r" not in provenance,
        "Header provenance must be one line",
    )
    require(patch_version > 0 and payload_version > 0, "Recipe versions must be positive")
    require(
        original_size > 0 and patched_size == original_size + len(appended), "Invalid recipe sizes"
    )
    require(bool(source_hashes), "At least one source identity is required")

    reserved = {
        "PatchSpan",
        "patchVersion",
        "payloadVersion",
        "originalSize",
        "patchedSize",
        "patchedSha256",
        "patchSpans",
        "patchBytes",
        "appendBytes",
    }

    for name in source_hashes:
        require(
            re.fullmatch(r"[A-Za-z_]\w*", name, re.ASCII) is not None and name not in reserved,
            "Invalid source hash array name",
        )

    for value in (*source_hashes.values(), patched_sha256):
        require(re.fullmatch(r"[0-9a-f]{64}", value) is not None, "Invalid SHA-256 identity")

    guard = namespace.replace("::", "_").upper() + "_FULLSCREEN_PATCH_DATA_HPP"
    description = (
        provenance
        + " Contains replacement bytes and appended patch sections, not a copy of the game executable."
    )
    parts = ["// " + line for line in textwrap.wrap(description, width=97)] + [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        f"namespace {namespace}",
        "{",
        "",
        "  struct PatchSpan",
        "  {",
        "    std::size_t offset;",
        "    std::size_t dataOffset;",
        "    std::size_t size;",
        "  };",
        "",
        f"  inline constexpr unsigned patchVersion = {patch_version};",
        f"  inline constexpr unsigned payloadVersion = {payload_version};",
        f"  inline constexpr std::size_t originalSize = {original_size};",
        f"  inline constexpr std::size_t patchedSize = {patched_size};",
        "",
    ]

    for name, value in source_hashes.items():
        parts += [byte_array(name, bytes.fromhex(value)), ""]

    parts += [
        byte_array("patchedSha256", bytes.fromhex(patched_sha256)),
        "",
        f"  inline constexpr std::array<PatchSpan, {len(spans)}> patchSpans{{{{",
    ]
    parts += [f"      {{{offset}, {data_offset}, {size}}}," for offset, data_offset, size in spans]
    parts += [
        "  }};",
        "",
        byte_array("patchBytes", replacements),
        "",
        byte_array("appendBytes", appended),
        "",
        "  static_assert(originalSize + appendBytes.size() == patchedSize);",
        "",
        f"}} // namespace {namespace}",
        "",
        f"#endif // {guard}",
        "",
    ]

    return "\n".join(parts)
