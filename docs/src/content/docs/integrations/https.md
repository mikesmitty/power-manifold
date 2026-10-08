---
title: HTTPS
description: Serve the web interface and the API over HTTPS with your own certificate, installed and renewed with acme.sh.
sidebar:
  order: 4
---

By default the web interface and the API use plain HTTP. On plain HTTP, the
API token and the passwords in **Settings** cross the network unencrypted.
With HTTPS on, the controller serves everything on port 443 with a
certificate you install, and port 80 only redirects to it.

The controller does not get a certificate by itself. You need:

- **A DNS name for the controller** that resolves to its address on your
  network, such as `pwrman.home.example.com`. This is usually a record on
  your router or local DNS server.
- **A certificate for that name with an ECDSA key** (P-256 or P-384). RSA
  keys are refused because signing with one takes the controller too long.
  Let's Encrypt, ZeroSSL and most private certificate authorities issue
  ECDSA certificates.

A certificate for a name on your own network comes from a public certificate
authority through a DNS challenge, which needs a domain you own at a DNS
provider with an API. [acme.sh](https://github.com/acmesh-official/acme.sh)
supports most providers and can install the certificate on the controller
and renew it there.

## With acme.sh

1. Copy the controller's deploy hook,
   [`pwrman.sh`](https://github.com/mikesmitty/power-manifold/blob/main/firmware/controller/tools/acme/pwrman.sh),
   to `~/.acme.sh/deploy/pwrman.sh`.
2. Issue the certificate with your DNS provider's plugin. ECDSA P-256 is
   acme.sh's default key type. For example, with Cloudflare:

   ```sh
   acme.sh --issue --dns dns_cf -d pwrman.home.example.com
   ```

3. Install it on the controller. HTTPS is not on yet, so this first install
   goes over plain HTTP to the controller's IP address:

   ```sh
   PWRMAN_TOKEN=<api token> PWRMAN_HOST=192.168.1.50 PWRMAN_HTTP=1 \
     acme.sh --deploy -d pwrman.home.example.com --deploy-hook pwrman
   ```

4. Turn HTTPS on: in **Settings**, **Network**, set **HTTPS** to *On* and
   press **Save**. The page reopens at `https://`.
5. Set the hook to use the name from now on, so renewals go over HTTPS:

   ```sh
   PWRMAN_TOKEN=<api token> PWRMAN_HOST=pwrman.home.example.com \
     acme.sh --deploy -d pwrman.home.example.com --deploy-hook pwrman
   ```

acme.sh saves the token and the host after a successful install. It renews
the certificate before it expires and installs the new one on the controller
each time, over HTTPS. The controller uses a new certificate for new
connections immediately, without a restart.

| Variable | Meaning |
|---|---|
| `PWRMAN_TOKEN` | The controller's API token. Saved. |
| `PWRMAN_HOST` | The name or address the hook connects to. Defaults to the certificate's name, which must be set for a wildcard certificate. Saved. |
| `PWRMAN_HTTP` | `1` sends over plain HTTP, for the first install only. Not saved. |

## With other tools

The controller takes the private key and the certificate chain in one
`POST /api/v1/tls`, as PEM text in any order, with the server's own
certificate first among the certificates. Any tool that can run a command
after a renewal can install it. For example, a certbot deploy hook:

```sh
cat "$RENEWED_LINEAGE/privkey.pem" "$RENEWED_LINEAGE/fullchain.pem" |
  curl --fail -H "Authorization: Bearer $PWRMAN_TOKEN" \
    -H 'Content-Type: application/x-pem-file' --data-binary @- \
    https://pwrman.home.example.com/api/v1/tls
```

certbot issues ECDSA keys by default from version 2.0.

You can also install the files from the web interface: in **Settings**,
**Network**, press **Install certificate…** and choose the key and the full
chain files together.

## What changes with HTTPS on

- **Port 80** answers every request with a redirect (307) to the same
  address over `https://`. It no longer serves the page or the API.
- **The web interface** asks for the API token again the first time it is
  opened over HTTPS, because the browser keeps the token per address.
- **Names the certificate covers** are accepted as
  [host names](/integrations/network/#host-names) without adding them to
  **Host names**. A wildcard certificate covers one level of names.
- **Opening the controller by IP address** still works, but the browser
  warns that the certificate is for another name.
- **Scripts and monitoring** need `https://` URLs. A Prometheus scrape
  follows the redirect if the job allows redirects.
- **Home Assistant** is unaffected: it uses MQTT.

## Expiry

The certificate's names, issuer and expiry date are shown under **HTTPS** in
**Settings**, by the `https` console command and by `GET /api/v1/tls`. The
Prometheus metric `pwrman_https_certificate_expiry_seconds` gives the
expiry time.

From 7 days before the certificate expires, the controller reports a problem
on the web interface and in Home Assistant, because a renewal normally
replaces it well before then. An expired certificate is still served:
browsers warn, and the controller stays reachable to install a new one.

## Turning it off

Set **HTTPS** to *Off* in **Settings**, or send `{"https": false}` to
`POST /api/v1/settings`. If the certificate's name no longer resolves, open
the controller by its IP address and accept the browser's warning that the
certificate is for another name.

To remove the certificate, turn HTTPS off first, then press **Remove
certificate** or send `POST /api/v1/tls/remove`.

## Backups and resets

Settings exports do not include the certificate, its key or the HTTPS
setting, so an import onto another controller leaves it on plain HTTP. A
[factory reset](/guide/front-panel/#the-button) erases the certificate.
