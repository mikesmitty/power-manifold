"""Sign the 112 bytes of `sign_image.py tbs` with the Ed25519 key in a
YubiKey's PIV signature slot (9c): the backup update key.

Runs inside YubiKey Manager, which brings the libraries:

    ykman script tools/yubikey_sign.py controller.tbs controller.sig

It asks for the PIV PIN and then waits for a touch. sign_image.py does this
for you with `sign --yubikey`.
"""

import getpass
import pathlib
import sys

from ykman.device import list_all_devices
from yubikit.core.smartcard import SmartCardConnection
from yubikit.piv import KEY_TYPE, SLOT, PivSession

TBS_LEN = 112


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"usage: ykman script {sys.argv[0]} <tbs-file> <signature-file>")
    tbs = pathlib.Path(sys.argv[1]).read_bytes()
    if len(tbs) != TBS_LEN or not tbs.startswith(b"PWRMSIG1"):
        sys.exit(f"{sys.argv[1]}: not the output of `sign_image.py tbs`")

    devices = list_all_devices()
    if len(devices) != 1:
        sys.exit(f"want exactly one YubiKey plugged in, found {len(devices)}")
    device, info = devices[0]
    with device.open_connection(SmartCardConnection) as connection:
        piv = PivSession(connection)
        if piv.get_slot_metadata(SLOT.SIGNATURE).key_type != KEY_TYPE.ED25519:
            sys.exit("slot 9c does not hold an Ed25519 key")
        piv.verify_pin(getpass.getpass(f"PIV PIN for YubiKey {info.serial}: "))
        print("touch the YubiKey...", file=sys.stderr)
        signature = piv.sign(SLOT.SIGNATURE, KEY_TYPE.ED25519, tbs, None)

    pathlib.Path(sys.argv[2]).write_bytes(signature)


main()
