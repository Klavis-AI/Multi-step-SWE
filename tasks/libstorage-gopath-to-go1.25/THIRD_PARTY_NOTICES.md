# Third-Party Notices — libstorage-gopath-to-go1.25

**Task.** Eight-step benchmark that modernizes libStorage (thecodeteam/libstorage,
2017) from a GOPATH-era project to Go 1.25, one milestone at a time, graded on
the behaviour of the tree each step produces and on the constructs the
milestone retires.

## 1. Upstream project

| | |
| --- | --- |
| Project | libStorage (thecodeteam/libstorage, formerly codedellemc/libstorage) |
| URL | <https://github.com/thecodeteam/libstorage> |
| Pin | commit `b4ebd75f857ac3077ee779bd240e92cd93de04cd` |
| Lockfile | `glide.lock`, updated 2017-07-17, hash `eb1fb9cabac5c94ad5fc7706afdab0607ae73517ccce8a385a2d042728fda3d4` |
| Bundled/derived | The complete upstream worktree, including `vendor/` and the documentation theme under `.docs-themes/yeti/`, is copied into the image at `/app`. The 39 `glide.lock` imports and `go.uber.org/goleak v1.3.0` are also republished as a file-based Go module mirror. |

## 2. License files

| SPDX ID | File under `LICENSES/` | Upstream component / versions covered |
| --- | --- | --- |
| Apache-2.0 | `Apache-2.0.txt` | Exact upstream libStorage root `LICENSE` (11,325 bytes); also covers the seven Apache-2.0 vendored modules listed in §5 |
| MIT | `MIT.txt` | SPDX canonical MIT text; covers the MIT vendored modules, `go.uber.org/goleak v1.3.0`, and the MIT-licensed documentation assets |
| BSD-2-Clause | `BSD-2-Clause.txt` | SPDX canonical two-clause BSD text; covers four vendored modules, the nested `check.v1` copy, and the MkDocs Yeti theme |
| BSD-3-Clause | `BSD-3-Clause.txt` | SPDX canonical three-clause BSD text; covers eleven vendored modules and `highlight.js` |
| ISC | `ISC.txt` | SPDX canonical ISC text; covers `github.com/davecgh/go-spew` |
| MPL-2.0 | `MPL-2.0.txt` | Exact `github.com/hashicorp/hcl` `LICENSE` (15,977 bytes) |
| LGPL-3.0 | `LGPL-3.0.txt` | Exact `gopkg.in/yaml.v1` `LICENSE` (8,790 bytes): the Canonical special exception followed by the full GNU Lesser General Public License version 3 |
| GPL-2.0 | `GPL-2.0.txt` | GNU General Public License version 2 text, covering `github.com/cesanta/validate-json`, which is dual-licensed GPL-2.0 or commercial (§5, §6) |
| OFL-1.1 | `OFL-1.1.txt` | SPDX canonical SIL Open Font License 1.1, covering the Font Awesome 4.0.3 fonts |

Every vendored package also carries its own upstream license file, which is
listed in §5. The canonical MIT, BSD and ISC texts under `LICENSES/` retain the
SPDX placeholders `<year>` and `<copyright holders>`; the copyright line of
each package they cover is listed in §5, and the package's exact upstream
license file remains in `environment/libstorage/vendor/`.

## 3. What is bundled or derived

- `environment/libstorage/` — the upstream worktree at the pin, with `.git`
  omitted. It contains the complete `vendor/` tree (39 glide modules and their
  nested test dependencies) and the `.docs-themes/yeti/` documentation theme.
- `environment/gomodmirror/` — a file-based `GOPROXY` that publishes each of
  the 39 `glide.lock` modules at exactly the locked pseudo-version, plus
  `go.uber.org/goleak v1.3.0`, as `@v/*.zip`, `*.mod` and `*.info` files.
  `MIRROR-INDEX.txt` records every module, pseudo-version and SHA-256.
- `environment/Dockerfile`, `environment/warm_module_cache.sh` and
  `environment/timer.sh` — task-authored image tooling.
- `steps/01..08/` — task-authored `instruction.md` and `tests/`; each step's
  `solution/` is a task-authored `solve.sh`, not upstream code.

## 4. Modifications made by the benchmark

- The upstream libStorage tree is copied without changes to its file contents.
  No vendored package, documentation theme asset or upstream source file is
  edited, added or removed by the benchmark.
- The module mirror is task-authored build infrastructure. Its zip payloads
  reproduce the vendored module versions; they are not modifications of them.
- The Dockerfile adds the upstream tree to `GOPATH` through a symlink and
  copies `timer.sh` to `/app/timer.sh`. Neither is part of the upstream
  repository.
- No deleted upstream directory is restored, and no upstream license file is
  altered.

