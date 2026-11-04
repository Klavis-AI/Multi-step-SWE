# Third-Party Notices: react-classes-to-19

## 1. Task

Migrate a 2017 React 15 / Redux 3 / react-router 3 dashboard to React 19,
Redux Toolkit, React Router 7, a Vite build and the React Compiler across eight
steps, with behaviour-equivalence and legacy-construct gates.

## 2. Upstream projects and pins

| Upstream | URL | Pin | Bundled or derived in this task |
|---|---|---|---|
| react-redux-echarts | https://github.com/ooooevan/react-redux-echarts | commit `2148e9e29a617e189cd902ef5c9d02a737572b81` (2017-10-07) | `environment/repo/` is the repository at that commit, byte-for-byte, with no `.git`, `node_modules` or `dist`. The same `LICENSE` is retained in every solution tree. The baseline's checked-in `server/demo1/public/` build and its `lib/` runtime assets are part of that repository. |
| Font Awesome | https://fontawesome.com/ | 4.7.0 | `lib/css/font-awesome.css`, `lib/css/font-awesome.min.css`, `lib/fonts/FontAwesome.otf` and `lib/fonts/fontawesome-webfont.{eot,svg,ttf,woff,woff2}` under `environment/repo/`, duplicated under `server/demo1/public/lib/`, and copied into every solution tree's `public/lib/`. |
| es5-shim | https://github.com/es-shims/es5-shim | 4.5.7 | `lib/js/es5-shim.js` and `lib/js/es5-sham.js`, duplicated under `server/demo1/public/lib/js/` and in the step 02 solution tree's `server/demo1/public/lib/js/`. |
| html5shiv | https://github.com/aFarkas/html5shiv | 3.7.0 | `lib/js/html5shiv.js`, duplicated under `server/demo1/public/lib/js/` and in the step 02 solution tree. The bundled file is the minified upstream 3.7.0 source with its banner comment removed; its embedded version string is `3.7.0`. |
| RequireJS | https://github.com/requirejs/requirejs | 2.3.2 | `server/demo1/public/javascripts/require.js` and the step 02 solution tree copy. |
| npm runtime packages in the checked-in build | https://registry.npmjs.org/ | versions recorded in `environment/repo/package.json` as semver ranges; React 15.4.2, ECharts 3.4.0 and zrender 3.3.0 are stated in the bundle | Compiled into `server/demo1/public/vendors-134fc62b.js`, `app-b5fee4db.js` and the route chunk files, and into the step 02 solution tree's `server/demo1/public/` copy. The repository has no lockfile, and the bundle exposes no version strings for the remaining packages. |

The solution trees' migrated applications are authored for the benchmark. Their
`package.json` and `package-lock.json` declare modern npm packages that are
installed at build time; no `node_modules` or built `dist` from those migrations
is stored in the task tree. The step 02 solution tree's `server/demo1/public/`
is a byte-identical copy of the baseline's checked-in build.

## 3. License map

| SPDX ID | File under `LICENSES/` | Component / version covered | In-tree evidence |
|---|---|---|---|
| MIT | `MIT-react-redux-echarts.txt` | react-redux-echarts at commit `2148e9e`; exact upstream `LICENSE`, Copyright (c) 2017 zxhz | `environment/repo/LICENSE`; `steps/*/solution/tree*/LICENSE` (ten byte-identical copies) |
| MIT | `MIT-react-redux.txt` | react-redux, declared `^5.0.3`; exact `LICENSE.md` from 5.0.3, Copyright (c) 2015-present Dan Abramov | `environment/repo/package.json`; compiled into `app-b5fee4db.js` |
| MIT | `MIT-react-router.txt` | react-router, declared `^3.0.2`; exact `LICENSE.md` from 3.0.2, Copyright (c) 2015-present, Ryan Florence, Michael Jackson | `environment/repo/package.json`; compiled into `app-b5fee4db.js` |
| MIT | `MIT-redux.txt` | redux, declared `^3.6.0`; exact `LICENSE.md` from 3.6.0, Copyright (c) 2015-present Dan Abramov | `environment/repo/package.json`; compiled into `app-b5fee4db.js` |
| MIT | `MIT-redux-thunk.txt` | redux-thunk, declared `^2.2.0`; exact `LICENSE.md` from 2.2.0, Copyright (c) 2015 Dan Abramov | `environment/repo/package.json`; compiled into `app-b5fee4db.js` |
| MIT | `MIT.txt` | Canonical SPDX MIT text for packages that declare MIT but ship no licence file: react-icons `^2.2.5`, the Font Awesome 4.7.0 CSS, and html5shiv 3.7.0 | `react-icons` npm metadata and README; Font Awesome CSS banner and npm metadata `(OFL-1.1 AND MIT)`; html5shiv source banner and `composer.json` `["MIT","GPL-2.0"]` |
| MIT | `MIT-es5-shim.txt` | es5-shim 4.5.7; exact upstream `LICENSE`, Copyright (C) 2009-2016 Kristopher Michael Kowal and contributors | `lib/js/es5-shim.js`, `lib/js/es5-sham.js` |
| MIT | `MIT-RequireJS.txt` | RequireJS 2.3.2; exact upstream `LICENSE`, Copyright jQuery Foundation and other contributors. Sample code in the prose documentation is waived via CC0; no sample documentation is bundled. | `server/demo1/public/javascripts/require.js` |
| BSD-3-Clause | `BSD-3-Clause-React.txt` | React 15.4.2, ReactDOM 15.4.2, and react-addons-css-transition-group (declared `^15.6.0`); the exact upstream `LICENSE` is byte-identical for all three packages, Copyright (c) 2013-present, Facebook, Inc. | `vendors-134fc62b.js` and `app-b5fee4db.js` |
| BSD-3-Clause | `BSD-3-Clause-Immutable.txt` | immutable, declared `^3.8.1`; exact upstream `LICENSE` from 3.8.1, Copyright (c) 2014-2015, Facebook, Inc. | `environment/repo/package.json`; compiled into `app-b5fee4db.js` |
| BSD-3-Clause | `BSD-3-Clause-redux-immutable.txt` | redux-immutable, declared `^4.0.0`; exact upstream `LICENSE` from 4.0.0, Copyright (c) 2016, Gajus Kuizinas | `environment/repo/package.json`; compiled into `app-b5fee4db.js` |
| BSD-2-Clause | `BSD-2-Clause-ECharts.txt` | ECharts 3.4.0 and zrender 3.3.0; exact ECharts `LICENSE`, Copyright (c) 2013, Baidu Inc. zrender 3.3.0 carries the same text in its `README.md` | `app-b5fee4db.js` (`version:"3.4.0"`, `dependencies:{zrender:"3.3.0"}`; zrender `version="3.3.0"`) |
| OFL-1.1 | `OFL-1.1.txt` | Font Awesome 4.7.0 fonts; canonical SIL Open Font License 1.1 with the upstream font copyright statement "Copyright Dave Gandy 2016. All rights reserved." | `lib/fonts/*` name table (nameID 0, version 4.7.0 2016) |

