// The scorer's lint configuration, never the candidate's.
//
// A gate that ran the tree's own config would be asking the candidate to grade
// itself: the config is a file in the repo, and turning a rule off is easier than
// satisfying it. This one lives in the verifier image, is mounted read-only, and
// its plugins resolve out of the verifier's node_modules.
//
// The parser is typescript-eslint's, for both .js and .ts: it accepts JSX and
// modern syntax without a project reference, so a candidate that migrates to
// TypeScript is linted by the same gate as one that stays on JavaScript. Type
// information is deliberately not required - a gate that needed tsconfig.json to
// be correct would fail trees for the wrong reason.
//
// Only the react-hooks plugin is configured. It is the one that answers the
// questions this migration is about: at v6 it carries both the classic hook
// rules and the React Compiler rules, so "the components are real hooks
// components" and "the components are compilable" are the same instrument.
import reactHooks from 'eslint-plugin-react-hooks';
import tsParser from '@typescript-eslint/parser';

// v6 renamed the flat preset; accept either name rather than pinning to one, and
// fail loudly if neither is there - an empty rule set would make every lint gate
// pass by scoring zero findings.
const preset = reactHooks.configs['recommended-latest'] || reactHooks.configs.recommended;
if (!preset || !preset.rules || Object.keys(preset.rules).length === 0) {
  throw new Error('eslint-plugin-react-hooks exposes no flat recommended preset');
}

export default [
  {
    files: ['**/*.{js,jsx,mjs,cjs,ts,tsx,mts,cts}'],
    // The same directories ast-scan.mjs skips, and no others. The two
    // instruments have to agree about what the tree is: while this list also
    // held `server/**`, a component parked outside `src/` was read by the AST
    // scan and invisible to the lint gate, and the instruction puts every
    // JavaScript file in the repository in scope. The mock API server is still
    // the fixed half of the contract and the candidate is not asked to migrate
    // it - it is simply not exempt from being looked at.
    ignores: [
      '**/node_modules/**',
      // The repository root's own version-control directory only: a `.git`
      // below the root is read like any other directory (wave H 2026-10-13, C-1).
      '.git/**',
      // Build output, by the interface's own definition of where the build
      // lands, and the README's own description of the packaged copy beside it.
      // Linting a minified bundle reports thousands of findings that say
      // nothing about the sources.
      'dist/**',
      'server/demo1/public/**',
    ],
    languageOptions: {
      parser: tsParser,
      ecmaVersion: 'latest',
      sourceType: 'module',
      parserOptions: { ecmaFeatures: { jsx: true } },
    },
    plugins: { 'react-hooks': reactHooks },
    // No configuration comment in the tree under test may change what this
    // config asks. Without it, one line at the top of a file -
    // `/* eslint react-hooks/rules-of-hooks: off */` - silences the scored rule
    // in that file, and it is not a disable directive, so the suppression
    // counter in ast-scan.mjs does not see it either. Counting comment shapes
    // cannot win that race; refusing every inline directive can, and it costs
    // an honest tree nothing: measured on the reference solution, setting this
    // changes neither the finding count nor the per-rule breakdown.
    linterOptions: { noInlineConfig: true },
    // Everything the preset turns on, at error severity. Severity matters
    // because a warning is a finding a candidate can ignore, and rule 11 counts
    // findings.
    rules: Object.fromEntries(Object.keys(preset.rules).map((r) => [r, 'error'])),
  },
];