## 5. Third-party dependencies bundled in the task

All vendored modules below are at `environment/libstorage/vendor/<module>` and
are also republished at `environment/gomodmirror/<module>/@v/`, except
`go.uber.org/goleak`, which exists only in the mirror. Pins are the exact
`glide.lock` commit hashes.

### Apache-2.0 (`LICENSES/Apache-2.0.txt`)

| Module | Pin | Upstream license file |
| --- | --- | --- |
| `github.com/akutz/gofig` | `65fa82bc59644020b3ad6be479c0954fc82bd0fc` | `LICENSE` |
| `github.com/akutz/golf` | `296de562c59e98534dc2d7b49f6ba310e3edb69d` | `LICENSE` |
| `github.com/akutz/goof` | `72efb1916248a881f8d39e20b9709696c5eda708` | `LICENSE` |
| `github.com/akutz/gotil` | `6fa2e80bd3ac40f15788cfc3d12ebba49a0add92` | `LICENSE` |
| `github.com/codedellemc/gournal` | `4293aaf7e91602963a5777caef4b346e1cf21936` | `LICENSE` |
| `github.com/spf13/afero` | `52e4a6cfac46163658bd4f123c49b6ee7dc75f78` | `LICENSE.txt` |
| `gopkg.in/yaml.v2` | `bc35f417f8a7664a73d46c9def2933417c03019f` | `LICENSE`, `Copyright 2011-2016 Canonical Ltd.` |

### MIT (`LICENSES/MIT.txt`)

| Module | Pin | Copyright line in the vendored license |
| --- | --- | --- |
| `github.com/SermoDigital/jose` | `f6df55f235c24f236d11dbcf665249a59ac2021f` | `Copyright (c) 2015 Sermo Digital LLC` |
| `github.com/Sirupsen/logrus` | `9dae9b48f12af7b929a16890896d5a8d980fa9ea` | `Copyright (c) 2014 Simon Eskildsen` |
| `github.com/asaskevich/govalidator` | `7b3beb6df3c42abd3509abfc3bcacc0fbfb7c877` | `Copyright (c) 2014 Alex Saskevich` |
| `github.com/dgrijalva/jwt-go` | `2268707a8f0843315e2004ee4f1d021dc08baedf` | `Copyright (c) 2012 Dave Grijalva` |
| `github.com/mitchellh/mapstructure` | `f3009df150dadf309fdee4a54ed65c124afad715` | `Copyright (c) 2013 Mitchell Hashimoto` |
| `github.com/onsi/ginkgo` | `77a8c1e5c40d6bb6c5eb4dd4bdce9763564f6298` | `Copyright (c) 2013-2014 Onsi Fakhouri` |
| `github.com/onsi/gomega` | `334b8f472b3af5d541c5642701c1e29e2126f486` | `Copyright (c) 2013-2014 Onsi Fakhouri` |
| `github.com/pelletier/go-buffruneio` | `df1e16fde7fc330a0ca68167c23bf7ed6ac31d6d` | `Copyright (c) 2016 Thomas Pelletier` (stated in `README.md`; no `LICENSE` file is vendored) |
| `github.com/pelletier/go-toml` | `45932ad32dfdd20826f5671da37a5f3ce9f26a8d` | `Copyright (c) 2013 - 2016 Thomas Pelletier, Eric Anderton` |
| `github.com/spf13/cast` | `2580bc98dc0e62908119e4737030cc2fdfc45e4c` | `Copyright (c) 2014 Steve Francia` |
| `github.com/spf13/jwalterweatherman` | `33c24e77fb80341fe7130ee7c594256ff08ccc46` | `Copyright (c) 2014 Steve Francia` |
| `github.com/spf13/viper` | `651d9d916abc3c3d6a91a12549495caba5edffd2` | `Copyright (c) 2014 Steve Francia` |
| `github.com/stretchr/testify` | `4d4bfba8f1d1027c4fdbe371823030df51419987` | `Copyright (c) 2012 - 2013 Mat Ryer and Tyler Bunnell` |
| `go.uber.org/goleak` | `v1.3.0` (module mirror only) | `Copyright (c) 2018 Uber Technologies, Inc.` |

### BSD-2-Clause (`LICENSES/BSD-2-Clause.txt`)

| Module | Pin | Copyright line in the vendored license |
| --- | --- | --- |
| `github.com/cesanta/ucl` | `97c016fce90e6af1b14558563ac46852167e6a76` | `Copyright (c) 2014-2015, Maxim Ignatenko` |
| `github.com/magiconair/properties` | `0723e352fa358f9322c938cc2dadda874e9151a9` | `Copyright (c) 2013-2014 - Frank Schroeder` |
| `github.com/pkg/errors` | `248dadf4e9068a0b3e79f02ed0a610d935de5302` | `Copyright (c) 2015, Dave Cheney <dave@cheney.net>` |
| `github.com/pkg/sftp` | `4d0e916071f68db74f8a73926335f809396d6b42` | `Copyright (c) 2013, Dave Cheney` |

