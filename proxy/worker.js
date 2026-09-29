// Optional CORS proxy for the browser demo (Cloudflare Worker, free plan).
//
// Some of the public flight-data services do not allow requests from web pages.
// This worker forwards the demo's requests to them and adds the header browsers
// need. It only forwards to the services listed below.
//
// Usage from the page:  https://<your-worker>.workers.dev/?url=<encoded service URL>

const ALLOWED_HOSTS = [
  "api.adsb.lol",
  "opendata.adsb.fi",
  "api.airplanes.live",
  "api.adsbdb.com",
  "api.planespotters.net",
  "t.plnspttrs.net",
  "nominatim.openstreetmap.org",
];
// Only pages on this site may use the proxy. Change it to your own site ("*" allows any).
const ALLOWED_ORIGIN = "https://n41kh05.github.io";

const LIVE_HOSTS = ["api.adsb.lol", "opendata.adsb.fi", "api.airplanes.live"];

function corsHeaders() {
  return {
    "Access-Control-Allow-Origin": ALLOWED_ORIGIN,
    "Access-Control-Allow-Methods": "GET, OPTIONS",
    "Access-Control-Max-Age": "86400",
  };
}

export default {
  async fetch(request) {
    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: corsHeaders() });
    if (request.method !== "GET") return new Response("Method not allowed", { status: 405, headers: corsHeaders() });

    let target;
    try {
      target = new URL(new URL(request.url).searchParams.get("url") || "");
    } catch {
      return new Response("Missing or invalid ?url=", { status: 400, headers: corsHeaders() });
    }
    if (target.protocol !== "https:" || !ALLOWED_HOSTS.includes(target.hostname))
      return new Response("Host not allowed", { status: 403, headers: corsHeaders() });

    const live = LIVE_HOSTS.includes(target.hostname);
    const upstream = await fetch(target.toString(), {
      headers: {
        "User-Agent": "NottaMihinaSeLentaa-demo/1.0 (+https://github.com/N41KH05/notta-mihina-se-lentaa)",
        "Accept": "application/json, image/*",
      },
      // Short cache for positions (many visitors, same area), long for routes/photos/search.
      cf: { cacheTtl: live ? 3 : 3600, cacheEverything: true },
    });
    const headers = new Headers(upstream.headers);
    for (const [k, v] of Object.entries(corsHeaders())) headers.set(k, v);
    headers.delete("Set-Cookie");
    return new Response(upstream.body, { status: upstream.status, headers });
  },
};
