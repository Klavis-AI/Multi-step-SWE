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
- `python_implementation` is available as an environment marker, carrying for
  the target interpreter the value `/app/SPECS/specs/pep-0508.rst` gives
  `platform_python_implementation` — for a CPython environment, `CPython`. The
  shorter spelling is pip's own legacy alias rather than a name that
  specification defines; both are accepted here. A requirement guarded by
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