### BSD-3-Clause (`LICENSES/BSD-3-Clause.txt`)

| Module | Pin | Copyright line in the vendored license |
| --- | --- | --- |
| `github.com/fsnotify/fsnotify` | `fd9ec7deca8bf46ecd2a795baaacf2b3a9be1197` | `Copyright (c) 2012 The Go Authors. All rights reserved.` and `Copyright (c) 2012 fsnotify Authors. All rights reserved.` |
| `github.com/gorilla/context` | `08b5f424b9271eedf6f9f0ce86cb9396ed337a42` | `Copyright (c) 2012 Rodrigo Moraes. All rights reserved.` |
| `github.com/gorilla/mux` | `757bef944d0f21880861c2dd9c871ca543023cba` | `Copyright (c) 2012 Rodrigo Moraes. All rights reserved.` |
| `github.com/kardianos/osext` | `9d302b58e975387d0b4d9be876622c86cefe64be` | `Copyright (c) 2012 The Go Authors. All rights reserved.` |
| `github.com/kr/fs` | `2788f0dbd16903de03cb8186e5c7d97b69ad387b` | `Copyright (c) 2012 The Go Authors. All rights reserved.` |
| `github.com/pmezard/go-difflib` | `d8ed2627bdf02c080bf22230dbb337003b7aba2d` | `Copyright (c) 2013, Patrick Mezard` |
| `github.com/spf13/pflag` | `d16db1e50e33dff1b6cdf37596cef36742128670` | `Copyright (c) 2012 Alex Ogier. All rights reserved.` and `Copyright (c) 2012 The Go Authors. All rights reserved.` |
| `golang.org/x/crypto` | `453249f01cfeb54c3d549ddb75ff152ca243f9d8` | `Copyright (c) 2009 The Go Authors. All rights reserved.` |
| `golang.org/x/net` | `b336a971b799939dd16ae9b1df8334cb8b977c4d` | `Copyright (c) 2009 The Go Authors. All rights reserved.` |
| `golang.org/x/sys` | `002cbb5f952456d0c50e0d2aff17ea5eca716979` | `Copyright (c) 2009 The Go Authors. All rights reserved.` |
| `golang.org/x/text` | `85c29909967d7f171f821e7a42e7b7af76fb9598` | `Copyright (c) 2009 The Go Authors. All rights reserved.` |

The four `golang.org/x/` modules also ship `PATENTS` files granting the
additional patent license reproduced there.

### ISC, MPL-2.0, LGPL-3.0 and GPL-2.0

| Module | Pin | License | Vendored license file |
| --- | --- | --- | --- |
| `github.com/davecgh/go-spew` | `04cdfd42973bb9c8589fd6a731800cf222fde1a9` | ISC (`Copyright (c) 2012-2013 Dave Collins <dave@davec.name>`) | `LICENSE` |
| `github.com/hashicorp/hcl` | `f74cf8281543a0797d7b4ab7d88e76e7ba125308` | MPL-2.0 | `LICENSE` |
| `gopkg.in/yaml.v1` | `9f9df34309c04878acc86042b16630b0f696e1de` | LGPL-3.0 with the Canonical special exception (`Copyright (c) 2011-2014 - Canonical Inc.`) | `LICENSE` |
| `github.com/cesanta/validate-json` | `2f16017c76fc2403d143e93cea1e1b9526a01148` | GPL-2.0, alternatively a commercial licence (`Copyright (c) 2015 Cesanta Software Limited`) | `LICENSE` |

### Nested packages vendored inside the modules above

| Nested package | License | Copyright line | Path |
| --- | --- | --- | --- |
| `gopkg.in/check.v1` | BSD-2-Clause | `Copyright (c) 2010-2013 Gustavo Niemeyer <gustavo@niemeyer.net>` | `vendor/github.com/magiconair/properties/_third_party/gopkg.in/check.v1/` |
| `github.com/mattn/go-colorable` | MIT | `Copyright (c) 2016 Yasuhiro Matsumoto` | `vendor/github.com/onsi/ginkgo/reporters/stenographer/support/go-colorable/` |
| `github.com/mattn/go-isatty` | MIT | `Copyright (c) Yasuhiro MATSUMOTO <mattn.jp@gmail.com>` | `vendor/github.com/onsi/ginkgo/reporters/stenographer/support/go-isatty/` |
| `github.com/davecgh/go-spew` | ISC | `Copyright (c) 2012-2013 Dave Collins <dave@davec.name>` | `vendor/github.com/stretchr/testify/vendor/github.com/davecgh/go-spew/` |
| `github.com/pmezard/go-difflib` | BSD-3-Clause | `Copyright (c) 2013, Patrick Mezard` | `vendor/github.com/stretchr/testify/vendor/github.com/pmezard/go-difflib/` |
| `github.com/stretchr/objx` | MIT | `Copyright (c) 2014 Stretchr, Inc.` | `vendor/github.com/stretchr/testify/vendor/github.com/stretchr/objx/` |
| libyaml Go port | MIT | `Copyright (c) 2006 Kirill Simonov` | `vendor/gopkg.in/yaml.v1/LICENSE.libyaml`, `vendor/gopkg.in/yaml.v2/LICENSE.libyaml` |

