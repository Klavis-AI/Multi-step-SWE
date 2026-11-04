// Page init script: records every 2d-canvas drawing operation the application
// performs, so that what the charts actually draw can be compared between the
// baseline and a migrated tree without reaching into any library's internals.
// Installed by the harness with page.addInitScript(); the application never
// sees it in its own tree.
//
// Numeric arguments are kept, rounded to a tenth of a pixel, because the
// geometry is the chart: a line series is a run of lineTo calls whose
// coordinates come straight from the data.  Angles are rounded to four places.
(() => {
  if (window.__probeInstalled) return;
  window.__probeInstalled = true;

  const entries = [];
  window.__probeEntries = entries;

  // op name -> how many leading arguments are numeric and worth keeping
  const GEOM = {
    moveTo: 2, lineTo: 2, bezierCurveTo: 6, quadraticCurveTo: 4, arcTo: 5,
    rect: 4, clip: 0, beginPath: 0, closePath: 0, save: 0, restore: 0,
    setTransform: 6, transform: 6, translate: 2, scale: 2, rotate: 1,
    drawImage: 0,
  };
  const ANGLE = { arc: [3, 4], ellipse: [4, 5, 6] }; // indices rounded finer
  const PAINT = { fill: 'fillStyle', stroke: 'strokeStyle', fillRect: 'fillStyle', strokeRect: 'strokeStyle' };
  const TEXT_OPS = ['fillText', 'strokeText'];

  const r1 = (n) => {
    if (typeof n === 'boolean') return n ? 1 : 0;
    return typeof n === 'number' && isFinite(n) ? Math.round(n * 10) / 10 : 0;
  };
  const r4 = (n) => (typeof n === 'number' && isFinite(n) ? Math.round(n * 10000) / 10000 : 0);

  const origGetContext = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (type, ...rest) {
    const ctx = origGetContext.call(this, type, ...rest);
    if (type !== '2d' || !ctx || ctx.__probed) return ctx;
    ctx.__probed = true;
    const canvas = this;

    const hook = (name, record) => {
      const orig = ctx[name];
      if (typeof orig !== 'function') return;
      ctx[name] = function (...args) {
        try {
          entries.push(record.call(this, args));
        } catch (e) { /* never break the app under test */ }
        return orig.apply(this, args);
      };
    };

    for (const name of TEXT_OPS) {
      hook(name, function (args) {
        return {
          c: canvas, op: name, t: String(args[0]), v: [r1(args[1]), r1(args[2])],
          s: String(name === 'fillText' ? this.fillStyle : this.strokeStyle),
          f: String(this.font), al: String(this.textAlign), bl: String(this.textBaseline),
        };
      });
    }

    hook('clearRect', function (args) {
      return { c: canvas, op: 'clearRect', v: args.slice(0, 4).map(r1), w: r1(args[2]), h: r1(args[3]) };
    });

    for (const [name, style] of Object.entries(PAINT)) {
      hook(name, function (args) {
        return {
          c: canvas, op: name, v: args.slice(0, 4).map(r1),
          s: String(this[style]), a: r4(this.globalAlpha),
        };
      });
    }

    for (const [name, n] of Object.entries(GEOM)) {
      hook(name, function (args) {
        return { c: canvas, op: name, v: args.slice(0, n).map(r1) };
      });
    }

    for (const [name, angles] of Object.entries(ANGLE)) {
      hook(name, function (args) {
        const set = new Set(angles);
        return { c: canvas, op: name, v: args.map((x, i) => (set.has(i) ? r4(x) : r1(x))) };
      });
    }

    return ctx;
  };
})();
