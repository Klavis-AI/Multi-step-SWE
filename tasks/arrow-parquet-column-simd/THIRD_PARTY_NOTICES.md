# Third-Party Notices — arrow-parquet-column-simd

A five-step benchmark ladder in which a candidate restores the optimized SIMD
implementations of Apache Arrow's bit-packing decoder, byte-stream split,
Bloom-filter, row-kernel and Swiss-table row-gather code in a build of Arrow
whose optimized implementations have been removed.

## Upstream project

| | |
|---|---|
| Project | Apache Arrow |
| Repository | https://github.com/apache/arrow |
| Pin | tag `apache-arrow-25.0.1`, commit `beccec0d0c451b7aa3e4530416ac431b3c035c69` |
| Primary license | Apache-2.0 (`LICENSE.txt`); upstream `NOTICE.txt` is reproduced below |
| Test data | https://github.com/apache/arrow-testing at `9ff285c88565f0f6abc855918c6a342e70e4909c`; https://github.com/apache/parquet-testing at `HEAD` (resolved at verifier build time). Both are Apache-2.0. |

The task repository does not vendor the Arrow source tree. The agent image and
every verifier image clone the pinned commit at build time and remove the
optimized implementations with `environment/vendor/strip.py`.

### Bundled and derived material

* `steps/*/solution/oracle.patch` — five patches derived from Apache Arrow
  source at the pin. They add or restore the optimized C++ kernels and their
  dispatch/build wiring for the five ladder stages (`01-bpacking`,
  `02-byte-stream-split`, `03-bloom-filters`, `04-row-kernels`,
  `05-swiss-row-gather`). Files carried over from upstream retain their ASF
  license headers.
* `environment/vendor/` and each step's `tests/vendor/` — benchmark-authored
  stripping, build and measurement tooling (`strip.py`, `build_arrow.sh`,
  `build_probe.sh`, `kbench.cc`, `e2e.cc`, `novelty.py`, and related scripts).
* Verifier images clone the Apache Arrow test-data repositories named above at
  build time; those fixtures are not committed to the task repository.

## Licenses

| SPDX ID | File under `LICENSES/` | Applies to |
|---|---|---|
| Apache-2.0 | `Apache-2.0.txt` | Apache Arrow 25.0.1. This is the verbatim upstream `LICENSE.txt`: it contains the Apache License 2.0 followed by the additional third-party license texts Arrow ships for bundled components (for example Chromium/BSD-3-Clause portions in `src/arrow/util`, Daniel Lemire's FrameOfReference, TensorFlow, and LLVM exception text). |

## Modifications made by the benchmark

* The starting tree is the upstream pin with the optimized implementation of
  every ladder kernel removed for every architecture; the portable scalar
  implementations in the same tree remain. `strip.py` performs only deletions
  and build-list edits.
* Verifier images rebuild the identically stripped base and measure against it;
  the oracle patches restore the upstream optimized code.
* No behavioural change is made to the remaining upstream code.

## Third-party dependencies bundled in the task

None beyond the upstream project itself. The benchmark-authored build and
measurement harnesses are task-authored content. Arrow's own bundled
third-party notices are included in `LICENSES/Apache-2.0.txt` and in the
upstream `NOTICE.txt` below.

## Required notices

Apache Arrow `NOTICE.txt`, reproduced verbatim:

```
Apache Arrow
Copyright 2016-2026 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (http://www.apache.org/).

This product includes software from the SFrame project (BSD, 3-clause).
* Copyright (C) 2015 Dato, Inc.
* Copyright (c) 2009 Carnegie Mellon University.

This product includes software from the Feather project (Apache 2.0)
https://github.com/wesm/feather

This product includes software from the DyND project (BSD 2-clause)
https://github.com/libdynd

This product includes software from the LLVM project
 * distributed under the University of Illinois Open Source

This product includes software from the mman-win32 project
 * Copyright https://code.google.com/p/mman-win32/
 * Licensed under the MIT License;

This product includes software from the LevelDB project
 * Copyright (c) 2011 The LevelDB Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * Moved from Kudu http://github.com/cloudera/kudu

This product includes software from the CMake project
 * Copyright 2001-2009 Kitware, Inc.
 * Copyright 2012-2014 Continuum Analytics, Inc.
 * All rights reserved.

This product includes software from https://github.com/matthew-brett/multibuild (BSD 2-clause)
 * Copyright (c) 2013-2016, Matt Terry and Matthew Brett; all rights reserved.

This product includes software from the Ibis project (Apache 2.0)
 * Copyright (c) 2015 Cloudera, Inc.
 * https://github.com/cloudera/ibis

This product includes software from Dremio (Apache 2.0)
  * Copyright (C) 2017-2018 Dremio Corporation
  * https://github.com/dremio/dremio-oss

This product includes software from Google Guava (Apache 2.0)
  * Copyright (C) 2007 The Guava Authors
  * https://github.com/google/guava

This product include software from CMake (BSD 3-Clause)
  * CMake - Cross Platform Makefile Generator
  * Copyright 2000-2019 Kitware, Inc. and Contributors

The web site includes files generated by Jekyll.

--------------------------------------------------------------------------------

This product includes code from Apache Kudu, which includes the following in
its NOTICE file:

  Apache Kudu
  Copyright 2016 The Apache Software Foundation

  This product includes software developed at
  The Apache Software Foundation (http://www.apache.org/).

  Portions of this software were developed at
  Cloudera, Inc (http://www.cloudera.com/).

--------------------------------------------------------------------------------

This product includes code from Apache ORC, which includes the following in
its NOTICE file:

  Apache ORC
  Copyright 2013-2019 The Apache Software Foundation

  This product includes software developed by The Apache Software
  Foundation (http://www.apache.org/).

  This product includes software developed by Hewlett-Packard:
  (c) Copyright [2014-2015] Hewlett-Packard Development Company, L.P
```

Apache Arrow and Apache are trademarks of The Apache Software Foundation.

---

Task-authored benchmark content is licensed separately by the benchmark maintainers.
