# PROFILE.md — the behavioural profile of `uv`

This is a normative document. Where a published specification under
`/app/SPECS/specs/` fixes a behaviour, that specification governs and this
profile does not repeat it. This profile fixes the behaviour that no
specification fixes: the command surface, the flag grammar, the defaults, the
diagnostics, the exit statuses, and the choices a specification explicitly
leaves to the implementation.

Read it as a contract, not as a tutorial. Every clause below is observable from
outside the process — an exit status, a stream, a file the run leaves behind.
Nothing in it constrains internal structure.

Sections accumulate. Section *N* states what step *N* added; every earlier
section still holds unless a later section says in so many words that it is
superseded, and where it does, the later text wins.

## Conventions used throughout

- **Exit status.** `0` means the request succeeded. A non-zero status means it
  did not, and the profile says which non-zero status when the distinction is
  part of the contract. An unhandled panic is never an acceptable outcome for
  an input this profile describes.
- **Streams.** Machine-readable output — resolved requirement sets, JSON
  payloads, the rendered lockfile — goes to stdout. Diagnostics, progress and
  warnings go to stderr. The two are never interleaved on one stream.
- **Quoting.** Where the profile shows a diagnostic, backticks around a name in
  the shown text are part of the text.
- **Package-name normalisation** follows the *Names and normalization* section
  of `/app/SPECS/specs/pypa-name-normalization.rst` everywhere a name is
  compared, printed in a resolved set, or used as a lookup key.

## 1. the pip-compatible core

`uv` is one binary. Its first positional argument is a subcommand: `pip`, which
takes a second-level subcommand of its own, or `venv`. Options may appear
before or after positional arguments, and a flag that takes a value takes it as
the following argument.

### global options, and what a status means

- `--version` is accepted with no subcommand. It writes a version line and
  exits `0`. Neither the wording nor the shape of that line is fixed here.
- `-q`/`--quiet` and `-v`/`--verbose` are accepted on every subcommand and
  conflict with one another. Both default to off.
- `-n`/`--no-cache` is accepted on every subcommand. Set, the run neither reads
  from nor writes to a persistent cache; unset, which is the default, a
  persistent cache is used. Every behaviour in this section holds identically
  in both states.
- `--cache-dir` takes a path and names the persistent cache. `UV_CACHE_DIR`
  carries the same value. The flag wins over the variable; `--no-cache` wins
  over both.
- Exit `1` is the status for a request that was understood and could not be
  carried out — a requirement set with no solution, a package the index does
  not serve, a distribution that cannot be installed. Exit `2` is reserved for
  an unexpected internal failure and is not a documented outcome of anything
  below.

### creating a virtual environment

- `uv venv [PATH]` creates a virtual environment. `PATH` is optional; omitted,
  the environment is created at `.venv` relative to the working directory.
  Success is `0`.
- `-p`/`--python` takes one value, an interpreter request. At this step the
  request may be a bare executable name to be looked for on `PATH`, such as
  `python3.12`, or an absolute path to an interpreter, which is used as given.
  With no `--python` the interpreter is found on `PATH`.
- What `/app/SPECS/specs/pypa-virtual-environments.rst` calls a virtual
  environment is created at `PATH`, and the checkpoint reads these paths from
  it, where `<X.Y>` is the minor version of the interpreter it was created
  from:
  - `PATH/pyvenv.cfg`;
  - `PATH/bin/python`, `PATH/bin/python3` and `PATH/bin/python<X.Y>` — all
    three exist, and no other entry under `PATH/bin` begins with `python`;
  - `PATH/lib/python<X.Y>/site-packages`, which exists and holds no
    distribution.
- The environment is immediately usable as an install target; nothing further
  is needed to make it one.

### which environment a `uv pip` command acts on

- `uv pip install`, `uv pip sync`, `uv pip uninstall` and `uv pip freeze` each
  act on exactly one environment. When `VIRTUAL_ENV` is set it names that
  environment; its value may be a path relative to the working directory, and
  it is honoured whether or not the environment was created by this run. When
  `VIRTUAL_ENV` is unset, a `.venv` directory in the working directory is used.
- `uv pip compile` acts on no environment. It runs, and succeeds, in a working
  directory that contains no virtual environment and with `VIRTUAL_ENV` unset.

### installing

- `uv pip install` takes its requirements from three sources, and at least one
  source is required:
  - positional requirement strings, in the grammar
    `/app/SPECS/specs/pypa-dependency-specifiers.rst` fixes, as many as are
    given;
  - `-r`/`--requirement`, which takes the path of a requirements file and may
    be given more than once, each occurrence adding a file;
  - `-e`/`--editable`, which takes the path of a local directory and may be
    given more than once.
- `-c`/`--constraint` takes the path of a constraints file and may be given
  more than once. `--no-deps` takes no value and defaults to off; set,
  dependencies are not followed and only the packages the sources name are
  resolved and installed. `--index-url`, short alias `-i`, takes the URL of a
  simple index as fixed by `/app/SPECS/specs/pep-0503.rst`.
- All sources are resolved together into a single version per package, and then
  installed. Success is `0`.
- Every installed distribution leaves
  `<site-packages>/<name>-<version>.dist-info`, its contents as fixed by
  `/app/SPECS/specs/pypa-recording-installed-packages.rst`. The set of
  `*.dist-info` directories in `site-packages` after a run is exactly the set
  of distributions the resolution produced, and no more.
- The `WHEEL` file inside an installed `.dist-info` is the `WHEEL` of the
  artefact that was actually installed, carried through unchanged — including
  its `Generator` and `Tag` lines. It is how the artefact chosen for a
  distribution is observed from outside.

### synchronising

- `uv pip sync` takes one or more positional requirements-file paths and
  requires at least one.
- The named files are the complete description of the environment. What they
  resolve to is installed, and any distribution already present that the
  resolution does not name is removed, together with its `.dist-info`. Success
  is `0`.

### uninstalling

- `uv pip uninstall` takes positional package names, `-r`/`--requirement` and
  `-e`/`--editable`, and requires at least one source.
- Each named distribution is removed along with its `.dist-info`. Distributions
  not named are untouched.
- Naming a package that is not installed is not an error. The status is `0`,
  a warning is written to stderr, and the environment is unchanged.

### reporting what is installed

- `uv pip freeze` writes the installed distributions to stdout, one per line,
  each as `<name>==<version>` with no surrounding text. Nothing else appears on
  stdout. Success is `0`. The order of the lines is not fixed.

### compiling a pinned requirement set

- `uv pip compile` takes one or more positional input-file paths and requires
  at least one.
- It writes the whole resolution — the named requirements and everything they
  pull in — as `<name>==<version>` lines, one per package, ordered by
  normalised package name ascending. Success is `0`.
- `-o`/`--output-file` takes a path and the set is written there. Without it
  the set is written to stdout.
- `--no-header` takes no value and defaults to off; set, the comment header
  that is otherwise written above the set is omitted. `--no-annotate` takes no
  value and defaults to off; set, the per-entry comment annotations recording
  which requirement each entry came from are omitted. With both set, the output
  is the pinned lines and nothing else.
- `--no-deps` has the same meaning here as on `uv pip install`: set, the output
  holds only the packages the input files name, at their resolved versions, and
  no dependency of theirs.
- A constraints file given with `-c`/`--constraint` bounds the version of a
  package that the resolution already contains, whether the input named it or a
  dependency pulled it in. It never causes a package to enter the resolution:
  constraining a package nothing depends on adds nothing to the output.
- `--python-version` takes a Python version string such as `3.11` and overrides
  the version used to evaluate `requires-python` constraints and environment
  markers during compilation. When absent, the version is taken from the
  interpreter used to resolve.

### choosing a version

`/app/SPECS/specs/pep-0440.rst` and
`/app/SPECS/specs/pypa-version-specifiers.rst` fix version identifiers, their
normalisation, their total ordering and the meaning of every specifier
operator. This profile fixes only the choice those documents leave open.

- Requirements on the same package from every source are intersected, and the
  candidate chosen is the highest under PEP 440 ordering among those that
  satisfy the intersection and are offered for the target environment.
- Ordering is applied in full, not on the release segment alone. An epoch
  dominates: offered `1.0.0`, `2.0.0` and `1!0.5.0`, an unconstrained
  requirement resolves to `1!0.5.0`. A post-release outranks the release it
  follows: offered `1.0.0` and `1.0.0.post1`, an unconstrained requirement
  resolves to `1.0.0.post1`.
- A pre-release — including a development release such as `1.0.1.dev1` — is not
  a candidate unless a specifier applying to that package itself names a
  pre-release, or every version of that package the index offers is a
  pre-release. Offered `1.0.0` and `2.0.0b1`, a bare requirement resolves to
  `1.0.0`; the requirement `>=2.0.0b1` on the same package resolves to
  `2.0.0b1`. Offered only `1.0.0a1` with no stable release, a bare requirement
  resolves to `1.0.0a1`.
- `--prerelease` is accepted on `uv pip install` and `uv pip compile`. The
  value `allow` makes pre-releases eligible for every package unconditionally.
- A local version is greater than the same release without a local identifier
  under PEP 440 ordering, so a range specifier such as `>=1.2.3` is satisfied
  by `1.2.3+foo`. A specifier carrying no local segment ignores the candidates'
  local segments, so `==1.2.3` is satisfied by `1.2.3+foo` as well; only
  `==1.2.3+foo` selects that exact local version.
- The compatible-release and wildcard-equality operators bound the candidate
  set, and the highest remaining candidate is still taken: `~=1.2` over
  `1.1.0`, `1.2.0`, `1.9.3` and `2.0.0` resolves to `1.9.3`; `~=1.2.0` over
  `1.2.0`, `1.2.7` and `1.3.0` resolves to `1.2.7`; `==1.2.*` over `1.1.9`,
  `1.2.0`, `1.2.9` and `1.3.0` resolves to `1.2.9`.

### reading the index

- Candidates come from the simple index named by `--index-url`, read as
  `/app/SPECS/specs/pep-0503.rst` and the documents beside it fix.
- A version whose `requires-python` metadata excludes the target environment's
  interpreter version is not a candidate. Where a lower version is compatible,
  that lower version is resolved and installed and the status is `0`.
- A yanked file, in the sense of `/app/SPECS/specs/pep-0592.rst` and
  `/app/SPECS/specs/pypa-file-yanking.rst`, is excluded from the candidate set
  of any requirement that does not pin its version exactly. A pin in the user's
  own requirements — a CLI positional argument, a file supplied with `-r`, or a
  constraint file supplied with `-c` — that names an exact version with `==`
  still resolves to the yanked file, and installing it succeeds with status
  `0`. A transitive `==` constraint in a dependency's own metadata does not
  re-admit a yanked file.

