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
the following argument or after an `=` in the same argument.

### global options, and what a status means

- `--version` is accepted with no subcommand. It writes a version line and
  exits `0`. Neither the wording nor the shape of that line is fixed here.
- `-q`/`--quiet` and `-v`/`--verbose` are accepted on every subcommand and
  conflict with one another. Both default to off. Set, `--quiet` suppresses the
  progress and summary a run writes to stderr — a run that succeeds writes
  nothing there — and changes neither what the run does nor what it writes to
  stdout; the diagnostic of a run that fails is not suppressed. Given together
  on one command line the pair is rejected: the request is not carried out and
  the environment is left exactly as it was.
- `-n`/`--no-cache` is accepted on every subcommand. Set, the run neither reads
  from nor writes to a persistent cache; unset, which is the default, a
  persistent cache is used. Every behaviour in this section holds identically
  in both states.
- `--cache-dir` takes a path and names the persistent cache. `UV_CACHE_DIR`
  carries the same value. The flag wins over the variable; `--no-cache` wins
  over both.
- A run that uses a persistent cache creates the directory it uses if it is not
  there already: afterwards the path that won — the flag's, or the variable's
  when no flag was given — exists, and a path that lost is not created. With
  `--no-cache` no cache directory is created at all, so a path named by
  `--cache-dir` or `UV_CACHE_DIR` does not exist after the run.
- Exit `1` is the status for a request that was understood and could not be
  carried out, wherever a clause below fixes that failure's status: a
  requirement set with no solution in the shapes § failing to resolve
  enumerates, a package the index does not serve, a candidate version with no
  installable artefact. Two kinds of failure have no status fixed here — a
  command line rejected before any request is carried out, `-q` and `-v`
  together for instance, and a failure no clause below gives a status for. For
  those, what is fixed is what § Conventions fixes, that the status is not `0`,
  together with the rule that the environment is left exactly as it was. `2` is
  not otherwise a documented outcome.

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
- Byte compilation is opt-in, and no option in this section asks for it, so
  nothing here creates a `__pycache__` directory or a `.pyc` file beside an
  installed module. The files directly inside an installed package's own
  directory under `site-packages` are exactly the ones its wheel — or, for an
  editable install, the backend's editable hook — put there.

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
- The choice is a search rather than a single pass: where the highest
  satisfying candidate's own requirements admit no solution, the next-highest
  is tried, and the run fails only when no assignment of versions satisfies
  every requirement at once.
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
- A project page is served in two forms: the HTML listing `pep-0503.rst` fixes
  and the JSON listing `/app/SPECS/specs/pep-0691.rst` fixes. A client that
  asks for the JSON form — by sending `application/vnd.pypi.simple.v1+json`
  among the media types of its `Accept` header — is served that form wherever
  the index offers it, and takes the candidates from it. An index that offers a
  project in the JSON form alone is therefore still readable, and resolves to
  what the HTML form would have resolved to.
- A file's entry on a project page, in either form, may advertise a detached
  copy of that distribution's core metadata, served beside the distribution as
  `<file>.metadata`. `/app/SPECS/specs/pep-0658.rst` fixes the advertisement
  and the hash that comes with it: on an HTML page the attribute is
  `data-dist-info-metadata`, and a JSON listing carries the same value under
  the matching key. Where a candidate's metadata is advertised that way, that
  copy is what
  the resolution reads — the requirements it declares are the ones followed,
  and the distribution itself need not be fetched to discover them.
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
  offered, the run fails without attempting to build from source, and installs
  nothing.
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

- No package is installed when the requirements admit no solution, and the
  environment is left exactly as it was. Partial installation is never an
  outcome: after a failed `uv pip install` the environment holds what it held
  before, and if it was empty it is still empty.
- Status `1`, and the cases are not distinguished from one another by it:
  - two requirements the command line names pin one shared dependency,
    directly or through their own dependencies, to different exact versions;
  - a requirement names a range the index offers no version within;
  - a requirement names a package the index does not serve at all.
- An unsatisfiable input of another shape — one requirement whose own
  dependency list pins a single dependency at two different exact versions is
  one — fails too, with nothing installed, and its status is not fixed here.
- Diagnostics go to stderr. Their wording is not fixed at this step.
