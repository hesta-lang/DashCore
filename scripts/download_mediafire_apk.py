#!/usr/bin/env python3
"""Resolve a user-provided MediaFire file page and download a validated APK."""
from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from html.parser import HTMLParser
from pathlib import Path

MAX_BYTES = 1024 * 1024 * 1024
USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120 Safari/537.36"


class DownloadLinkParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:
        if tag.lower() != "a":
            return
        values = dict(attrs)
        href = values.get("href", "")
        element_id = values.get("id", "")
        if href and (element_id == "downloadButton" or "download" in element_id.lower()):
            self.links.append(html.unescape(href))


def resolve_download_url(page_url: str) -> str:
    parsed = urllib.parse.urlparse(page_url)
    if parsed.scheme != "https" or parsed.hostname not in {"mediafire.com", "www.mediafire.com"}:
        raise ValueError("The source must be an HTTPS MediaFire file page.")
    request = urllib.request.Request(page_url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=45) as response:
        final_page_url = response.geturl()
        body = response.read(8 * 1024 * 1024).decode("utf-8", errors="replace")

    parser = DownloadLinkParser()
    parser.feed(body)
    candidates = list(parser.links)
    # MediaFire's page commonly exposes its actual transfer URL in downloadButton.
    candidates += re.findall(
        r'''(?i)(?:href|download_url|downloadLink)\s*[=:]\s*["'](https?://[^"'<> ]+)''',
        body,
    )
    candidates += re.findall(r'''["'](https?://download[^"'<> ]+)["']''', body)
    for candidate in candidates:
        candidate = html.unescape(candidate).replace("\\/", "/")
        candidate = urllib.parse.urljoin(final_page_url, candidate)
        host = (urllib.parse.urlparse(candidate).hostname or "").lower()
        if candidate.startswith("https://") and ("mediafire.com" in host or "mediafire" in host):
            # Ignore the page URL itself; it is not a transfer endpoint.
            if "/file/" not in urllib.parse.urlparse(candidate).path:
                return candidate
        if candidate.startswith("https://") and host.startswith("download"):
            return candidate
    raise RuntimeError(
        "MediaFire did not expose a direct download URL. The page may require a CAPTCHA, "
        "JavaScript challenge, or an interactive confirmation; the script will not bypass it."
    )


def download(url: str, destination: Path) -> tuple[int, str, str]:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Referer": "https://www.mediafire.com/"})
    digest = hashlib.sha256()
    total = 0
    with urllib.request.urlopen(request, timeout=90) as response, destination.open("wb") as output:
        content_type = response.headers.get("Content-Type", "")
        final_url = response.geturl()
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            total += len(chunk)
            if total > MAX_BYTES:
                raise RuntimeError("Downloaded file exceeds the 1 GiB safety limit.")
            output.write(chunk)
            digest.update(chunk)
    if total == 0:
        raise RuntimeError("MediaFire returned an empty file.")
    return total, digest.hexdigest(), content_type


def validate_apk(path: Path) -> None:
    with path.open("rb") as stream:
        if stream.read(4) != b"PK\x03\x04":
            raise RuntimeError("Downloaded content is not a ZIP/APK file (likely an HTML error page).")
    try:
        with zipfile.ZipFile(path) as archive:
            bad = archive.testzip()
            if bad:
                raise RuntimeError(f"APK archive is corrupt at entry: {bad}")
            names = set(archive.namelist())
            if "AndroidManifest.xml" not in names:
                raise RuntimeError("ZIP is missing AndroidManifest.xml; it is not an Android APK.")
            if not any(name.startswith("classes") and name.endswith(".dex") for name in names):
                raise RuntimeError("APK is missing DEX bytecode; refusing to install.")
    except zipfile.BadZipFile as exc:
        raise RuntimeError("Downloaded file is not a valid APK archive.") from exc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True, help="MediaFire file page URL")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    try:
        direct_url = resolve_download_url(args.url)
        size, sha256, content_type = download(direct_url, args.output)
        validate_apk(args.output)
        report = {
            "result": "downloaded_and_apk_structure_valid",
            "source": args.url,
            "download_host": urllib.parse.urlparse(direct_url).hostname,
            "size_bytes": size,
            "sha256": sha256,
            "content_type": content_type,
            "apk": str(args.output),
            "package_and_version": "verified later by Android Package Manager after installation",
        }
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
        return 0
    except (ValueError, RuntimeError, urllib.error.URLError, TimeoutError) as exc:
        print(f"[MEDIAFIRE DOWNLOAD FAILED] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