### evaluating environment markers

- Markers are evaluated against the marker environment of the interpreter the
  target environment was created from — never the interpreter running `uv`, and
  never whichever `python3` happens to be first on `PATH`. The same requirement
  set therefore resolves differently against a 3.9 environment and a 3.12 one.
- A dependency whose marker evaluates false is neither resolved nor installed;
  one whose marker evaluates true is resolved and installed, transitively.
  `sys_platform`, `os_name` and `python_version` are all evaluated, with the
  values `/app/SPECS/specs/pep-0508.rst` specifies for the target environment.
- `extra` evaluates false unless that extra was requested of the package
  carrying the requirement. A dependency guarded by `extra == '<name>'` on a
  package installed without extras is dropped.
- Requesting an extra that a version's metadata does not declare is not an
  error. The extra is silently ignored, and the highest version satisfying the
  other constraints is resolved without that extra's dependencies.

### choosing a distribution for a version

- For each candidate version the index may offer wheels, a source distribution,
  or both. A version with no installable artefact for the target environment —
  no matching wheel and no source distribution — is skipped, and the
  next-highest version satisfying the requirements is tried.
- `--no-binary` takes a package name and may be given more than once. When set
  for a package, wheels for that package are not considered; a source
  distribution is used if one is offered, otherwise the run fails with
  status `1`.
- `--only-binary` takes a package name and may be given more than once. When
  set for a package, only a wheel is considered; if no matching wheel is
  offered, the run fails with status `1` without attempting to build from
  source.
- No option overrides the platform component of wheel tag matching: the
  platform of the target environment is always the one matched against.
  `--python-platform` is not accepted at this milestone; a later milestone
  introduces it.
- A wheel is a candidate only if its compatibility tags match the target
  environment, as `/app/SPECS/specs/pep-0425.rst` and
  `/app/SPECS/specs/pypa-platform-compatibility-tags.rst` fix. Wheels whose
  tags do not match — a foreign interpreter version or ABI in either direction —
  are skipped without diagnosis and without failing the run.
- Among matching wheels the most specific wins. A wheel tagged for the
  environment's own interpreter, ABI and platform is preferred over a
  `py3-none-any` wheel of the same version.
- When no wheel matches — because none is offered at all, or because none of
  those offered has matching tags — and a source distribution is offered, the
  source distribution is built and the wheel that build produces is installed.
  Status `0`.
- When no candidate version offers an installable artefact for the target
  environment, the run fails with status `1` and installs nothing.

### building from source

- Source distributions and local directories are built through the hooks
  `/app/SPECS/specs/pep-0517.rst`, `/app/SPECS/specs/pep-0518.rst` and
  `/app/SPECS/specs/pep-0660.rst` define, in an environment isolated from the
  target environment, into which `[build-system] requires` is installed first.
  `build-backend` names the backend and `backend-path` makes a backend module
  that ships inside the tree importable, so a build whose `requires` is empty
  completes with no index reachable at all.
- The wheel a build returns is installed exactly as a downloaded wheel is: the
  `.dist-info` it carries, `WHEEL` included, is what lands in `site-packages`,
  and the distribution appears in `uv pip freeze` under the name and version
  its metadata declares.
- `-e`/`--editable` installs a local directory. Where the backend offers the
  editable hook of `/app/SPECS/specs/pep-0660.rst`, that hook is called in
  preference to the ordinary wheel hook, so files the backend emits only for an
  editable build are present under `site-packages` afterwards. The
  `.dist-info` directory is named from the built distribution's own name and
  version, which for a `pyproject.toml` are the `[project]` `name` and
  `version` fields `/app/SPECS/specs/pep-0621.rst` fixes.

### failing to resolve

- Status `1`, with no package installed and the environment left exactly as it
  was, is the outcome whenever the requirements admit no solution. Partial
  installation is never an outcome: after a failed `uv pip install` the
  environment holds what it held before, and if it was empty it is still empty.
- The cases are not distinguished by status. All of the following are `1`:
  - two requirements, direct or transitive, pin one shared dependency to
    different exact versions;
  - a requirement names a range the index offers no version within;
  - a requirement names a package the index does not serve at all.
- Diagnostics go to stderr. Their wording is not fixed at this step.

## 2. find-links, hashes, editables, constraints and overrides

### the command surface grows

- `uv cache` takes a second-level subcommand: `clean`, `prune` or `dir`.
  `uv cache dir` writes the path of the cache directory and exits `0`. It needs
  no virtual environment, no index and no network.
- `uv pip list` enumerates the distributions installed in the environment.
  `--format` takes one value — `columns`, `freeze` or `json` — and defaults to
  `columns`. With `--format=json` the whole of stdout is one JSON array with an
  object per installed distribution, each carrying the key `name` followed by
  the key `version`, serialised without any whitespace between tokens. For an
  environment holding only `a` at `1.0.0` stdout is exactly
  `[{"name":"a","version":"1.0.0"}]`. Status `0`.
- `uv pip show` takes one or more package names and reports what the
  environment holds for each. Showing an installed distribution is `0`.
- `uv pip check` validates the installed distributions against one another. An
  environment with nothing installed is consistent: status `0`.
- The environment these three act on is the one § 1 fixes for `install`, `sync`,
  `uninstall` and `freeze`: the `VIRTUAL_ENV` the process is given, or `.venv` in
  the working directory when it is given none. Naming no environment on the
  command line is not an error and does not make them act on the system
  interpreter.

### `uv pip uninstall` narrows, and status `2` becomes a documented outcome

- § 1 listed `-e`/`--editable` among the sources of `uv pip uninstall`. That is
  superseded. `uv pip uninstall` now takes positional package names and
  `-r`/`--requirement` only, as the sources of what to remove; the
  `-p`/`--python` option that names the environment is unaffected.
  `-e`/`--editable` is not a recognised option
  there, and supplying it is a command-line error: status `2`, with the
  environment untouched. `uv pip install` keeps `-e`/`--editable` exactly as
  § 1 fixed it.
- § 1 reserved status `2` for an unexpected internal failure. That is
  superseded: `2` is also the status for a command line that does not parse,
  and a rejected option is reported that way rather than as `1`.

### option values, and the index named by the environment

- An option's value may be given as the next argument or joined to the option
  with `=`; `--format json` and `--format=json` are the same request.
- `UV_INDEX_URL` carries the setting of `--index-url`, and the flag wins over
  the variable. A `uv pip` invocation that names no index on its command line
  resolves against the index the variable names, and nothing else about the
  invocation changes.

### index configuration

- `--extra-index-url` takes a URL and may be given more than once.
  `UV_EXTRA_INDEX_URL` carries the same setting, with several URLs separated by
  spaces.
- Every index given as an extra takes priority over the one `--index-url`
  names, and among several extras the earlier ones take priority.
- The search for a package stops at the first index that serves it; versions of
  one package are never pooled across indexes. A package an extra index serves
  is therefore resolved and installed without the index `--index-url` names
  being consulted for it at all — which holds even when that index could not
  have been reached had it been consulted.
- `-f` is the short alias of `--find-links`. It takes a URL or a directory
  path and may be given more than once. A URL must serve a flat list of links
  to distribution files; a directory must hold `.whl`, `.tar.gz` or `.zip`
  files at its top level.
- `--no-index` takes no value and defaults to off. Set, no registry index is
  consulted at all and candidates come only from `--find-links` locations; a
  requirement met from such a location alone resolves and installs with status
  `0`. It conflicts with `--index-url` and `--extra-index-url`.

### further options on `uv pip install`

- `-U`/`--upgrade` takes no value and defaults to off.
  `-P`/`--upgrade-package` takes a package name and may be given more than
  once.
- `--pre` takes no value and defaults to off. It makes pre-release versions
  eligible for every package, is equivalent to `--prerelease allow`, and
  conflicts with `--prerelease`.
- `-C`/`--config-setting` takes one `KEY=VALUE` pair and may be given more than
  once. The pairs are passed to the PEP 517 build backend. The option is
  accepted, and changes nothing observable, on a run that builds nothing.
- `--dry-run` takes no value and defaults to off. Set, the requirements are
  resolved and the resulting plan reported, and the environment is not
  modified. Status `0`.
- `--compile` takes no value and defaults to off. Set, installed `.py` files
  are byte-compiled as part of the install, so for a module at
  `<site-packages>/<pkg>/__init__.py` the file
  `<site-packages>/<pkg>/__pycache__/__init__.cpython-<XY>.pyc` exists after
  the run, `<XY>` being the target interpreter's major and minor version
  written without a separator. Unset, installation creates no `__pycache__`
  entry.

### build policy

- `--no-build` takes no value and defaults to off. Set, no source distribution
  is built, so a version whose only artefact is a source distribution cannot be
  installed. A requirement that can be met no other way fails the run with
  status `1`.
- `--only-binary` takes a value and may be given more than once: a package
  name, `:all:` for every package, or `:none:` to clear what earlier
  occurrences selected. `--no-build` is equivalent to `--only-binary :all:`,
  and the two conflict.
- Under any of the three policies — `--no-build`, `--only-binary` and
  `--no-binary` — a version whose only offered artefact is of the excluded kind
  simply leaves the candidate set; it is not an error by itself. The highest
  remaining version the policy does allow is resolved and installed, so a lower
  version is chosen in preference to failing, and the run fails with status `1`
  only when no version of the package survives the policy. § 1's "otherwise the
  run fails with status `1`" for `--no-binary` is that last case — no version of
  the package offers a source distribution — and not a rule applied to each
  version separately.

### pre-release eligibility

- `--prerelease` takes one value: `disallow`, `allow`, `if-necessary`,
  `explicit` or `if-necessary-or-explicit`. The default is
  `if-necessary-or-explicit`. `UV_PRERELEASE` carries the same setting and the
  flag wins over the variable.
- The default refines § 1. A pre-release is eligible when a specifier applying
  to that package names one, as § 1 said, **and also** when every version of
  that package the candidate sources offer is a pre-release. In the second case
  the resolution succeeds and installs a pre-release although nothing asked for
  one.
- `allow` makes pre-releases eligible for every package, whether or not a
  stable version exists.
- Eligibility is applied after the specifier, never instead of it. PEP 440's
  exclusion of pre-releases of the boundary version still holds, so a
  requirement `<0.2.0` never selects `0.2.0a1`: it selects the highest eligible
  version below `0.2.0`, which is a stable `0.1.0` where one is offered and
  `0.1.0a1` where every offered version is a pre-release.