### Documentation theme assets

These files are part of the upstream libStorage tree under
`environment/libstorage/.docs-themes/yeti/`.

| Component | Version | License | Copyright / attribution | Bundled path |
| --- | --- | --- | --- | --- |
| Bootstrap | 3.0.3 | Apache-2.0 | `Copyright 2013 Twitter, Inc.` | `js/bootstrap-3.0.3.min.js`, `css/bootstrap-custom.min.css` |
| normalize.css | 2.1.3, embedded at the top of `bootstrap-custom.min.css` | MIT | Header: `normalize.css v2.1.3 | MIT License | git.io/normalize` | `css/bootstrap-custom.min.css` |
| jQuery | 1.10.2 | MIT | `(c) 2005, 2013 jQuery Foundation, Inc. | jquery.org/license` | `js/jquery-1.10.2.min.js` |
| Font Awesome | 4.0.3 | SIL OFL 1.1 for the fonts, MIT for the CSS | `Font Awesome 4.0.3 by @davegandy` | `css/font-awesome-4.0.3.css`, `fonts/fontawesome-webfont.{eot,svg,ttf,woff}` |
| highlight.js | version not stated in the bundle (the file is the one shipped by the MkDocs Yeti theme in mkdocs-bootswatch 0.2.0–0.5.0) | BSD-3-Clause | `Copyright (c) 2006, Ivan Sagalaev`; GitHub style by Vasily Polovnyov | `js/highlight.pack.js`, `css/highlight.css` |
| MkDocs Yeti theme | MkDocs 0.16 / mkdocs-bootswatch lineage | BSD-2-Clause | `Copyright © 2014-present, Tom Christie` and `Copyright © 2015, Dougal Matthews` | `*.html`, `js/base.js`, `css/base.css` (modified by libStorage; no header in the copied files) |

## 6. Required notices

- Apache-2.0: the libStorage root `LICENSE` carries no copyright line and the
  repository ships no `NOTICE` file. The vendored Apache-2.0 modules similarly
  ship no `NOTICE` files. No Apache NOTICE text is reproduced.
- MPL-2.0 (`hashicorp/hcl`): the source form is included unmodified in
  `vendor/github.com/hashicorp/hcl/`, satisfying the MPL-2.0 source
  availability requirement; the license text is `LICENSES/MPL-2.0.txt`.
- LGPL-3.0 (`gopkg.in/yaml.v1`): the complete source and the upstream
  `LICENSE` (which begins with the Canonical special exception permitting
  static or dynamic linking without the §4d/§4e obligations) are included in
  `vendor/gopkg.in/yaml.v1/`. `LICENSES/LGPL-3.0.txt` reproduces that exact
  upstream license file.
- GPL-2.0 (`cesanta/validate-json`): the upstream `LICENSE` offers the choice
  of GPL-2.0 or a commercial license. This task redistributes the package under
  GPL-2.0; its complete source is in `vendor/github.com/cesanta/validate-json/`
  and the full GPL-2.0 text is `LICENSES/GPL-2.0.txt`.
- Copyright lines for the MIT, BSD, ISC, Apache and MPL components are listed
  in the §5 tables. Each vendored package retains its own upstream license
  file; the canonical SPDX texts under `LICENSES/` cover the same terms.
- Documentation assets: Bootstrap `Copyright 2013 Twitter, Inc.` under
  Apache-2.0; jQuery `(c) 2005, 2013 jQuery Foundation, Inc.` under MIT;
  Font Awesome 4.0.3 by Dave Gandy, fonts under SIL OFL 1.1 and CSS under MIT;
  highlight.js `Copyright (c) 2006, Ivan Sagalaev` under BSD-3-Clause; the
  MkDocs Yeti theme under BSD-2-Clause. No trademark statement is made by any
  of these upstream license files.
- Neither libStorage nor any vendored module makes a trademark statement in its
  license file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
