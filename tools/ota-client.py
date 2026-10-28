"""Authenticated LAN OTA uploader. The per-board key stays in Windows DPAPI storage.
Run with Python on the same Windows account used for provisioning.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import hmac
import json
from pathlib import Path
import time
import urllib.request
import urllib.error


class Blob(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]


def dpapi(data, protect=False):
    storage = ctypes.create_string_buffer(data)
    source = Blob(len(data), ctypes.cast(storage, ctypes.POINTER(ctypes.c_ubyte)))
    target = Blob()
    crypt = ctypes.windll.crypt32
    if protect:
        ok = crypt.CryptProtectData(ctypes.byref(source), "Cornelius OTA", None, None, None, 1, ctypes.byref(target))
    else:
        ok = crypt.CryptUnprotectData(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(target))
    if not ok:
        raise ctypes.WinError()
    try:
        return ctypes.string_at(target.data, target.size)
    finally:
        ctypes.windll.kernel32.LocalFree(target.data)


def fetch(base):
    with urllib.request.urlopen(base + "/ota/status", timeout=8) as response:
        return json.load(response)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--mac", required=True, help="Expected physical board MAC")
    parser.add_argument("--key-file", required=True, type=Path)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--expected-version", help="Required for an actual upload")
    args = parser.parse_args()
    base = "http://" + args.host + ":8081"
    before = fetch(base)
    if before["mac"].lower() != args.mac.lower():
        raise SystemExit("Wrong board: no upload performed")
    print(json.dumps(before | {"nonce": "[omitted]"}))
    if not args.image:
        return
    if not args.expected_version:
        raise SystemExit("--expected-version is required")
    if not before["online"] or not before["idle"]:
        raise SystemExit("Satellite busy or offline; finish the conversation first")
    image = args.image.read_bytes()
    if not image or image[0] != 0xE9:
        raise SystemExit("Not an ESP application image")
    digest = hashlib.sha256(image).hexdigest()
    key = dpapi(args.key_file.read_bytes())
    manifest_path = args.image.with_suffix(args.image.suffix + ".ota.json")
    if not manifest_path.exists():
        raise SystemExit("Missing board manifest: package the built image before uploading")
    manifest = json.loads(manifest_path.read_text())
    if (manifest.get("mac", "").lower() != args.mac.lower()
            or manifest.get("sha256") != digest
            or manifest.get("key_sha256") != hashlib.sha256(key).hexdigest()
            or manifest.get("version") != args.expected_version):
        raise SystemExit("Image, key, version or destination do not match the board manifest; no upload performed")
    message = f'{before["nonce"]}:{digest}:{len(image)}'.encode("ascii")
    signature = hmac.new(key, message, hashlib.sha256).hexdigest()
    request = urllib.request.Request(base + "/ota/firmware", data=image, method="POST", headers={
        "Content-Type": "application/octet-stream", "X-Image-SHA256": digest, "X-OTA-Signature": signature,
    })
    with urllib.request.urlopen(request, timeout=190) as response:
        print(response.read().decode())
    print("Waiting for reboot and Cornelius self-test...")
    time.sleep(4)
    deadline = time.monotonic() + 115
    stable_since = None
    while time.monotonic() < deadline:
        try:
            after = fetch(base)
            if after["mac"].lower() != args.mac.lower():
                raise SystemExit("Wrong device after reboot")
            if after["version"] == args.expected_version and after["online"] and after["partition"] != before["partition"] and after.get("ota_state") == 2:
                stable_since = stable_since or time.monotonic()
                if time.monotonic() - stable_since >= 15:
                    print(json.dumps(after | {"nonce": "[omitted]", "verified": True, "image_sha256": digest}))
                    return
            else:
                stable_since = None
        except (OSError, urllib.error.URLError):
            stable_since = None
        time.sleep(2)
    raise SystemExit("Update not validated: inspect version and rollback state; do not blindly retry")


if __name__ == "__main__":
    main()
