#!/usr/bin/env python3
"""Build and inject a deterministic, safe Geometry Dash local AutoTest level."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import plistlib
import zlib
from pathlib import Path
from typing import Any

LEVEL_NAME = "DashCore AutoTest"
LEVEL_DESCRIPTION = "Generated deterministic DashCore renderer diagnostic fixture. No gameplay hazards."
SAVE_XOR_KEY = 11
MAX_SAVE_BYTES = 64 * 1024 * 1024
GAME_BUILD_FALLBACK = 2200


def _decompress_limited(data: bytes, limit: int = MAX_SAVE_BYTES) -> bytes:
    """Decode GD's zlib-wrapped save data, with safe gzip/raw compatibility."""
    last_error: Exception | None = None
    for window_bits in (zlib.MAX_WBITS, -zlib.MAX_WBITS, zlib.MAX_WBITS | 16):
        try:
            decoder = zlib.decompressobj(window_bits)
            output = decoder.decompress(data, limit + 1)
            if len(output) > limit or decoder.unconsumed_tail:
                raise ValueError("Compressed save exceeds the 64 MiB safety limit")
            output += decoder.flush()
            if len(output) > limit:
                raise ValueError("Decompressed save exceeds the 64 MiB safety limit")
            if not decoder.eof:
                raise ValueError("Compressed save is truncated")
            return output
        except zlib.error as exc:
            last_error = exc
    raise ValueError(f"Compressed Geometry Dash data is invalid: {last_error}")


def decode_save(raw: bytes) -> bytes:
    if not raw:
        raise ValueError("CCLocalLevels.dat is empty")
    xor_data = bytes(value ^ SAVE_XOR_KEY for value in raw)
    try:
        # GD saves are URL-safe base64 text after XOR; accept omitted padding.
        compact = b"".join(xor_data.split())
        compact += b"=" * ((-len(compact)) % 4)
        compressed = base64.b64decode(compact, altchars=b"-_", validate=True)
    except Exception as exc:
        raise ValueError(f"CCLocalLevels.dat base64 decoding failed: {exc}") from exc
    return _decompress_limited(compressed)


def encode_save(xml_data: bytes) -> bytes:
    # Geometry Dash's save codec uses the zlib wrapper (not the gzip container).
    compressed = zlib.compress(xml_data, level=9)
    encoded = base64.urlsafe_b64encode(compressed)
    return bytes(value ^ SAVE_XOR_KEY for value in encoded)


def object_record(object_id: int, grid_x: int, grid_y: int,
                  attrs: dict[int, str | int] | None = None) -> str:
    # GD block coordinates are stored as pixel coordinates at grid centers.
    values: list[tuple[int, str | int]] = [
        (1, object_id),
        (2, grid_x * 30 + 15),
        (3, grid_y * 30 + 15),
    ]
    if attrs:
        values.extend(attrs.items())
    return ",".join(f"{key},{value}" for key, value in values)


def build_level() -> tuple[str, dict[str, Any]]:
    header = ",".join((
        "kA2,0", "kA3,0", "kA4,0", "kA6,1",
        "kA7,1", "kA10,0", "kA17,2", "kA18,0",
    ))
    records: list[str] = []
    counts: dict[str, Any] = {}
    section_bounds = ((0, 79), (80, 159), (160, 239))

    # Continuous, hazard-free runway. The player can move while diagnostics run.
    for x in range(240):
        section = 1 if x < 80 else 2 if x < 160 else 3
        records.append(object_record(1, x, 0, {
            20: section, 24: section - 1, 33: section,
        }))

    # Three deterministic object-density zones above the player lane.
    for section_index, (start, end) in enumerate(section_bounds, start=1):
        if section_index == 1:
            x_positions, y_positions = range(start + 4, end, 8), (12, 15)
        elif section_index == 2:
            x_positions, y_positions = range(start + 2, end, 2), (12, 14, 16, 18)
        else:
            x_positions, y_positions = range(start, end), (11, 13, 15, 17, 19, 21)

        local_count = 0
        for x in x_positions:
            for row_index, y in enumerate(y_positions):
                attrs: dict[int, str | int] = {
                    20: section_index,
                    24: (row_index + section_index) % 3,
                    25: row_index,
                }
                if (x + row_index) % 3 == 0:
                    attrs[33] = section_index
                if (x + row_index) % 7 == 0:
                    attrs[57] = f"{section_index}.{section_index + 10}"
                records.append(object_record(1, x, y, attrs))
                local_count += 1
        counts[f"section_{section_index}_decorative_objects"] = local_count

    level_string = header + ";" + ";".join(records) + ";"
    counts.update({
        "floor_objects": 240,
        "total_objects": len(records),
        "level_length_grid_units": 240,
        "sections": [
            {"name": "sparse", "start_x": 0, "end_x": 79},
            {"name": "medium", "start_x": 80, "end_x": 159},
            {"name": "dense", "start_x": 160, "end_x": 239},
        ],
    })
    return level_string, counts


