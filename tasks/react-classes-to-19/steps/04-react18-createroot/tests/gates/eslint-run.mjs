// Runs the scorer's lint configuration over the tree under test and reports
// findings per rule, per site.
//
// Two things it records beyond the counts, both there to stop a lint gate from
// passing for the wrong reason:
//
//   configuredRules - the rules the config actually turned on, read back out of
//                     ESLint's own resolved configuration. A gate that names a
//                     rule which no longer exists would otherwise see zero
//                     findings and score full marks, and rule names in this
//                     plugin have moved before.
//   filesLinted     - how many files were looked at. Zero files and zero
//                     findings are indistinguishable in a count, and a candidate
//                     that moves its sources somewhere the walk misses would
//                     otherwise clear every lint gate by being invisible.
//
// What is looked at is the tree, not a glob. This used to lint `src/**` plus the
// files at the repository root, which meant a component under `app/` or `lib/`
// was read by ast-scan.mjs and invisible here - two instruments disagreeing
// about what the tree is, with the lint gate taking the lenient half. The walk
// below is ast-scan's: its skipped directories, its skipped paths, and explicit
// file paths handed to ESLint rather than patterns, so what was linted is a list
// rather than an inference.
//
// Suppressions are not counted here; ast-scan.mjs counts them as findings in
// their own right. That count is belt-and-braces now rather than the instrument:
// the config sets `linterOptions.noInlineConfig`, so a comment cannot switch the
// scored rule off in the first place.
//
// usage: node eslint-run.mjs --root /app --config /tests/gates/eslint.config.mjs
//                            --json /logs/verifier/eslint.json
import fs from 'node:fs';
import path from 'node:path';
import { ESLint } from 'eslint';
import { inlineScripts } from './reach.mjs';

const argv = process.argv.slice(2);
const arg = (n, d) => {
  const i = argv.indexOf(`--${n}`);
  return i === -1 ? d : argv[i + 1];
};
const ROOT = path.resolve(arg('root', '/app'));
const CONFIG = arg('config', path.join(path.dirname(new URL(import.meta.url).pathname), 'eslint.config.mjs'));
const JSON_OUT = arg('json', null);

// ast-scan.mjs's scope, spelled the same way: installed packages, version
// control metadata and tool caches, plus the two directories the interface
// defines as build output. The extension list is the config's own, which is
// ast-scan's set plus `.mts`/`.cts`, so nothing this walk hands over falls
// outside what the config configures.
// Tool-cache names are no longer skipped (wave D 2026-10-12; ast-scan.mjs says why).
// `.git` is skipped only at the repository root (wave H 2026-10-13, C-1; ast-scan.mjs).
const SKIP = new Set(['node_modules']);
const SKIP_PATHS = new Set(['dist', '.git', path.join('server', 'demo1', 'public')]);
const SOURCE = /\.(js|jsx|mjs|cjs|ts|tsx|mts|cts)$/;
const HTML_NAME = /\.html$/i;

const ownSources = (root, keep = SOURCE) => {
  const out = [];
  const walk = (dir) => {
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      if (SKIP.has(e.name)) continue;
      const full = path.join(dir, e.name);
      const rel = path.relative(root, full);
      if (SKIP_PATHS.has(rel)) continue;
      // A symlink is not this tree's source however it is spelled, and
      // following one out of the root would lint somebody else's files.
      if (e.isSymbolicLink()) continue;
      if (e.isDirectory()) walk(full);
      else if (e.isFile() && keep.test(e.name)) out.push(rel);
    }
  };
  walk(root);
  return out.sort();
};

const result = {
  root: ROOT,
  config: CONFIG,
  ok: false,
  error: null,
  filesLinted: 0,
  errorCount: 0,
  warningCount: 0,
  fatalCount: 0,
  byRule: {},
  configuredRules: [],
  sample: [],
};

const write = () => {
  const text = JSON.stringify(result, null, 1) + '\n';
  if (JSON_OUT) {
    fs.writeFileSync(JSON_OUT, text);
    try {
      fs.chmodSync(JSON_OUT, 0o644);
    } catch {}
  } else {
    process.stdout.write(text);
  }
};

try {
  const eslint = new ESLint({
    cwd: ROOT,
    overrideConfigFile: CONFIG,
    // The tree's own .eslintrc / eslint.config.* must not participate: this gate
    // is the scorer's question, asked the same way of every candidate.
    flags: [],
    errorOnUnmatchedPattern: false,
  });

  // A representative in-scope file is enough to read the resolved rule set back
  // out; which file does not matter, since the config has one block.
  const probe = ['src/index.js', 'src/main.jsx', 'src/main.tsx', 'src/index.jsx', 'src/index.tsx']
    .map((p) => path.join(ROOT, p))
    .find((p) => fs.existsSync(p)) || path.join(ROOT, 'src/__probe__.jsx');
  const resolved = await eslint.calculateConfigForFile(probe);
  result.configuredRules = Object.keys(resolved.rules || {}).sort();

  // An empty tree has no files to hand over, and `lintFiles([])` is an error
  // rather than an empty run. Zero files linted is reported, and the gates that
  // read it refuse rather than score a clean sheet.
  const targets = ownSources(ROOT);
  const results = targets.length ? await eslint.lintFiles(targets) : [];
  // And the scripts written inline in HTML files, which the page runs as it
  // runs a module: each body is linted as `<file>.html#inline-module-<n>.js`
  // (or `#inline-script-<n>.js`), the name ast-scan.mjs gives it plus an
  // extension the config's file pattern matches, through reach.mjs's
  // extractor.  (wave H 2026-10-14, H-CR8, final review G2-R7b)
  for (const rel of ownSources(ROOT, HTML_NAME)) {
    let html;
    try {
      html = fs.readFileSync(path.join(ROOT, rel), 'utf8');
    } catch {
      continue;
    }
    for (const sc of inlineScripts(html)) {
      results.push(...await eslint.lintText(sc.body, { filePath: path.join(ROOT, `${rel}#${sc.name}.js`) }));
    }
  }

  for (const r of results) {
    result.filesLinted++;
    result.errorCount += r.errorCount;
    result.warningCount += r.warningCount;
    result.fatalCount += r.fatalErrorCount || 0;
    for (const m of r.messages) {
      // A parse error has no ruleId. It is a finding too - an unparseable file
      // is not a migrated file - and it is counted under its own key so it never
      // hides inside a rule's total.
      const key = m.ruleId || (m.fatal ? '<parse-error>' : '<no-rule>');
      result.byRule[key] = (result.byRule[key] || 0) + 1;
      if (result.sample.length < 40) {
        result.sample.push({
          file: path.relative(ROOT, r.filePath),
          line: m.line,
          rule: key,
          message: m.message,
        });
      }
    }
  }
  result.ok = true;
} catch (e) {
  result.error = e.message;
}

write();
process.stderr.write(result.ok
  ? `eslint: ${result.filesLinted} files, ${result.errorCount} errors, `
    + `${Object.keys(result.byRule).length} rules with findings, `
    + `${result.configuredRules.length} rules configured\n`
  : `eslint: did not run: ${result.error}\n`);
