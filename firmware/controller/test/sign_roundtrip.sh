#!/bin/sh
# tools/sign_image.py against the firmware's verifier: an image signed with a
# throwaway key must verify under that key, and must not once it is touched,
# signed for another board, or checked against another key.
#
#   sign_roundtrip.sh <controller-dir> <verify_image> <python3>
set -eu

dir=$1 verify=$2 python=$3
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pubhex() { openssl pkey -in "$1" -pubout -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n'; }
refused() { if "$@" 2>/dev/null; then echo "FAIL: accepted: $*"; exit 1; fi; }

openssl genpkey -algorithm ed25519 -out "$tmp/key.pem"
openssl genpkey -algorithm ed25519 -out "$tmp/other.pem"

# a stand-in image: an IMAGE_DEF block (version 1.2.3) in its first sector
"$python" - "$tmp/image.bin" <<'PY'
import struct, sys
block = struct.pack("<7I", 0xffffded3, 0x42 | 1 << 8 | 0x1021 << 16, 0x48 | 2 << 8,
                    1 << 16 | 2 << 8 | 3, 0xff | 3 << 8, 0, 0xab123579)
image = bytes(0x134) + block + bytes(range(256)) * 40
open(sys.argv[1], "wb").write(image)
PY

"$python" "$dir/tools/sign_image.py" sign "$tmp/image.bin" --board pico2_w \
    --openssl-key "$tmp/key.pem" -o "$tmp/signed.bin"
"$verify" "$tmp/signed.bin" pico2_w "$(pubhex "$tmp/key.pem")"

# the by-hand route a hardware token takes gives the same file
"$python" "$dir/tools/sign_image.py" tbs "$tmp/image.bin" --board pico2_w -o "$tmp/image.tbs"
openssl pkeyutl -sign -inkey "$tmp/key.pem" -rawin -in "$tmp/image.tbs" | openssl base64 > "$tmp/image.sig"
"$python" "$dir/tools/sign_image.py" attach "$tmp/image.bin" --board pico2_w \
    --signature "$tmp/image.sig" -o "$tmp/attached.bin"
cmp "$tmp/signed.bin" "$tmp/attached.bin"

refused "$verify" "$tmp/signed.bin" pico2_w "$(pubhex "$tmp/other.pem")"
refused "$verify" "$tmp/signed.bin" pwrman_controller_card "$(pubhex "$tmp/key.pem")"
"$python" - "$tmp/signed.bin" "$tmp/touched.bin" <<'PY'
import sys
image = bytearray(open(sys.argv[1], "rb").read())
image[1000] ^= 1
open(sys.argv[2], "wb").write(image)
PY
refused "$verify" "$tmp/touched.bin" pico2_w "$(pubhex "$tmp/key.pem")"
echo "sign round trip ok"