### post-release and local-version boundaries

`/app/SPECS/specs/pep-0440.rst` fixes these comparisons; this section fixes
that they are observed exactly, on transitive requirements as much as on
command-line ones.

- An exclusive `>V` matches neither a post-release nor a local version of `V`.
  A requirement `>1.2.3` against a package offering only `1.2.3.post1` has no
  solution, and so does one against a package offering only `1.2.3+foo`;
  `>1.2.3.post1` against a package offering only `1.2.3.post1` and
  `1.2.3.post1+local` has no solution too. Each is status `1` with nothing
  installed.
- A specifier carrying no local segment ignores the candidates' local segments,
  so `==2.0.0` is satisfied by `2.0.0+foo` and `<=2.0.0` is satisfied by
  `2.0.0+foo`.
- Where a dependency's specifier and a command-line requirement on the same
  package cannot both be met by one version, the resolver backtracks through
  versions of the dependent package instead of reporting failure. Given a
  package whose newer version requires a local version that the command line
  rules out and whose older version requires one it admits, the older version
  is selected and both packages are installed.

### requirements files and markers

- A requirements file may contain `-r <path>` to include another. The path is
  resolved relative to the directory of the file that contains the line, not
  the working directory.
- `${NAME}` inside such a path is expanded from the process environment before
  the path is resolved.
- An empty extras list is accepted: `name[]` names no extras and installs the
  package itself.
- `python_implementation` is available as an environment marker with the value
  `/app/SPECS/specs/pep-0508.rst` gives it for the target interpreter — for a
  CPython environment, `CPython`. A requirement guarded by
  `python_implementation == "CPython"` is installed in such an environment.

### extras that name extras

- An extra of a package may require that same package under another extra.
  Self-referential extras are followed transitively: requesting one extra whose
  requirement list names two others resolves the dependencies of both, and the
  package carrying them appears exactly once in the result.

### further options on `uv pip compile`

- `--annotation-style` takes one value, `split` or `line`, and defaults to
  `split`. It selects the layout of the annotation comments and has no effect
  when `--no-annotate` is in force.
- `--no-strip-extras` takes no value and defaults to off. By default extras are
  stripped from the entries of a compiled requirement set, because the packages
  those extras pull in appear in the output in their own right; set, the extras
  are kept on the entries.
- `--unsafe-package` takes a package name and may be given more than once. The
  named package is omitted from the compiled output, while still being resolved
  and while its own dependencies still appear.
- A constraints file entry may pin a pre-release version. It is applied as any
  other constraint is, and the compile succeeds with status `0`.

### an unsatisfiable set from a single version

- A version that states two requirements on one package which no single version
  of it satisfies — `b==2.0.0` and `b==1.0.0` in the same metadata — has no
  solution. Status `1`.

### more of what `uv venv` does and accepts

- `--python` also accepts a bare `<major>.<minor>` version such as `3.12`,
  which looks for an installed interpreter of that minor version — on this
  platform, `python<major>.<minor>` on `PATH`.
- Every environment created carries `<venv>/CACHEDIR.TAG` in addition to the
  entries § 1 fixed, and `<venv>/bin/activate`.
- `--prompt` takes one value, the prompt prefix to write into the environment.
  Given no `--prompt`, the prefix is the environment directory's name when a
  path was given on the command line and the working directory's name when it
  was not; the literal value `.` selects the current directory's name. The
  environment is created either way, `<venv>/bin/activate` included, with
  status `0`.
- `--system-site-packages` takes no value and defaults to off. Set, the
  environment is given access to the system site-packages directory at run
  time; it does not change which distributions `uv`'s own commands consider.
  Status `0`, and `<venv>/bin/activate` is created.
- `--seed` takes no value and defaults to off. Set, seed packages are resolved
  from the configured index and installed into the new environment: `pip`
  always, and `setuptools` and `wheel` only when the environment's interpreter
  is older than Python 3.12. On a Python 3.12 environment no `setuptools`
  entry appears in the environment's `site-packages`.
- `uv venv` accepts `--index-url`: with `--seed`, the seed packages are
  resolved from the index it names.

## 3. interpreter discovery redefined

### how an interpreter request is satisfied

§ 1 fixed that `uv venv` finds an interpreter on `PATH`. That is superseded.
Discovery now works like this, wherever an interpreter request is taken:

- Candidate interpreters are enumerated in a defined order — for names looked
  for on `PATH`, the order of the directories in `PATH`.
- Each candidate is queried and tested against the request, and the search
  **continues past every candidate that does not satisfy it**. The first
  candidate that does satisfy the request is used. A request is never failed,
  and never answered with the wrong interpreter, merely because an unsuitable
  interpreter came earlier in the search order: with four CPython
  installations on `PATH` in descending version order, a request for the
  oldest of them still selects that oldest one and the environment is created
  from it, status `0`.
- When the request names no version, the first candidate matching the rest of
  the request wins. A request naming only an implementation selects the
  earliest interpreter of that implementation in the search order, whatever its
  version.

### the grammar of an interpreter request

`-p`/`--python` takes exactly one request, in any of these forms. `-p` and
`--python` are interchangeable and every form below works with either.

- A version alone: `3.12`, as § 2 fixed.
- An implementation name alone: `cpython`. Implementation names are matched
  case-insensitively, so `cpython` and `CPython` are the same request.
- An implementation and a version joined by `@`: `cpython@3.12`,
  `CPython@3.10`.
- An implementation and a version written together with no separator:
  `cpython3.10`.
- The name of an executable to look for on `PATH`, as § 1 fixed.
- A path. An absolute path and a relative path are both accepted, and a request
  that names neither an implementation nor a version and is not found on `PATH`
  is taken as a path relative to the working directory — so a bare `other`
  names the directory `other` beside the working directory, not an executable.
  A path may point at:
  - an interpreter executable, which is used as given, as § 1 fixed;
  - the root directory of an interpreter installation, whose interpreter is
    then used;
  - an existing virtual environment directory, whether written `./other` or
    `other`, whose interpreter is then used.

  A path request is satisfied from the path alone. The interpreter it selects
  is used even when `PATH` offers a different version first.

### `UV_PYTHON`

- `UV_PYTHON` holds an interpreter request in exactly the grammar above and
  supplies it to a command that was given none. With `UV_PYTHON` set and no
  `--python` on the command line, `uv venv` creates the environment from the
  interpreter the variable's request selects.
- `-p`/`--python` wins over `UV_PYTHON`.

### a request that cannot be satisfied is not quietly replaced

- When no discoverable interpreter satisfies the request, the request fails.
  Nothing is substituted for it: no virtual environment is created at the
  requested path, and in particular no interpreter that happens to be on `PATH`
  is used instead.
- This holds for both ways a request goes unsatisfied:
  - the request names an implementation that no discoverable interpreter
    provides — `graalpy@3.11` where only CPython installations are reachable;
  - the request names a version that no discoverable interpreter provides —
    `cpython@3.13` where only CPython 3.9 and 3.12 are reachable.
- Continuing the search past unsuitable candidates, fixed above, never degrades
  into accepting one of them at the end of the search.

### naming the environment a `uv pip` command acts on

- `uv pip install`, `uv pip sync` and `uv pip freeze` — and the other `uv pip`
  subcommands that act on an installed environment — take `-p`/`--python`,
  with one value.
- Here the request identifies the environment to operate on rather than an
  interpreter to build one from. A path to an existing virtual environment
  directory, such as `./target`, names that environment, and the command reads
  and writes that environment and no other:
  - `uv pip install` installs into it, leaving each distribution's
    `<target>/lib/python<X.Y>/site-packages/<name>-<version>.dist-info` behind,
    where `<X.Y>` is the minor version of the interpreter *that environment*
    was created from and not the one that would otherwise have been found;
  - `uv pip sync` installs exactly what the named requirements files resolve
    to, into that environment;
  - `uv pip freeze` reports that environment's contents to stdout in the format
    § 1 fixed.
  Each succeeds with status `0`.
- Where `-p`/`--python` is given, it determines the environment; the discovery
  § 1 fixed applies only when it is absent.

## 4. the project, tool, script and python interfaces

A *project* is a directory holding a `pyproject.toml` with a `[project]`
table; it is discovered by walking up from the working directory. Its
*project environment* is `.venv` at the project root. The commands below
resolve against the index configuration § 1 and § 2 fixed; in addition, a
`[tool.uv]` table in the project's `pyproject.toml` is a configuration
source for them, so `index-url` given there is used when no flag and no
environment variable supplies one. Precedence is: command-line flag, then
environment variable, then configuration table. They also accept the index
and resolver options of §§ 1 and 2 on the command line, and `-p`, long form
`--python`, which takes a value naming the interpreter to resolve and build
against, spelled as § 3 fixes interpreter requests.

The exit-status convention § 1 fixed, as superseded above, governs every
command below: a request that was understood and could not be carried out — a
requirement set with no solution, a package the index does not serve — exits
`1`, while `2` covers an unexpected internal failure and a command line that
does not parse. So `uv add` naming a package the index does not serve, `uv run`
in a project whose requirements admit no solution, and `uv run` on a script
whose inline metadata admits none all exit `1`.

### `uv.lock`

`uv.lock` is uv's own lockfile. No published specification governs it and
this section is its whole contract.

- It is written to `uv.lock` at the project root, in TOML.
- `uv lock` writes it. `uv add`, `uv remove`, `uv sync`, `uv run` and
  `uv tree` write or refresh it as part of their work unless told not to.
- It carries an array of tables named `package`. Every element has a `name`
  key whose value is the normalised package name as a string and a `version`
  key whose value is the release as a PEP 440 version string.
- Every resolved distribution contributes exactly one element, and the
  project itself is one of them: a project `root` at `0.1.0` contributes an
  element with `name = "root"` and `version = "0.1.0"`, alongside its direct
  and transitive dependencies. A package resolved to different versions for
  different environments contributes one element per version.
- Elements are ordered by name, and by version within a name.
- No other key of the file is fixed here.

### `uv init`

`uv init [PATH]` scaffolds a project. With `PATH` the project is created in
that directory, which is created if absent; without it the working directory
is used. Exit status `0`.

- The scaffold contains `pyproject.toml` and `README.md`, relative to the
  project directory.
- `--no-readme` takes no value and suppresses `README.md`; `pyproject.toml`
  is still written. Without it the README is written.
- `--name <NAME>` takes a value and sets `[project] name`. Without it the
  name is derived from the project directory.
- The scaffolded project's `[project] version` is `0.1.0`, so a `uv lock`
  immediately afterwards yields a lockfile whose only `package` element is
  the project at `0.1.0`.