def build_level_entry() -> tuple[dict[str, Any], dict[str, Any]]:
    level_string, counts = build_level()
    compressed_level = zlib.compress(level_string.encode("utf-8"), level=9)
    encoded_level = base64.urlsafe_b64encode(compressed_level).decode("ascii")
    description = base64.urlsafe_b64encode(
        LEVEL_DESCRIPTION.encode("utf-8")
    ).decode("ascii")
    entry = {
        "kCEK": 4, "k2": LEVEL_NAME, "k3": description,
        "k4": encoded_level, "k5": "Player", "k14": False,
        "k21": 2, "k48": counts["total_objects"], "k50": 35,
    }
    counts["level_string_sha256"] = hashlib.sha256(
        level_string.encode("utf-8")
    ).hexdigest()
    counts["compressed_level_bytes"] = len(compressed_level)
    return entry, counts


def _empty_save_document(game_build: int = GAME_BUILD_FALLBACK) -> dict[str, Any]:
    return {"LLM_01": {"_isArr": True}, "LLM_02": game_build}


def inject_level(xml_data: bytes) -> tuple[bytes, dict[str, Any]]:
    try:
        document = plistlib.loads(xml_data)
    except Exception as exc:
        raise ValueError(f"CCLocalLevels XML is not a valid plist: {exc}") from exc
    if not isinstance(document, dict):
        raise ValueError("CCLocalLevels plist root must be a dictionary")

    levels = document.get("LLM_01")
    if levels is None:
        levels = {"_isArr": True}
        document["LLM_01"] = levels
    if not isinstance(levels, dict):
        raise ValueError("CCLocalLevels LLM_01 must be a dictionary")
    levels.setdefault("_isArr", True)
    document.setdefault("LLM_02", GAME_BUILD_FALLBACK)

    # Replace only our fixture; keep user-created local levels intact.
    for key in list(levels):
        if key.startswith("k_") and isinstance(levels[key], dict):
            if levels[key].get("k2") == LEVEL_NAME:
                del levels[key]

    existing = [
        (int(key[2:]), key) for key in levels
        if key.startswith("k_") and key[2:].isdigit()
    ]
    for index, key in sorted(existing, reverse=True):
        levels[f"k_{index + 1}"] = levels.pop(key)

    entry, summary = build_level_entry()
    levels["k_0"] = entry
    output = plistlib.dumps(document, fmt=plistlib.FMT_XML, sort_keys=False)

    verified = plistlib.loads(output)
    first = verified["LLM_01"]["k_0"]
    if first.get("k2") != LEVEL_NAME:
        raise ValueError("Generated level did not round-trip as the first local level")
    decoded_level = _decompress_limited(
        base64.urlsafe_b64decode(first["k4"].encode("ascii"))
    )
    if decoded_level.count(b";1,") < summary["total_objects"]:
        raise ValueError("Generated level object payload did not pass validation")
    summary["existing_levels_shifted"] = len(existing)
    summary["first_local_level"] = LEVEL_NAME
    return output, summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path,
                        help="Existing encrypted or decrypted CCLocalLevels.dat")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--game-build", type=int, default=GAME_BUILD_FALLBACK)
    args = parser.parse_args()

    try:
        if args.input and args.input.is_file():
            raw = args.input.read_bytes()
            if raw.startswith(b"<?xml") or raw.lstrip().startswith(b"<plist"):
                xml_data, source_kind = raw, "decrypted-xml"
            else:
                xml_data, source_kind = decode_save(raw), "encrypted-save"
        else:
            empty_doc = _empty_save_document(args.game_build)
            xml_data = plistlib.dumps(
                empty_doc, fmt=plistlib.FMT_XML, sort_keys=False
            )
            source_kind = "new-empty-local-level-list"

        output_xml, summary = inject_level(xml_data)
        output_bytes = encode_save(output_xml)
        round_trip = decode_save(output_bytes)
        if plistlib.loads(round_trip)["LLM_01"]["k_0"].get("k2") != LEVEL_NAME:
            raise ValueError("Encrypted save did not pass round-trip validation")

        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(output_bytes)
        summary.update({
            "result": "passed",
            "level_name": LEVEL_NAME,
            "source_kind": source_kind,
            "input_sha256": hashlib.sha256(args.input.read_bytes()).hexdigest()
                if args.input and args.input.is_file() else None,
            "output_sha256": hashlib.sha256(output_bytes).hexdigest(),
            "output_bytes": len(output_bytes),
            "game_build": args.game_build,
            "savefile_round_trip_verified": True,
            "rendering_or_gameplay_code_changed": False,
        })
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(json.dumps(summary, indent=2) + "\n",
                                encoding="utf-8")
        print(json.dumps(summary, indent=2))
        return 0
    except Exception as exc:
        print(f"[AUTOTEST LEVEL FAILED] {type(exc).__name__}: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
