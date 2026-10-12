"""Exercise the C++ ZIP validator using synthetic local archives only."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    for index, extra in enumerate([None, "../bad.exe", "templates/bad.dll", "ClientConfig.ini",
                                    "UPDATER.EXE", "file.txt", "a/b.exe", "C:bad.exe", "CON.exe", "bad?.exe"]):
        path = root / f"fixture-{index}.zip"
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("lis_workbench.exe", b"synthetic-app")
            archive.writestr("Updater.exe", b"synthetic-updater")
            if extra:
                archive.writestr(extra, b"bad")
        subprocess.run([sys.argv[1], str(path), "valid" if extra is None else "invalid"], check=True)
    path = root / "not-a-zip.zip"
    path.write_bytes(b"no zip data")
    subprocess.run([sys.argv[1], str(path), "invalid"], check=True)
print("C++ package validation: 11 fixtures passed")
