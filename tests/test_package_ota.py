import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class EspIdfPackage(unittest.TestCase):
    def test_packages_native_build_through_private_pipe(self):
        key = bytes(range(32))  # Public fixture.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            output = root / "package"
            artifacts = {
                "cornelius-satellite.bin": b"0.5.0\0" + key,
                "bootloader/bootloader.bin": b"boot",
                "partition_table/partition-table.bin": b"table",
                "ota_data_initial.bin": b"initial",
            }
            for filename, data in artifacts.items():
                path = build / filename
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            tool = Path(__file__).resolve().parents[1] / "tools" / "package-ota.py"
            subprocess.run(
                [sys.executable, str(tool), "--build-dir", str(build),
                 "--output-dir", str(output), "--key-stdin", "--mac",
                 "00:11:22:33:44:55", "--version", "0.5.0"],
                input=key, check=True, capture_output=True,
            )
            self.assertEqual((output / "firmware.bin").read_bytes(), artifacts["cornelius-satellite.bin"])
            self.assertEqual((output / "bootloader.bin").read_bytes(), b"boot")
            self.assertEqual((output / "partitions.bin").read_bytes(), b"table")
            self.assertEqual((output / "ota_data_initial.bin").read_bytes(), b"initial")
            manifest = json.loads((output / "firmware.bin.ota.json").read_text())
            self.assertEqual(manifest["key_sha256"], hashlib.sha256(key).hexdigest())
