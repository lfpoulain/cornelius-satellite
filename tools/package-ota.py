"""Package a built application and bind it to the intended board and protected key."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-dir", required=True, type=Path)
parser.add_argument("--output-dir", required=True, type=Path)
keys = parser.add_mutually_exclusive_group(required=True)
keys.add_argument("--key-file", type=Path, help="Windows DPAPI key file")
keys.add_argument("--key-stdin", action="store_true", help="Read 32 raw key bytes from a private pipe")
parser.add_argument("--mac", required=True)
parser.add_argument("--version", required=True)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location("client", Path(__file__).with_name("ota-client.py"))
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)
key = sys.stdin.buffer.read(33) if args.key_stdin else client.dpapi(args.key_file.read_bytes())
artifacts = {
    "firmware.bin": "cornelius-satellite.bin",
    "bootloader.bin": "bootloader/bootloader.bin",
    "partitions.bin": "partition_table/partition-table.bin",
    "ota_data_initial.bin": "ota_data_initial.bin",
}
for relative in artifacts.values():
    if not (args.build_dir / relative).is_file():
        parser.error(f"Missing ESP-IDF build artifact: {relative}")
application = (args.build_dir / artifacts["firmware.bin"]).read_bytes()
if len(key) != 32 or key not in application:
    raise SystemExit("Application was built with a different board key; package refused")
if args.version.encode("ascii") + b"\0" not in application:
    raise SystemExit("Application version does not match; package refused")
args.output_dir.mkdir(parents=True, exist_ok=True)
for name, relative in artifacts.items():
    source = args.build_dir / relative
    destination = args.output_dir / name
    if source.resolve() != destination.resolve():
        shutil.copyfile(source, destination)
image = args.output_dir / "firmware.bin"
manifest = {"mac": args.mac.lower(), "version": args.version,
            "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
            "key_sha256": hashlib.sha256(key).hexdigest()}
image.with_suffix(".bin.ota.json").write_text(json.dumps(manifest, indent=2))
print("Board package prepared:", args.mac, args.version)