### `uv add` and `uv remove`

`uv add <PACKAGES>...` adds PEP 508 requirements to `[project] dependencies`
in `pyproject.toml`; `uv remove <PACKAGES>...` takes bare names and deletes
the matching entries. Both exit `0` on success and rewrite the file in place,
leaving every other table, key and value — including `[tool.uv]` — untouched.

The rewritten `dependencies` value is always a multi-line array: the opening
`[` alone on the key's line, one requirement per line indented by four
spaces and quoted, each line ending in a comma including the last, then `]`
alone on its own line. An emptied list is written as `[]`.

By default both commands re-lock the project and then sync the project
environment. Two flags change that; neither takes a value.

- `--frozen` neither re-locks nor syncs the project environment. A
  requirement given without a version specifier is therefore written exactly
  as supplied: `uv add --frozen midpkg` writes `"midpkg"`.
- `--no-sync` re-locks but does not create or update the project
  environment. Because a resolution is available, a requirement given
  without a version specifier is written with a lower bound at the resolved
  version: with `midpkg 2.1.0` the newest match, `uv add --no-sync midpkg`
  writes `"midpkg>=2.1.0"` and leaves `uv.lock` behind.

`--script <SCRIPT>` takes a value and redirects both commands to that
single-file script instead of a project; see below.

### `uv lock`, `uv sync` and `uv run`

`uv lock` resolves the project and writes `uv.lock`. Exit status `0`.

`uv sync` materialises the project environment at `.venv`: it creates the
environment if absent, installs the locked packages and installs the project
itself. Exit status `0`. The environment holds exactly the locked
distributions and nothing else — no seed packages are placed in it — so for
a project `rootpkg 0.1.0` depending on `toppkg 3.0.0`, which pulls
`midpkg 2.1.0` and then `leafpkg 1.0.0`, `.venv/lib/python<X.Y>/site-packages`
ends up with exactly those four `.dist-info` directories.

`uv run <COMMAND> [ARGS]...` prepares the project environment the same way
and then executes `COMMAND` inside it, with the project importable. It exits
with the status of the command it ran, and the command's own stdout and
stderr pass through unaltered.

- `--no-project` takes no value: no project or workspace is discovered and
  no project environment is prepared. The command still runs.
- `--with <PACKAGE>` takes a value and makes that requirement, and its
  transitive dependencies, available for this invocation only. It does not
  touch `pyproject.toml` or `uv.lock`.

### decision order

More than one set of versions can satisfy every requirement of a project at
once. Which of them the lock holds is fixed, not left to the resolver: the
resolution proceeds outward from the project, settling the project's own
declared requirements before the requirements of the packages those reach,
and each version is chosen by the rule § 1 fixes at the moment it is
settled.

A requirement that a package introduces and that contradicts a version
already settled is answered by taking a lower version of the package that
introduced it, for as long as some version of that package is compatible
with what has been settled already. A version already settled gives way
only when no version of the package introducing the requirement can be
taken.

So for a project declaring `a` and `b`, where `a` is offered at `1.0.0` and
`2.0.0`, `b 1.0.0` requires `b-inner`, and `b-inner` is offered at `1.0.0`
requiring `too-old 1.0.0` and at `2.0.0` through `2.0.9` each requiring
`a==1.0.0`, the lock holds `a 2.0.0`, `b 1.0.0`, `b-inner 1.0.0` and
`too-old 1.0.0`. `a 2.0.0` is settled as a declared requirement before
`b-inner` is reached, and `b-inner` gives way to it rather than the
reverse.

### universal resolution

Resolving a project is not resolving for the interpreter in hand. `uv lock`
produces a resolution valid for every environment the project supports, and
environment markers on requirements are therefore not evaluated against the
running interpreter. Which version of a package is chosen once the
candidates are known still follows the rules §§ 1 and 2 and the decision
order above fix; `uv lock` records their answer. Exit status `0`.

- The supported environments are bounded by `[project] requires-python`.
  When the project declares none, the bound is `>=` the `<major>.<minor>` of
  the interpreter the resolution ran against.
- A requirement whose marker cannot hold anywhere within that bound is
  dropped: under `requires-python = ">=3.10"`, a requirement guarded by
  `python_version == '3.9'` contributes nothing, while under
  `>=3.10.1` one guarded by `python_version == '3.10'` still applies.
- A requirement whose marker can hold somewhere is resolved, whether or not
  it holds here. Requirements guarded by `sys_platform`, `os_name`,
  `platform_machine` or `implementation_name` are all resolved, and a
  distribution is locked even when none of its wheels matches the platform
  or the interpreter the resolution ran on.
- Where requirements for one package carry markers that cannot hold together
  and demand different versions, each such case is resolved separately and
  every version reached is recorded, with the markers under which it
  applies. Where a single version satisfies them all, only that version is
  recorded: `a>=1` together with `a<2`, both unguarded, gives one element,
  and so does a pair whose markers can never hold together but whose
  admitted sets overlap — with `1.0.0` and `2.0.0` offered, `a>=1 ;
  sys_platform == 'linux'` together with `a<2 ; sys_platform == 'darwin'`
  records `a==1.0.0` alone.
- A marker under which a package was reached continues to apply to that
  package's own requirements. A requirement whose inherited marker and own
  marker cannot hold at once is dropped.
- A package reached only through unguarded requirements takes a single
  version, valid everywhere: where the highest candidate would be unusable
  in one part of the resolution, a lower candidate that works in all of them
  is taken instead.

### `uv tree`

`uv tree` resolves the project and renders the dependency graph to stdout,
exit status `0`. The project is the root line, written `<name> v<version>`
with no prefix. Each dependency is one line, indented four columns per level
of depth. A node's last child is prefixed `└── ` and its subtree is indented
by four spaces; any earlier child is prefixed `├── ` and its subtree by `│`
followed by three spaces. So a project `root 0.1.0` requiring `toppkg 3.0.0`,
which requires `midpkg 2.1.0`, which requires `leafpkg 1.0.0`, renders as
four lines with each successive package one level deeper.

`--depth <DEPTH>`, short alias `-d`, takes an integer and is the greatest
depth rendered, the root being depth `0`. Its default is `255`, which no
fixture reaches, so the whole graph is rendered when the flag is absent.
`--depth 1` prints the root and its direct dependencies only.

### the script interface

A single-file script may carry an inline metadata block as PEP 723 fixes it.
Two behaviours are added here.

`uv run <SCRIPT>.py` — a sole operand ending in `.py` — reads that block
instead of a project, resolves its `dependencies` against its
`requires-python`, prepares an ephemeral environment containing them and
their transitive dependencies, and runs the script in it. Exit status is the
script's.

`uv add --script <SCRIPT> <PACKAGES>...` writes the requirement into the
script's block, exit status `0`. If the script has no block, one is created
at the very top of the file, immediately followed by the original contents,
with no blank line between. uv writes `requires-python` first and
`dependencies` second, the latter as a multi-line array with four spaces of
indent inside the comment prefix and a trailing comma on every entry:

    # /// script
    # requires-python = ">=3.12"
    # dependencies = [
    #     "leafpkg==1.0.0",
    # ]
    # ///

A requirement supplied with a version specifier is written verbatim. The
generated `requires-python` is `>=` the `<major>.<minor>` of the interpreter
uv selected for the script.

### `uv tool`

Tools live in a *tools directory*: `$UV_TOOL_DIR` if set, otherwise a
user-level data directory. Their executables are linked into an *executable
directory*: the first of `$UV_TOOL_BIN_DIR`, `$XDG_BIN_HOME`,
`$XDG_DATA_HOME/../bin` and `$HOME/.local/bin` that is set.

- `uv tool dir` prints the tools directory to stdout as a single line and
  nothing else. `--bin` prints the executable directory instead. Exit `0`.
- `uv tool install <PACKAGE>` builds and installs the package into an
  environment of its own and creates, in the executable directory, one
  executable per `console_scripts` entry point of the package, named for the
  entry point. `PACKAGE` may be a local directory path such as `./mytool`.
  Exit `0`. A package declaring `greet = greeter:main` yields exactly one
  file, `greet`, in that directory.
- `uv tool list` prints one line per installed tool, `<name> v<version>`,
  each followed by one line per executable it provides, `- <executable>`,
  written at column zero. Exit `0`.
- `uv tool uninstall <NAME>` takes the tool's package name, removes its
  environment and its executables, and leaves every other tool's executables
  in place. Exit `0`.
- `uv tool run --from <PACKAGE> <COMMAND> [ARGS]...` makes `PACKAGE`
  available in an ephemeral environment and runs `COMMAND` from it.
  `--from` takes a value and may be a local directory path. The exit status
  and output are the command's.

### `uv python` and interpreter selection

`uv python find [REQUEST]` resolves an interpreter request against the
discovery rules § 3 fixed and exits `0` when one satisfies it.

`uv python pin [REQUEST]` writes the request to `.python-version` in the
working directory and exits `0`. The file holds the request verbatim: `uv
python pin 3.12` leaves the single line `3.12`.

`uv venv`, invoked with neither `--python` nor `UV_PYTHON`, now takes its
request from the project context, in this order:

1. a `.python-version` file in the working directory, read as a request;
2. the `requires-python` of the `[project]` table in `pyproject.toml` in the
   working directory, read as a PEP 440 specifier set, satisfied by any
   discoverable interpreter whose version is in range.

A `.python-version` holding `3.11` therefore produces an environment whose
`lib/` contains `python3.11`, as does a `requires-python` of `>=3.11,<3.12`.

## 5. packaging as an opt-in, workspaces, sources, build and export

Every command § 5 adds accepts the global flag `--offline` alongside the ones
§ 1 fixes: it takes no value, is accepted wherever a global flag is accepted,
and forbids network access for that run rather than changing its result.

### the packaging default inverts

§ 4 treated every project as something to build and install into its own
environment. That is superseded here: a project is packaged only if it opts
in.

- A `pyproject.toml` with no `[build-system]` table declares an unpackaged
  project. It is neither built nor installed into the project environment.
  `uv sync` then installs the project's dependencies and nothing else — for
  a project depending on `a 1.0.0`, `.venv` ends with exactly one
  `.dist-info`, `a-1.0.0`, and `uv pip freeze` against it reports exactly
  `a==1.0.0`. With no dependencies at all `uv sync` still exits `0` and
  leaves an empty environment.
- `uv run` against such a project prepares that environment and executes the
  command in it, exit status `0` for a command that succeeds. The project is
  not importable from it.
