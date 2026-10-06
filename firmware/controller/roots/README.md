# Let's Encrypt roots

The CA certificates in this directory are built into the controller image by
`tools/letsencrypt_roots.py`, which CMake runs for every build. A verified TLS
link to a broker configured by name trusts them when no certificate is
installed in the settings; a broker configured by address, or one whose
certificate did not come from Let's Encrypt, needs its certificate installed
instead. The TLS section of the controller README has the whole rule.

They are the four roots published at <https://letsencrypt.org/certificates/>
as of 2026-10-04: the two current ones and the two of the next generation,
which Let's Encrypt cross-signs from the current ones until the root programs
carry them.

| File | Subject | Key | Valid to | SHA-256 fingerprint |
| --- | --- | --- | --- | --- |
| `isrg-root-x1.pem` | ISRG Root X1 | RSA 4096 | 2035-06-04 | `96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6` |
| `isrg-root-x2.pem` | ISRG Root X2 | ECDSA P-384 | 2040-09-17 | `69:72:9B:8E:15:A8:6E:FC:17:7A:57:AF:B7:17:1D:FC:64:AD:D2:8C:2F:CA:8C:F1:50:7E:34:45:3C:CB:14:70` |
| `isrg-root-ye.pem` | Root YE | ECDSA P-384 | 2045-09-02 | `E1:4F:FC:AD:5B:00:25:73:10:06:CA:A4:3A:12:1A:22:D8:E9:70:0F:4F:B9:CF:85:2F:02:A7:08:AA:5D:56:66` |
| `isrg-root-yr.pem` | Root YR | RSA 4096 | 2045-09-02 | `E5:7B:7E:6F:15:0C:41:91:02:E8:D5:C0:55:72:9F:F9:67:B9:D1:A8:29:BF:00:CE:C8:9C:A6:04:EB:F4:A8:6F` |

Limit this directory to the roots the firmware should trust by default.
Anyone can get a certificate from a public CA for a name they control, so
every root here widens who could stand in for a broker whose name they also
control. Let's Encrypt is included because it issued the certificates of
most brokers that have one. To add or replace a root, download its
PEM from the page above, check the fingerprint printed by

```sh
openssl x509 -in roots/<file>.pem -noout -fingerprint -sha256
```

against the page, drop the file here and rebuild. An expired root uses only
flash space and can be removed at the next cleanup.
