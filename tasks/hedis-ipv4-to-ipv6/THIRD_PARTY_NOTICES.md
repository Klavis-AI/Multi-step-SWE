# Third-Party Notices — hedis-ipv4-to-ipv6

Benchmark task that migrates the hedis fork of Redis 3.2-rc from an IPv4-first
network stack to an IPv6-only one, in six cumulative steps.

## Upstream project

| | |
|---|---|
| Project | hedis (a fork of Redis 3.2-rc) |
| URL | https://github.com/hedisdb/hedis |
| Pin | commit `270eb6512b8602cefc7883346755ffbb6fa3bc93` (2016-07-27) |
| Bundled/derived | The complete upstream tree is shipped unmodified under `environment/hedis/`. `src/version.h` identifies the fork as `REDIS_VERSION "3.1.999"` (the Redis 3.2 release-candidate line). The step ladder, instructions, tests and verifiers are task-authored. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| BSD-3-Clause | `BSD-3-Clause.txt` | hedis / Redis tree at `270eb651` (exact upstream `COPYING`, copy at `environment/hedis/COPYING`) |
| BSD-3-Clause | `BSD-3-Clause-hiredis.txt` | hiredis 0.11.0 under `environment/hedis/deps/hiredis/` (exact upstream `COPYING`) |
| BSD-2-Clause | `BSD-2-Clause-jemalloc.txt` | jemalloc `3.6.0-0-g46c0af68bd248b04df75e4f92d5fb804c3d75340` under `environment/hedis/deps/jemalloc/` (exact upstream `COPYING`) |
| MIT | `MIT-lua.txt` | Lua 5.1.5 under `environment/hedis/deps/lua/` (exact upstream `COPYRIGHT`) |
| BSD-2-Clause | `BSD-2-Clause-linenoise.txt` | linenoise under `environment/hedis/deps/linenoise/` (exact two-clause text from the `linenoise.c` header; there is no separate license file) |

## Modifications made by the benchmark

- The upstream hedis tree is shipped without modification. The only change is
  that it is copied into `environment/hedis/` as the agent workspace.
- The six migration steps, instructions, tests and verifiers are task-authored.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| hedis / Redis | commit `270eb651`, `REDIS_VERSION "3.1.999"` | BSD-3-Clause | `environment/hedis/` (`COPYING`) |
| hiredis | 0.11.0 | BSD-3-Clause | `environment/hedis/deps/hiredis/` (`COPYING`) |
| jemalloc | `3.6.0-0-g46c0af68bd248b04df75e4f92d5fb804c3d75340` | BSD-2-Clause | `environment/hedis/deps/jemalloc/` (`COPYING`, `VERSION`) |
| Lua | 5.1.5 | MIT | `environment/hedis/deps/lua/` (`COPYRIGHT`) |
| linenoise | as vendored at the hedis pin | BSD-2-Clause | `environment/hedis/deps/linenoise/linenoise.c` (license header) |

## Required notices

- hedis / Redis: `Copyright (c) 2006-2015, Salvatore Sanfilippo. All rights
  reserved.` The upstream `COPYING` carries the three-clause BSD license,
  including the clause that neither the name of Redis nor the names of its
  contributors may be used to endorse or promote products derived from the
  software without specific prior written permission.
- hiredis: `Copyright (c) 2009-2011, Salvatore Sanfilippo <antirez at gmail dot
  com>` and `Copyright (c) 2010-2011, Pieter Noordhuis <pcnoordhuis at gmail
  dot com>`, under the three-clause BSD license.
- jemalloc: `Copyright (C) 2002-2014 Jason Evans <jasone@canonware.com>`,
  `Copyright (C) 2007-2012 Mozilla Foundation`, and `Copyright (C) 2009-2014
  Facebook, Inc.`, under the two-clause BSD license.
- Lua: `Copyright (C) 1994-2012 Lua.org, PUC-Rio.` under the MIT license.
- linenoise: `Copyright (c) 2010-2013, Salvatore Sanfilippo` and
  `Copyright (c) 2010-2013, Pieter Noordhuis`, under the two-clause BSD
  license reproduced in `LICENSES/BSD-2-Clause-linenoise.txt`.
- Redis is a trademark of Redis Ltd. The hedis fork is an independent project
  and is not endorsed by Redis Ltd.
- No upstream component in this task ships an Apache `NOTICE` file, so no
  additional Apache NOTICE text applies.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
