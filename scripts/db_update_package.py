#!/usr/bin/env python3
"""Fetch one Release and prepare offline upload materials. Never connects to SQL."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile
import urllib.request
import zipfile

REPO = "zemise/lis-workbench"
MAX_SIZE = 100 * 1024 * 1024
VERSION = re.compile(r"v\d{4}\.\d{2}\.\d{2}\Z")
PROTECTED = {"clientconfig.ini", "log", "backup", "templates", "documents"}


def validate_manifest(manifest):
    if manifest.get("appId") != "lis-workbench" or manifest.get("channel") != "stable":
        raise ValueError("Unsupported application or channel")
    version = manifest.get("version", "")
    if not VERSION.fullmatch(version):
        raise ValueError("Invalid release version")
    if manifest.get("minUpdaterVersion") != "1.0.0":
        raise ValueError("This upload tool supports updater protocol 1.0.0 only")
    package = manifest.get("package", {})
    if package.get("file") != f"LISWorkbench-{version}-win7-win11.zip":
        raise ValueError("Unexpected package filename")
    size = package.get("size")
    if type(size) is not int or not 0 < size <= MAX_SIZE:
        raise ValueError("Invalid package size (maximum 100 MiB)")
    if not re.fullmatch(r"[0-9a-f]{64}", package.get("sha256", "")):
        raise ValueError("Invalid SHA-256")
    notes = manifest.get("notes", [])
    if not isinstance(notes, list) or not all(isinstance(note, str) for note in notes):
        raise ValueError("Invalid release notes")
    if len("\n".join(notes).encode("utf-16-le")) // 2 > 2000:
        raise ValueError("Release notes exceed 2000 UTF-16 units")
    return package


def validate_zip(path, package):
    if path.stat().st_size != package["size"]:
        raise ValueError("Package size mismatch")
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest() if hasattr(hashlib, "file_digest") else hashlib.sha256(stream.read()).hexdigest()
    if digest != package["sha256"]:
        raise ValueError("Package SHA-256 mismatch")
    with zipfile.ZipFile(path) as archive:
        names = set()
        total = 0
        for item in archive.infolist():
            name = item.filename.replace("\\", "/")
            parts = name.split("/")
            if (name.startswith("/") or ":" in name or any(p in (".", "..") for p in parts)
                    or any(p.lower() in PROTECTED for p in parts)
                    or stat.S_ISLNK(item.external_attr >> 16)):
                raise ValueError("Unsafe or protected ZIP entry")
            # Current Release packages consist of flat executables and DLLs.
            if item.is_dir() or len(parts) != 1 or not name.lower().endswith((".exe", ".dll")):
                raise ValueError("Unsupported ZIP entry")
            folded = name.lower()
            stem = folded.split(".")[0]
            if (not re.fullmatch(r"[A-Za-z0-9_.-]+", name)
                    or re.fullmatch(r"con|prn|aux|nul|com[1-9]|lpt[1-9]", stem)):
                raise ValueError("Invalid Windows package filename")
            if folded in names:
                raise ValueError("Duplicate ZIP entry")
            names.add(folded)
            total += item.file_size
            if total > 512 * 1024 * 1024:
                raise ValueError("Expanded package exceeds 512 MiB")
        if not {"lis_workbench.exe", "updater.exe"}.issubset(names):
            raise ValueError("Missing application or updater")
        if archive.testzip() is not None:
            raise ValueError("Corrupt ZIP entry")


def api(path):
    req = urllib.request.Request(f"https://api.github.com/repos/{REPO}/{path}", headers={
        "Accept": "application/vnd.github+json", "User-Agent": "LISWorkbench-package",
        **({"Authorization": "Bearer " + os.environ["GH_TOKEN"]} if os.environ.get("GH_TOKEN") else {})})
    with urllib.request.urlopen(req, timeout=30) as response:
        return json.load(response)


def assets(release):
    return {a["name"]: a for a in release["assets"] if a.get("state") == "uploaded"}


def download(asset, target, limit):
    url = asset["browser_download_url"]
    if not url.startswith(f"https://github.com/{REPO}/releases/download/"):
        raise ValueError("Unexpected release asset origin")
    # Public release assets: do not forward an API credential to redirected hosts.
    req = urllib.request.Request(url, headers={"User-Agent": "LISWorkbench-package"})
    with urllib.request.urlopen(req, timeout=60) as response, target.open("wb") as stream:
        count = 0
        while True:
            data = response.read(64 * 1024)
            if not data:
                break
            count += len(data)
            if count > limit:
                raise ValueError("Release asset exceeds size limit")
            stream.write(data)
    if count != asset["size"]:
        raise ValueError("Release asset length changed")


def prepare(output, tag=None):
    release = api("releases/tags/" + tag if tag else "releases/latest")
    if release.get("draft") or release.get("prerelease"):
        raise ValueError("Only published stable Releases are supported")
    version = release["tag_name"]
    if not VERSION.fullmatch(version):
        raise ValueError("Invalid Release tag")
    names = ["manifest.json", f"LISWorkbench-{version}-win7-win11.zip"]
    listing = assets(release)
    if not all(name in listing for name in names):
        raise ValueError("Release has no complete update assets")
    destination = output / version
    if destination.exists():
        raise ValueError(f"Output already exists: {destination}; preserve or remove it explicitly")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".db-update-", dir=output) as working:
        stage = Path(working) / version
        stage.mkdir()
        download(listing[names[0]], stage / names[0], 1024 * 1024)
        manifest = json.loads((stage / names[0]).read_text(encoding="utf-8-sig"))
        package = validate_manifest(manifest)
        if manifest["version"] != version:
            raise ValueError("Manifest version does not match Release tag")
        download(listing[names[1]], stage / names[1], MAX_SIZE)
        validate_zip(stage / names[1], package)
        fresh = assets(api(f"releases/{release['id']}"))
        for name in names:
            if name not in fresh or any(fresh[name].get(k) != listing[name].get(k) for k in ("id", "size", "updated_at")):
                raise ValueError("Release assets changed during download; retry the entire operation")
        templates = Path(__file__).parent / "db_update"
        for name in ("upload.ps1", "upload.cmd", "README-upload.txt"):
            # BOM is intentional for Windows PowerShell 5.1 Unicode source files.
            (stage / name).write_text((templates / name).read_text(encoding="utf-8"), encoding="utf-8-sig" if name.endswith(".ps1") else "utf-8")
        for name in ("02-publish.sql", "03-cleanup.sql"):
            sql = (templates / name).read_text(encoding="utf-8")
            sql = sql.replace("__VERSION__", version).replace("__SHA256__", package["sha256"]).replace("__SIZE__", str(package["size"]))
            (stage / name).write_text(sql, encoding="utf-8-sig")
        info = {"repository": REPO, "releaseId": release["id"], "tag": version,
                "publishedAt": release.get("published_at"), "assets": [
                    {k: listing[name][k] for k in ("id", "name", "size", "updated_at", "browser_download_url")} for name in names],
                "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in stage.iterdir()}}
        (stage / "release-info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding="utf-8")
        stage.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "out/db-update")
    args = parser.parse_args()
    if args.release and not VERSION.fullmatch(args.release):
        parser.error("release must have form vYYYY.MM.DD")
    try:
        print(prepare(args.output, args.release))
    except Exception as exc:
        parser.exit(1, f"db-update-package: {exc}\n")


if __name__ == "__main__":
    main()