`MIT.txt` is the canonical SPDX MIT template because react-icons, Font Awesome's
CSS and html5shiv do not publish a standalone MIT licence file or a copyright
line for the MIT-licensed code. No copyright line has been invented for them.

## 4. Benchmark modifications

- `environment/repo/` is the upstream repository at commit `2148e9e`, copied
  byte-for-byte. No source, asset, build output or `LICENSE` file in it was
  edited for the benchmark.
- The solution trees replace the legacy application with benchmark-authored
  React 16, 17, 18 and 19 migrations, Redux Toolkit, React Router 6/7 and a Vite
  build. They retain the upstream `LICENSE` unchanged.
- Font Awesome, es5-shim, html5shiv and RequireJS assets copied into solution
  trees are byte-identical to the baseline copies.
- The step 02 solution tree's `server/demo1/public/` directory is a byte-identical
  copy of the baseline build.
- The test harness, scenario recordings, gate scripts and documentation under
  `steps/*/tests/` are authored for the benchmark.
- The baseline `package.json` declares `"license": "ISC"`, but the repository
  ships an MIT `LICENSE` and no ISC text. This bundle follows the in-tree
  `LICENSE`; see section 6.

## 5. Bundled third-party dependencies

| Name | Version | License | Bundled path |
|---|---|---|---|
| react-redux-echarts | commit `2148e9e` | MIT | `environment/repo/`; `steps/*/solution/tree*/` |
| React | 15.4.2 | BSD-3-Clause | `environment/repo/server/demo1/public/vendors-134fc62b.js`; step 02 solution tree copy |
| ReactDOM | 15.4.2 | BSD-3-Clause | same `vendors-134fc62b.js` |
| react-addons-css-transition-group | declared `^15.6.0`; resolved version not recorded | BSD-3-Clause | `app-b5fee4db.js` and route chunks |
| react-redux | declared `^5.0.3`; resolved version not recorded | MIT | `app-b5fee4db.js` and route chunks |
| react-router | declared `^3.0.2`; resolved version not recorded | MIT | `app-b5fee4db.js` and route chunks |
| redux | declared `^3.6.0`; resolved version not recorded | MIT | `app-b5fee4db.js` and route chunks |
| redux-thunk | declared `^2.2.0`; resolved version not recorded | MIT | `app-b5fee4db.js` and route chunks |
| redux-immutable | declared `^4.0.0`; resolved version not recorded | BSD-3-Clause | `app-b5fee4db.js` and route chunks |
| immutable | declared `^3.8.1`; resolved version not recorded | BSD-3-Clause | `app-b5fee4db.js` and route chunks |
| react-icons | declared `^2.2.5`; resolved version not recorded | MIT for the package; imported glyphs are Font Awesome 4.7 under OFL-1.1 | `app-b5fee4db.js` and route chunks |
| ECharts | 3.4.0 | BSD-2-Clause | `app-b5fee4db.js` |
| zrender | 3.3.0 | BSD-2-Clause | `app-b5fee4db.js` |
| Font Awesome | 4.7.0 | Fonts: OFL-1.1; CSS: MIT | `environment/repo/lib/{css,fonts}/`, `environment/repo/server/demo1/public/lib/{css,fonts}/`, `steps/*/solution/tree*/public/lib/{css,fonts}/` |
| es5-shim / es5-sham | 4.5.7 | MIT | `environment/repo/lib/js/`, `environment/repo/server/demo1/public/lib/js/`, step 02 solution tree |
| html5shiv | 3.7.0 | MIT or GPL-2.0 (dual); this bundle relies on MIT | `environment/repo/lib/js/html5shiv.js`, duplicated as above |
| RequireJS | 2.3.2 | MIT; CC0 for prose sample code | `environment/repo/server/demo1/public/javascripts/require.js`, step 02 solution tree |