- `[tool.uv] package` is a boolean that overrides the default in both
  directions. `package = false` in a project that does declare a
  `[build-system]` switches packaging off: no `.dist-info` for the project
  appears in the environment, and none of its `[project.scripts]` console
  scripts appears in `.venv/bin`. Importing it from `uv run` fails, and the
  interpreter's exit status `1` is what `uv run` returns, per § 4.

### `uv init` layouts

The flags below take no value. Without any of them `uv init` scaffolds a
project that declares no build system and no `src/` tree, consistent with
the default above.

- `--lib` scaffolds a packaged library: for `uv init --lib x` the module is
  at `x/src/x/__init__.py`.
- `--package` scaffolds a packaged project with the same source layout: `uv
  init --package y` gives `y/src/y/__init__.py`.
- `--script` makes `PATH` the path of a single-file script rather than a
  project directory, and `uv init --script s.py` writes `s.py` and no other
  Python file, carrying an inline metadata block of the shape § 4 fixed.
- `--app` names the default project kind and `--no-package` is the negation
  of `--package`; both take no value. `uv init --no-package z` therefore
  writes `z/pyproject.toml` and no `z/src/` tree.

All of these exit `0`.

### workspaces

`[tool.uv.workspace]` in a `pyproject.toml` makes that project a workspace
root. `members` is a list of path globs naming the member projects. A
workspace shares one lockfile and one environment, both at the root.

- The root may carry a `[project]` table and still be unpackaged; the
  packaging rule above decides. A root without `[build-system]` whose
  dependencies name member `m` syncs to an environment holding `m` alone.
- A packaged root and a packaged member are both installed: a root `root`
  whose members are `m` and `m2`, with `m2` carrying `package = false`,
  syncs to exactly `m-0.1.0.dist-info` and `root-0.1.0.dist-info`.
- `--package <PACKAGE>` on `uv sync` takes a member's name and syncs that
  member's dependencies instead of the root's, installing that member rather
  than the root project. It exits `0` for an unpackaged member.

### `[tool.uv.sources]`

`[tool.uv.sources]` redirects a declared dependency away from the index. A
key is the dependency's name; its value is a table naming the redirect —
`path` for a local directory, `workspace = true` for a member of the same
workspace — or an array of such tables. An array entry may carry a `marker`
key holding a PEP 508 marker, and the entry whose marker matches is the one
used. Several entries for one dependency are legal: a dependency with one
`path` entry guarded by `sys_platform == 'linux'` and another guarded by
`sys_platform != 'linux'` locks successfully, exit status `0`, and `uv.lock`
is written as § 4 fixed.

### `uv build`

`uv build [SRC]` builds distributions from a project through its PEP 517
backend and writes them to `dist/` — under the workspace root when the project
is a member of a workspace, and under the project root otherwise — creating the
directory if absent. Exit status `0`.

- With neither `--sdist` nor `--wheel` it builds both: for a project `proj
  0.1.0` the directory ends with `proj-0.1.0.tar.gz` and
  `proj-0.1.0-py3-none-any.whl`, named as the source-distribution and wheel
  specifications require.
- `--wheel` takes no value and limits the run to the wheel; `--sdist`
  likewise limits it to the source distribution. Each may be given alone.
- Whenever `uv build` creates `dist/`, it also writes `dist/.gitignore`.
- `-o`, long form `--out-dir`, takes a value and replaces `dist/` as the
  destination: a relative value is resolved against the working directory,
  and no `dist/` is created under the project root.

### `uv export`

`uv export` resolves the project, then writes the locked resolution as a
requirements file. `--format` takes a value; its only accepted value and its
default is `requirements-txt`. `-o`, long form `--output-file`, takes a value
and names the file to write; `uv export -o req.txt` leaves `req.txt` in the
working directory and exits `0`. Without `-o` the output goes to stdout.

### further `uv sync` and `uv run` grammar

- `uv sync --no-editable` takes no value and installs the project, and any
  workspace member, as an ordinary wheel rather than as an editable. The
  default is editable. Either way a packaged project contributes its own
  `.dist-info` to the environment, so a project `proj 0.1.0` yields
  `proj-0.1.0.dist-info`. Exit status `0`.
- `[tool.uv] dev-dependencies` is a list of PEP 508 requirements installed
  into the project environment in addition to `[project] dependencies`.
  `uv sync --only-dev` takes no value and installs those alone: neither the
  project's own dependencies nor the project itself is installed, so a
  project depending on `a` with `dev-dependencies = ["b"]` yields exactly
  `b-1.0.0.dist-info`. Exit status `0`.
- `uv run -m <MODULE> [ARGS]...`, long form `--module`, runs a Python module
  in the prepared environment rather than an executable. `uv run
  --no-project -m mymod` prints what `mymod` prints and exits `0`.

### upper bounds in `Requires-Python`

A release's declared `Requires-Python` constrains resolution through its
lower bound only; an upper bound does not disqualify it. A release of `a`
declaring `<=3.8` is an acceptable candidate for an environment running a
later interpreter, so `uv pip install a==1.0.0` against such an index exits
`0` and installs `a 1.0.0`.

### the project environment's location

`UV_PROJECT_ENVIRONMENT` names the project environment's directory,
overriding the `.venv` of § 4 for every project command. A relative value is
resolved against the project root. With `UV_PROJECT_ENVIRONMENT=myenv`,
`uv sync` creates `myenv/pyvenv.cfg` and no `.venv` at all, and exits `0`.

## 6. the breaking batch

This step changes rules earlier sections fixed. Each change below names the
section it supersedes; the rest of those sections stands.

One convention is superseded across the whole of this section rather than in
one place below it. § 1 reserved exit status `2` for an unexpected internal
failure and § 4 restated that reservation; § 2 had already superseded it for a
command line that does not parse. § 6 extends the supersession: `2` is the
documented status of a `uv python find` whose `.python-version` request cannot
be satisfied, and of any command that reads a `uv.toml` carrying a rejected
setting. Where this section gives `2` as an outcome, it is that outcome and
not an internal failure.

### dependency groups

`[dependency-groups]` is a top-level table in `pyproject.toml`. Each key is a
group name and each value a list of PEP 508 requirements. Groups are not
published metadata and are not extras.

- `--group <GROUP>` includes a group in addition to the default set,
  `--only-group <GROUP>` installs that group alone — as `--only-dev` above,
  neither the project's own dependencies nor the project itself is installed —
  and `--no-group <GROUP>` excludes one. Each takes a value and may be
  repeated. They are accepted by
  the project commands, `uv sync` and `uv run` among them.
- By default `uv sync` installs the `dev` group, matching the treatment
  `[tool.uv] dev-dependencies` already receives. `[tool.uv] default-groups`
  is a list of group names that replaces that default set.
- Group members are installed into the project environment like any other
  dependency: for a project with no dependencies of its own and a group
  `docs = ["a"]`, `uv sync --group docs` exits `0` and leaves exactly
  `a 1.0.0` installed. Declaring `default-groups = ["extra"]` makes a bare
  `uv sync` do the same for group `extra`.

### the base interpreter of a created environment

§ 1 fixed the layout `uv venv` produces. This section fixes the interpreter
path recorded in it. The path is recorded exactly as the interpreter was
requested or discovered; symbolic links along it are not resolved, so a
version-only directory name is preserved rather than being replaced by the
full version it points at.

- `pyvenv.cfg` in the environment carries a `home` key whose value is the
  directory containing the base interpreter, written as `home = <directory>`.
  An environment built from `<root>/cpython-3.12-linux-x86_64-gnu/bin/
  python3.12` records `home = <root>/cpython-3.12-linux-x86_64-gnu/bin`.
- Python running in that environment reports that same interpreter as
  `sys._base_executable`: `<root>/cpython-3.12-linux-x86_64-gnu/bin/
  python3.12`, and `<root>/cpython-3.11-linux-x86_64-gnu/bin/python3.11` for
  a 3.11 environment.

### `.python-version` in parent directories

§ 4 read `.python-version` from the project directory only. That is
superseded: the search ascends from that directory through its parents, and
the nearest file found supplies the request. A file two or more levels above
is honoured.

- A request that resolves to an unavailable version is fatal. `uv python
  find` exits `2`, and `uv venv` creates no environment at all — not even
  `pyvenv.cfg` — under the target directory.
- Because parents are searched, `uv init` no longer writes a
  `.python-version` for a workspace member whose version would repeat the
  one at the workspace root: after `uv init <MEMBER>` under such a root, the
  root's file is the only `.python-version` in the tree.

### rejected settings in `uv.toml`

A `uv.toml` may only carry settings that are meaningful outside a project.
Settings that are respected solely in `pyproject.toml` are now an error
rather than being ignored: `workspace`, `sources`, `dev-dependencies`,
`default-groups`, `managed` and `package`, as top-level keys or tables.

The `[pip]` table likewise rejects any key that is not one of its own
settings; `allow-insecure-host` is no longer among them, having become a
global option.

A `uv.toml` containing a rejected setting fails every command that reads
configuration, with exit status `2`, before any work is done — `uv venv`
in such a directory creates no `.venv/pyvenv.cfg`.

### local versions

Where §§ 1 and 2 fix the matching of versions carrying a local segment, this
section supersedes them: matching now follows the version-specifier
specification exactly.

- A specifier with no local segment matches a candidate that has one. A
  request for `a==1.0.0` installs `a 1.0.0+cpu` whether or not a release
  `1.0.0` without a local segment exists; `uv pip freeze` then reports
  `a==1.0.0+cpu`, and a resolution compiled to a file records the same
  string.
- Where more than one candidate matches, the highest version wins: a release
  carrying a local segment sorts above the same release without one, and
  local segments are ordered among themselves. With `1.2.3+bar` and
  `1.2.3+foo` published, `a==1.2.3` and `a<=1.2.3` alike install
  `a 1.2.3+foo`, and that holds whether or not the release without a local
  segment offers a distribution that could be used here.
- The rule applies to dependencies of dependencies: `a 1.0.0` requiring
  `b==2.0.0` resolves against an index offering only `b 2.0.0+local`,
  giving `a==1.0.0` and `b==2.0.0+local`.
- Locking obeys the same rule: a project depending on `a==1.0.0` with only
  `a 1.0.0+cpu` published locks successfully, exit status `0`, and `uv.lock`
  is written.

### pre-releases and `!=`

§ 1 made a specifier naming a pre-release an opt-in to pre-release versions
for that package. That is superseded for the `!=` operator alone: a
specifier such as `a != 2.0.0a1` no longer opts in. Against an index holding
`1.0.0` and `3.0.0a1`, `a!=2.0.0a1` resolves to `a==1.0.0`, both when
installed and when compiled. Every other operator keeps the behaviour § 1
fixed.

