// Evaluates a step's migrated-ness gate spec and writes the gates.json that
// score.mjs turns into the second half of the reward.
//
// The scanners do the looking and this does the judging, which keeps the
// per-step work down to a spec file: the three scanners (ast-scan, deps,
// artifacts) run once each over the whole tree and every check is a question
// asked of their output.  No check reads the tree itself, so no check can
// disagree with another about what is in scope.
//
// Grading.  A check with a `baseline` is graded against the untouched starting
// tree's own count, so removing nine tenths of the legacy sites scores 0.9 and a
// tree that has not been touched scores 0.  A check marked `binary` is all or
// nothing, for the gates where one surviving site defeats the point of the gate
// - a single legacy root render still renders the whole application the old way,
// and a single lint suppression still hides whatever it was pointed at.
//
// Check kinds:
//   ast-max      gate site count must be <= max (graded against baseline)
//   ast-min      gate site count must be >= min (evidence the new API is used);
//                `scope: "reachable"` counts only the sites that sit in a file
//                the build actually compiles into the page it serves, rather
//                than anywhere in the repository
//   parse-errors files the scorer's parser cannot read must be <= max
//   dep-absent   no copy of the package anywhere in the installed tree
//   dep-version  installed package's major satisfies `major`/`minMajor`, and
//                every installed copy's minor satisfies an optional `minMinor`
//   dep-declared package appears in package.json's dependency ranges
//   vendor-absent        package contributes no module to the shipped graph
//   source-present       a repo-relative source path is in the shipped graph
//   assets-self-contained  every asset index.html references is in dist/, and
//                none of them is fetched from another origin
//   budget       an artifacts.mjs metric is <= max (graded against baseline)
//   min-chunks   the page the build serves reaches at least `min` javascript
//                chunks (not every `*.js` under dist/: `public/` is copied in
//                verbatim and its files are reached by nothing)
//   lint-rules   findings from named lint rules must be <= max
//   lint-total   all lint findings must be <= max
//   json-flag    a boolean/number field of a file another runner step wrote,
//                against `equals`, `min` or `max`
//
// usage: node run-gates.mjs --spec spec.json --ast ast.json --deps deps.json
//                           --artifacts art.json --lint eslint.json
//                           [--provenance provenance.json] --out gates.json
import fs from 'node:fs';

const argv = process.argv.slice(2);
const arg = (n, d) => {
  const i = argv.indexOf(`--${n}`);
  return i === -1 ? d : argv[i + 1];
};

