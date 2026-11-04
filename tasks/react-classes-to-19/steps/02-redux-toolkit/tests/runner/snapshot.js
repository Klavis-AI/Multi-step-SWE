// Page init script: defines window.__snapshot(), the canonical observation of
// what the application currently shows.  Two parts:
//
//   dom    - a canonical rendering of the #app subtree.  Comment nodes are
//            dropped and adjacent text is merged (React 15 splits interpolated
//            text into several nodes separated by react-text comments; later
//            React versions emit one node), attributes are sorted, the
//            frameworks' own bookkeeping attributes are dropped (data-react*,
//            which React 15 writes onto the nodes it owns, and data-discover,
//            which react-router 7 puts on every link it renders so that its
//            route discovery can tell its own anchors apart from the page's -
//            neither is written by this application, and no recording carries
//            a data- attribute of any kind), asset URLs are reduced to their
//            basename with any
//            content hash stripped (a bundler change legitimately renames
//            assets), <svg> subtrees are collapsed to their class (react-icons
//            v2 reads legacy context and cannot survive React 19, so the icon
//            markup necessarily changes; its internals are third-party detail),
//            an element that carries an echarts instance is emitted as a leaf
//            because its interior is covered by the canvas transcript, and the
//            classes react-addons-css-transition-group puts on the children it
//            is animating are dropped along with the children it is only still
//            holding on to so they can fade out (see TRANSITION_TOKEN below).
//
//   charts - for every echarts container, the last complete frame each of its
//            canvases drew: the text it painted with positions, colours and
//            font, plus per-operation counts.  This is what the chart actually
//            rendered, observed from outside the library.
(() => {
  if (window.__snapshotInstalled) return;
  window.__snapshotInstalled = true;

  const ASSET_ATTR = { img: 'src', script: 'src', link: 'href', source: 'src' };
  // react-addons-css-transition-group decorates the children it animates: a
  // child on its way in gets `<name>-enter` and then `<name>-enter-active`, and
  // the previous child is kept mounted with `<name>-leave` on it until its
  // fade-out is over.  Both are removed a fixed time later - the token, not the
  // attribute, so an empty class="" is left behind.  Which of those three
  // states a snapshot catches is decided by where the clock stopped, and the
  // addon is React 15-only: it is the one piece of this application a migration
  // has to replace rather than convert, and no replacement is obliged to
  // reproduce another library's class timing.  So the decoration is not part of
  // what is compared.  A class token ending in -enter/-leave/-exit/-appear,
  // with or without -active or -done, is dropped; a class attribute left empty
  // is dropped with it (an empty class attribute and no class attribute are the
  // same page, and `class=""` is what the removal leaves); and a child that is
  // only still in the document because it is animating out is not walked at
  // all.  What is left is what the carousel shows, which is what its scenarios
  // are about.
  const TRANSITION_TOKEN = /-(enter|leave|exit|appear)(-active|-done)?$/;
  const LEAVING_TOKEN = /-(leave|exit)(-active|-done)?$/;

  function isLeaving(el) {
    const cls = el.getAttribute ? el.getAttribute('class') : null;
    if (!cls) return false;
    return cls.split(/\s+/).some((t) => t && LEAVING_TOKEN.test(t));
  }
  const VOID = new Set(['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input',
    'link', 'meta', 'param', 'source', 'track', 'wbr']);

  function assetName(u) {
    if (!u) return String(u);
    const s = String(u);
    if (/^data:/.test(s)) return 'data:';
    let t = s.split('?')[0].split('#')[0];
    t = t.substring(t.lastIndexOf('/') + 1);
    // Drop a trailing content hash: a separator plus eight or more word
    // characters containing at least one digit.  Webpack's [chunkhash:8] and
    // Vite's default 8-character hash both match; real name parts such as
    // "-webfont" (seven characters) and "fontawesome" (no digit) do not.
    const m = /^(.*?)(\.[A-Za-z0-9]+)$/.exec(t);
    if (m) {
      const stem = m[1].replace(/[-._][A-Za-z0-9_-]{8,}$/, (seg) => (/\d/.test(seg) ? '' : seg));
      t = (stem || m[1]) + m[2];
    }
    return t;
  }

  function canonAttrs(el, opts) {
    const out = [];
    const assetAttr = ASSET_ATTR[el.tagName.toLowerCase()];
    for (const a of el.attributes) {
      const n = a.name;
      if (n.startsWith('data-react')) continue;
      if (n === 'data-discover') continue;
      if (n === '_echarts_instance_') continue;
      if (opts && opts.dropStyle && n === 'style') continue;
      let v = a.value;
      if (n === assetAttr) v = assetName(v);
      if (n === 'class') {
        v = v.split(/\s+/).filter((t) => t && !TRANSITION_TOKEN.test(t)).join(' ');
        if (!v) continue;
      }
      out.push(`${n}="${v}"`);
    }
    // The calendar writes into its input through the DOM node rather than
    // through React, and a property assignment leaves no attribute behind, so
    // the live value of every form control is recorded explicitly.
    const tag = el.tagName.toLowerCase();
    if (tag === 'input' || tag === 'textarea' || tag === 'select') {
      out.push(`:value="${el.value}"`);
      if (tag === 'input') out.push(`:checked="${el.checked}"`);
    }
    out.sort();
    return out;
  }

  function walk(el, out, depth) {
    const tag = el.tagName.toLowerCase();
    const isChart = el.hasAttribute && el.hasAttribute('_echarts_instance_');
    const pad = '  '.repeat(depth);
    if (tag === 'svg') {
      const cls = el.getAttribute('class');
      out.push(`${pad}<svg${cls ? ` class="${cls}"` : ''}/>`);
      return;
    }
    const attrs = canonAttrs(el, { dropStyle: isChart });
    out.push(`${pad}<${tag}${attrs.length ? ' ' + attrs.join(' ') : ''}>`);
    if (isChart) {
      out.push(`${pad}  [chart]`);
      out.push(`${pad}</${tag}>`);
      return;
    }
    let run = '';
    const flush = () => {
      const t = run.replace(/\s+/g, ' ').trim();
      if (t) out.push(`${pad}  #${t}`);
      run = '';
    };
    for (const n of el.childNodes) {
      if (n.nodeType === 3) {
        run += n.nodeValue;
      } else if (n.nodeType === 1) {
        // Not flushed first: a node that is on its way out is one the document
        // would not have had at all, so the text on either side of it is one run.
        if (isLeaving(n)) continue;
        flush();
        walk(n, out, depth + 1);
      }
      // comment nodes contribute nothing and do not break a text run
    }
    flush();
    if (!VOID.has(tag)) out.push(`${pad}</${tag}>`);
  }

  function chartKey(el) {
    const cls = (el.getAttribute('class') || '').trim();
    const id = el.getAttribute('id');
    return cls || (id ? `#${id}` : el.tagName.toLowerCase());
  }

  // FNV-1a, 32 bits at a time over two lanes, printed as 16 hex digits.  The
  // full operation stream of a busy chart is tens of thousands of numbers; the
  // digest is what gets stored, with the operation counts, the painted text and
  // the palette kept verbatim so a failure still says something useful.
  function digest(str) {
    let h1 = 0x811c9dc5;
    let h2 = 0x01000193;
    for (let i = 0; i < str.length; i++) {
      const c = str.charCodeAt(i);
      h1 = Math.imul(h1 ^ c, 0x01000193) >>> 0;
      h2 = Math.imul(h2 + c, 0x85ebca6b) >>> 0;
      h2 = (h2 ^ (h2 >>> 13)) >>> 0;
    }
    return h1.toString(16).padStart(8, '0') + h2.toString(16).padStart(8, '0');
  }

  function fmt(e) {
    const v = e.v ? e.v.join(',') : '';
    if (e.op === 'fillText' || e.op === 'strokeText') {
      return `${e.op}(${JSON.stringify(e.t)},${v})|${e.s}|${e.f}|${e.al}|${e.bl}`;
    }
    if (e.s !== undefined) return `${e.op}(${v})|${e.s}|${e.a}`;
    return `${e.op}(${v})`;
  }

  function frameOf(canvas, entries) {
    // The trailing frame is everything after the last full-canvas clearRect.
    let start = 0;
    for (let i = 0; i < entries.length; i++) {
      const e = entries[i];
      if (e.op === 'clearRect' && e.w >= canvas.width - 1 && e.h >= canvas.height - 1) start = i + 1;
    }
    const frame = entries.slice(start);
    const ops = {};
    const texts = [];
    const colors = new Set();
    const lines = [];
    for (const e of frame) {
      ops[e.op] = (ops[e.op] || 0) + 1;
      lines.push(fmt(e));
      if (e.op === 'fillText' || e.op === 'strokeText') {
        texts.push({ t: e.t, v: e.v, s: e.s, f: e.f, al: e.al, bl: e.bl });
      }
      if (e.s) colors.add(e.s);
    }
    const out = {
      w: canvas.width, h: canvas.height, ops, texts, colors: [...colors].sort(),
      geomOps: lines.length, geomHash: digest(lines.join('\n')),
    };
    if (window.__snapshotFull) out.geom = lines;
    return out;
  }

  window.__snapshot = function () {
    const app = document.getElementById('app') || document.body;
    const out = [];
    walk(app, out, 0);

    const byCanvas = new Map();
    for (const e of window.__probeEntries || []) {
      let arr = byCanvas.get(e.c);
      if (!arr) byCanvas.set(e.c, (arr = []));
      arr.push(e);
    }

    const charts = [];
    const containers = [...document.querySelectorAll('[_echarts_instance_]')];
    const seen = new Set();
    const counts = {};
    for (const c of containers) {
      const key = chartKey(c);
      counts[key] = (counts[key] || 0) + 1;
      const canvases = [...c.querySelectorAll('canvas')];
      const layers = [];
      for (const cv of canvases) {
        seen.add(cv);
        const entries = byCanvas.get(cv);
        if (!entries || !entries.length) { layers.push(null); continue; }
        layers.push(frameOf(cv, entries));
      }
      charts.push({ key: counts[key] > 1 ? `${key}#${counts[key]}` : key, layers });
    }

    const orphans = [];
    for (const [cv, entries] of byCanvas) {
      if (seen.has(cv) || !cv.isConnected) continue;
      orphans.push(frameOf(cv, entries));
    }

    charts.sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0));
    return {
      dom: out.join('\n'),
      charts,
      orphanCanvases: orphans.length,
      title: document.title,
      hash: location.hash,
    };
  };
})();