### additions to the pip interface

- `uv pip install --exact` takes no value and makes the installation exact:
  distributions already present in the environment that the resolution does
  not require are removed. Installing `a` with `--exact` into an environment
  holding `a` and `b` leaves `a` alone. Without the flag extraneous
  distributions are kept.
- `uv pip list --outdated` takes no value and lists installed distributions
  for which the index offers a newer version. Exit status `0`.
- `uv pip show -f`, long form `--files`, takes no value and adds the
  installed files of each named distribution to the report. Exit status `0`.

### additions to `uv run`

Extending § 4:

- `uv run -` reads a Python script from standard input and runs it as if it
  were a file, inline metadata included. A script whose block requires `a`
  runs with `a` importable and exits `0`.
- `--with` accepts several packages in one value, comma-delimited:
  `--with a,b` makes both importable. The flag remains repeatable.

### `uv lock` and `uv export`

- `uv lock --dry-run` takes no value: the resolution is performed and
  reported but `uv.lock` is not written. Exit status `0`.
- `uv export --no-header` takes no value and omits the comment header from
  the generated output. Exit status `0`.

### global options

These precede the subcommand or follow it, and apply to every command.

- `--allow-insecure-host <HOST>` takes a value, may be repeated, and permits
  insecure connections to that host. Its environment variable is
  `UV_INSECURE_HOST`. It is accepted even by commands that make no requests,
  which then behave as usual: `uv --allow-insecure-host 127.0.0.1 cache dir`
  exits `0`.
- `--directory <DIRECTORY>` takes a value and changes to that directory
  before the command runs, so discovery of projects, configuration and
  `.python-version` starts there and relative paths are resolved against it.
- `UV_FROZEN` is the environment spelling of `--frozen`. Set to `1`, it
  stops `uv sync` from resolving: no `uv.lock` is created, and none is
  updated. `UV_LOCKED` is the spelling of `--locked`.

## 7. the forking universal resolver

A resolution no longer has to describe a single marker environment. A *universal*
resolution describes every environment its request covers at once, and where no one
assignment of versions satisfies them all it splits into *forks*: regions of the marker
space that cannot overlap, each resolved on its own terms. Every entry a universal
resolution produces carries the marker of the region it belongs to.

### Universal resolution

`uv pip compile --universal` resolves universally. `uv lock` always resolves universally,
over the whole of the project's `requires-python`, and takes no flag to do so.

- A universal resolution is independent of the platform and the interpreter of the process
  that produces it. A fork guarded by `sys_platform == 'win32'` is produced on any
  platform, and an entry is never dropped because the running environment does not match
  its marker.
- With `--universal`, `--python-version X.Y` fixes the *lower bound* of the Python versions
  the resolution covers instead of pinning one version: the resolution must hold for every
  `python_full_version >= X.Y`. Without `--universal` the flag keeps the meaning fixed
  earlier.
- Each resolved entry occupies one line, `<name>==<version>`, followed by ` ; ` and a
  marker when the entry does not apply in every covered environment, and by nothing when
  it does. Entries are ordered by package name, then by version.
- A package may occur more than once, once per fork, provided the markers guarding the
  occurrences are pairwise disjoint.

### Marker algebra

Markers are manipulated symbolically — normalised, conjoined, and tested for
satisfiability — rather than evaluated against one environment. Two markers that select the
same set of environments are interchangeable, and what is written out is the normalised
form, not the form the requirement was written in.

- A `platform_system` comparison is rewritten in terms of `sys_platform` wherever the value
  has a counterpart: `'Linux'` to `'linux'`, `'Darwin'` to `'darwin'`, `'Windows'` to
  `'win32'`. The operator is carried across, so `platform_system != 'Windows'` is written
  `sys_platform != 'win32'`. This holds for markers on direct requirements and for markers
  on the requirements of resolved packages alike.
- A bound on the Python version is written with `python_full_version` against a quoted
  release, as in `python_full_version >= '3.12'`. `python_version` is not used in output.
- String values are single-quoted, a conjunction is joined by ` and `, and the lower bound
  of a range precedes the upper one:
  `python_full_version >= '3.11' and python_full_version < '3.13'`.
- A marker is simplified against the region the resolution covers before it is written: a
  bound that region already implies is omitted. Over `python_full_version >= '3.9'`, the
  fork covering 3.9 up to 3.11 is written `python_full_version < '3.11'`.
- The marker recorded for a dependency is the conjunction of the dependency's own marker
  with the marker of the fork that required it.

### Disjoint markers

`sys_platform`, `platform_system` and `os_name` describe one platform and are reconciled
against each other. `sys_platform == 'win32'`, `platform_system == 'Windows'` and
`os_name == 'nt'` hold in exactly the same environments. Beyond that equivalence the
reconciliation rules out a fixed list of pairs and nothing else: `os_name == 'nt'` with
`sys_platform == 'linux'`, `'darwin'` or `'ios'`; `os_name == 'posix'` with
`sys_platform == 'win32'`; and each of `platform_system == 'FreeBSD'`, `'NetBSD'`,
`'OpenBSD'`, `'SunOS'`, `'iOS'` and `'iPadOS'` with each of `sys_platform == 'aix'`,
`'android'`, `'emscripten'`, `'ios'`, `'linux'`, `'darwin'`, `'win32'`, `'cygwin'` and
`'wasi'` — except `platform_system == 'iOS'` or `'iPadOS'`, which hold with
`sys_platform == 'ios'`. A pair outside that list is not ruled out, however unlikely it
looks: `os_name == 'linux'` with `sys_platform == 'illumos'` is a live combination.

- A conjunction that demands two values which cannot hold together is unsatisfiable, and
  the requirement it guards is unreachable: it contributes no entry of its own and none of
  its dependencies. This is not a failure — the remaining requirements resolve and the run
  exits 0. `os_name == 'nt' and sys_platform == 'linux'`,
  `os_name == 'posix' and sys_platform == 'win32'` and
  `sys_platform == 'linux' and platform_system == 'Windows'` are each such a conjunction.
- The same test applies inside a fork. A dependency whose marker is disjoint from the
  marker of the fork that introduced it is pruned in that fork, together with everything
  reachable only through it, while the same dependency may still be reached in another
  fork.

### When a resolution forks

- Requirements on one package guarded by markers that cannot both hold are resolved
  separately, one fork per marker, each fork keeping only the constraints that apply in it.
- Each fork resolves independently. A version chosen in one fork constrains no other, so
  two forks may select different versions of a shared transitive dependency; each such
  version is written under the marker of its own fork.
- Under the default strategy, candidate versions of a package whose `requires-python`
  differ also fork: the covered Python range is split at each distinct lower bound, and
  each resulting region takes the highest version that supports the whole of it. Over
  `python_full_version >= '3.9'`, with `a` at 1.0.0 requiring `>=3.9`, 2.0.0 requiring
  `>=3.11` and 3.0.0 requiring `>=3.13`, a universal resolution of `a` is

      a==1.0.0 ; python_full_version < '3.11'
      a==2.0.0 ; python_full_version >= '3.11' and python_full_version < '3.13'
      a==3.0.0 ; python_full_version >= '3.13'

  and the dependencies of each version are written under the same marker as the version
  that brought them in.

### `--fork-strategy`

- `--fork-strategy <strategy>` takes one value, either `requires-python` or `fewest`. The
  default is `requires-python`.
- `requires-python` prefers the newest version of a package for each supported Python
  version, splitting the covered range as needed.
- `fewest` prefers the fewest distinct versions of a package, taking an older version where
  it is compatible with a wider part of the covered range. Where one version supports the
  whole range, that version is selected and no fork on version is made.
- The flag is accepted by `uv pip compile`, `uv lock` and `uv sync`.
  `[tool.uv] fork-strategy = "<strategy>"` sets the same thing for the project; a value
  given on the command line wins over the setting.
- With `a` at 1.0.0 requiring `>=3.9` and 2.0.0 requiring `>=3.12`, over `>=3.9`, the
  default produces `a==1.0.0 ; python_full_version < '3.12'` and
  `a==2.0.0 ; python_full_version >= '3.12'`, while `fewest` produces `a==1.0.0` alone,
  unmarked.

### Fork guards in `uv.lock`

- `uv lock` writes `uv.lock` beside `pyproject.toml` and exits 0. The guards of the forks it
  made are recorded there — the resolution's fork markers as `resolution-markers`, and the
  marker under which each locked package and each dependency edge applies alongside it — so
  that a later command can pick a fork without resolving again.
- `uv sync` installs exactly those locked entries whose markers hold for the interpreter of
  the target environment. With `a` at 1.0.0 (`>=3.9`), 2.0.0 (`>=3.11`) and 3.0.0 (`>=3.13`)
  locked over `>=3.9`, syncing on 3.11 installs 2.0.0 and nothing else; with 1.0.0 (`>=3.9`)
  and 2.0.0 (`>=3.12`), syncing on 3.13 installs 2.0.0.
- `--frozen` does not resolve. The fork is chosen from the recorded markers alone, and
  `uv sync --frozen` installs what `uv sync` would have installed for the same interpreter.
- A resolution made under `--fork-strategy fewest` is locked as it was resolved: syncing on
  3.13 against a lock whose single entry is 1.0.0 installs 1.0.0.

### `uv tree` over a forked resolution

- `uv tree` renders only the fork that applies to the current environment. Against the lock
  above, on 3.12, it renders the project with the single child `a v2.0.0`.
- `uv tree --universal` renders every fork. A package resolved to more than one version
  appears once per version, ordered by version, each occurrence carrying its own subtree:

      proj v0.1.0
      ├── a v1.0.0
      │   └── b v1.0.0
      └── a v2.0.0
          └── c v1.0.0

- In a universal tree, an entry reached only when an extra is enabled is annotated after
  its version with ` (extra: <name>)`.
- `--frozen` is accepted by `uv tree` exactly as it is by `uv sync`, and means the same
  thing: the tree is rendered from the guards the lock already records rather than from a
  fresh resolution, which requires that `uv.lock` exist. `uv tree --frozen` renders what
  `uv tree` would have rendered, and `uv tree --universal --frozen` what
  `uv tree --universal` would have rendered, for the same lock.

### `[tool.uv] conflicts`

As § 6 said of its own outcomes, the exit status `2` this subsection gives is that
documented outcome and not the internal failure § 1 reserved.

- `conflicts` is an array of arrays in `[tool.uv]`. Each inner array declares a set of
  project features that may not be enabled together. Each item is a table with exactly one
  key: `extra = "<name>"`, naming an entry of `[project.optional-dependencies]`, or
  `group = "<name>"`, naming an entry of `[dependency-groups]`.