`react-bootstrap` and `babel-polyfill` are declared in `environment/repo/package.json`
but are not compiled into the bundles: the `react-bootstrap` imports and
`babel-polyfill` imports are commented out. The redux-devtools packages are
imported only by `src/components/devTool.js`, which the store does not use, and
do not appear in the build. Express and the other `server/demo1/package.json`
dependencies are not bundled because no `node_modules` is present. The modern
packages named in the solution trees' `package-lock.json` files are installed at
build time and are not redistributed in this task tree.

## 6. Required notices

- react-redux-echarts: "Copyright (c) 2017 zxhz" under the MIT licence in
  `LICENSES/MIT-react-redux-echarts.txt`. The repository's `package.json` says
  `"license": "ISC"`, but the only licence file in the tree is MIT. No ISC text
  is bundled or asserted here.
- React and ReactDOM 15.4.2: "Copyright (c) 2013-present, Facebook, Inc. All
  rights reserved." under BSD-3-Clause. React 15 is not MIT-licensed.
- react-addons-css-transition-group: the same Facebook BSD-3-Clause notice.
- immutable 3.8.1: "Copyright (c) 2014-2015, Facebook, Inc. All rights
  reserved." The licence includes the clause "Neither the name Facebook nor the
  names of its contributors may be used to endorse or promote products derived
  from this software without specific prior written permission."
- redux-immutable 4.0.0: "Copyright (c) 2016, Gajus Kuizinas
  (http://gajus.com/). All rights reserved." The licence includes the
  non-endorsement clause naming Gajus Kuizinas.
- redux and react-redux: "Copyright (c) 2015-present Dan Abramov" under MIT.
- react-router: "Copyright (c) 2015-present, Ryan Florence, Michael Jackson"
  under MIT.
- redux-thunk: "Copyright (c) 2015 Dan Abramov" under MIT.
- react-icons: the npm package declares `"license": "MIT"` and no `LICENSE`
  file; its README says "MIT" and notes that icons are taken from other
  projects. No copyright line is published, so `LICENSES/MIT.txt` is the
  canonical SPDX text without an added line. The glyphs imported by this
  application are Font Awesome icons, covered by OFL-1.1 below.
- ECharts 3.4.0 and zrender 3.3.0: "Copyright (c) 2013, Baidu Inc. All rights
  reserved." under BSD-2-Clause. Both licences carry the additional statement
  that the views and conclusions contained in the software and documentation
  are those of the authors and should not be interpreted as representing
  official policies, either expressed or implied, of the FreeBSD Project.
- Font Awesome 4.7.0: the CSS banner reads "Font Awesome 4.7.0 by @davegandy -
  http://fontawesome.io - @fontawesome; License - http://fontawesome.io/license
  (Font: SIL OFL 1.1, CSS: MIT License)". The fonts' name table records
  "Copyright Dave Gandy 2016. All rights reserved.", version "Version 4.7.0
  2016", and the licence URL "http://fontawesome.io/license/". The fonts are
  redistributed under OFL-1.1; the CSS is redistributed under MIT.
- es5-shim 4.5.7: "Copyright (C) 2009-2016 Kristopher Michael Kowal and
  contributors" under MIT. The file banners say "Copyright 2009-2015 by
  contributors"; the standalone `LICENSE` says 2009-2016 and is the text
  bundled here.
- html5shiv 3.7.0: the exact upstream source banner is "HTML5 Shiv v3.7.0 |
  @afarkas @jdalton @jon_neal @rem | MIT/GPL2 Licensed". The project ships no
  standalone licence file at that tag; `composer.json` declares `["MIT",
  "GPL-2.0"]`. This bundle relies on the MIT option, using `LICENSES/MIT.txt`.
- RequireJS 2.3.2: "Copyright jQuery Foundation and other contributors,
  https://jquery.org/" under MIT. The licence also waives copyright and related
  rights for sample code displayed in the prose documentation via CC0; no such
  documentation is bundled.
- The names React, ReactDOM, Redux, React Router, Font Awesome, ECharts and the
  other project names are trademarks or names of their respective owners. No
  trademark licence is granted by the licences above.

## 7. Closing note

Task-authored benchmark content is licensed separately by the benchmark maintainers.
