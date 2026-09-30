# Optional CORS proxy for the browser demo

The browser demo (`docs/simulator.html`) shows simulated traffic by default. The public aircraft-position services (adsb.lol, adsb.fi, airplanes.live) do not send the CORS headers that browsers require, so a web page cannot fetch them directly.

To show live traffic in the demo, deploy this small Cloudflare Worker (free plan) and point the demo at it. A **Live data / Simulated** switch then appears on the page.

1. Create a Worker in the Cloudflare dashboard (**Workers & Pages → Create → Worker**), paste the contents of [`worker.js`](worker.js), and deploy it.
2. Set `ALLOWED_ORIGIN` in the worker to your own site (e.g. `https://<user>.github.io`), so that only your page can use it.
3. In `docs/simulator.js`, set `const LIVE_PROXY = "https://<name>.<account>.workers.dev/";`.

   To try a proxy without rebuilding, add it to the page address instead: `simulator.html?proxy=https://<name>.<account>.workers.dev/`

The worker forwards requests only to the hosts listed in `ALLOWED_HOSTS`. Aircraft positions are cached for 3 seconds, so many visitors looking at the same area make few upstream requests. Routes, photos and address searches are cached for an hour.

Use of the upstream services is subject to their own terms. The live-position services are free for personal, non-commercial use.
