// Plain-HTTP front for the controller's signed release images.
//
// A controller pulls updates over plain HTTP (no TLS on that stack) and
// GitHub serves release downloads over HTTPS only, behind a redirect. This
// Worker fetches from GitHub and answers with one fixed-length response,
// which the controller's HTTP client requires:
//
//   /controller/<x.y.z>/<board>/controller.signed.bin
//       the signed image of release controller-firmware-v<x.y.z> for a board
//   /controller/<board>/latest.json
//       {"version":"x.y.z","url":"http://<host>/controller/<x.y.z>/<board>/controller.signed.bin"}
//       the newest release that has a signed image for the board; a
//       controller asks once a day (src/net/update_check.h)
//
// Nothing here has to be trusted: the controller installs an image only when
// its signature checks out and it is no older than what it runs
// (firmware/controller/docs/flash-and-updates.md, "Signed updates"). The
// Worker serves these assets of this one repository and nothing else, so it
// is no use as a general proxy.

const REPO = "mikesmitty/power-manifold";
const TAG_PREFIX = "controller-firmware-v";
// board (PICO_BOARD) -> its signed image among a release's assets
const BOARDS = {
  pwrman_controller_card: "controller.signed.bin",
  pico2_w: "controller-pico2w.signed.bin", // development stand-in for the card
};
// release-please keeps the released version of every package here
const MANIFEST = `https://raw.githubusercontent.com/${REPO}/main/.release-please-manifest.json`;
const MANIFEST_KEY = "firmware/controller";

const VERSION = "(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})";
const BOARD = "([a-z0-9_]{1,31})";
const IMAGE_ROUTE = new RegExp(`^/controller/${VERSION}/${BOARD}/controller\\.signed\\.bin$`);
const LATEST_ROUTE = new RegExp(`^/controller/${BOARD}/latest\\.json$`);
const MAX_BYTES = 4 * 1024 * 1024; // the largest image slot
const LATEST_TTL_S = 600;

const UA = { "user-agent": "power-manifold-update-proxy" };

const text = (status, body, headers = {}) =>
  new Response(body + "\n", { status, headers: { "content-type": "text/plain", ...headers } });

const imagePath = (version, board) => `/controller/${version}/${board}/controller.signed.bin`;

// One board's signed image of one release: from the cache, or from GitHub
// once and then the cache. A release asset never changes. The key carries no
// query string, so one cannot be used to get around the cache.
async function image(origin, version, board, ctx) {
  const cache = caches.default;
  const key = new Request(`https://${origin.hostname}${imagePath(version, board)}`);
  const hit = await cache.match(key);
  if (hit) return hit;

  const upstream = await fetch(
    `https://github.com/${REPO}/releases/download/${TAG_PREFIX}${version}/${BOARDS[board]}`,
    { headers: UA },
  );
  if (upstream.status === 404) return text(404, "no such release, or it has no signed image for this board");
  if (!upstream.ok) return text(502, `github answered ${upstream.status}`);
  if (Number(upstream.headers.get("content-length")) > MAX_BYTES) return text(502, "asset too large");

  // Held whole (well under a megabyte) so the response has a fixed length.
  const bytes = await upstream.arrayBuffer();
  if (bytes.byteLength > MAX_BYTES) return text(502, "asset too large");
  const response = new Response(bytes, {
    headers: {
      "content-type": "application/octet-stream",
      "cache-control": "public, max-age=31536000, immutable",
    },
  });
  ctx.waitUntil(cache.put(key, response.clone()));
  return response;
}

// The newest release, named only once its signed image for the board can be
// served: for the few minutes between a release and its signing job, and for
// a release whose signing failed, there is nothing to point at yet.
async function latest(origin, board, ctx) {
  const cache = caches.default;
  const key = new Request(`https://${origin.hostname}/controller/${board}/latest.json`);
  const hit = await cache.match(key);
  if (hit) return hit;

  const manifest = await fetch(MANIFEST, { headers: UA });
  if (!manifest.ok) return text(502, `github answered ${manifest.status}`);
  let version;
  try {
    version = (await manifest.json())[MANIFEST_KEY];
  } catch {
    version = undefined;
  }
  if (typeof version !== "string" || !new RegExp(`^${VERSION}$`).test(version))
    return text(502, "no controller firmware version in the release manifest");

  const signed = await image(origin, version, board, ctx);
  if (!signed.ok) return text(404, `release ${version} has no signed image for this board (yet)`);

  const body = JSON.stringify({ version, url: `http://${origin.host}${imagePath(version, board)}` });
  const response = new Response(body, {
    headers: { "content-type": "application/json", "cache-control": `public, max-age=${LATEST_TTL_S}` },
  });
  ctx.waitUntil(cache.put(key, response.clone()));
  return response;
}

export default {
  async fetch(request, env, ctx) {
    if (request.method !== "GET" && request.method !== "HEAD")
      return text(405, "method not allowed", { allow: "GET, HEAD" });

    const url = new URL(request.url);
    let match;
    if ((match = IMAGE_ROUTE.exec(url.pathname)))
      return Object.hasOwn(BOARDS, match[2]) ? image(url, match[1], match[2], ctx) : text(404, "unknown board");
    if ((match = LATEST_ROUTE.exec(url.pathname)))
      return Object.hasOwn(BOARDS, match[1]) ? latest(url, match[1], ctx) : text(404, "unknown board");
    return text(404, "not found");
  },
};
