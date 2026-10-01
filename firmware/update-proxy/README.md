# Update proxy

A Cloudflare Worker that serves the controller's signed release images over
plain HTTP. The controller's update pull has no TLS, and GitHub release
downloads are HTTPS-only behind a redirect; this sits between the two.

```
http://fw.powermanifold.io/controller/<x.y.z>/controller.signed.bin
```

maps to the `controller.signed.bin` asset of the
`controller-firmware-v<x.y.z>` release, and nothing else is served. The
Worker does not need to be trusted: a controller installs an image only when
its signature checks out (see "Signed updates" in the controller README).

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
   curl -sI http://fw.powermanifold.io/controller/<x.y.z>/controller.signed.bin
   ```

On the Workers Free plan the Worker stops answering past the daily request
limit rather than billing; updates are then unavailable until the limit
resets.

## Using it

Point a controller at a release from its console, or publish the retained
MQTT pointer that Home Assistant's update entity installs from:

```
update http://fw.powermanifold.io/controller/0.11.0/controller.signed.bin
```

```
pwrman/<name>/update/latest  {"version":"0.11.0","url":"http://fw.powermanifold.io/controller/0.11.0/controller.signed.bin"}
```

The controller's limits on the URL: 63 characters of host, 159 of path.

## Local run

`npx wrangler dev`, then `curl -sI http://127.0.0.1:8787/controller/<x.y.z>/controller.signed.bin`.
