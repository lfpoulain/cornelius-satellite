"""Prepare a per-board OTA key on Windows; never prints key material.
The private directory and generated header MUST remain outside version control.
"""
import argparse
import importlib.util
from pathlib import Path
import secrets

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--key-file", type=Path, required=True)
parser.add_argument("--firmware-dir", type=Path, required=True)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location("client", Path(__file__).with_name("ota-client.py"))
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)
args.key_file.parent.mkdir(parents=True, exist_ok=True)
if not args.key_file.exists():
    args.key_file.write_bytes(client.dpapi(secrets.token_bytes(32), True))
key = client.dpapi(args.key_file.read_bytes())
if len(key) != 32:
    raise SystemExit("Invalid key length")
header = args.firmware_dir / "main" / "ota_key.h"
header.write_text("#pragma once\nstatic const unsigned char OTA_KEY[32] = {" + ",".join(str(b) for b in key) + "};\n")
exclude = args.firmware_dir / ".git" / "info" / "exclude"
if exclude.is_file() and "main/ota_key.h" not in exclude.read_text():
    with exclude.open("a") as stream:
        stream.write("\nmain/ota_key.h\n")
print("Board key prepared; retain the DPAPI file for future updates on this Windows account.")
