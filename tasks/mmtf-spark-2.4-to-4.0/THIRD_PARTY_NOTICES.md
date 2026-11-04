# Third-Party Notices — mmtf-spark-2.4-to-4.0

Benchmark task that migrates sbl-sdsc/mmtf-spark from Spark 2.4 to Spark 3.4 on
Scala 2.13 and JDK 17 over five cumulative steps, holding a recorded behavior
suite byte-identical at every step.

## Upstream project

| | |
|---|---|
| Project | mmtf-spark |
| URL | https://github.com/sbl-sdsc/mmtf-spark |
| Pin | commit `e74298a6511732c5d6905eeec86b3a73e3d24447` |
| Bundled/derived | `environment/app.tgz` is the complete upstream repository at the pin. Each step's solution patch (`steps/*/solution/`) migrates that tree toward Spark 3.4/Scala 2.13/JDK 17. The 140/141-job recorded behavior suite and the step instructions are task-authored. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| Apache-2.0 | `Apache-2.0.txt` | mmtf-spark at the pinned commit (exact upstream `LICENSE` from `environment/app.tgz`) |

The upstream `LICENSE` is the full Apache License 2.0 text with the upstream
copyright line `Copyright 2018 University of California, San Diego`. It is
reproduced byte-for-byte from the tarball; the same text is at
`environment/app.tgz:LICENSE`.

## Modifications made by the benchmark

- The upstream repository is shipped unmodified in `environment/app.tgz`.
- The cumulative workspace evolves only through the benchmark's solution
  patches and the candidate's own work. The benchmark applies no UTF-8
  conversion, stripping, or subsetting to the upstream tree.
- The recorded job suite, expected outputs, verifier, and instructions are
  task-authored and are not derived from upstream code.

## Third-party dependencies bundled in the task

No third-party source is bundled in this repository. The image build downloads
the following toolchains from their publishers; they are installed in the
container, not redistributed in the task tree:

| Component | Version | License | Source |
|---|---|---|---|
| Apache Spark | 2.4.8 (`spark-2.4.8-bin-hadoop2.7`); 3.0.3, 3.2.4, 3.3.4 and 3.4.4 as the ladder advances | Apache-2.0 | https://archive.apache.org/dist/spark/ |
| Apache Maven | 3.9.9 | Apache-2.0 | https://archive.apache.org/dist/maven/ |
| Eclipse Temurin JDK | 8u462-b08, 11.0.28+6, 17.0.16+8, 21.0.8+9 | GPL-2.0-only WITH Classpath-exception-2.0 | https://github.com/adoptium/temurin*-binaries/releases |
| Scala | 2.11, 2.12 and 2.13 (matching the Spark distribution at each step) | Apache-2.0 | https://www.scala-lang.org/ |

Hashes for the JDK, Maven and Spark 2.4.8 downloads are recorded in
`environment/toolchain/pins.tsv`.

## Required notices

- Apache-2.0 copyright: `Copyright 2018 University of California, San Diego`,
  as carried in the upstream `LICENSE`.
- The upstream repository contains no `NOTICE` file, so no additional Apache
  NOTICE text applies.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
