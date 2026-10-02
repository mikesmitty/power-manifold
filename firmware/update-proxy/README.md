# Update proxy

A Cloudflare Worker that serves the controller's signed release images over
plain HTTP. The controller's update pull has no TLS, and GitHub release
downloads are HTTPS-only behind a redirect; this sits between the two.

| Path | Answer |
|---|---|
| `/controller/<x.y.z>/<board>/controller.signed.bin` | the signed image of release `controller-firmware-v<x.y.z>` for a board: `pwrman_controller_card`, or `pico2_w` while that stand-in is still built |
| `/controller/<board>/latest.json` | `{"version":"x.y.z","url":"http://fw.powermanifold.io/controller/<x.y.z>/<board>/controller.signed.bin"}`: the newest release, once its signed image for the board exists |

Nothing else is served. The newest version is read from this repository's
`.release-please-manifest.json`; images are fetched from GitHub once and
answered from Cloudflare's cache afterwards, the pointer for ten minutes at a
time. The Worker does not need to be trusted: a controller installs an image
only when its signature checks out and it is no older than what it runs (see
"Signed updates" in the controller README).

## Deploying

1. The hostname is `fw.powermanifold.io` (`routes` in `wrangler.toml`); the
   `powermanifold.io` zone has to be in the Cloudflare account that deploys.
2. Let plain HTTP through for that hostname: a Configuration Rule that turns
   **Always Use HTTPS** off for it. Otherwise Cloudflare answers port 80 with
   a redirect, which the controller cannot follow.
3. `npx wrangler deploy`
4. Check what a controller will see — a `200` with a `Content-Length`, over
   HTTP:

   ```
   curl -si http://fw.powermanifold.io/controller/pwrman_controller_card/latest.json
   curl -sI http://fw.powermanifold.io/controller/<x.y.z>/pwrman_controller_card/controller.signed.bin
   ```

A firewall rule in front of the Worker has to let both path shapes through:
those under `/controller/` ending in `/controller.signed.bin` or
`/latest.json`.

On the Workers Free plan the Worker stops answering past the daily request
limit rather than billing; updates are then unavailable until the limit
resets.

## Using it

A controller asks `latest.json` once a day by itself (its `update_url`
setting is this host by default) and offers the release it names in Home
Assistant and on its console, where `update latest` installs it. A specific
release can be pulled by URL:

```
update http://fw.powermanifold.io/controller/0.12.0/pwrman_controller_card/controller.signed.bin
```

The controller's limits on the URL: 63 characters of host, 159 of path.

## Local run

`npx wrangler dev`, then `curl -si http://127.0.0.1:8787/controller/pwrman_controller_card/latest.json`.
