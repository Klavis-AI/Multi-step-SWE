import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [
    react({
      babel: {
        plugins: [['babel-plugin-react-compiler', { target: '19' }]],
      },
    }),
  ],
  // Every component in src/ is a .js file containing JSX, as it was under
  // babel-preset-react.  Renaming ~70 files to .jsx would be a bigger diff than
  // the migration itself, so esbuild - which is what actually performs the JSX
  // transform in a production build, the React plugin's babel pass being skipped
  // when fast refresh is off - is told to read src/*.js with the jsx loader.
  // `exclude` has to be given as well: it defaults to /\.js$/, which would
  // otherwise throw away everything `include` just selected.
  esbuild: { include: /src\/.*\.js$/, exclude: [], loader: 'jsx' },
  // The same thing for the dev server's dependency scan.
  optimizeDeps: {
    esbuildOptions: { loader: { '.js': 'jsx' } },
  },
  build: {
    // The old build emitted app and vendors chunks plus one per require.ensure
    // group; the dynamic import()s in components/app.js keep that split.
    sourcemap: true,
    rollupOptions: {
      output: {
        // Static assets keep their own names.  lib/ and favicon.ico are served
        // verbatim out of public/, the way `npm run Copy` used to place them.
        assetFileNames: 'assets/[name][extname]',
      },
    },
  },
});
