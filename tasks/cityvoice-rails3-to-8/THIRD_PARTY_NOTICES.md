# Third-Party Notices

## Task

`cityvoice-rails3-to-8` — migrate the Code for America CityVoice civic IVR
application from Rails 4.0.0 / Ruby 2.1.7 to Rails 8.1 / Ruby 3.4, one release
at a time.

## Upstream project

| Field | Value |
| --- | --- |
| Project | CityVoice |
| URL | <https://github.com/codeforamerica/cityvoice> |
| Pin | commit `7308d4eeea2aae1f61e3a4e9b9f9aaf97a1e6b57` (2016-01-19, the repository's last commit) |
| Bundled path | `environment/repo/` — full Rails application at the pin |
| Upstream license | MIT; copyright line `Copyright (c) 2013-2014 Code for America` (`environment/repo/LICENSE.md`, also stated in `README.md`) |

## License files

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| MIT | `MIT.txt` | CityVoice at commit `7308d4e`. Exact copy of `environment/repo/LICENSE.md`. |
| Public-Domain | `Public-Domain.txt` | `ruby-advisory-db` snapshot commit `44784c295391577f25d198a9205eae4ba73ec4da`. Exact copy of the `LICENSE.txt` inside `environment/ruby-advisory-db.tar.gz`. |
| MIT | `MIT-audiojs.txt` | audio.js player assets bundled at `environment/repo/app/assets/images/audiojs.swf` and `environment/repo/app/assets/images/player-graphics.gif`. Exact upstream audio.js `LICENSE`. |

## Bundled third-party dependencies

| Component | Version | License | Bundled path |
| --- | --- | --- | --- |
| ruby-advisory-db | commit `44784c295391577f25d198a9205eae4ba73ec4da` (2026-09-16) | Public-domain statement with an OSVDB caveat (see below) | `environment/ruby-advisory-db.tar.gz` |

The tarball is a snapshot of <https://github.com/rubysec/ruby-advisory-db>. Its
`PROVENANCE.txt` records that it keeps `gems/` and `LICENSE.txt`; the
repository's specs, docs, Rakefile and tooling are not shipped because nothing
in the task runs them. Its `LICENSE.txt` states that submissions are released
into the public domain, but that not all of the database can be considered
public domain: it may contain information copyrighted by the Open Source
Vulnerability Database (OSVDB), and users must review the OSVDB license terms.
The snapshot is unmodified.

The application also ships two small third-party front-end artifacts:

| Component | Version | License | Bundled path |
| --- | --- | --- | --- |
| audio.js player assets (`audiojs.swf`, `player-graphics.gif`) | no version stated in the tree | MIT; `Copyright (c) 2010 Anthony Kolber` | `environment/repo/app/assets/images/audiojs.swf`, `environment/repo/app/assets/images/player-graphics.gif` |
| Jasmine-Ajax `mock-ajax.js` | no version stated; header says `Copyright (c) 2008-2013 Pivotal Labs` | MIT; the full license text is embedded in the file header | `environment/repo/spec/javascripts/support/mock-ajax.js` |

Ruby gems declared in `Gemfile` / `Gemfile.lock` (including Rails 4.0.0 and its
transitive dependencies) are resolved from rubygems.org when the image is built.
They are not vendored or redistributed in the task tree. The Ruby interpreter is
built by `environment/build-ruby.sh`; the `bundler-audit` tool is installed into
the image at build time. Neither is shipped in the task tree.

## Modifications by the benchmark

- `environment/repo/` is the repository at its pinned commit, unmodified;
  nothing is added to it (see `environment/Dockerfile`).
- Migration steps under `steps/` add benchmark-authored instructions, tests,
  workdirs and solutions; no upstream application file is edited in place.

## Required notices

- CityVoice: `Copyright (c) 2013-2014 Code for America` (MIT).
- `ruby-advisory-db`: the public-domain/OSVDB statement reproduced in
  `LICENSES/Public-Domain.txt`; upstream project rubysec/ruby-advisory-db.
- audio.js assets: `Copyright (c) 2010 Anthony Kolber` (MIT).
- Jasmine-Ajax `mock-ajax.js`: `Copyright (c) 2008-2013 Pivotal Labs` (MIT).
- The pinned CityVoice tree contains no upstream `NOTICE` file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
