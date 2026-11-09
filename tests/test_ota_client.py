import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("ota_client", Path(__file__).resolve().parents[1] / "tools" / "ota-client.py")
client = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(client)


class UploadGuards(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.image = root / "firmware.bin"
        self.image.write_bytes(b"\xe9test image")
        self.key = bytes(range(32))  # Public deterministic test fixture, never a device key.
        self.key_file = root / "test.dpapi"
        self.key_file.write_bytes(b"mock protected data")
        self.mac = "00:11:22:33:44:55"
        self.manifest = {"mac": self.mac, "version": "test", "sha256": hashlib.sha256(self.image.read_bytes()).hexdigest(), "key_sha256": hashlib.sha256(self.key).hexdigest()}
        self.save_manifest()

    def save_manifest(self):
        self.image.with_suffix(".bin.ota.json").write_text(json.dumps(self.manifest))

    def reject(self, expected, status=None):
        status = status or {"mac": self.mac, "online": True, "idle": True, "nonce": "a" * 64}
        argv = ["ota-client", "--host", "example.invalid", "--mac", self.mac, "--key-file", str(self.key_file), "--image", str(self.image), "--expected-version", "test"]
        with patch.object(sys, "argv", argv), patch.object(client, "fetch", return_value=status), patch.object(client, "dpapi", return_value=self.key), patch.object(client.urllib.request, "urlopen") as send:
            with self.assertRaisesRegex(SystemExit, expected):
                client.main()
            send.assert_not_called()

    def test_wrong_physical_board(self):
        self.reject("Wrong board", {"mac": "00:00:00:00:00:00"})

    def test_busy_board(self):
        self.reject("busy or offline", {"mac": self.mac, "online": True, "idle": False})

    def test_missing_manifest(self):
        self.image.with_suffix(".bin.ota.json").unlink()
        self.reject("Missing board manifest")

    def test_modified_image(self):
        self.image.write_bytes(b"\xe9modified")
        self.reject("do not match")

    def test_wrong_manifest_fields(self):
        for field in ("mac", "version", "key_sha256"):
            with self.subTest(field=field):
                original = self.manifest[field]
                self.manifest[field] = "different"
                self.save_manifest()
                self.reject("do not match")
                self.manifest[field] = original
                self.save_manifest()


if __name__ == "__main__":
    unittest.main()
