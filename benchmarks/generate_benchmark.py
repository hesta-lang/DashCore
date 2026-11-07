#!/usr/bin/env python3
"""Generate and validate a DashCore Benchmark level for Geometry Dash 2.2081."""
from __future__ import annotations
import argparse, base64, gzip, hashlib, json, math, plistlib
from pathlib import Path

LEVEL_NAME = "DashCore Benchmark"
LEVEL_START = ("kA1,0,kA2,0,kA3,0,kA4,0,kA6,0,kA7,0,kA8,0,kA9,0,"
               "kA10,0,kA11,0,kA13,0,kA15,0,kA16,0,kA17,0,kA18,0")
OBJECT_BLOCK = 1
TRIGGER_IDS = {"color": 899, "move": 901, "pulse": 1006, "alpha": 1007, "toggle": 1049}

def fmt(value):
    if isinstance(value, float):
        if not math.isfinite(value): raise ValueError("Non-finite coordinate")
        return f"{value:.3f}".rstrip("0").rstrip(".")
    return str(value)

def object_record(object_id, x, y, **properties):
    values = ["1", str(object_id), "2", fmt(x), "3", fmt(y)]
    for key, value in properties.items(): values.extend((key, str(value)))
    if len(values) % 2: raise ValueError("Malformed object property list")
    return ",".join(values)

def grid(output, *, x0, y0, columns, rows, dx=30, dy=30, z_layer=0,
         group_id=None, color_channel=None, second_layer=False):
    for row in range(rows):
        for col in range(columns):
            props = {"24": z_layer, "25": (row + col) % 5}
            if group_id is not None: props["57"] = group_id
            if color_channel is not None: props["21"] = color_channel
            output.append(object_record(OBJECT_BLOCK, x0 + col*dx, y0 + row*dy, **props))
            if second_layer:
                output.append(object_record(OBJECT_BLOCK, x0 + col*dx + 8, y0 + row*dy + 8,
                                            **{"24": z_layer + 1, "25": (row + col) % 5}))

def build_objects():
    objects, counts = [], {}
    before = len(objects)
    grid(objects, x0=360, y0=150, columns=6, rows=4, z_layer=1)
    counts["baseline"] = len(objects) - before
    before = len(objects)
    grid(objects, x0=2100, y0=150, columns=24, rows=12, z_layer=1)
    counts["static_density"] = len(objects) - before
    before = len(objects)
    grid(objects, x0=5100, y0=150, columns=40, rows=16, z_layer=2, second_layer=True)
    counts["layered_overdraw"] = len(objects) - before
    before = len(objects)
    grid(objects, x0=9300, y0=150, columns=6, rows=5, z_layer=1, group_id=11)
    grid(objects, x0=9900, y0=150, columns=6, rows=5, z_layer=1, group_id=12)
    grid(objects, x0=10500, y0=150, columns=6, rows=5, z_layer=1, group_id=13)
    counts["grouped_objects"] = len(objects) - before
    before = len(objects)
    grid(objects, x0=11100, y0=150, columns=6, rows=4, z_layer=1, color_channel=2)
    counts["color_channel_objects"] = len(objects) - before
    before = len(objects)
    objects.extend([
        object_record(899, 11280, 90, **{"7":80,"8":180,"9":255,"10":0.25,"23":2}),
        object_record(901, 11580, 90, **{"10":0.25,"28":12,"29":0,"30":0,"51":11}),
        object_record(1006, 11880, 90, **{"7":255,"8":120,"9":60,"10":0.4,"45":0.1,"46":0.1,"47":0.2,"51":12,"52":1}),
        object_record(1007, 12180, 90, **{"10":0.25,"35":0.65,"51":12}),
        object_record(1049, 12480, 90, **{"51":13,"56":0}),
    ])
    counts["triggers"] = len(objects) - before
    before = len(objects)
    grid(objects, x0=13200, y0=150, columns=24, rows=10, z_layer=1)
    counts["tail"] = len(objects) - before
    return objects, counts

def encode_level(raw):
    return base64.urlsafe_b64encode(gzip.compress(raw.encode("utf-8"), compresslevel=9, mtime=0)).decode("ascii")

def decode_level(encoded):
    return gzip.decompress(base64.urlsafe_b64decode(encoded.encode("ascii"))).decode("utf-8")

def parse_objects(raw):
    parts = raw.split(";")
    if len(parts) < 2 or not parts[0].startswith("kA1,"):
        raise ValueError("Missing or malformed level-start header")
    parsed = []
    for index, part in enumerate(parts[1:]):
        if not part: continue
        fields = part.split(",")
        if len(fields) % 2: raise ValueError(f"Object {index} has odd property count")
        item = dict(zip(fields[0::2], fields[1::2]))
        if not {"1","2","3"}.issubset(item): raise ValueError(f"Object {index} missing ID/X/Y")
        float(item["2"]); float(item["3"])
        parsed.append(item)
    return parsed

def validate_gmd(path, expected_counts):
    with path.open("rb") as stream: document = plistlib.load(stream)
    if document.get("k2") != LEVEL_NAME: raise ValueError("GMD level name mismatch")
    encoded = document.get("k4")
    if not isinstance(encoded, str): raise ValueError("GMD k4 level data missing")
    raw = decode_level(encoded)
    objects = parse_objects(raw)
    ids = {}
    for item in objects:
        oid = int(item["1"]); ids[oid] = ids.get(oid, 0) + 1
    expected_total = sum(expected_counts.values())
    if len(objects) != expected_total: raise ValueError(f"Object count mismatch: {expected_total} != {len(objects)}")
    for name, oid in TRIGGER_IDS.items():
        if ids.get(oid, 0) != 1: raise ValueError(f"Missing expected {name} trigger ID {oid}")
    return {
        "level_name": LEVEL_NAME, "game_target": "2.2081", "geode_target": "5.11.2",
        "object_count": len(objects), "trigger_count": sum(ids.get(v,0) for v in TRIGGER_IDS.values()),
        "object_id_counts": {str(k):v for k,v in sorted(ids.items())},
        "section_counts": expected_counts,
        "raw_levelstring_sha256": hashlib.sha256(raw.encode()).hexdigest(),
        "compressed_levelstring_bytes": len(encoded), "gmd_bytes": path.stat().st_size,
        "structural_validation": "PASS", "runtime_validation": "NOT_RUN"
    }

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default="dist")
    args = parser.parse_args()
    output = Path(args.output_dir); output.mkdir(parents=True, exist_ok=True)
    objects, counts = build_objects()
    raw = LEVEL_START + ";" + ";".join(objects) + ";"
    encoded = encode_level(raw)
    gmd = {
        "k2": LEVEL_NAME,
        "k3": base64.b64encode(b"Controlled local performance test level for DashCore.").decode("ascii"),
        "k4": encoded, "k5": "DashCore", "k6": 0, "k7": 0, "k8": 0,
        "k9": 0, "k10": 0, "k11": 0, "k12": 0, "k13": 1, "k14": 0,
        "k15": 0, "k16": 1, "k17": 22, "k18": 0, "k19": 0, "k20": 0
    }
    gmd_path = output / "DashCore-Benchmark.gmd"
    with gmd_path.open("wb") as stream: plistlib.dump(gmd, stream, fmt=plistlib.FMT_XML, sort_keys=False)
    (output / "DashCore-Benchmark.levelstring.txt").write_text(raw + "\n", encoding="utf-8")
    (output / "DashCore-Benchmark.levelstring.b64").write_text(encoded + "\n", encoding="ascii")
    report = validate_gmd(gmd_path, counts)
    (output / "benchmark-manifest.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
