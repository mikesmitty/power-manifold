#!/usr/bin/env python3
"""Sign a controller image for OTA: controller.bin -> controller.signed.bin.

The signed image is the raw .bin with a 176-byte trailer (src/update_sig.h):
what the image is (its length, board and SHA-512), then an Ed25519 signature
over that description. Three ways to get the signature:

  sign IMAGE --kms-key projects/P/locations/L/keyRings/R/cryptoKeys/K/cryptoKeyVersions/V
        Cloud KMS, through gcloud (the release workflow)
  sign IMAGE --openssl-key private.pem
        a key file, through openssl (bench and test keys)
  sign IMAGE --yubikey
        the backup key in a YubiKey's PIV slot 9c, through ykman; asks for
        the PIN and a touch
  tbs IMAGE, then attach IMAGE --signature FILE
        any other signer (a hardware token): `tbs` writes the 112 bytes to
        sign, `attach` takes the 64-byte signature back, raw or base64
"""

import argparse
import base64
import binascii
import hashlib
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

MAGIC = b"PWRMSIG1"
BOARD_LEN = 32
TBS_LEN = 112
UF2_MAGIC = b"UF2\n"


def to_be_signed(image: bytes, board: str) -> bytes:
    if image.startswith(UF2_MAGIC):
        sys.exit("that is a UF2; sign the raw controller.bin")
    if image[-(TBS_LEN + 64) :].startswith(MAGIC):
        sys.exit("image is already signed")
    name = board.encode()
    if not name or len(name) >= BOARD_LEN:
        sys.exit(f"board name must be 1..{BOARD_LEN - 1} bytes")
    tbs = (
        MAGIC
        + struct.pack("<II", len(image), 0)
        + name.ljust(BOARD_LEN, b"\0")
        + hashlib.sha512(image).digest()
    )
    assert len(tbs) == TBS_LEN
    return tbs


def read_signature(path: pathlib.Path) -> bytes:
    sig = path.read_bytes()
    if len(sig) != 64:  # gcloud and some token tools write base64
        try:
            sig = base64.b64decode(b"".join(sig.split()), validate=True)
        except binascii.Error:
            pass
    if len(sig) != 64:
        sys.exit(f"{path}: not a 64-byte Ed25519 signature, raw or base64")
    return sig


def run(cmd: list[str]) -> None:
    try:
        subprocess.run(cmd, check=True)
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit(f"signing failed: {e}")


def kms_sign(tbs_path: pathlib.Path, sig_path: pathlib.Path, resource: str) -> None:
    m = re.fullmatch(
        r"projects/([^/]+)/locations/([^/]+)/keyRings/([^/]+)"
        r"/cryptoKeys/([^/]+)/cryptoKeyVersions/([^/]+)",
        resource,
    )
    if not m:
        sys.exit("--kms-key wants projects/…/locations/…/keyRings/…/cryptoKeys/…/cryptoKeyVersions/…")
    project, location, keyring, key, version = m.groups()
    # no --digest-algorithm: Ed25519 signs the bytes themselves
    run(["gcloud", "kms", "asymmetric-sign", f"--project={project}", f"--location={location}",
         f"--keyring={keyring}", f"--key={key}", f"--version={version}",
         f"--input-file={tbs_path}", f"--signature-file={sig_path}"])


def openssl_sign(tbs_path: pathlib.Path, sig_path: pathlib.Path, key: pathlib.Path) -> None:
    run(["openssl", "pkeyutl", "-sign", "-inkey", str(key), "-rawin",
         "-in", str(tbs_path), "-out", str(sig_path)])


def yubikey_sign(tbs_path: pathlib.Path, sig_path: pathlib.Path) -> None:
    script = pathlib.Path(__file__).with_name("yubikey_sign.py")
    run(["ykman", "script", "--force", str(script), str(tbs_path), str(sig_path)])


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["sign", "tbs", "attach"])
    ap.add_argument("image", type=pathlib.Path, help="the raw controller.bin")
    ap.add_argument("--board", required=True, help="PICO_BOARD the image was built for, e.g. pico2_w")
    ap.add_argument("-o", "--out", type=pathlib.Path, help="default: IMAGE with .signed.bin or .tbs")
    ap.add_argument("--kms-key", help="sign: Cloud KMS key version resource name")
    ap.add_argument("--openssl-key", type=pathlib.Path, help="sign: Ed25519 private key file (PEM)")
    ap.add_argument("--yubikey", action="store_true", help="sign: the key in a YubiKey's PIV slot 9c")
    ap.add_argument("--signature", type=pathlib.Path, help="attach: the signature over the tbs file")
    args = ap.parse_args()

    image = args.image.read_bytes()
    tbs = to_be_signed(image, args.board)

    if args.action == "tbs":
        out = args.out or args.image.with_suffix(".tbs")
        out.write_bytes(tbs)
        print(f"{out}: {TBS_LEN} bytes to sign (Ed25519, no prehash)")
        return

    if args.action == "attach":
        if not args.signature:
            sys.exit("attach needs --signature")
        sig = read_signature(args.signature)
    else:
        if bool(args.kms_key) + bool(args.openssl_key) + args.yubikey != 1:
            sys.exit("sign needs one of --kms-key, --openssl-key, --yubikey")
        with tempfile.TemporaryDirectory() as tmp:
            tbs_path = pathlib.Path(tmp, "image.tbs")
            sig_path = pathlib.Path(tmp, "image.sig")
            tbs_path.write_bytes(tbs)
            if args.kms_key:
                kms_sign(tbs_path, sig_path, args.kms_key)
            elif args.yubikey:
                yubikey_sign(tbs_path, sig_path)
            else:
                openssl_sign(tbs_path, sig_path, args.openssl_key)
            sig = read_signature(sig_path)

    out = args.out or args.image.with_suffix(".signed.bin")
    out.write_bytes(image + tbs + sig)
    print(f"{out}: {len(image)} bytes + trailer, board {args.board}")


if __name__ == "__main__":
    main()