- An inner array with fewer than two items is invalid. The command fails with exit status
  2 and writes no `uv.lock`.
- A well-formed declaration is resolved rather than refused: `uv lock` exits 0 and writes
  `uv.lock`. The declared items are resolved in separate forks, so one package may be
  locked at a different version for each of them.
- Enabling two items declared to conflict in one invocation fails with exit status 2.
  `uv sync --extra x --extra y`, with `x` and `y` declared in conflict, is refused.
- Either item on its own is unaffected: `uv sync --extra y` exits 0 and installs exactly
  the resolution that belongs to `y`.
- `uv tree --universal` over such a lock shows each version with the item that reaches it:

      proj v0.1.0
      ├── a v1.0.0 (extra: x)
      └── a v2.0.0 (extra: y)

## 8. request validation and the subprocess contract

A request that names something the project does not declare is now refused instead of
being quietly narrowed, and a process `uv` starts on your behalf is accounted for in
`uv`'s own exit status.

### Requests are checked against the project

- `uv sync --extra <name>`, where `<name>` is not an entry of
  `[project.optional-dependencies]`, fails with exit status 2. The extra is not silently
  ignored.
- `uv sync --group <name>`, where `<name>` is not an entry of `[dependency-groups]`, fails
  with exit status 2. This holds under `--frozen` as well: the requested groups are checked
  against the groups the existing lockfile records, which the run uses unchanged.
- Extras and groups declared to conflict remain refused with exit status 2, as § 7 fixes.

### `uv sync` and `uv tree` flags

- `uv sync --dry-run` resolves and reports what it would do without creating or modifying
  the environment, and exits 0.
- `uv sync --active` installs into the activated virtual environment named by
  `VIRTUAL_ENV`, in preference to the project environment. It takes precedence over
  `UV_PROJECT_ENVIRONMENT`: with `UV_PROJECT_ENVIRONMENT` set and an environment activated,
  `uv sync --active` exits 0 having installed the project's dependencies into the activated
  environment.
- `uv sync --no-default-groups` disables every dependency group that would otherwise be
  enabled by default, `dev` included. A group named explicitly with `--group` is still
  enabled. The run exits 0.
- `uv tree` accepts `--no-dev` and `--invert` in the same invocation; the combination is not
  a usage error and exits 0.

### Interpreter selection across the surface

- `-p` is the short alias of `--python` on every subcommand that has one, `uv pip compile`
  included, and takes an interpreter request — a version, an implementation, an executable
  name or a path. `uv pip compile -p python3.12` selects that interpreter and exits 0.
- `UV_PYTHON` is read by the `uv pip` commands and by `uv python install`. It takes
  precedence over a `.python-version` file and yields to `--python`/`-p` given on the
  command line. `uv pip compile` run with `UV_PYTHON=3.9` resolves for 3.9: a requirement
  no version of which supports 3.9 has no solution, and the run fails with exit status 1.
- `uv tool run python` runs an interpreter rather than looking for a package named
  `python`; no package is resolved for it. `uv tool run python -c "print('ok')"` writes
  `ok` to stdout and exits 0.

### `uv run` as a parent process

- Every process `uv` spawns has `UV` set in its environment, to the path of the `uv`
  executable handling the invocation. It is set even when there is no project, as under
  `uv run --no-project`, and it overwrites any inherited value.
- When the spawned process is terminated by a signal, `uv` exits with `128` plus the
  signal number rather than reporting success. A child killed by `SIGTERM` gives exit
  status 143.

### `uv init` scaffolding

- `uv init` writes a sample module into a new project. For an application project that is
  not packaged — the default shape, also selected by `--app --no-package` — the module is
  `main.py` in the project directory.
- `uv init --bare` creates only `pyproject.toml`: no sample module, no `README.md`, no
  source tree, and no `.python-version`. It exits 0.

### Build options from the environment

- `UV_NO_BINARY` is the environment equivalent of `--no-binary`: when set, every
  distribution is built from source.
- `UV_NO_BINARY_PACKAGE`, the environment variable, is the equivalent of the flag
  `--no-binary-package`; it takes a space-delimited list of package names.
- Both are honoured by `uv sync`. A package that publishes no source distribution cannot
  satisfy either, so `uv sync` fails with exit status 1 and installs nothing — the project
  environment is left with no distributions in it.

### `uv publish`

- `uv publish` is no longer a preview feature and emits no preview or experimental warning.
- Invoked where it finds nothing to upload, its entire diagnostic output is the single
  line `error: No files found to publish`.

## 9. project versions and the standard lock file

### `uv version`

`uv version` reports and edits the *project's* version.

- Run where `pyproject.toml` carries a `[project]` table with a `name` and a
  `version`, `uv version` writes one line to stdout and exits `0`: the name, a
  space, the version. For a project named `project` at `7.7.7` the whole line
  is `project 7.7.7`.
- `--short` takes no value. The name is dropped and the line is the version
  alone: `1.2.3`.
- `--bump <SEGMENT>` takes one of `major`, `minor` and `patch`. It raises that
  segment of the project version by one and zeroes every segment after it, so
  `--bump minor` against `1.2.3` gives `1.3.0`.
- A bump writes the new version into the `version` value of the `[project]`
  table of `pyproject.toml` and changes nothing else in that file: the other
  keys, their order, their values and the file's spacing are preserved. A file
  that read

      [project]
      name = "project"
      version = "1.2.3"
      requires-python = ">=3.12"
      dependencies = []

  reads identically afterwards but for `version = "1.3.0"`.
- A bump reports the transition on stdout as the name, the old version, ` => `
  and the new version — for that file, `project 1.2.3 => 1.3.0` — and exits `0`.
- `--preview` takes no value and makes the command strict about the project:
  where no `pyproject.toml` is discoverable in the working directory or any
  parent, `uv version --preview` exits `2` and writes nothing to stdout. What
  `uv version` does in that situation without `--preview` is not fixed here.
- uv's own version is now reported by `uv self version`, which exits `0` and
  writes nothing at all to stderr — it is not deprecated and warns about
  nothing. `--short` has the same meaning there.

### `--version` is a top-level flag only

§ 1 fixed `--version` with no subcommand. It is accepted there and nowhere
else. Given to a subcommand — `uv run --version`, `uv pip install --version` —
it is an unrecognised argument: the run is a usage error, exit status `2`, with
nothing on stdout and the subcommand's work not begun.

### TOML files are PEP 751 lock files

A `.toml` path in a requirements position is a PEP 751 lock file and is never
read as a `requirements.txt`. Which names such a file may bear is fixed by
PEP 751 under "File Name" and is not restated here beyond this: uv recognises
as one exactly the names that specification admits — `pylock.toml`, or
`pylock.<name>.toml` where `<name>` holds no dot.

- The input positions are the positional arguments of `uv pip compile` and the
  `-r`/`--requirement` values of `uv pip install` and `uv pip sync`. A `.toml`
  path in one of them whose file name is not of that form is refused whatever
  the file contains, an empty file included: exit status `2`, nothing on
  stdout, nothing resolved and nothing installed. `uv pip compile reqs.toml`
  and `uv pip install -r reqs.toml` both fail that way.
- The output positions are `-o`/`--output-file` of `uv pip compile` and of
  `uv export`. `uv pip compile` reads the format off the extension: `.toml`, in
  any case, selects PEP 751 output and any other extension selects
  `requirements.txt`. Where PEP 751 output is selected and the file name is not
  of the form above, the run is refused before the file is created —
  `uv pip compile -o out.toml reqs.txt` exits `2` and leaves no `.toml` file in
  the working directory.
- `uv export` reads the format off the whole file name rather than the
  extension: an `-o` naming a PEP 751 file selects that format, anything else
  selects `requirements.txt`.
- `--format` takes `requirements.txt` or `pylock.toml` and overrides the
  inference on both commands. Absent, the format is inferred as above. This
  supersedes the `uv export` bullet of § 5, which fixed a single accepted
  value: `pylock.toml` is accepted from this milestone on, and the
  requirements format may be spelled `requirements.txt` or, as § 5 spells it,
  `requirements-txt`. Both spellings name the same format.
- What goes inside a `pylock.toml` is PEP 751's business, not this document's.

### unknown dependency group object specifiers

An entry of a list under `[dependency-groups]` is either a requirement string
or a table. `include-group` is the only key such a table may carry; a table
with any other key is not a valid entry, and it is no longer ignored.

- A project whose `[dependency-groups]` holds one cannot be locked or synced.
  `uv lock` and `uv sync` both stop on it and write no `uv.lock`.
- This holds even when the offending group is neither requested nor enabled by
  default, and even when the project has no dependencies at all: the table is
  rejected when the project's metadata is read, not when the group is used.

### arbitrary requests in a version file

§ 4 fixed `.python-version` in the working directory as a source of interpreter
requests, read verbatim. That is narrowed here. A line that is not a version, an
implementation, an implementation with a version, or a path is an executable
name to search for; requests of that form are ignored when they come from a
version file, and the file is read as though the line were absent. The
restriction is on version files alone — such a request given to `--python` or in
`UV_PYTHON` is unaffected.

- A `.python-version` holding `my-pyenv-env` therefore has no effect at all.
  `uv python find` in that directory selects an interpreter by the discovery
  rules § 3 fixed, prints its path and exits `0` — the same path as with no
  version file present.
- `uv venv` in that directory likewise creates `.venv` from that selection,
  leaving `.venv/pyvenv.cfg`, and exits `0`.

### `--frozen` and `--no-sources` are mutually exclusive

`--no-sources` requires a fresh resolution and `--frozen` forbids one, so the
two may not be given together. `uv sync --frozen --no-sources` is a usage
error: exit status `2`, nothing resolved and no project environment created, so
no `.venv/pyvenv.cfg` appears.

### an empty directory variable is an unset one

`UV_PYTHON_INSTALL_DIR` names the directory holding managed Python
installations, and `UV_TOOL_DIR` the tools directory § 4 fixed. Each takes
effect only when set to a non-empty value; set to the empty string, it is
treated as unset rather than as the working directory.

- `uv python dir` writes the managed-Python installation directory to stdout as
  a single line and nothing else. With `UV_PYTHON_INSTALL_DIR` set to the empty
  string, that line is the default location: `uv/python` under the user-level
  data directory, which is `$XDG_DATA_HOME` where that variable is set.
- `uv tool dir` under an empty `UV_TOOL_DIR` prints the default tools directory
  the same way, `uv/tools` under that same data directory.

