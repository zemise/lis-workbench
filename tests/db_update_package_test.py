import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("packager", ROOT / "scripts/db_update_package.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.zip = self.root / "source.zip"
        self.write_zip([("lis_workbench.exe", b"synthetic app"), ("Updater.exe", b"synthetic updater")])
        self.manifest = {"appId": "lis-workbench", "channel": "stable", "version": "v2026.10.12",
                         "minUpdaterVersion": "1.0.0", "notes": ["测试"],
                         "package": {"file": "LISWorkbench-v2026.10.12-win7-win11.zip",
                                     "size": self.zip.stat().st_size, "sha256": self.digest()}}

    def tearDown(self):
        self.tmp.cleanup()

    def write_zip(self, entries):
        with zipfile.ZipFile(self.zip, "w", compression=zipfile.ZIP_DEFLATED) as z:
            for name, data in entries:
                z.writestr(name, data)

    def digest(self):
        return hashlib.sha256(self.zip.read_bytes()).hexdigest()

    def test_valid_package(self):
        packager.validate_zip(self.zip, packager.validate_manifest(self.manifest))

    def test_release_versions(self):
        for version in ('v2026.10.12', 'v2026.10.12.1', 'v2026.10.12.10', 'v2024.02.29', 'v2026.10.12.999999999'):
            with self.subTest(version=version):
                m = copy.deepcopy(self.manifest)
                m['version'] = version
                m['package']['file'] = f'LISWorkbench-{version}-win7-win11.zip'
                m['minUpdaterVersion'] = '1.1.0'
                packager.validate_manifest(m)
        for version in ('v2026.02.29', 'v0000.01.01', 'v2026.10.12.0', 'v2026.10.12.01',
                        'v2026.10.12.1000000000', 'v2026.10.12.1.1', 'v2026.10.12.1\n'):
            self.assertFalse(packager.valid_version(version), version)

    def test_revision_offline_materials(self):
        self.manifest['version'] = 'v2026.10.12.4'
        self.manifest['minUpdaterVersion'] = '1.1.0'
        self.manifest['package']['file'] = 'LISWorkbench-v2026.10.12.4-win7-win11.zip'
        self.test_offline_materials_and_sql_size()

    def test_bad_metadata(self):
        for key, value in [("appId", "other"), ("channel", "preview"),
                           ("version", "../bad"), ("minUpdaterVersion", "2.0.0"), ("notes", "bad")]:
            with self.subTest(key=key):
                m = copy.deepcopy(self.manifest); m[key] = value
                with self.assertRaises(ValueError): packager.validate_manifest(m)

    def test_bad_package_metadata(self):
        for key, value in [("file", "../bad.zip"), ("size", True), ("size", 0),
                           ("size", packager.MAX_SIZE + 1), ("sha256", "z" * 64)]:
            with self.subTest(key=key):
                m = copy.deepcopy(self.manifest); m["package"][key] = value
                with self.assertRaises(ValueError): packager.validate_manifest(m)

    def test_unicode_notes_limit(self):
        self.manifest["notes"] = ["😀" * 1001]
        with self.assertRaises(ValueError): packager.validate_manifest(self.manifest)

    def test_corrupted_package(self):
        data = bytearray(self.zip.read_bytes()); data[35] ^= 1; self.zip.write_bytes(data)
        with self.assertRaises(ValueError): packager.validate_zip(self.zip, self.manifest["package"])

    def test_protected_and_unsafe_entries(self):
        for name in ["../evil.exe", "templates/file.exe", "ClientConfig.ini", "/evil.exe",
                     "C:evil.exe", "documents/a.dll", "folder/a.dll", "source.cpp"]:
            with self.subTest(name=name):
                self.write_zip([("lis_workbench.exe", b"a"), ("Updater.exe", b"b"), (name, b"c")])
                p = dict(self.manifest["package"], size=self.zip.stat().st_size, sha256=self.digest())
                with self.assertRaises(ValueError): packager.validate_zip(self.zip, p)

    def test_duplicate_windows_filename(self):
        self.write_zip([("lis_workbench.exe", b"a"), ("Updater.exe", b"b"), ("UPDATER.EXE", b"c")])
        p = dict(self.manifest["package"], size=self.zip.stat().st_size, sha256=self.digest())
        with self.assertRaises(ValueError): packager.validate_zip(self.zip, p)

    def release(self):
        return {"id": 42, "tag_name": self.manifest["version"], "draft": False, "prerelease": False,
                "published_at": "2026-10-12T00:00:00Z", "assets": [
                    {"id": i, "name": name, "size": size, "state": "uploaded", "updated_at": "fixed",
                     "browser_download_url": f"https://github.com/{packager.REPO}/releases/download/v2026.10.12/{name}"}
                    for i, (name, size) in enumerate([("manifest.json", 1), (self.manifest["package"]["file"], self.zip.stat().st_size)])]}

    def fake_download(self, asset, target, limit):
        if asset["name"] == "manifest.json": target.write_text(json.dumps(self.manifest), encoding="utf-8")
        else: shutil.copyfile(self.zip, target)

    def test_offline_materials_and_sql_size(self):
        release = self.release()
        with patch.object(packager, "api", return_value=release), patch.object(packager, "download", side_effect=self.fake_download):
            result = packager.prepare(self.root / "output")
        self.assertTrue((result / "upload.ps1").read_bytes().startswith(b"\xef\xbb\xbf"))
        self.assertFalse((result / "01-upload.sql").exists())
        sql = (result / "02-publish.sql").read_text(encoding="utf-8-sig")
        self.assertNotIn("__", sql)
        self.assertNotIn("TRY_CONVERT(", sql)
        self.assertIn("TRY_CAST(", sql)
        self.assertIn(str(self.manifest["package"]["size"]), sql)
        self.assertLess((result / "02-publish.sql").stat().st_size, 16384)
        info = json.loads((result / "release-info.json").read_text(encoding="utf-8"))
        self.assertEqual(info["releaseId"], 42)
        self.assertEqual((result / self.manifest["package"]["file"]).read_bytes(), self.zip.read_bytes())

    def test_latest_asset_change_leaves_no_publishable_directory(self):
        first = self.release(); second = copy.deepcopy(first); second["assets"][1]["id"] += 1
        with patch.object(packager, "api", side_effect=[first, second]), patch.object(packager, "download", side_effect=self.fake_download):
            with self.assertRaises(ValueError): packager.prepare(self.root / "output")
        self.assertFalse((self.root / "output" / self.manifest["version"]).exists())

    def test_pre_release_rejected(self):
        release = self.release(); release["prerelease"] = True
        with patch.object(packager, "api", return_value=release):
            with self.assertRaises(ValueError): packager.prepare(self.root / "output")

    def test_existing_materials_preserved(self):
        destination = self.root / "output" / self.manifest["version"]; destination.mkdir(parents=True)
        (destination / "keep").write_text("keep", encoding="utf-8")
        with patch.object(packager, "api", return_value=self.release()):
            with self.assertRaises(ValueError): packager.prepare(self.root / "output")
        self.assertEqual((destination / "keep").read_text(encoding="utf-8"), "keep")


class DatabaseBoundaryTests(unittest.TestCase):
    def test_no_operational_ddl_or_other_business_tables(self):
        import re
        paths = list((ROOT / "scripts/db_update").glob("*.sql")) + [ROOT / "scripts/db_update/upload.ps1", ROOT / "src/update_database.cpp"]
        for path in paths:
            with self.subTest(path=path.name):
                text = path.read_text(encoding="utf-8")
                self.assertEqual(set(re.findall(r"dbo\.\w+", text)), {"dbo.LS_AS_LISWORK"})
                self.assertFalse(re.search(r"\b(?:CREATE TABLE|ALTER DATABASE|DROP TABLE|TRUNCATE TABLE|DBCC|KILL|GRANT|REVOKE|BACKUP DATABASE|RESTORE DATABASE)\b", text, re.I))

    def test_publication_supports_older_database_compatibility(self):
        # Guard the SQL 195 regression in BOTH template and generated upload materials.
        # These checks are offline; actual SQL execution is reserved for the isolated test DB.
        text = (ROOT / 'scripts/db_update/02-publish.sql').read_text(encoding='utf-8')
        self.assertNotRegex(text, r'(?i)\bTRY_CONVERT\s*\(')
        self.assertIn("TRY_CAST(REPLACE(SUBSTRING(Version,2,10),'.','') AS date)", text)
        self.assertNotRegex(text, r'(?i)\bSET\s+(?:LANGUAGE|DATEFORMAT)\b')

    def test_runtime_cpp_is_read_only(self):
        import re
        text = (ROOT / "src/update_database.cpp").read_text(encoding="utf-8")
        self.assertNotRegex(text, r'(?i)\b(?:INSERT INTO|UPDATE dbo\.|DELETE FROM|CREATE TABLE)')
        self.assertIn("SQL_MODE_READ_ONLY", text)
        query = re.search(r'L"SELECT PackageId.*?";', text).group()
        self.assertNotIn("PackageData", query)
        self.assertNotIn("SELECT *", text)

    def test_cleanup_cannot_delete_current_or_staged(self):
        for path in [ROOT / "scripts/db_update/03-cleanup.sql", ROOT / "scripts/db_update/upload.ps1"]:
            text = path.read_text(encoding="utf-8")
            self.assertIn("State='retired'", text)
            self.assertIn("IsCurrent=0", text)
            self.assertIn("DATEADD(hour,-24", text)

    def test_mutations_refuse_cross_table_dependencies(self):
        for name in ("upload.ps1", "02-publish.sql", "03-cleanup.sql"):
            text = (ROOT / "scripts/db_update" / name).read_text(encoding="utf-8")
            self.assertIn("sys.triggers", text)
            self.assertIn("sys.foreign_keys", text)

    def test_password_never_enters_command_line_or_materials(self):
        text = (ROOT / "scripts/db_update/upload.ps1").read_text(encoding="utf-8")
        self.assertIn("-AsSecureString", text)
        self.assertIn("$password.MakeReadOnly()", text)
        self.assertIn("SqlCredential", text)
        self.assertNotIn("$builder.Password", text)
        self.assertNotIn("[string]$Password", text)


if __name__ == "__main__":
    unittest.main()
