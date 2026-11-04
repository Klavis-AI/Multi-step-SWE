// Reports what is actually installed in the tree under test, for the gates that
// ask about the dependency graph rather than about source text.
//
// Two different questions, and both matter:
//
//   declared  - what package.json asks for.  A range, possibly a lie: `^15.4.2`
//               and `^19.0.0` are both satisfiable by an install that never ran.
//   installed - what node_modules/<name>/package.json says after the install the
//               build just did.  This is the artifact, and it is the one a
//               retired-toolchain gate has to read: a package can vanish from
//               `dependencies` and still be in the tree as somebody's transitive
//               dependency, which is exactly the hiding place a text scan of
//               package.json misses.
//
// Resolution walks node_modules upwards from the root the way node does, so a
// nested copy is found and reported with the path it was found at.
//
// usage: node deps.mjs /app [--json out.json]
import fs from 'node:fs';
import path from 'node:path';

const argv = process.argv.slice(2);
const ROOT = path.resolve(argv.find((a) => !a.startsWith('--')) || '/app');
const JSON_OUT = (() => {
  const i = argv.indexOf('--json');
  return i === -1 ? null : argv[i + 1];
})();

const readJson = (p) => {
  try {
    return JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch {
    return null;
  }
};

const rootPkg = readJson(path.join(ROOT, 'package.json')) || {};
const declared = {
  ...(rootPkg.dependencies || {}),
  ...(rootPkg.devDependencies || {}),
  ...(rootPkg.peerDependencies || {}),
  ...(rootPkg.optionalDependencies || {}),
};

// Every directory in the tree that is a package root, keyed by package name.
// One name can appear more than once (npm nests on version conflict) and a gate
// that says "this package is gone" must be false if any copy survives.
const installed = {};
const walkModules = (dir, depth) => {
  if (depth > 6) return;
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const e of entries) {
    if (!e.isDirectory() && !e.isSymbolicLink()) continue;
    const full = path.join(dir, e.name);
    if (e.name.startsWith('@')) {
      walkModules(full, depth);
      continue;
    }
    const meta = readJson(path.join(full, 'package.json'));
    // What the install *path* says this package is, beside what the package says
    // it is.  The gate asks the question this way round because a dependency is
    // absent when nothing named it is installed *and* nothing is installed in
    // its place: `"prop-types": "npm:some-other-package@1.0.0"` puts a package
    // of another name at node_modules/prop-types, and a reading keyed only on
    // the name in its own package.json does not see it sitting there.
    //
    // The scope is part of the specifier a path implies - node_modules/@foo/bar
    // is `@foo/bar` and never a bare `bar`, which could collide with a real
    // package of that name somewhere else in the tree.  When the two agree,
    // which is every package in a tree with no aliases, nothing is added: the
    // second key is only ever a key the first reading did not already have.
    const scope = path.basename(dir);
    const specifier = scope.startsWith('@') ? `${scope}/${e.name}` : e.name;
    if (meta && meta.name) {
      const record = (key) => {
        (installed[key] = installed[key] || []).push({
          version: meta.version || null,
          at: path.relative(ROOT, full),
        });
      };
      record(meta.name);
      if (specifier !== meta.name) record(specifier);
    }
    const nested = path.join(full, 'node_modules');
    if (fs.existsSync(nested)) walkModules(nested, depth + 1);
  }
};
const topModules = path.join(ROOT, 'node_modules');
if (fs.existsSync(topModules)) walkModules(topModules, 0);

// Sort each name's copies so the report is stable across runs regardless of
// readdir order.
for (const name of Object.keys(installed)) {
  installed[name].sort((a, b) => (a.at < b.at ? -1 : a.at > b.at ? 1 : 0));
}

const result = {
  root: ROOT,
  declared,
  installed,
  counts: {
    declared: Object.keys(declared).length,
    installed: Object.keys(installed).length,
    // Distinct install directories, not records: a package reachable under two
    // specifiers is one copy on disk and is counted once here.
    copies: new Set(Object.values(installed).flat().map((c) => c.at)).size,
  },
};

if (JSON_OUT) fs.writeFileSync(JSON_OUT, JSON.stringify(result, null, 1) + '\n');
else process.stdout.write(JSON.stringify(result, null, 1) + '\n');

process.stderr.write(`deps: ${result.counts.declared} declared, `
  + `${result.counts.installed} installed (${result.counts.copies} copies)\n`);