### `uv init --build-backend`

`--build-backend` takes a value, the backend the new project declares in its
`[build-system]`. It implies `--package`, and may not be combined with
`--no-package` or `--script`: either pairing is a usage error, refused with
exit status `2`.

- `poetry-core` is among the accepted values. `uv init --build-backend
  poetry-core proj` creates the project directory `proj` with a
  `proj/pyproject.toml` in it and exits `0`.

### a tool command must come from the tool's package

§ 4 fixed `uv tool run <COMMAND>` and its `--from`. The plain form is narrowed
here: `uv tool run <NAME>` prepares an environment for the package `NAME` and
then requires an executable called `NAME` to be provided by a package installed
in it. The `uv tool run python` exception § 8 fixes stands unchanged: that one
name still runs an interpreter rather than resolving a package.

- Where none is, the run fails with exit status `1` before anything is
  executed. Nothing is written to stdout, and an executable of that name found
  on `PATH` is not run in its place: `uv tool run true`, for a package named
  `true` that installs no executable, fails even where a `true` program exists
  on `PATH`.
- A package that provides no executables at all is refused the same way: for an
  empty package `foo`, `uv tool run foo` writes nothing to stdout.
- An executable provided by a dependency of the requested package satisfies the
  requirement.
- The requirement is on this form alone. A command named through `--from` is not
  checked against the package that provides it, as § 4 already fixes.

## 10. managed interpreters on `PATH`, virtual dependencies and exit-code conventions

Installing a managed interpreter now places an executable where a shell will
find it; a dependency that declares no build system is now built and installed
rather than skipped; and the commands that reported a failure as a success, or
a user error as a crash, take the statuses the rest of the interface uses.

### installing a managed interpreter onto `PATH`

`uv python install [<REQUEST>...]` installs managed interpreters. From this
step it also links a versioned executable — `python3.13` for a CPython 3.13
install — into an executable directory, so that the installed interpreter is
reachable by name.

- `--bin` and `--no-bin` take no value and select that linking. `--bin` is the
  default. `UV_PYTHON_INSTALL_BIN` carries the same switch, `0` meaning
  `--no-bin`; the flag wins over the variable.
- `--default` takes no value and defaults to off. Set, the unversioned names
  `python3` and `python` are linked as well; unset, only the versioned name is.
- The target directory is `UV_PYTHON_BIN_DIR` when that is set, and otherwise
  the same user executable directory uv uses for installed tools.
- A linked name that shadows an interpreter already on `PATH` is still linked;
  shadowing is not refused. The linked executables are the base installation,
  through which no project dependency is importable.

Whether an interpreter may be downloaded is governed by the `python-downloads`
setting, spelled `UV_PYTHON_DOWNLOADS` in the environment, whose values are
`automatic`, `manual` and `never`. Under `never`, an explicit
`uv python install` downloads nothing and fails. Its status is `1`, the status
§ 1 fixed for a request that was understood and could not be carried out, and
its whole stderr is:

    Python downloads are not allowed (`python-downloads = "never"`). Change to `python-downloads = "manual"` to allow explicit installs.

Nothing is written to stdout. The refusal is reached after the command line
parses, so `--bin`, `--no-bin` and `--default` are accepted options whatever
the download policy is.

### `uv python upgrade`

`uv python upgrade [<TARGET>...]` is a subcommand of `uv python`. Each target
is a minor version whose installed patch release is replaced by the newest
supported one. Replacing it is a download, so under
`python-downloads = "never"` an upgrade of an installed target is refused the
way an explicit install is, with the status `1` above and nothing installed.
`uv python upgrade --help` writes usage to stdout, writes nothing at all to
stderr, and exits `0`.

### removing a Python pin

§ 4 fixed `uv python pin [REQUEST]` as the writer of `.python-version`.

- `--rm` takes no value and removes a pin instead of writing one. Against the
  working directory it deletes `.python-version`, exits `0`, and writes to
  stdout, as the whole message:

      Removed Python version file at `.python-version`

- `--global` takes no value and redirects `uv python pin` from the working
  directory to uv's user-level pin. It is required to remove that pin:
  `--rm` on its own only ever removes the working directory's file.

### statuses for the remaining outcomes

§ 1 reserved status `2` for an unexpected internal failure and said it was not
a documented outcome of anything below. That is superseded here as § 2
superseded it for a command line that does not parse. The three outcomes this
section gives status `2` — a `uv venv` that cannot be carried out, a
`uv version` with no project discoverable, and any command that reads a
`uv.toml` carrying a rejected field — are documented outcomes, and a run that
reaches one of them has not failed unexpectedly.

- `uv lock --check` and `uv sync --check` take no value. Each reports whether
  the lockfile, respectively the project environment, is up to date without
  changing it. Up to date is `0`; out of date is `1`, not `2` — being out of
  date is an outcome of the command, not an error in the request.
- A `uv venv` that cannot be carried out exits `2`, and its diagnostic is a
  single `error: ` line on stderr rather than a multi-line rendering. § 3
  fixed that an unsatisfiable interpreter request fails without substituting
  anything; this fixes how. `uv venv --python 3.7 env`, with no interpreter
  matching `3.7` reachable, writes nothing to stdout, exits `2`, and its whole
  stderr is:

      error: No interpreter found for Python 3.7 in managed installations or search path

- `uv version`, with no project discoverable from the working directory, exits
  `2` and writes nothing to stdout. The fallback of printing uv's own version
  in that situation is gone; `uv self version` is unaffected.

### dependencies that declare no build system

A project that declares no `[build-system]` is not built for its own sake.
That is unchanged. What changes is its treatment as a *dependency*:

- A dependency resolved through a `path` source is built and installed into
  the environment whether or not it declares a build system. Absent one, it is
  built through the legacy setuptools backend.
- A workspace member that is a dependency of another workspace member is
  treated the same way.
- A `uv sync` whose only dependency is such a directory therefore performs a
  build. When that build cannot be carried out — the legacy backend's own
  requirements are not obtainable, for instance — the sync fails with status
  `1` and installs nothing.
- `package = false`, set either in the source declaration or as `tool.uv.package`
  in the dependency's own `pyproject.toml`, opts back out and restores the
  earlier treatment.

### `uv add` on a subdirectory

`uv add ./foo`, where `foo` is a directory below the project, now adds `foo`
as a workspace member rather than as a bare path source. In a project whose
`pyproject.toml` has no `[tool.uv]` tables, `uv add --frozen ./foo` leaves:

```toml
[tool.uv.workspace]
members = [
    "foo",
]

[tool.uv.sources]
foo = { workspace = true }
```

appended after the `[project]` table, with `"foo"` added to `dependencies` in
the multi-line form § 4 fixed. `--no-workspace` takes no value and opts out,
writing `foo = { path = "foo" }` as before. `--workspace` states the default
explicitly.

`--bounds` takes a value and selects the upper bound `uv add` writes beside the
lower bound it resolves. Its values are `lower`, `major`, `minor` and `exact`,
and `lower` is the default. For a resolved `1.2.3`, `lower` writes `a>=1.2.3`,
`major` writes `a>=1.2.3,<2.0.0`, `minor` writes `a>=1.2.3,<1.3.0`, and `exact`
writes `a==1.2.3`. The entry is written in the same multi-line `dependencies`
form. The option is in preview, so using it also prints the preview warning to
stderr; nothing else about the run changes.

### conflicting editable annotations

A `path` source may be declared more than once for one package with differing
`editable` values, provided each declaration names a distinct dependency group
through `group`. Two declarations of the same path, one with `editable = true`
and one with no `editable` annotation at all, conflict: `uv lock` fails and
writes no `uv.lock`.

### the default build backend of `uv init`

- `uv init --lib` and `uv init --package` write a `[build-system]` naming uv's
  own build backend, `uv_build`. Since uv provides that backend, building such
  a project fetches no build requirement. The table is a `requires` naming
  `uv_build` and nothing else, then `build-backend = "uv_build"`:

      [build-system]
      requires = ["uv_build<bound>"]
      build-backend = "uv_build"

  The `<bound>` is a version range over uv's own version. Which range it is
  is not fixed here and is no part of any requirement, consistent with the
  version string being nothing the checkpoint compares.
- `--build-backend <NAME>` takes a value and selects another backend. `NAME`
  is the distribution that provides the backend; the table written names that
  distribution in `requires` and the backend's own entry point in
  `build-backend`, and whether a version bound accompanies the distribution
  depends on the backend. For `hatchling` the table is:

      [build-system]
      requires = ["hatchling"]
      build-backend = "hatchling.build"

- Plain `uv init`, with neither `--lib` nor `--package`, still writes no
  `[build-system]`.
- `uv sync --project <PATH>` takes a value naming the project directory and
  operates on `<PATH>/.venv`. For a project scaffolded as `demo` by
  `uv init --lib demo`, a subsequent `uv sync --project demo` exits `0` and
  leaves exactly one distribution directory,
  `demo/.venv/lib/python<X.Y>/site-packages/demo-0.1.0.dist-info`.

### what `--python-platform linux` means

`--python-platform linux` denotes `x86_64-manylinux_2_28`; it previously
denoted `x86_64-manylinux_2_17`. A wheel tagged `manylinux_2_Y_x86_64` is
compatible with the target when `Y` is at most `28`, so a distribution
published only as a `manylinux_2_24_x86_64` wheel resolves under that target,
status `0`, instead of failing as unsatisfiable. `--python-platform
x86_64-manylinux_2_17` names the older target explicitly. Detection of the
local platform, when no target is requested, is unaffected.

Two shapes of the compiled output are fixed here. The header, written unless
`--no-header` is given, is two comment lines: `# This file was autogenerated by
uv via the following command:`, then `#` followed by four spaces and the
command as invoked, with index arguments omitted from the reproduction. An
entry's annotation, written unless `--no-annotate` is given, is a following
line indented four spaces, `# via ` and then the origin — `-r <path>` naming
the input file that asked for it.

### `uv run --with` runs in an ephemeral environment

§ 4 fixed `--with` as making a requirement available for one invocation. The
interpreter now runs in an empty environment layered over the one holding
those requirements, rather than in that environment itself. A `--with` package
is importable, but its module file does not lie under `sys.prefix`.

### fields `uv.toml` does not accept

A `uv.toml` in the working directory is read by every command. Fields that are
meaningful only for a project are rejected rather than ignored: `build-backend`
and `conflicts` are two of them. The run stops before doing any of its work —
even a `uv python find` that would otherwise print a path — with status `2` and
nothing on stdout. `--no-config` takes no value and skips configuration
discovery entirely.
