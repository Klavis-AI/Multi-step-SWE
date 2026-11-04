# Third-Party Notices — mysql5-to-postgres17

Benchmark task that migrates WebGoatPHP from MySQL 5.7 to PostgreSQL 17,
repairing its Latin-1 text to UTF-8 along the way.

## Upstream projects

| | |
|---|---|
| Application | WebGoatPHP |
| URL | https://github.com/shivamdixit/WebGoatPHP |
| Pin | commit `f3b80714fbb2c5adbe6d1bb3f81db914b734336e` (2016-10-12) |
| Bundled/derived | `environment/webgoat-src.tar.gz` is the complete application tree at the pin. The step solutions migrate that tree (configuration, schema, queries, encoding repair); the test suite and deployment scripts are task-authored. |
| Database (source) | MySQL Community Server 5.7.44, https://github.com/mysql/mysql-server, tag `mysql-5.7.44` |
| Database (target) | PostgreSQL 17, https://github.com/postgres/postgres, branch `REL_17_STABLE` (PGDG packages) |
| Bundled/derived | MySQL 5.7.44 server and client binaries are copied from the official `mysql:5.7.44` image into the environment image at build time. PostgreSQL 17 is installed from apt.postgresql.org. Neither database is redistributed as source in this repository. |

## License table

| SPDX ID | File under `LICENSES/` | Covers |
|---|---|---|
| Apache-2.0 | `Apache-2.0.txt` | WebGoatPHP at the pinned commit (exact upstream `LICENSE`, extracted from `environment/webgoat-src.tar.gz:webgoat/LICENSE`) |
| GPL-2.0 | `GPL-2.0.txt` (full GPLv2 text) and `MySQL-5.7-Additional-Permissions.txt` (MySQL's additional permissions, its GPLv2 election, and the Universal FOSS Exception v1.0, quoted from MySQL's own `LICENSE`) | MySQL Community Server 5.7.44 and its client programs |
| PostgreSQL | `PostgreSQL.txt` | PostgreSQL 17 (exact upstream `COPYRIGHT` from `REL_17_6`) |
| MIT | `MIT.txt` | Symfony Console and Symfony Yaml components vendored inside WebGoatPHP |

The WebGoatPHP `LICENSE` is the full Apache License 2.0 text. Its appendix
retains the upstream placeholder `Copyright {yyyy} {name of copyright owner}`;
the repository does not state a different copyright owner in that file.

## Modifications made by the benchmark

- The WebGoatPHP application tree is shipped unmodified in the tarball. The
  image installs it at `/app` using the application's own documented setup
  (`environment/files/install-app.sh`), which fills in local database
  credentials and disables jframework's error-to-exception handler.
- Benchmark-authored deployment files (`environment/files/init-db.sh`,
  `my.cnf`, `wg-services`, Apache/PHP configuration, seed data) and the step
  solutions/tests are not derived from WebGoatPHP source.
- MySQL's binaries are used as shipped in the official image; PostgreSQL is
  used as shipped in the PGDG packages. The benchmark modifies no database
  source code.

## Third-party dependencies bundled in the task

| Component | Version | License | Bundled path |
|---|---|---|---|
| Symfony Console (inside Doctrine) | as vendored by WebGoatPHP at the pin | MIT, Copyright (c) 2004-2012 Fabien Potencier | `webgoat/app/plugin/doctrine/Doctrine/Symfony/Component/Console/LICENSE` inside `environment/webgoat-src.tar.gz` |
| Symfony Yaml (inside Doctrine) | as vendored by WebGoatPHP at the pin | MIT, Copyright (c) 2004-2012 Fabien Potencier | `webgoat/app/plugin/doctrine/Doctrine/Symfony/Component/Yaml/LICENSE` inside `environment/webgoat-src.tar.gz` |
| Doctrine ORM / DBAL / Common | 2.3.3 / 2.3.3 / 2.3.0 | MIT, per the header of each component's `Version.php`; the snapshot carries no separate doctrine LICENSE file | `webgoat/app/plugin/doctrine/Doctrine/` inside `environment/webgoat-src.tar.gz` |
| MySQL Community Server | 5.7.44 | GPL-2.0 with the FOSS exception (see `MySQL-5.7-Additional-Permissions.txt`) | Copied from the `mysql:5.7.44` image at build time; not in this repository |
| PostgreSQL | 17 (PGDG) | PostgreSQL License | Installed from apt.postgresql.org at image build; not in this repository |
| PHP | 7.4 (Debian packages) | PHP-3.01 / Zend-2.00 | Base image packages |
| Apache HTTP Server | 2.4 (Debian packages) | Apache-2.0 | Base image packages |

## Required notices

- MySQL: `Copyright (c) 1997, 2023, Oracle and/or its affiliates.` — from the
  MySQL 5.7.44 `LICENSE` file. The distribution also includes separately
  licensed software (notably OpenSSL) under its own terms, as the FOSS
  exception states.
- PostgreSQL: `Portions Copyright (c) 1996-2025, PostgreSQL Global Development
  Group` and `Portions Copyright (c) 1994, The Regents of the University of
  California`, as carried in `LICENSES/PostgreSQL.txt`.
- WebGoatPHP: no `NOTICE` file is present in the upstream tree, so no Apache
  NOTICE text applies.
- Symfony components: `Copyright (c) 2004-2012 Fabien Potencier`, as carried in
  their in-tree MIT license files.
- Doctrine ORM, DBAL and Common: MIT, as stated in each component's source
  headers; the bundled snapshot has no standalone copyright line for them.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
