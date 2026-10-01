# Update signing keys

Every `*.pem` in this directory is an Ed25519 **public** key that the
firmware trusts: an image installs over the network only when one of them
signed it (see "Signed updates" in the controller README). The build turns
them into `update_keys.h` (`tools/update_keys.py`); with no keys here, a
build accepts unsigned images and says so.

Private keys never belong in this repository.

## Adding a key

Export the public key as PEM (`-----BEGIN PUBLIC KEY-----`) and commit it
under a name that says where the private half lives:

```
gcloud kms keys versions get-public-key <version> --key <key> \
    --keyring <keyring> --location <location> --output-file keys/kms-primary.pem
```

Keep two: the one the release workflow signs with, and an offline backup
whose only job is to sign the release that replaces a lost or leaked primary.

## Retiring a key

Delete its file and release. That release has to be signed by a key the
fielded firmware still trusts, which is what the backup is for. A controller
that never takes the release keeps trusting the old key.

## Signing

The release workflow signs `controller.bin` with Cloud KMS
(`.github/workflows/release-please.yml`) and checks the result against the
keys here before publishing it. It reads two variables from the
`firmware-signing` environment:

| Variable | Value |
|---|---|
| `UPDATE_SIGNING_KMS_KEY` | `projects/…/locations/…/keyRings/…/cryptoKeys/…/cryptoKeyVersions/…` |
| `GCP_WORKLOAD_IDENTITY_PROVIDER` | `projects/…/locations/global/workloadIdentityPools/…/providers/…` |

There is no service account and no stored credential. The provider accepts
GitHub's OIDC tokens for this repository only (by repository ID), and the
key's `roles/cloudkms.signer` is granted to the pool's
`attribute.environment/firmware-signing` principal set: a job signs only by
running in that environment, so the environment's protection rules are what
guard the key.

Until `UPDATE_SIGNING_KMS_KEY` is set, releases are published unsigned.

By hand, with any signer that produces a plain (not prehashed) Ed25519
signature:

```
tools/sign_image.py tbs build/controller.bin --board pico2_w      # -> controller.tbs, 112 bytes
<sign controller.tbs, giving a 64-byte signature, raw or base64>
tools/sign_image.py attach build/controller.bin --board pico2_w --signature controller.sig
test/build/verify_image build/controller.signed.bin pico2_w       # the firmware's check, these keys
```
