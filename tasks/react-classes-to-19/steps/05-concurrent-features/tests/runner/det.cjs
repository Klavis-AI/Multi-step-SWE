// Determinism preload for the mock API server. Loaded with `node --require`,
// both when the baseline transcript is recorded against the pristine server and
// when it is replayed against a candidate's. Three effects, applied identically
// on both sides:
//   1. Date is frozen to FIXED_MS.
//   2. Math.random is a mulberry32 PRNG seeded per request from sha256 of the
//      request and held in that request's async context, so a response is a pure
//      function of the request and does not depend on how many requests came
//      before it or on what else is in flight. The key is method + ' ' + url,
//      except on the two routes that answer randomly, where it is
//      method + ' ' + route - see ROUTE_SEEDED below.
//   3. Every response is held back by DELAY_MS. The chart components start their
//      fetch while mounting and raise the echarts loading mask once mounted, so
//      against a loopback server that answers in under a millisecond it is a
//      coin toss whether the data arrives before the mask goes up - and when it
//      does, the mask is never lowered and stays in the drawing. The delay puts
//      the response unambiguously after mount, the same way at recording and at
//      replay, which is the only thing that decides that race. The two orderings
//      are a property of the component, not of how it is written, so the same
//      delay settles it for the class version and the hook version alike.
'use strict';
const http = require('http');
const crypto = require('crypto');
const { AsyncLocalStorage } = require('async_hooks');

const FIXED_MS = Date.UTC(2024, 0, 15, 10, 0, 0); // 2024-01-15T10:00:00Z

const RealDate = Date;
function FrozenDate(...args) {
  if (!(this instanceof FrozenDate)) return new RealDate(FIXED_MS).toString();
  if (args.length === 0) return new RealDate(FIXED_MS);
  return new RealDate(...args);
}
FrozenDate.prototype = RealDate.prototype;
FrozenDate.now = () => FIXED_MS;
FrozenDate.parse = RealDate.parse;
FrozenDate.UTC = RealDate.UTC;
global.Date = FrozenDate;

function mulberry32(a) {
  return function () {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// The generator lives in the request's async context, not in a module-level
// variable.  A module-level `rng` reseeded immediately before `handler(req,res)`
// looks request-scoped and is not: app.js mounts `express.static` ahead of every
// API router, so a request that misses in public/ - which every API request does
// - reaches its route only after send()'s fs.stat callback, a tick or more later.
// Two API requests whose 250 ms timers fire in the same tick therefore both
// reseed before either one draws, and the first one answers with the second
// one's seed.  That is not hypothetical: it is what made c-pick-two-days and
// d-stat-range-two-digit trade answers under an 8-worker load, one asking
// statisticsAvgInit and the other statisticsPeakInit.  AsyncLocalStorage follows
// the request across those callbacks, so each handler draws from its own seed no
// matter how many ticks or how much concurrency sit in between.
const rngStore = new AsyncLocalStorage();
const bootRng = mulberry32(0x12345678);
Math.random = () => (rngStore.getStore() || bootRng)();

// Two routes - statisticsAvgInit and statisticsPeakInit - pick between two
// different payloads with `Math.random() > 0.5`, and they are the only two that
// do.  Seeding per URL makes each one's *answer* stable but not the *rendering*:
// a scenario that has two of them in flight at once - the mount-time init for
// `day` and the range the user just searched - writes both into the same store
// slot, so what the chart ends up showing is decided by which response lands
// last.  That is network scheduling, not application logic, and it inverted
// under an 8-worker load: the same suite recorded twice disagreed on
// m-stat-avg-query, where `day` answers "2017-04-12 04:32:42" and the searched
// range answers "2017-04-12".
//
// Seed those two routes from the route rather than the URL, so every `:time`
// answers identically and the arrival order stops being observable.  Nothing is
// concealed by this: which URLs the page requested is itself a compared field
// (compare.mjs), so a migration that sends the wrong time range still fails -
// it just no longer fails at random on the ones that send the right one.
const ROUTE_SEEDED = /^(\/(?:index\/)?statistics(?:Avg|Peak)Init)\//;
const seedKey = (method, url) => {
  const p = String(url || '').split('?')[0];
  const m = ROUTE_SEEDED.exec(p);
  return `${method} ${m ? `${m[1]}/` : url}`;
};

const DELAY_MS = Number(process.env.DET_DELAY_MS || 250);
// Only the data routes are held back; the static assets beside them are served
// at full speed, so the component is always mounted before its data lands.
const API_PREFIX = /^\/(index|hour|mock|users|userList)(\/|$|\?)/;

const origCreateServer = http.createServer;
http.createServer = function (...args) {
  const i = args.length - 1;
  const handler = args[i];
  if (typeof handler === 'function') {
    args[i] = function (req, res) {
      // Seed inside the timer and enter the handler inside that context, so the
      // whole continuation of this request - middleware, fs callbacks, the route
      // itself - draws from this request's generator and nothing else's.
      const run = () => {
        const h = crypto.createHash('sha256').update(seedKey(req.method, req.url)).digest();
        rngStore.run(mulberry32(h.readUInt32LE(0)), () => handler.call(this, req, res));
      };
      const path = String(req.url || '').split('?')[0];
      if (DELAY_MS > 0 && API_PREFIX.test(path)) setTimeout(run, DELAY_MS); else run();
    };
  }
  return origCreateServer.apply(this, args);
};

// Exported so det-check.sh can assert the mapping directly: the scoping of the
// route-seeding change cannot be observed over HTTP, because every route that
// does not call Math.random returns a constant string.
module.exports = { seedKey };