const readJson = (p, what) => {
  try {
    return JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch (e) {
    throw new Error(`cannot read ${what} (${p}): ${e.message}`);
  }
};

const spec = readJson(arg('spec'), 'spec');
const OUT = arg('out', '/logs/verifier/gates.json');

// A scanner that failed to run leaves its file absent.  Every check that needed
// it then scores 0 with its own message, rather than the whole gate family
// collapsing to one unexplained zero.
const load = (flag, what) => {
  const p = arg(flag, null);
  if (!p || !fs.existsSync(p)) return { missing: `${what} scan absent` };
  try {
    return readJson(p, what);
  } catch (e) {
    return { missing: e.message };
  }
};
const ast = load('ast', 'ast');
const deps = load('deps', 'deps');
const art = load('artifacts', 'artifacts');
const lint = load('lint', 'lint');

// The provenance record is written by the runner's two probe builds rather than
// by a scanner, so it is read the same way but its absence has its own sentence:
// "the file is not there" and "the probes ran and found nothing" are different
// answers and must not collapse into one.
const readRecord = (p) => {
  if (!p || !fs.existsSync(p)) {
    return { missing: 'the build was not re-run, so what it compiles could not be measured' };
  }
  try {
    return readJson(p, 'provenance');
  } catch (e) {
    return { missing: e.message };
  }
};
const prov = readRecord(arg('provenance', '/logs/verifier/provenance.json'));

// The files the build compiles into the page it serves: the modules whose
// injected markers survive into the rebuilt bundle and vanish when the entry
// module is emptied.  This is the one set every "did the new idiom replace the
// old one?" question is asked of, and it is measured by the bundler rather than
// resolved by a walk written here.
//
// Returned as a reason rather than an empty set when there is nothing to report,
// so a check cannot read "not measured" as "measured, and nothing is there".
const compiledModules = (record) => {
  if (record.missing) return { error: record.missing };
  if (record.error) return { error: record.error };
  const files = Array.isArray(record.files) ? record.files : [];
  if (!Array.isArray(record.difference)) {
    return { error: 'the re-run of the build recorded no module set' };
  }
  const set = new Set(record.difference.map((i) => files[i]).filter((f) => typeof f === 'string'));
  if (!set.size) {
    return { error: 'no module of this tree is compiled into the page the build serves' };
  }
  return { set };
};

const clamp01 = (n) => Math.max(0, Math.min(1, n));

// count -> score, given a permitted maximum and the untouched tree's count.
const graded = (count, max, baseline) => {
  if (count <= max) return 1;
  if (baseline == null || baseline <= max) return 0;
  return clamp01(1 - (count - max) / (baseline - max));
};

const dig = (obj, dotted) => dotted.split('.').reduce((o, k) => (o == null ? o : o[k]), obj);

const majorOf = (v) => {
  const m = /^\D*(\d+)/.exec(String(v || ''));
  return m ? Number(m[1]) : null;
};

const minorOf = (v) => {
  const m = /^\D*\d+\.(\d+)/.exec(String(v || ''));
  return m ? Number(m[1]) : null;
};

function evaluate(check) {
  const need = (src, what) => {
    if (src && src.missing) throw new Error(src.missing);
    if (!src) throw new Error(`${what} scan absent`);
    return src;
  };
  // Every question about the installed tree needs an install to have happened.
  // An empty node_modules answers "is this retired package gone?" with yes, so a
  // tree whose dependencies cannot be installed at all would clear every
  // retired-package gate by being unbuildable - which is the opposite of the
  // finding. React itself is the anchor: no React, no install worth reading.
  const needInstalled = () => {
    const d = need(deps, 'deps');
    if (!((d.counts || {}).installed > 0)) throw new Error('nothing is installed under node_modules');
    if (!(d.installed || {}).react) throw new Error('react is not installed; the install did not complete');
    return d;
  };
  // ast-scan reports `gates: {id: {count, sites}}` and declares every id it can
  // emit, so a missing id means the check names a query the scanner does not
  // have - a typo, or a query that was renamed out from under this spec. Reading
  // that as a count of 0 would score full marks on an ast-max gate for a tree
  // nobody looked at, so it is an error instead.
  const gateCount = (id) => {
    const a = need(ast, 'ast');
    const g = (a.gates || {})[id];
    if (!g) throw new Error(`ast-scan has no gate named ${id}`);
    // A malformed entry has to be an error too. `undefined` compares false
    // against every bound, which in an ast-min reads as "not enough" but in a
    // guard reads as "satisfied" - NaN < 1 is false - so a scan that lost its
    // counts would hand out the guarded gates for free.
    if (!Number.isFinite(g.count)) throw new Error(`ast-scan gate ${id} has no count`);
    return g.count;
  };
  // Every site of a gate, with the file it was found in.  ast-scan keys these on
  // paths relative to the tree root, and so does the provenance record, so the
  // two intersect directly.
  const gateSites = (id) => {
    const a = need(ast, 'ast');
    const g = (a.gates || {})[id];
    if (!g) throw new Error(`ast-scan has no gate named ${id}`);
    if (!Array.isArray(g.sites)) throw new Error(`ast-scan gate ${id} reports no sites`);
    return g.sites;
  };

  // The same gate, asked of a narrower scope.  "Is this construct gone?" is a
  // question about the repository - a surviving class in a file nothing imports
  // is still a surviving class - but "did the new idiom replace the old one?" is
  // a question about the code that runs, and a hook in a file nothing runs is no
  // evidence of it.  So the minimum gates may ask for `scope: "reachable"`,
  // which counts only sites in files the build compiles into the page.
  //
  // There is no repository-wide fallback.  One existed, for trees whose entry a
  // static resolver could not find, and it was the padding vector: a tree that
  // made itself unresolvable had its presence gates scored over every file in
  // it, including files nothing builds.  A check that cannot be measured in the
  // scope it asked for scores 0 and says why, which is a candidate's own build
  // to fix and never a silent widening.
  const scopedCount = (id, scope) => {
    if (scope !== 'reachable') return { count: gateCount(id), where: 'repository-wide' };
    const sites = gateSites(id);
    const m = compiledModules(prov);
    if (m.error) return { error: m.error };
    return {
      count: sites.filter((s) => m.set.has(s.file)).length,
      where: `in the ${m.set.size} module(s) the build compiles into the page`,
    };
  };

  switch (check.kind) {
    case 'ast-max': {
      const max = check.max == null ? 0 : check.max;
      // A ceiling is a question about the repository by default - a surviving
      // class in a file nothing imports is still a surviving class - so `scope`
      // is absent on every "is this gone?" check and the count is repository
      // wide.  It is declared where the ceiling is about the code that runs
      // instead: "no root render is left unwrapped" and "no module writes over a
      // console method" are claims about the page the build serves, and charging
      // them for a file the build does not compile would fail a tree for a
      // module that never mounts anything.  Unmeasurable scores 0 and says so,
      // the way the minimums do, rather than quietly widening to the repository.
      const s = scopedCount(check.gate, check.scope);
      if (s.error) return { score: 0, detail: `not measured: ${s.error} (max ${max})` };
      const where = check.scope === 'reachable' ? ` ${s.where}` : '';
      return {
        score: graded(s.count, max, check.baseline),
        detail: `${s.count} site(s)${where}, max ${max}`,
      };
    }
    case 'ast-min': {
      const min = check.min == null ? 1 : check.min;
      const s = scopedCount(check.gate, check.scope);
      if (s.error) return { score: 0, detail: `not measured: ${s.error} (min ${min})` };
      return {
        score: s.count >= min ? 1 : clamp01(s.count / min),
        detail: `${s.count} site(s) ${s.where}, min ${min}`,
      };
    }
    case 'parse-errors': {
      const a = need(ast, 'ast');
      const errs = a.parseErrors || [];
      const max = check.max == null ? 0 : check.max;
      const where = errs.slice(0, 3).map((e) => e.file).join(', ');
      return {
        score: graded(errs.length, max, check.baseline),
        detail: errs.length ? `${errs.length} unparsable file(s): ${where}` : 'every source file parses',
      };
    }
    case 'dep-absent': {
      const d = needInstalled();
      const names = check.packages || [check.package];
      const present = names.filter((n) => (d.installed || {})[n]);
      return {
        score: present.length === 0 ? 1 : clamp01(1 - present.length / names.length),
        detail: present.length ? `still installed: ${present.join(', ')}` : 'absent from the installed tree',
      };
    }
    case 'dep-version': {
      const d = needInstalled();
      const copies = (d.installed || {})[check.package];
      if (!copies || !copies.length) return { score: 0, detail: `${check.package} is not installed` };
      const majors = [...new Set(copies.map((c) => majorOf(c.version)))];
      const versions = copies.map((c) => c.version).join(', ');
      // More than one major installed means something still depends on the old
      // one; that is the case the gate exists to catch, so it fails.
      if (majors.length !== 1) return { score: 0, detail: `${check.package} installed at ${versions}` };
      const major = majors[0];
      // `minMinor` is for the case where the feature the step is about arrived
      // in a minor rather than a major - hooks in 16.8 - so "the right major" is
      // not the same question as "a version that can do this".  Applied to every
      // installed copy, since the single-major rule above already establishes
      // that one old copy is a failure and an old minor is the same problem.
      const minorOk = check.minMinor == null
        || copies.every((c) => (minorOf(c.version) ?? -1) >= check.minMinor);
      const ok = (check.major != null ? major === check.major : major >= check.minMajor) && minorOk;
      return { score: ok ? 1 : 0, detail: `${check.package}@${versions}` };
    }
    case 'dep-declared': {
      const d = need(deps, 'deps');
      const range = (d.declared || {})[check.package];
      return {
        score: range ? 1 : 0,
        detail: range ? `${check.package} ${range}` : `${check.package} not in package.json`,
      };
    }
    case 'vendor-absent': {
      const a = need(art, 'artifacts');
      if (!a.maps) throw new Error('build emitted no source maps, module graph unreadable');
      const names = check.packages || [check.package];
      const present = names.filter((n) => (a.vendor || {})[n]);
      return {
        score: present.length === 0 ? 1 : clamp01(1 - present.length / names.length),
        detail: present.length ? `still shipped: ${present.join(', ')}` : 'contributes no shipped module',
      };
    }
    case 'source-present': {
      const a = need(art, 'artifacts');
      if (!a.maps) throw new Error('build emitted no source maps, module graph unreadable');
      const want = check.sources || [check.source];
      const have = new Set(a.sources || []);
      const found = want.filter((s) => have.has(s) || [...have].some((h) => h.endsWith(s)));
      return {
        score: clamp01(found.length / want.length),
        detail: `${found.length}/${want.length} in the shipped graph`,
      };
    }
    case 'assets-self-contained': {
      const a = need(art, 'artifacts');
      if (!a.indexHtml || !a.indexHtml.present) return { score: 0, detail: 'dist/index.html absent' };
      const missing = a.indexHtml.missing || [];
      // An asset the page fetches from another host fails this for the same
      // reason a hand-copied one does, from the other side: the site does not
      // stand on its own.  It is also the only reference nothing else in this
      // verifier can see - every walk of dist/ starts inside dist/ - so a
      // `<script src="https://…/echarts.min.js">` would otherwise be javascript
      // the visitor waits for that no byte budget is summed over.  No tree of
      // this task names one: the project's own three IE shims are local files
      // inside a conditional comment, which htmlSansComments has already
      // removed before this loop runs.
      const off = a.indexHtml.external || [];
      if (missing.length || off.length) {
        return {
          score: 0,
          detail: [
            missing.length ? `missing from dist/: ${missing.join(', ')}` : '',
            off.length ? `fetched from another origin: ${off.join(', ')}` : '',
          ].filter(Boolean).join('; '),
        };
      }
      return {
        score: 1,
        detail: `${(a.indexHtml.referenced || []).length} local asset(s), all present, none off-origin`,
      };
    }
    // Is the site that is served built from the tree that is scanned?
    //
    // Nothing else in this spec connects the two.  The behavior family reads the
    // served bundle and the gate family reads the source, so a dist/ produced
    // from other source and put back by the build script satisfies both at once:
    // the bundle is the application, the tree is whatever the gates ask for, and
    // they need not be the same program.
    //
    // No static comparison can answer this, because everything under dist/ is
    // written by the candidate's own build script - the source maps included -
    // and a map naming the tree's paths and carrying its text is a few lines to
    // forge.  So the question is put to the build instead: the runner copies the
    // tree twice, marks every one of its own modules, and runs the candidate's
    // own build on each copy, once whole and once with the module index.html
    // loads emptied.  A marker that reaches the page in the first and not in the
    // second belongs to a module the build compiled *because the entry asked for
    // it*.  gates/provenance.mjs carries the measurements; runner/provenance.sh
    // runs it.
    //
    // The floor is 1: this fails closed on a difference of 0, not on a count.
    // A higher floor is a floor on how many separate compiling source files the
    // tree is allowed to have - which the instruction never states, which a
    // legal consolidation could fall below, and which costs all 31 of the gate
    // weight when it bites.  Bounding a thin difference set is already the job
    // of the three `ast-min` checks at `scope: "reachable"`, and they do it
    // better: those are scored, so a thin tree loses the points it did not earn
    // rather than the whole family.
    case 'built-from-source': {
      const record = check.file ? readRecord(check.file) : prov;
      if (record.missing) throw new Error(record.missing);
      if (record.error) throw new Error(record.error);
      if (!Array.isArray(record.difference)) {
        throw new Error('the re-run of the build recorded no module set');
      }
      const min = check.minModules == null ? 1 : check.minModules;
      const markers = record.markers || {};
      const a = Array.isArray(markers.A) ? markers.A.length : 0;
      const b = Array.isArray(markers.B) ? markers.B.length : 0;
      const entry = record.entry || 'the entry module';
      // Named for the same reason `identicalOutput` is: the difference was
      // zeroed by a rule rather than by counting, and a reader of gates.json has
      // to be able to tell that from a bare 0.  The rebuild without the entry
      // left no page at all, so it is A that has to carry the evidence, and the
      // entry module's own marker is not in it - either because it did not
      // survive into A's page, or because there was no marker to look for.
      const entryUnproven = record.bProduced === false && record.entryCompiled !== true;
      const detail = `${record.difference.length} of this tree's own module(s) are compiled `
        + `into the page its build serves, min ${min} `
        + `(${a} reached the page, ${b} still reached it with ${entry} emptied)`
        + (record.identicalOutput
          // Named when it fires, because it is a different finding from "no
          // markers survived": the two rebuilds emitted the same bytes, so
          // nothing the page serves is a function of the entry module, whatever
          // the markers and the exit statuses say.
          ? `; both rebuilds emitted byte-identical files, so nothing the page serves is compiled from ${entry}`
          : '')
        + (entryUnproven
          ? `; the rebuild without ${entry} produced no page at all, and `
            + (record.entryCompiled === false
              ? `${entry}'s own marker is not among the ${a} that reached the page`
              : `${entry} carries no marker of its own (it is an inline script, or it did not parse)`)
            + `, so nothing measured here shows the build compiled ${entry}`
          : '');
      // Code the build compiled from under a nested node_modules, which no source
      // gate reads: the page is then not the tree the gates measured.
      // (wave D 2026-10-12, final review C1; provenance.mjs records it)
      // The same for server/demo1/public (wave F) and for any `.git` directory
      // (wave H 2026-10-13, C-1).
      const unscanned = Array.isArray(record.unscannedReached) ? record.unscannedReached : [];
      if (unscanned.length) {
        return {
          score: 0,
          detail: `${detail}; the page also carries code from ${unscanned.length} file(s) under a nested `
            + `node_modules, a .git or server/demo1/public, or with a non-JavaScript extension, which is not the source the gates score: `
            + `${unscanned.slice(0, 5).join(', ')}`,
        };
      }
      // A module imported from a `data:` URL is the same finding from the other
      // side: code the bundler compiles into the page that is in no file of this
      // tree, so no gate read it.  ast-scan.mjs records every such specifier,
      // and an absent scan fails this check rather than passing it.  (wave H
      // 2026-10-14, H-CR7, final review G2-R6)
      const dataImports = gateSites('data-uri-import');
      if (dataImports.length) {
        return {
          score: 0,
          detail: `${detail}; the tree also imports ${dataImports.length} module(s) from a data: URL, `
            + 'code that is in no file of this tree and that no gate reads: '
            + `${dataImports.slice(0, 5).map((x) => `${x.file}:${x.line} ${x.what}`).join(', ')}`,
        };
      }
      // And a module imported from the root `.git` or from server/demo1/public,
      // the two directories exempt from provenance marking: exempt from the
      // marker, not from this check (coordinator ruling 2026-10-14).  Read the
      // same way, so an absent scan fails here too.  (wave H 2026-10-14,
      // H-CR8, final review G2-R7c)
      const exemptImports = gateSites('exempt-dir-import');
      if (exemptImports.length) {
        return {
          score: 0,
          detail: `${detail}; the tree also imports ${exemptImports.length} module(s) from the root .git `
            + 'or server/demo1/public, which no gate reads and no provenance marker follows: '
            + `${exemptImports.slice(0, 5).map((x) => `${x.file}:${x.line} ${x.what}`).join(', ')}`,
        };
      }
      return { score: record.difference.length >= min ? 1 : 0, detail };
    }
    case 'budget': {
      const a = need(art, 'artifacts');
      const value = dig(a, check.metric);
      if (typeof value !== 'number') return { score: 0, detail: `${check.metric} unreadable` };
      return {
        score: graded(value, check.max, check.baseline),
        detail: `${check.metric} = ${value}, max ${check.max}`,
      };
    }
    case 'min-chunks': {
      const a = need(art, 'artifacts');
      // The chunks the built page reaches, and deliberately no fallback to
      // `js.count`.  Every `*.js` under dist/ includes whatever `public/` was
      // copied in - this project vendors `lib/js/es5-shim.js` and
      // `lib/js/html5shiv.js` by hand - so a single application bundle beside
      // them counted as three chunks and cleared a gate about code splitting.
      // A page that reaches no JavaScript scores 0 here, which is the right
      // answer: `assets-self-contained` has something to say about that tree
      // too.
      const count = dig(a, 'js.reachable.count');
      if (!Number.isFinite(count)) {
        throw new Error('the artifact scan reports no reachable javascript count');
      }
      return {
        score: count >= check.min ? 1 : clamp01(count / check.min),
        detail: `${count} javascript chunk(s) reached by the built page, min ${check.min}`,
      };
    }
    case 'lint-rules':
    case 'lint-total': {
      const l = need(lint, 'lint');
      if (!l.ok) throw new Error(`eslint did not run: ${l.error}`);
      // Zero files linted and zero findings look identical in a count. They are
      // not: the first means the sources moved out from under the glob.
      if (!l.filesLinted) throw new Error('eslint linted no files');
      const max = check.max == null ? 0 : check.max;
      // A rule can be satisfied or it can be switched off, and a count of
      // findings cannot tell the difference - the suppressed site reports
      // nothing. So a suppression is a finding, and those sites are added here:
      // a candidate that comments out the rule instead of migrating the code
      // moves the count sideways rather than down.
      //
      // Only the suppressions that can silence a hook rule are added.  Charging
      // this gate for an unrelated `eslint-disable-next-line no-console` made it
      // a gate about lint hygiene in general, which is not a requirement the
      // instruction states - and could not state without naming the tool.
      let suppressions = 0;
      if (check.includeSuppressions !== false) suppressions = gateCount('hook-suppressions');
      const suffix = suppressions ? ` (incl. ${suppressions} suppression site(s))` : '';
      if (check.kind === 'lint-total') {
        const count = l.errorCount + l.warningCount + suppressions;
        return {
          score: graded(count, max, check.baseline),
          detail: `${count} finding(s) over ${l.filesLinted} files, max ${max}${suffix}`,
        };
      }
      // A rule the config never turned on reports no findings, which would score
      // full marks for a gate that is not being enforced at all.
      const configured = new Set(l.configuredRules || []);
      const unknown = check.rules.filter((r) => !configured.has(r));
      if (unknown.length) throw new Error(`rule(s) not configured: ${unknown.join(', ')}`);
      const count = check.rules.reduce((n, r) => n + ((l.byRule || {})[r] || 0), 0) + suppressions;
      return {
        score: graded(count, max, check.baseline),
        detail: `${count} finding(s) from ${check.rules.length} rule(s), max ${max}${suffix}`,
      };
    }
    case 'json-flag': {
      const data = readJson(check.file, check.id);
      const value = dig(data, check.field);
      if (check.equals !== undefined) {
        return { score: value === check.equals ? 1 : 0, detail: `${check.field} = ${JSON.stringify(value)}` };
      }
      if (!Number.isFinite(value)) return { score: 0, detail: `${check.field} = ${JSON.stringify(value)}` };
      // A floor rather than a ceiling, for the fields that are evidence of
      // something having happened rather than of something being absent.
      if (check.min != null) {
        return {
          score: value >= check.min ? 1 : 0,
          detail: `${check.field} = ${value}, min ${check.min}`,
        };
      }
      return {
        score: graded(value, check.max == null ? 0 : check.max, check.baseline),
        detail: `${check.field} = ${value}, max ${check.max == null ? 0 : check.max}`,
      };
    }
    default:
      throw new Error(`unknown check kind ${check.kind}`);
  }
}

// A gate that asks for the absence of something is vacuous on a tree where that
// something was never possible. Measured, not hypothetical: the react-hooks
// rules report exactly zero findings on the untouched baseline, because the
// baseline has no hooks at all - so "no hook-rule violations" would be a free
// 1.0 for an agent that did nothing, and the nop trial would not be 0.
//
// A check may therefore declare `guard`: a list of ordinary check specs that
// must each score a full 1 before the check itself is evaluated. The lint gates
// guard on "there are hook calls", so the absence of findings only counts once
// there is something for the rules to have found.
//
// The spec may declare a `guard` of its own, which is evaluated before every
// check's own guard and applies to all of them. That is where a precondition
// belongs when it is a precondition for crediting migrated-ness at all rather
// than for one gate: the absence gates are all satisfied by an absent tree, so a
// gate family with nothing anchoring it pays a candidate for deleting the
// application. A spec-level guard is that anchor.
//
// A guard may carry `unmet`, the sentence to report when it is not satisfied.
// Without one the reader of gates.json gets the raw reason - often that a file
// could not be read - which names a path rather than a problem.
const specGuard = spec.guard || [];

const checkGuard = (g) => {
  let detail;
  try {
    const r = evaluate(g);
    // Written as "not at least 1" rather than "< 1" so that a score that is
    // not a number fails the guard instead of slipping past it.
    if (r.score >= 1) return;
    detail = r.detail;
  } catch (e) {
    detail = e.message;
  }
  throw new Error(g.unmet
    ? `${g.unmet} [${g.id || g.kind}: ${detail}]`
    : `guard ${g.id || g.kind} not satisfied (${detail})`);
};

const checks = [];
for (const check of spec.checks) {
  let score = 0;
  let detail;
  try {
    for (const g of specGuard) checkGuard(g);
    for (const g of check.guard || []) checkGuard(g);
    const r = evaluate(check);
    score = r.score;
    detail = r.detail;
    if (!Number.isFinite(score)) {
      throw new Error(`${check.kind} produced no number (${detail})`);
    }
  } catch (e) {
    score = 0;
    detail = `not evaluated: ${e.message}`;
  }
  checks.push({
    id: check.id,
    weight: check.weight == null ? 1 : check.weight,
    binary: !!check.binary,
    score,
    detail,
  });
}

fs.writeFileSync(OUT, JSON.stringify({ step: spec.step ?? null, checks }, null, 1) + '\n');
for (const c of checks) {
  process.stderr.write(`gate ${c.id.padEnd(30)} ${c.score.toFixed(3)}  ${c.detail}\n`);
}
