#!/usr/bin/env python3
"""Validate a built DashCore .geode package without installing Geometry Dash."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zipfile
from pathlib import Path

EXPECTED_ID = "hesta.optgod"
EXPECTED_MOD_VERSION = "0.8.0"
EXPECTED_GEODE = "5.11.2"
EXPECTED_GD_ANDROID = "2.2081"
EXPECTED_ABI_MACHINE = 183  # ELF e_machine: EM_AARCH64


def fail(message: str, errors: list[str]) -> None:
    errors.append(message)
    print(f"[FAIL] {message}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    args = parser.parse_args()

    args.report_dir.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    checks: list[str] = []

    packages = sorted(args.artifact_dir.rglob("*.geode"))
    if len(packages) != 1:
        fail(f"Expected exactly one .geode package; found {len(packages)}", errors)
        package = packages[0] if packages else None
    else:
        package = packages[0]
        checks.append("Exactly one .geode artifact found")
        print(f"[OK] Package found: {package}")

    metadata: dict = {}
    elf_files: list[dict] = []
    package_sha256 = None

    if package is not None:
        package_sha256 = hashlib.sha256(package.read_bytes()).hexdigest()
        try:
            with zipfile.ZipFile(package) as archive:
                bad_member = archive.testzip()
                if bad_member:
                    fail(f"Corrupt ZIP member: {bad_member}", errors)
                else:
                    checks.append("Package archive integrity verified")
                    print("[OK] Archive integrity")

                names = [n for n in archive.namelist() if not n.endswith("/")]
                metadata_names = [n for n in names if Path(n).name == "mod.json"]
                if not metadata_names:
                    fail("mod.json not found inside .geode package", errors)
                else:
                    metadata_path = next(
                        (n for n in metadata_names if n == "mod.json"),
                        metadata_names[0],
                    )
                    try:
                        metadata = json.loads(archive.read(metadata_path))
                        checks.append(f"Manifest parsed: {metadata_path}")
                        print(f"[OK] Manifest parsed: {metadata_path}")
                    except Exception as exc:
                        fail(f"Could not parse mod.json: {exc}", errors)

                for name in names:
                    try:
                        with archive.open(name) as stream:
                            header = stream.read(64)
                    except Exception:
                        continue
                    if len(header) >= 20 and header[:4] == b"\x7fELF":
                        elf_class = header[4]
                        data_encoding = header[5]
                        if data_encoding == 1:
                            machine = struct.unpack_from("<H", header, 18)[0]
                        elif data_encoding == 2:
                            machine = struct.unpack_from(">H", header, 18)[0]
                        else:
                            machine = -1
                        elf_files.append({
                            "path": name,
                            "elf_class": elf_class,
                            "machine": machine,
                        })

        except zipfile.BadZipFile:
            fail("The .geode file is not a valid ZIP archive", errors)
        except Exception as exc:
            fail(f"Package inspection failed: {exc}", errors)

    if metadata:
        if metadata.get("id") == EXPECTED_ID:
            checks.append(f"Mod ID matches {EXPECTED_ID}")
            print("[OK] Mod ID")
        else:
            fail(f"Unexpected mod ID: {metadata.get('id')!r}", errors)

        if metadata.get("version") == EXPECTED_MOD_VERSION:
            checks.append(f"Mod version matches {EXPECTED_MOD_VERSION}")
            print("[OK] Mod version")
        else:
            fail(f"Expected mod version {EXPECTED_MOD_VERSION}, got {metadata.get('version')!r}", errors)

        if metadata.get("geode") == EXPECTED_GEODE:
            checks.append(f"Geode loader target matches {EXPECTED_GEODE}")
            print("[OK] Geode version target")
        else:
            fail(f"Expected Geode {EXPECTED_GEODE}, got {metadata.get('geode')!r}", errors)

        android_version = metadata.get("gd", {}).get("android")
        if android_version == EXPECTED_GD_ANDROID:
            checks.append(f"Android GD target matches {EXPECTED_GD_ANDROID}")
            print("[OK] GD Android target")
        else:
            fail(f"Expected GD Android {EXPECTED_GD_ANDROID}, got {android_version!r}", errors)

        culling = metadata.get("settings", {}).get("experimental-render-culling", {})
        if culling.get("default") is False:
            checks.append("Experimental render culling defaults to disabled")
            print("[OK] Render culling default is disabled")
        else:
            fail("Experimental render culling is not explicitly disabled by default", errors)

    if not elf_files:
        fail("No ELF native library found inside the .geode package", errors)
    else:
        arm64_files = [item for item in elf_files if item["machine"] == EXPECTED_ABI_MACHINE]
        if arm64_files:
            checks.append(f"Found {len(arm64_files)} AArch64 ELF native file(s)")
            for item in arm64_files:
                print(f"[OK] ARM64 ELF: {item['path']}")
        else:
            fail(
                "No AArch64 ELF (EM_AARCH64=183) found. "
                f"Detected ELF entries: {elf_files}",
                errors,
            )

    report = {
        "result": "passed" if not errors else "failed",
        "package": str(package) if package else None,
        "sha256": package_sha256,
        "expected": {
            "mod_id": EXPECTED_ID,
            "mod_version": EXPECTED_MOD_VERSION,
            "geode": EXPECTED_GEODE,
            "gd_android": EXPECTED_GD_ANDROID,
            "native_elf_machine": EXPECTED_ABI_MACHINE,
            "render_culling_default": False,
        },
        "checks": checks,
        "elf_files": elf_files,
        "errors": errors,
        "scope_note": (
            "This is package/static validation only. It does not install or launch "
            "Geometry Dash, load the mod in Geode, or validate GPU rendering."
        ),
    }
    (args.report_dir / "autotest-report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )

    lines = [
        "# DashCore AutoTest report",
        "",
        f"**Result: {report['result'].upper()}**",
        "",
        f"- Package: {report['package'] or 'not found'}",
        f"- SHA-256: {package_sha256 or 'unavailable'}",
        "",
        "## Passed checks",
    ]
    lines += [f"- {item}" for item in checks] or ["- None"]
    lines += ["", "## Errors"]
    lines += [f"- {item}" for item in errors] or ["- None"]
    lines += [
        "",
        "## Test scope",
        report["scope_note"],
    ]
    (args.report_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"\nDashCore AutoTest: {report['result'].upper()}")
    print(f"Passed checks: {len(checks)}; errors: {len(errors)}")
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
