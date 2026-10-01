// Plain-HTTP front for the controller's signed release images.
//
// A controller pulls updates over plain HTTP (no TLS on that stack) and
// GitHub serves release downloads over HTTPS only, behind a redirect. This
// Worker fetches the asset from GitHub and hands it over as one response
// with a Content-Length, which the controller's pull insists on:
//
//   http://<host>/controller/<x.y.z>/controller.signed.bin
//     -> github.com/<REPO>/releases/download/controller-firmware-v<x.y.z>/controller.signed.bin
//
// Nothing here has to be trusted: the controller installs an image only when
// its signature checks out (firmware/controller/README.md, "Signed updates").
// The Worker serves that one asset of this one repository and nothing else,
// so it is no use as a general proxy.

const REPO = "mikesmitty/power-manifold";
const ASSET = "controller.signed.bin";
const ROUTE = new RegExp(`^/controller/(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})/${ASSET.replaceAll(".", "\\.")}$`);
const MAX_BYTES = 4 * 1024 * 1024; // the largest image slot

const text = (status, body, headers = {}) =>
  new Response(body + "\n", { status, headers: { "content-type": "text/plain", ...headers } });

export default {
  async fetch(request, env, ctx) {
    if (request.method !== "GET" && request.method !== "HEAD")
      return text(405, "method not allowed", { allow: "GET, HEAD" });

    const url = new URL(request.url);
    const match = ROUTE.exec(url.pathname);
    if (!match) return text(404, "not found");

    // A release asset never changes, so one fetch from GitHub serves every
    // later request. The key leaves the query string out: it cannot be used
    // to get around the cache.
    const cache = caches.default;
    const key = new Request(`https://${url.hostname}${url.pathname}`);
    const hit = await cache.match(key);
    if (hit) return hit;

    const upstream = await fetch(
      `https://github.com/${REPO}/releases/download/controller-firmware-v${match[1]}/${ASSET}`,
      { headers: { "user-agent": "power-manifold-update-proxy" } },
    );
    if (upstream.status === 404) return text(404, "no such release, or it has no signed image");
    if (!upstream.ok) return text(502, `github answered ${upstream.status}`);
    if (Number(upstream.headers.get("content-length")) > MAX_BYTES) return text(502, "asset too large");

    // Held whole (well under a megabyte) so the response has a fixed length.
    const image = await upstream.arrayBuffer();
    if (image.byteLength > MAX_BYTES) return text(502, "asset too large");
    const response = new Response(image, {
      headers: {
        "content-type": "application/octet-stream",
        "cache-control": "public, max-age=31536000, immutable",
      },
    });
    ctx.waitUntil(cache.put(key, response.clone()));
    return response;
  },
};
