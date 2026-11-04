#!/usr/bin/env python3
"""Observe Bento's behaviour on the corpus, and compare two observations.

One code path does both jobs.  ``observe`` run against the pinned, untouched
tree produces the expected file; ``observe`` run against a candidate's
installed distribution produces the actual file; ``compare`` diffs them.  The
recorder and the scorer are therefore the same instrument, so a bug in
canonicalisation cannot make a candidate look wrong without also having made
the baseline look wrong.

Observations are a flat ``{"<case-id>::<probe>": "<canonical text>"}`` map.

Run ``observe`` with the interpreter that has the candidate's distribution
importable: the API probes import ``bento`` in process, and the CLI probe
spawns ``<that interpreter> -m bentomakerlib.bentomaker``.
"""
import argparse
import base64
import functools
import inspect
import io
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile

CASE_MARK = "<CASE>"
SITE_MARK = "<SITE>"

# Probes, in the order they run.  API probes come before the CLI probes because
# bentomaker writes a build/ cache into the working directory.
API_PROBES = ["api_package", "api_meta", "api_conv", "api_pypi", "api_static",
              "api_lex", "api_ast", "api_ipkg", "api_multipart", "api_upload",
              "api_utils", "api_node", "api_node_package", "api_node_update",
              # The build pipeline. Unreachable from the CLI on this
              # interpreter (see the section comment above `_pipeline_manifest`),
              # so reached through the functions the commands call.
              "api_egg", "api_sdist", "api_scripts", "api_wininst",
              "api_install", "api_registries", "api_subpackage",
              # The context layer sits on top of the node layer and the
              # registries, so it runs after both.
              "api_contexts"]
# Probes that need an input only one stratum carries.
STRATUM_API_PROBES = {"config": ["api_pypirc"]}
# `configure` is the deepest command that completes on the baseline interpreter:
# it walks the source tree, resolves Recurse/HookFile, and writes the build
# state. `parse` is the shallow one. Both run on every case.
CLI_PROBES = {
    "cli_parse": ["parse", "bento.info"],
    "cli_configure": ["configure"],
}
# The three extra CLI shapes are expensive (one process each), so they run on
# the strata whose documents actually reach the option-parsing layer.
CLI_EXTRA_PROBES = {
    "cli_parse_flags": ["parse", "-f", "bento.info"],
    "cli_parse_paths": ["parse", "-p", "bento.info"],
    "cli_parse_meta_name": ["parse", "-m", "name", "bento.info"],
}
CLI_EXTRA_STRATA = ("real", "interaction")

# The global command-line shapes README.rst documents -- the ones that do not
# depend on which document is in the directory: the banner, the help tree, the
# per-command option help, and the two error paths (unknown command, missing
# bento.info). They are recorded against author-owned fixture directories rather
# than against the repository root, because the repository root is the thing the
# candidate restructures.
GLOBAL_ARGV = [
    [],
    ["-h"],
    ["--help"],
    ["--version"],
    ["help"],
    ["help", "commands"],
    ["help", "globals"],
    ["help", "configure"],
    ["help", "build"],
    ["help", "install"],
    ["help", "sdist"],
    ["help", "build_egg"],
    ["help", "build_wininst"],
    ["help", "convert"],
    ["help", "no_such_command"],
    ["no_such_command"],
    ["parse"],
    ["parse", "--help"],
    ["configure", "--help"],
    ["build", "--help"],
    ["install", "--help"],
    ["sdist", "--help"],
    ["configure", "--prefix=/opt/x"],
]

# The version is metadata, not command-line behaviour, and it is a declared
# must-flip: a source checkout reports `nobuilt` because bento/__package_info.py
# is generated at install time. Pinning it here would turn every step that fixes
# the version into a false regression, so the banner and `--version` carry a
# mark and the real string is scored by the metadata checks instead.
_BANNER_VERSION_RE = re.compile(r"^(bentomaker )\S+( -- an alternative)", re.M)
_LONE_TOKEN_RE = re.compile(r"\A\S+\Z")

SUBPROCESS_ENV = {
    "PYTHONHASHSEED": "0",
    "PYTHONDONTWRITEBYTECODE": "1",
    "PYTHONWARNINGS": "ignore",
    "PYTHONIOENCODING": "utf-8",
    "LC_ALL": "C.UTF-8",
    "LANG": "C.UTF-8",
    "TZ": "UTC",
    "COLUMNS": "80",
    "TERM": "dumb",
    # Which `distutils` `import distutils` means. setuptools ships a `.pth` that
    # shadows the stdlib module with its own vendored copy, and that copy moves
    # every release: on the image's setuptools 79.0.1, `write_pkg_file` reads a
    # `license_expression` attribute its own `_METHOD_BASENAMES` never sets, so
    # `bento.conv.to_distutils_meta` -> `EggInfo.get_pkg_info` raises
    # `AttributeError` instead of emitting PKG-INFO. Steps 1, 3 and 6 all
    # re-resolve the build environment, so an expectation recorded against the
    # vendored copy would be an expectation about setuptools' version rather
    # than about the candidate's work. CPython 3.11's own distutils is frozen
    # and is what the reference interpreter provides.
    # Read by setuptools' `.pth` at interpreter start, so it has to be in the
    # environment *before* python launches -- `_require_pinned_distutils` below
    # refuses to run if a caller forgot.
    "SETUPTOOLS_USE_DISTUTILS": "stdlib",
    # The codebase's own switch for sorted, indented .ipkg output. Set on both
    # sides of the comparison, so it pins a format rather than choosing one.
    "BENTOMAKER_PRETTY": "1",
}

_ADDR_RE = re.compile(r"0x[0-9a-fA-F]{6,}")

# Traceback frames pointing into the distribution itself. The candidate is
# expected to move, split and rewrite those files, so the line number and the
# echoed source line are not behaviour -- but which function raised is. Frames
# pointing at a corpus document keep their line numbers: those are the thing
# under test.
_DIST_MARKS = ("<BENTOPKG>", "<BENTOMAKERPKG>", "<BENTOROOT>", "<SITE>",
               "<PREFIX>", "<DRIVER>")
_FRAME_RE = re.compile(r'^(\s*)File "([^"]*)", line (\d+)(, in \S+)?\s*$')


def fold_dist_frames(lines):
    out = []
    folding = False
    for line in lines:
        match = _FRAME_RE.match(line)
        if match:
            indent, path, _lineno, where = match.groups()
            if path.startswith(_DIST_MARKS):
                out.append('%sFile "%s", line <N>%s' % (indent, path, where or ""))
                folding = True
                continue
            folding = False
        elif folding:
            # The echoed source line and its caret marker belong to the frame
            # just folded away. A blank line ends the frame group.
            if line.strip():
                continue
            folding = False
        out.append(line)
    return out


# --------------------------------------------------------------------------
# canonicalisation
# --------------------------------------------------------------------------

class Canon(object):
    """Erase everything that legitimately varies between two correct trees.

    Absolute paths, the temporary case directory, interpreter and site-packages
    locations, and object addresses all differ between the recording container
    and the scoring container without any behaviour having changed.  Nothing
    else is erased: a canonicaliser that scrubs too much turns the whole
    equivalence family into a tautology.
    """

    def __init__(self, case_dir):
        self.subs = []
        for path in self._variants(case_dir):
            self.subs.append((path, CASE_MARK))
        for path in sys.path:
            if path and ("site-packages" in path or "dist-packages" in path):
                for variant in self._variants(path):
                    self.subs.append((variant, SITE_MARK))
        for attr in ("prefix", "base_prefix", "exec_prefix"):
            value = getattr(sys, attr, None)
            if value:
                for variant in self._variants(value):
                    self.subs.append((variant, "<PREFIX>"))
        for variant in self._variants(os.path.dirname(os.path.abspath(__file__))):
            self.subs.append((variant, "<DRIVER>"))
        # How the interpreter was *invoked*. `/usr/local/bin/python`,
        # `/usr/local/bin/python3` and `/usr/local/bin/python3.11` are one
        # binary reached by two symlinks, and `sys.executable` reports whichever
        # spelling started the process. `bento/commands/script_utils.py:11`
        # copies it into `SYS_EXECUTABLE`, which is what `get_script_header`
        # stamps into every generated console script, so the whole `api_scripts`
        # family moves when the recorder is started as `python3.11` and the
        # verifier as `python`. A venv adds a third spelling, and steps 4 and 6
        # replay under 3.12/3.13 from a fourth. None of that is Bento
        # behaviour -- the behaviour is the quoting and the `-x` flag, which the
        # probe also exercises with two literal paths this never touches.
        for variant in self._variants(sys.executable):
            self.subs.append((variant, "<PYEXE>"))
        # Where the distribution itself lives. At recording time that is the
        # source checkout on PYTHONPATH; at scoring time it is a venv's
        # site-packages, and after step 2 it is a src/ layout. All three have
        # to canonicalise to the same text or every observation that mentions a
        # module path would differ for reasons that are not behaviour.
        for module_name, mark in (("bento", "<BENTOPKG>"),
                                  ("bentomakerlib", "<BENTOMAKERPKG>")):
            module = sys.modules.get(module_name)
            location = getattr(module, "__file__", None)
            if not location:
                continue
            package_dir = os.path.dirname(location)
            for variant in self._variants(package_dir):
                self.subs.append((variant, mark))
            for variant in self._variants(os.path.dirname(package_dir)):
                self.subs.append((variant, "<BENTOROOT>"))
        # The running interpreter's version, wherever the code spells it out:
        # `egg_filename` and `wininst_filename` build `-py3.11` into a
        # filename, `get_script_header` writes the interpreter path into a
        # script, and extension suffixes carry `cpython-311`. All three are the
        # interpreter, not the distribution. Steps 4 and 6 gate artifacts on
        # 3.12/3.13; leaving the version literal in the
        # expectation would make the frozen family read as broken the moment
        # anything runs it elsewhere, which is a fact about the harness rather
        # than about the candidate's work.
        major, minor = sys.version_info[:2]
        self.subs.append(("python%d.%d" % (major, minor), "python<PYVER>"))
        self.subs.append(("cpython-%d%d" % (major, minor), "cpython-<PYTAG>"))
        self.subs.append(("py%d.%d" % (major, minor), "py<PYVER>"))
        # Longest first: /tmp/x/y must be replaced before /tmp/x.
        self.subs.sort(key=lambda pair: -len(pair[0]))

    @staticmethod
    def _variants(path):
        out = {os.path.abspath(path)}
        try:
            out.add(os.path.realpath(path))
        except OSError:
            pass
        return sorted(v for v in out if v and v != "/")

    def __call__(self, text):
        if isinstance(text, bytes):
            text = text.decode("utf-8", "replace")
        for needle, mark in self.subs:
            text = text.replace(needle, mark)
        text = _ADDR_RE.sub("<ADDR>", text)
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        lines = fold_dist_frames([line.rstrip() for line in text.split("\n")])
        while lines and not lines[-1]:
            lines.pop()
        return "\n".join(lines) + ("\n" if lines else "")


def render(value, depth=0):
    """Deterministic rendering of an arbitrary Bento object graph."""
    pad = "  " * depth
    if depth > 8:
        return "<depth>"
    if value is None or isinstance(value, (bool, int, float)):
        return repr(value)
    if isinstance(value, bytes):
        return repr(value)
    if isinstance(value, str):
        return repr(value)
    if isinstance(value, dict):
        if not value:
            return "{}"
        items = sorted(value.items(), key=lambda kv: repr(kv[0]))
        body = "".join("%s  %s: %s\n" % (pad, repr(k), render(v, depth + 1))
                       for k, v in items)
        return "{\n" + body + pad + "}"
    if isinstance(value, (set, frozenset)):
        return "set(" + render(sorted(value, key=repr), depth) + ")"
    if isinstance(value, (list, tuple)):
        if not value:
            return "[]"
        body = "".join("%s  %s\n" % (pad, render(v, depth + 1)) for v in value)
        return "[\n" + body + pad + "]"
    attrs = getattr(value, "__dict__", None)
    if attrs is not None:
        name = type(value).__name__
        items = sorted((k, v) for k, v in attrs.items() if not k.startswith("_"))
        if not items:
            return "%s()" % name
        body = "".join("%s  .%s = %s\n" % (pad, k, render(v, depth + 1)) for k, v in items)
        return "%s(\n%s%s)" % (name, body, pad)
    return "<%s>" % type(value).__name__


def failure(exc):
    return "EXC %s: %s\n" % (type(exc).__name__, exc)


class ProbeTimeout(Exception):
    """An in-process probe ran past its time limit.

    The CLI probes are subprocesses with a `--timeout`; the API probes run in
    this interpreter and had no bound at all, so one candidate loop that never
    terminates produced no observation file, no `reward.json`, and a run that is
    dropped from the mean rather than counted as a zero. The alarm turns that
    into an ordinary failed observation: it is raised inside the probe, caught
    by the same handler every other exception is, and recorded through
    `failure()`, so it counts as a mismatch against the freeze.
    """


def _probe_alarm(_signum, _frame):
    raise ProbeTimeout("probe exceeded its time limit")


# --------------------------------------------------------------------------
# probes
# --------------------------------------------------------------------------

def probe_api_package(_case):
    from bento.core import PackageDescription
    pkg = PackageDescription.from_file("bento.info")
    return "PackageDescription\n" + render(pkg)


def probe_api_meta(_case):
    from bento.core import PackageDescription, PackageMetadata
    pkg = PackageDescription.from_file("bento.info")
    meta = PackageMetadata.from_package(pkg)
    out = []
    for attr in sorted(meta.metadata_attributes):
        try:
            out.append("%s = %s" % (attr, render(getattr(meta, attr))))
        except Exception as exc:  # a property may legitimately raise
            out.append("%s = %s" % (attr, failure(exc).rstrip()))
    return "\n".join(out) + "\n"


def probe_api_conv(_case):
    from bento.core import PackageDescription
    from bento import conv
    pkg = PackageDescription.from_file("bento.info")
    out = ["pkg_to_distutils_meta:", render(conv.pkg_to_distutils_meta(pkg))]
    try:
        out += ["pkg_info:", render(conv.pkg_to_distutils_meta_pkg_info(pkg))]
    except Exception as exc:
        out += ["pkg_info:", failure(exc).rstrip()]
    try:
        out += ["pkg_to_distutils:", render(conv.pkg_to_distutils(pkg))]
    except Exception as exc:
        out += ["pkg_to_distutils:", failure(exc).rstrip()]

    # Package validation walks the tree, so it needs a node root. The declared
    # packages are checked against what the case actually ships.
    from bento.core.node import create_base_nodes
    here = os.getcwd()
    build_dir = os.path.join(here, "build")
    os.makedirs(build_dir, exist_ok=True)
    top, _build, _run = create_base_nodes(here, build_dir, here)
    for name in sorted(pkg.packages) + ["definitely.not.here"]:
        for helper in ("validate_package", "find_package"):
            try:
                result = getattr(conv, helper)(name, top)
                if hasattr(result, "path_from"):
                    result = result.path_from(top)
                # find_package returns the package's files in the order the
                # filesystem lists the directory, which is a property of the
                # filesystem (ext4's htree order depends on a per-filesystem
                # hash seed), not of Bento: the same pinned tree listed
                # ['hello/bar.py', 'hello/__init__.py'] where it was recorded
                # and ['hello/__init__.py', 'hello/bar.py'] on another host.
                # The set is the behaviour; the order is volatile, so it is
                # canonicalised here, which is to say at recording and at
                # replay alike.
                if helper == "find_package" and isinstance(result, list):
                    result = sorted(result)
                out.append("%s %s -> %r" % (helper, name, result))
            except Exception as exc:
                out.append("%s %s -> %s" % (helper, name, failure(exc).rstrip()))
    out.append("validate_packages -> %r"
               % (conv.validate_packages(sorted(pkg.packages), top),))
    shutil.rmtree(build_dir, ignore_errors=True)
    return "\n".join(out) + "\n"


def probe_api_pypi(_case):
    from bento.core import PackageDescription
    from bento.pypi.register_utils import build_post_data
    pkg = PackageDescription.from_file("bento.info")
    return render(build_post_data(pkg, "submit")) + "\n"


def probe_api_static(_case):
    from bento.core import PackageDescription
    from bento.core.package import static_representation
    pkg = PackageDescription.from_file("bento.info")
    text = static_representation(pkg)
    out = ["static_representation:", text]
    try:
        again = PackageDescription.from_string(text)
        same = render(again) == render(pkg)
        out.append("fixed_point: %s" % ("yes" if same else "no"))
    except Exception as exc:
        out.append("fixed_point: " + failure(exc).rstrip())
    return "\n".join(out) + "\n"


def probe_api_lex(_case):
    from bento.parser.lexer import BentoLexer
    with open("bento.info", "rb") as fh:
        raw = fh.read()
    data = raw.decode("utf-8", "surrogateescape")
    lexer = BentoLexer()
    lexer.input(data)
    out = []
    while True:
        token = lexer.token()
        if token is None:
            break
        out.append("%s %r @%s" % (token.type, token.value, token.lineno))
    return "\n".join(out) + "\n"


def probe_api_ipkg(_case):
    """The .ipkg build manifest: the format `bentomaker install` reads back.

    Exercised through the same classmethods the install command uses, over a
    manifest built from the case document, so the section bookkeeping and the
    path-variable substitution are pinned as well as the JSON shape.
    """
    from bento.core import PackageDescription
    import bento.installed_package_description as ipkg
    pkg = PackageDescription.from_file("bento.info")
    meta = ipkg.build_manifest_meta_from_pkg(pkg)
    out = ["meta:", render(meta)]

    # Sections over files the case actually ships, so resolving them against
    # the tree below has something real to find.
    shipped = sorted(_case["files"])
    python_files = [f for f in shipped if f.endswith(".py")] or shipped[:1]
    section = ipkg.InstalledSection.from_source_target_directories(
        "pythonfiles", "mainpkg", "$_srcrootdir", "$sitedir", python_files)
    data_section = ipkg.InstalledSection.from_source_target_directories(
        "datafiles", "manifest", "$_srcrootdir", "$sitedir", shipped)
    sections = {"pythonfiles": {"mainpkg": section},
                "datafiles": {"manifest": data_section}}
    manifest = ipkg.BuildManifest(sections, meta, pkg.executables)
    out += ["repr:", repr(section)]
    out += ["eq_self: %s" % (section == data_section)]

    buf = io.StringIO()
    manifest._write(buf)
    text = buf.getvalue()
    # BENTOMAKER_PRETTY is the codebase's own switch; without it the writer
    # emits one unsorted line, which is neither readable nor stable.
    out += ["written:", text]
    try:
        again = ipkg.BuildManifest.from_string(text)
        out += ["roundtrip_meta: %s" % (again.meta == manifest.meta),
                "roundtrip_executables: %s" % (
                    render(again.executables) == render(manifest.executables))]
    except Exception as exc:
        out += ["roundtrip: " + failure(exc).rstrip()]

    manifest.update_paths({"sitedir": "$prefix/site", "_srcrootdir": "$prefix/src"})
    out += ["resolve_path: " + render(manifest.resolve_path("$sitedir/$pkgname"))]

    # Resolving a manifest against a real tree is what `bentomaker install`
    # does; it is also the only path that exercises the section-to-node
    # translation and the destdir prefixing.
    from bento.core.node import create_base_nodes
    here = os.getcwd()
    build_dir = os.path.join(here, "build")
    os.makedirs(build_dir, exist_ok=True)
    top, _build, _run = create_base_nodes(here, build_dir, here)
    manifest.update_paths({"prefix": "/usr", "destdir": "/dest",
                           "sitedir": "/usr/site", "_srcrootdir": here})
    for label, call in (("resolve_paths", lambda: manifest.resolve_paths(top)),
                        ("with_destdir",
                         lambda: manifest.resolve_paths_with_destdir(top))):
        try:
            resolved = call()
            pairs = sorted("%s: %s -> %s"
                           % (kind, source and source.path_from(top),
                              target and target.abspath())
                           for kind, source, target in ipkg.iter_files(resolved))
            sources = sorted(s.abspath() for s in ipkg.iter_source_files(resolved))
            out += ["%s files:" % label, render(pairs),
                    "%s sources:" % label, render(sources)]
        except Exception as exc:
            out += ["%s: %s" % (label, failure(exc).rstrip())]
    shutil.rmtree(build_dir, ignore_errors=True)
    return "\n".join(out) + "\n"


def probe_api_multipart(_case):
    """The exact bytes bento would POST to PyPI for this document."""
    from bento.core import PackageDescription
    from bento.pypi.register_utils import build_post_data, encode_multipart
    pkg = PackageDescription.from_file("bento.info")
    fields = sorted(build_post_data(pkg, "submit").items(), key=lambda kv: kv[0])
    files = [("content", "dist.tar.gz", b"payload")]
    content_type, body = encode_multipart(fields, files)
    return "content_type: %r\nlength: %d\nbody:\n%s\n" % (
        content_type, len(body), body.decode("utf-8", "replace"))


def probe_api_ast(_case):
    """The parse tree, through the codebase's own pretty printer."""
    from bento.parser.parser import parse
    from bento.parser.nodes import ast_pprint, Node
    with open("bento.info", "rb") as fh:
        data = fh.read().decode("utf-8", "surrogateescape")
    tree = parse(data)
    out = ["root: %s / %r" % (tree, tree)]
    out.append("leaf: %r" % (Node("leaf", value="v"),))
    printed = io.StringIO()
    ast_pprint(tree, string=printed)
    out.append(printed.getvalue())
    return "\n".join(out) + "\n"


def probe_api_upload(_case):
    """The upload request bento would build for a distribution of this package.

    Nothing is sent: `build_upload_post_data` and `build_request` are the two
    pure halves, and the socket call lives in `upload` above them.
    """
    from bento.core import PackageDescription
    from bento.pypi.upload_utils import build_upload_post_data, build_request
    pkg = PackageDescription.from_file("bento.info")
    with open("fake-dist.tar.gz", "wb") as fh:
        fh.write(b"not really a tarball, but a fixed number of fixed bytes\n")
    out = []
    try:
        data = build_upload_post_data("fake-dist.tar.gz", "sdist", pkg)
        out.append("post_data: " + render(data))
        request = build_request("http://example.invalid/pypi", data, "Basic xyz")
        out.append("url: %s" % request.full_url)
        out.append("headers: " + render(dict(request.headers)))
        out.append("data length: %d" % len(request.data))
    except Exception as exc:
        out.append("upload: " + failure(exc).rstrip())
    finally:
        os.unlink("fake-dist.tar.gz")
    return "\n".join(out) + "\n"


def probe_api_pypirc(case):
    """Reading a .pypirc: both the current and the pre-2009 file layouts."""
    from bento.pypi.register_utils import parse_pypirc
    text = base64.b64decode(case["files"][".pypirc"]).decode("utf-8", "replace")
    out = []
    for repository in ("pypi", "other"):
        try:
            config = parse_pypirc(io.StringIO(text), repository)
            out.append("%s -> %s" % (repository, render(config)))
        except Exception as exc:
            out.append("%s -> %s" % (repository, failure(exc).rstrip()))
    return "\n".join(out) + "\n"


def probe_api_utils(_case):
    """`bento.utils` over values taken from the case document.

    Variable substitution, the comma-list lexer and the path helpers are what
    every `bento.info` field goes through, so they are driven with this case's
    own metadata rather than with constants.
    """
    from bento.core import PackageDescription
    from bento.utils import utils
    from bento.utils.path import find_root as _  # noqa: F401 - import is the check

    pkg = PackageDescription.from_file("bento.info")
    variables = {"pkgname": pkg.name or "", "version": pkg.version or "",
                 "prefix": "/usr", "sitedir": "$prefix/lib/site"}
    out = []

    for template in ("$pkgname-$version", "$sitedir/$pkgname", "$$literal",
                     "$missing", "no variables here", "$pkgname$version",
                     "trailing $"):
        try:
            out.append("subst_vars %r -> %r" % (template,
                                                utils.subst_vars(template, variables)))
        except Exception as exc:
            out.append("subst_vars %r -> %s" % (template, failure(exc).rstrip()))

    for field in ("name", "summary", "license", "url"):
        value = getattr(pkg, field, None)
        if not isinstance(value, str):
            continue
        for helper in ("to_camel_case", "comma_list_split", "explode_path"):
            try:
                out.append("%s %s -> %r" % (helper, field,
                                            getattr(utils, helper)(value)))
            except Exception as exc:
                out.append("%s %s -> %s" % (helper, field, failure(exc).rstrip()))

    out.append("fix_kw -> " + render(utils.fix_kw(
        {"name": pkg.name, "version": pkg.version, "unicode-key": 1})))

    names = sorted(_case["files"])
    for first in names[:2]:
        for second in names[:2]:
            try:
                out.append("same_content %s %s -> %s"
                           % (first, second, utils.same_content(first, second)))
            except Exception as exc:
                out.append("same_content %s %s -> %s"
                           % (first, second, failure(exc).rstrip()))

    printed = io.StringIO()
    try:
        utils.pprint("BLUE", "coloured line", fout=printed)
        out.append("pprint -> %r" % printed.getvalue())
    except Exception as exc:
        out.append("pprint -> " + failure(exc).rstrip())
    out.append("get_color -> %r" % (utils.get_color("BLUE"),))

    try:
        out.append("read_or_create_dict -> " + render(
            utils.read_or_create_dict("bento.info")))
    except Exception as exc:
        out.append("read_or_create_dict -> " + failure(exc).rstrip())
    return "\n".join(out) + "\n"


def probe_api_node(_case):
    """The Node filesystem model, rooted at this case directory.

    Path arithmetic is the layer that `src/` layouts and workspace members move
    around, so it is pinned here against the corpus tree -- which the candidate
    does not restructure -- rather than against the distribution.
    """
    from bento.core.node import (create_base_nodes, find_root, split_path,
                                 to_list)

    here = os.getcwd()
    # create_base_nodes resolves the build directory with find_node, which only
    # sees paths that exist.
    build_dir = os.path.join(here, "build")
    os.makedirs(build_dir, exist_ok=True)
    top, build, run = create_base_nodes(here, build_dir, here)
    out = ["to_list: %r" % (to_list("a b c"),),
           "split_path: %r" % (split_path("a/b/c"),),
           "root is top parent chain: %s" % (find_root(top) is find_root(build)),
           # Heights are reported relative to top: the absolute value counts the
           # temporary directory's own path components, which is not behaviour.
           "top str: %s" % top,
           "build srcpath: %s" % build.srcpath(),
           "build bldpath: %s" % build.bldpath(),
           "run is top: %s" % (run is top)]

    for relpath in sorted(_case["files"]):
        node = top.find_node(relpath.split("/"))
        if node is None:
            out.append("find_node %s -> None" % relpath)
            continue
        out.append("find_node %s -> %s suffix=%r depth=%d is_src=%s is_bld=%s"
                   % (relpath, node.path_from(top), node.suffix(),
                      node.height() - top.height(), node.is_src(), node.is_bld()))
        out.append("  abspath %s" % node.abspath())
        out.append("  get_bld %s" % node.get_bld().path_from(build))
        out.append("  change_ext %s" % node.change_ext(".out").path_from(top))
        out.append("  is_child_of(top) %s" % node.is_child_of(top))
        try:
            out.append("  read %r" % node.read(flags="rb")[:40])
        except Exception as exc:
            out.append("  read " + failure(exc).rstrip())

    # `find_dir` on a name that is not there is a baseline defect, not a probe
    # one: `find_node` returns None and the `os.path.isdir(node.abspath())`
    # guard below it catches only OSError, so the AttributeError escapes.
    for label, call in (("search missing", lambda: top.search(["no", "such", "path"])),
                        ("find_dir missing", lambda: top.find_dir(["no-such-dir"])),
                        ("find_dir existing", lambda: top.find_dir(["build"]).path_from(top)),
                        ("build path_from top", lambda: build.path_from(top))):
        try:
            out.append("%s: %r" % (label, call()))
        except Exception as exc:
            out.append("%s: %s" % (label, failure(exc).rstrip()))
    made = top.make_node(["scratch", "made.txt"])
    out.append("make_node: %s" % made.path_from(top))
    made.parent.mkdir()
    made.write("written\n")
    out.append("write/read: %r" % made.read())
    made.safe_write("safe\n")
    out.append("safe_write/read: %r" % made.read())
    made.chmod(0o600)
    out.append("listdir scratch: %r" % sorted(made.parent.listdir()))
    made.delete()
    out.append("after delete, search: %r" % top.search(["scratch", "made.txt"]))
    # The case directory is copied verbatim for the CLI probes; leaving scratch
    # files behind would show up in their --created-- listing.
    shutil.rmtree(os.path.join(here, "scratch"), ignore_errors=True)
    shutil.rmtree(os.path.join(here, "build"), ignore_errors=True)
    return "\n".join(out) + "\n"


# A declared name that can be turned into a real path without escaping the
# scratch root. The malformed stratum contains names that cannot: they are
# reported as skipped rather than materialised.
_SAFE_DOTTED = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$")
_SAFE_RELPATH = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.+-]*(/[A-Za-z0-9_][A-Za-z0-9_.+-]*)*$")


def _scratch_tree(pkg, name):
    """Materialise what `pkg` declares under a private scratch root.

    The corpus documents describe trees that mostly do not exist on disk, and
    the node layer only sees paths that do. Building the declared tree makes the
    resolution behaviour observable instead of collapsing every case onto the
    same "not found".  Returns (root, top, build, run, notes).
    """
    from bento.core.node import create_base_nodes

    root = os.path.join(os.getcwd(), name)
    shutil.rmtree(root, ignore_errors=True)
    build_dir = os.path.join(root, "_build")
    os.makedirs(build_dir)
    notes = []

    def touch(relpath):
        # The payload is the path, so every file has a distinct size. The
        # archive probes record sizes next to arcnames; with empty files a
        # writer that mapped a source onto the wrong archive member would
        # still produce a listing that matched.
        target = os.path.join(root, relpath)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w") as fh:
            fh.write("# %s\n" % relpath)

    def populate(section, prefix):
        for dotted in sorted(section.packages):
            if not _SAFE_DOTTED.match(dotted):
                notes.append("skipped package name %r" % (dotted,))
                continue
            parts = dotted.split(".")
            for depth in range(1, len(parts) + 1):
                touch("/".join(prefix + parts[:depth] + ["__init__.py"]))
            # A second module, so a package's node list is more than its
            # __init__ and `listdir`'s ordering is observable.
            touch("/".join(prefix + parts + ["_impl.py"]))
        # A module is resolved literally -- `find_node("%s.py" % m)` -- so `a.b`
        # names the file `a.b.py`, not `a/b.py`. The corpus carries both.
        for module in sorted(section.py_modules):
            if not _SAFE_RELPATH.match(module):
                notes.append("skipped module name %r" % (module,))
                continue
            touch("/".join(prefix + [module + ".py"]))
        sources = []
        for entity in (list(section.extensions.values())
                       + list(section.compiled_libraries.values())):
            sources.extend(entity.sources)
        for relpath in sorted(set(sources)):
            if not _SAFE_RELPATH.match(relpath):
                notes.append("skipped source %r" % (relpath,))
                continue
            touch("/".join(prefix + [relpath]))

    populate(pkg, [])
    for rdir in sorted(pkg.subpackages):
        sub = pkg.subpackages[rdir]
        if not _SAFE_RELPATH.match(sub.rdir):
            notes.append("skipped subpackage dir %r" % (sub.rdir,))
            continue
        os.makedirs(os.path.join(root, sub.rdir), exist_ok=True)
        populate(sub, sub.rdir.split("/"))

    top, build, run = create_base_nodes(root, build_dir, root)
    return root, top, build, run, notes


def probe_api_node_package(_case):
    """The node-backed view of a package's contents, section class by section class.

    `update_package` is a separate probe: it resolves globs through `ant_glob`,
    which is a defect site on this interpreter, and a crash there would hide
    everything below it.
    """
    from bento.core import PackageDescription
    from bento.core.node_package import (NodeDataFiles, NodeExtension,
                                         NodePythonPackage, NodeRepresentation,
                                         translate_name)

    pkg = PackageDescription.from_file("bento.info")
    root, top, build, run, notes = _scratch_tree(pkg, "nodepkg")
    out = list(notes)

    sub = top.make_node(["subdir"])
    sub.mkdir()
    out.append("translate_name at top   -> %r" % translate_name("pkg", top, top))
    out.append("translate_name nested   -> %r" % translate_name("pkg", sub, top))

    rep = NodeRepresentation(run, top)
    try:
        NodeRepresentation(run, sub, top)
    except Exception as exc:
        out.append("sub_directory_node not below top -> " + failure(exc).rstrip())
    out.append("sub_directory_node below top -> %s"
               % (NodeRepresentation(run, top, sub).top_or_sub_directory_node.path_from(top),))

    for dotted in sorted(pkg.packages):
        init = top.find_node(dotted.split(".") + ["__init__.py"])
        if init is None:
            out.append("package %s -> no __init__" % dotted)
            continue
        parent = init.parent
        nodes = sorted((parent.find_node(f) for f in parent.listdir()
                        if f.endswith(".py")), key=lambda n: n.abspath())
        try:
            entity = NodePythonPackage(dotted, nodes, top, top, None)
            out.append("package %s -> full_name=%s nodes=%r"
                       % (dotted, entity.full_name,
                          [n.path_from(top) for n in entity.nodes]))
            rep.register_entity("packages", entity.full_name, entity)
        except Exception as exc:
            out.append("package %s -> %s" % (dotted, failure(exc).rstrip()))

    for module in sorted(pkg.py_modules):
        node = top.find_node((module + ".py").split("/"))
        if node is None:
            out.append("module %s -> not found" % module)
            continue
        out.append("module %s -> %s" % (module, node.path_from(top)))
        rep.register_entity("modules", module, node)

    for label, sections in (("extension", pkg.extensions),
                            ("library", pkg.compiled_libraries)):
        for name in sorted(sections):
            section = sections[name]
            nodes = []
            for relpath in section.sources:
                found = top.find_node(relpath.split("/")) if "/" in relpath \
                    else top.find_node([relpath])
                if found is not None:
                    nodes.append(found)
            entity = NodeExtension(name, nodes, top, top, None, section.include_dirs)
            out.append("%s %s -> full_name=%s nodes=%r include_dirs=%r"
                       % (label, name, entity.full_name,
                          [n.path_from(top) for n in entity.nodes],
                          list(entity.include_dirs)))
            for from_node in (None, top, sub, build):
                try:
                    rebuilt = entity.extension_from(from_node)
                    out.append("  extension_from(%s) -> %s sources=%r"
                               % ("None" if from_node is None
                                  else from_node.path_from(top),
                                  rebuilt.name, list(rebuilt.sources)))
                except Exception as exc:
                    out.append("  extension_from(%s) -> %s"
                               % ("None" if from_node is None
                                  else from_node.path_from(top),
                                  failure(exc).rstrip()))
            rep.register_entity("extensions" if label == "extension"
                                else "compiled_libraries", entity.full_name, entity)

    for name in sorted(pkg.data_files):
        section = pkg.data_files[name]
        entity = NodeDataFiles(name, [], top, section.target_dir)
        out.append("datafiles %s -> target_dir=%r ref=%s"
                   % (name, entity.target_dir, entity.ref_node.path_from(top)))
        rep.register_entity("datafiles", name, entity)

    for category in ("packages", "modules", "extensions", "compiled_libraries",
                     "datafiles", "no-such-category"):
        try:
            out.append("iter_category %s -> %r"
                       % (category, sorted(k for k, _ in rep.iter_category(category))))
        except Exception as exc:
            out.append("iter_category %s -> %s" % (category, failure(exc).rstrip()))
    try:
        rep.register_entity("no-such-category", "x", None)
    except Exception as exc:
        out.append("register_entity unknown -> " + failure(exc).rstrip())

    out.append("iter_source_files -> " + render(sorted(rep.iter_source_files())))
    shutil.rmtree(root, ignore_errors=True)
    return "\n".join(out) + "\n"


def _drop_glob_sections(pkg):
    """Empty every section `update_package` would resolve through `ant_glob`.

    `ant_glob` raises `RuntimeError: generator raised StopIteration` for *every*
    pattern on this interpreter (`bento/core/node.py:392`, PEP 479), and
    `extra_source_files` defaults to `['bento.info']`, so a probe that leaves
    these in records that one defect for every document and observes nothing
    else. The defect itself is recorded by the CLI probes, which reach it
    through the real commands. What was dropped is returned, so the probes that
    call this still assert the document's glob sections rather than ignoring
    them.
    """
    notes = []
    for holder in [pkg] + [pkg.subpackages[k] for k in sorted(pkg.subpackages)]:
        where = "" if holder is pkg else " in %s" % (holder.rdir,)
        for attr in ("extensions", "compiled_libraries", "data_files",
                     "extra_source_files"):
            declared = getattr(holder, attr, None)
            if not declared:
                continue
            notes.append("glob section dropped: %s%s = %r"
                         % (attr, where, sorted(declared)))
            setattr(holder, attr, [] if isinstance(declared, list) else {})
    return notes


def probe_api_node_update(_case):
    """`NodeRepresentation.update_package` against the tree the document declares.

    This is the call every build command makes first: it turns declared names
    into nodes, including the ones a `Recurse:` subpackage contributes.

    The glob-driven sections are removed first, and listed. `ant_glob` raises
    `RuntimeError: generator raised StopIteration` for *every* pattern on this
    interpreter (`bento/core/node.py:392`, PEP 479), and `extra_source_files`
    defaults to `['bento.info']`, so leaving them in makes this probe record
    that one defect for every document and observe nothing else. The defect
    itself is recorded by the CLI probes, which reach it through the real
    commands; here it is isolated so the name resolution below is assertable.
    """
    from bento.core import PackageDescription
    from bento.core.node_package import NodeRepresentation

    pkg = PackageDescription.from_file("bento.info")
    root, top, build, run, notes = _scratch_tree(pkg, "nodeupd")
    out = list(notes)
    out.extend(_drop_glob_sections(pkg))
    rep = NodeRepresentation(run, top)
    try:
        rep.update_package(pkg)
    except Exception as exc:
        out.append("update_package -> " + failure(exc).rstrip())
        shutil.rmtree(root, ignore_errors=True)
        return "\n".join(out) + "\n"
    out.append("update_package -> ok")
    for category in ("packages", "modules", "extensions", "compiled_libraries",
                     "datafiles"):
        for name, entity in sorted(rep.iter_category(category),
                                   key=lambda kv: repr(kv[0])):
            nodes = getattr(entity, "nodes", [entity])
            out.append("%s %s -> %r"
                       % (category, name, sorted(n.path_from(top) for n in nodes)))
    out.append("iter_source_files -> " + render(sorted(rep.iter_source_files())))
    shutil.rmtree(root, ignore_errors=True)
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------
# the build pipeline, reached through its API
# --------------------------------------------------------------------------
#
# `configure` is the last command that completes on this interpreter: `build`
# and everything downstream of it -- install, sdist, build_egg, build_wininst --
# die in `ant_glob` before they do any work, on every document, so the CLI
# probes above can never reach the code those commands are made of.  That is a
# property of the pinned tree, not of the corpus, and it leaves roughly a
# thousand statements of packaging behaviour -- the egg writer, the archive
# writers, the transaction journal, the script generators, the registries --
# unobservable from the command line.
#
# Those layers are reached here instead, through the same functions the commands
# call, with the inputs built directly rather than globbed.  This is not a
# workaround for a thin corpus: it is the only way the behaviour a *packaging*
# migration must preserve can be pinned at all on the baseline.


def scratch(*names):
    """Remove a probe's scratch directories however the probe ended.

    These probes write into the case directory, and the CLI probes run
    afterwards on a copy of it: residue from a probe that raised past its own
    `rmtree` would surface in the next observation's `--created--` listing and
    make the recording depend on probe ordering.
    """
    def decorate(func):
        @functools.wraps(func)
        def wrapper(case):
            here = os.getcwd()
            try:
                return func(case)
            finally:
                for name in names:
                    shutil.rmtree(os.path.join(here, name), ignore_errors=True)
        return wrapper
    return decorate


def _pipeline_manifest(case, pkg, node, include_python=True):
    """A build manifest over files that really exist under `node`.

    The same construction `probe_api_ipkg` uses, lifted out because four probes
    below need a manifest they can resolve against a tree rather than a JSON
    shape.  `$_srcrootdir` is left symbolic: `resolve_paths` binds it to
    whatever node it is handed, which is what lets the same manifest be
    resolved against the case directory and against a build directory.

    The file list is the document's own, never a synthetic addition: 17 of the
    234 corpus documents ship a .py file of their own, so both the
    section-present and the section-absent shape occur naturally.

    `include_python=False` drops the pythonfiles section for the one caller
    that cannot survive it; see `probe_api_egg`.
    """
    import bento.installed_package_description as ipkg

    meta = ipkg.build_manifest_meta_from_pkg(pkg)
    shipped = sorted(case["files"])
    python_files = [f for f in shipped if f.endswith(".py")] if include_python else []
    sections = {}
    if python_files:
        sections["pythonfiles"] = {"mainpkg":
            ipkg.InstalledSection.from_source_target_directories(
                "pythonfiles", "mainpkg", "$_srcrootdir", "$sitedir",
                python_files)}
    if shipped:
        sections["datafiles"] = {"manifest":
            ipkg.InstalledSection.from_source_target_directories(
                "datafiles", "manifest", "$_srcrootdir", "$sitedir", shipped)}
    manifest = ipkg.BuildManifest(sections, meta, pkg.executables)
    manifest.update_paths({"prefix": node.abspath(), "eprefix": node.abspath(),
                           "sitedir": node.abspath()})
    return manifest


def _archive_listing(path):
    """Names and sizes from a zip or a tarball, never bytes or times.

    A .pyc carries the source mtime in its header and every archive member
    carries its own, so hashing an entry would record the clock rather than the
    build.  Names and uncompressed sizes are the part that is behaviour.
    """
    import tarfile
    import zipfile

    out = []
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as zid:
            for info in sorted(zid.infolist(), key=lambda i: i.filename):
                out.append("  %-58s %7d" % (info.filename, info.file_size))
    else:
        with tarfile.open(path, "r:*") as tid:
            for member in sorted(tid.getmembers(), key=lambda m: m.name):
                out.append("  %-58s %7d %s"
                           % (member.name, member.size,
                              "dir" if member.isdir() else "file"))
    return out


@scratch("_eggbuild", "_eggdist", "_eggextract")
def probe_api_egg(case):
    """The egg writer: EGG-INFO generation and the zip `build_egg` produces.

    `bentomaker build_egg` cannot run on this interpreter, so `build_egg` is
    called directly over a manifest resolved against the case directory.  The
    EGG-INFO members are recorded as text because they are metadata the
    migration must keep byte-stable; the payload members are recorded as names
    and sizes only.
    """
    from bento.core import PackageDescription
    from bento.core.node import create_base_nodes
    from bento.commands import egg_utils
    from bento.commands.build_egg import build_egg

    pkg = PackageDescription.from_file("bento.info")
    here = os.getcwd()
    build_dir = os.path.join(here, "_eggbuild")
    os.makedirs(build_dir, exist_ok=True)
    top, _build, _run = create_base_nodes(here, build_dir, here)
    out = []

    for fullname in ("demo-1.0", "%s-%s" % (pkg.name, pkg.version), "no-version"):
        # Not the running version: Canon rewrites `py3.11` to `py<PYVER>`, so
        # passing it explicitly would be indistinguishable from the default.
        for pyver in (None, "2.6", "3.9"):
            out.append("egg_filename %r %r -> %r"
                       % (fullname, pyver, egg_utils.egg_filename(fullname, pyver)))
            out.append("egg_info_dirname %r %r -> %r"
                       % (fullname, pyver,
                          egg_utils.egg_info_dirname(fullname, pyver)))

    manifest = _pipeline_manifest(case, pkg, top)
    try:
        info = egg_utils.EggInfo.from_build_manifest(manifest, top)
    except Exception as exc:
        out.append("EggInfo.from_build_manifest -> " + failure(exc).rstrip())
        shutil.rmtree(build_dir, ignore_errors=True)
        return "\n".join(out) + "\n"

    for getter in ("get_pkg_info", "get_sources", "get_install_requires",
                   "get_top_levels", "get_not_zip_safe", "get_dependency_links",
                   "get_entry_points"):
        try:
            out.append("%s ->\n%s" % (getter, getattr(info, getter)()))
        except Exception as exc:
            out.append("%s -> %s" % (getter, failure(exc).rstrip()))

    try:
        out.append("iter_meta names -> %r"
                   % ([name for name, _ in info.iter_meta(top)],))
    except Exception as exc:
        out.append("iter_meta -> " + failure(exc).rstrip())

    # The manifest carries no build_manifest node of its own here, so the
    # `self.build_manifest is None` half of get_build_manifest_info needs a
    # second instance to be reachable.
    bare = egg_utils.EggInfo(info.meta, info.executables, info.sources)
    node = top.make_node("_eggbuild/manifest.info")
    node.parent.mkdir()
    node.write("written manifest\n")
    try:
        out.append("get_build_manifest_info(bare) -> %r"
                   % (bare.get_build_manifest_info(node),))
    except Exception as exc:
        out.append("get_build_manifest_info(bare) -> " + failure(exc).rstrip())

    # Every file the manifest declares, with the kind the writer dispatches on.
    # This is the loop `build_egg` walks, observed before the writer runs so
    # that the pythonfiles entries are recorded even though the writer below
    # cannot be given them.
    try:
        out.append("iter_built_files -> " + render(sorted(
            (kind, target.path_from(top))
            for kind, _source, target
            in manifest.iter_built_files(top, {"prefix": top.abspath(),
                                               "eprefix": top.abspath(),
                                               "sitedir": top.abspath()}))))
    except Exception as exc:
        out.append("iter_built_files -> " + failure(exc).rstrip())

    # The byte-compiler `build_egg` runs over every pythonfiles entry. It lives
    # in `bento/private/`, which step 3 empties of VENDORED upstreams -- this
    # module is Bento's own and has to survive that, which is the property
    # pinned here.
    #
    # Its API shape is pinned rather than its result. `bcompile`
    # reaches `py_compile.wr_long`, a Python-2-era stdlib name that no longer
    # exists, and `build_egg` catches only PyCompileError -- so for the 17
    # documents that ship a .py file the whole egg build dies with an
    # AttributeError. That branch is 100% dead at baseline, exactly like
    # `ant_glob`, and step 6 sends a candidate hunting for exactly this class
    # of removed-stdlib call: an expectation that holds only while the bug
    # survives is one that punishes the fix. So the egg
    # below is built from a manifest with the pythonfiles section dropped, and
    # what is pinned about the byte-compiler is what a deletion would move --
    # import, origin, callability, signature -- and a repair would not.
    from bento.private import bytecode
    out.append("bytecode.PyCompileError -> %r"
               % (getattr(bytecode, "PyCompileError", None) is not None,))
    func = getattr(bytecode, "bcompile", None)
    if func is None:
        out.append("bytecode.bcompile -> absent")
    else:
        # __module__ names the interpreter-specific implementation the
        # version switch at the top of bytecode.py selected.
        out.append("bytecode.bcompile -> from=%s callable=%r signature=%s"
                   % (func.__module__, callable(func), inspect.signature(func)))

    egg_dir = os.path.join(here, "_eggdist")
    nopy = _pipeline_manifest(case, pkg, top, include_python=False)
    try:
        build_egg(nopy, top, top, output_dir=egg_dir)
        eggs = sorted(f for f in os.listdir(egg_dir) if f.endswith(".egg"))
        out.append("built eggs -> %r" % (eggs,))
        for name in eggs:
            path = os.path.join(egg_dir, name)
            out.append("egg %s:" % name)
            out.extend(_archive_listing(path))
            import zipfile
            with zipfile.ZipFile(path) as zid:
                for member in sorted(n for n in zid.namelist()
                                     if n.startswith("EGG-INFO/")):
                    text = zid.read(member).decode("utf-8", "replace")
                    out.append("  --- %s ---\n%s" % (member, text))
            # The egg has to read back into the same manifest it was built
            # from. `BuildManifest.from_egg` is the half of the round trip that
            # works, so it carries the observation.
            from bento.installed_package_description import BuildManifest
            back = BuildManifest.from_egg(path)
            out.append("from_egg meta -> %s" % render(back.meta))
            out.append("from_egg sections -> " + render(sorted(
                (category, name, section.source_dir, section.target_dir,
                 section.files)
                for category, sections in back.file_sections.items()
                for name, section in sections.items())))
            back.update_paths({"prefix": ".", "eprefix": ".", "sitedir": "."})
            out.append("from_egg resolve_path($sitedir) -> %r"
                       % (back.resolve_path("$sitedir"),))

            # `extract_egg` is the other half, and it is dead at baseline: it
            # reads `build_manifest.files`, an attribute BuildManifest has
            # never had (it is `file_sections`), so the first iteration raises.
            # Pinned as the crash it is: this is a plain misnamed
            # attribute, not a removed-stdlib call, so no step's requirements
            # reach it -- unlike `bcompile` above. Freezing it
            # stops a candidate silently rewriting the egg reader.
            extract_dir = os.path.join(here, "_eggextract")
            try:
                again = egg_utils.extract_egg(path, extract_dir)
                out.append("extract_egg meta -> %s" % render(again.meta))
                out.append("extract_egg tree -> %r" % (sorted(
                    os.path.relpath(os.path.join(root, f), extract_dir)
                    for root, _dirs, files in os.walk(extract_dir)
                    for f in files),))
            except Exception as exc:
                out.append("extract_egg -> " + failure(exc).rstrip())
            finally:
                shutil.rmtree(extract_dir, ignore_errors=True)
    except Exception as exc:
        out.append("build_egg -> " + failure(exc).rstrip())
    finally:
        shutil.rmtree(egg_dir, ignore_errors=True)
        shutil.rmtree(build_dir, ignore_errors=True)
    return "\n".join(out) + "\n"


@scratch("sdisttree")
def probe_api_sdist(_case):
    """The two archive writers `bentomaker sdist` chooses between.

    Both are driven from a NodeRepresentation over the tree the document
    declares, which is the same object the command passes them.  The glob
    sections are dropped first for the reason `probe_api_node_update` gives.
    """
    from bento.core import PackageDescription
    from bento.core.node_package import NodeRepresentation
    from bento.commands import sdist
    from bento.conv import write_pkg_info

    pkg = PackageDescription.from_file("bento.info")
    out = ["archive_basename -> %r" % (sdist.archive_basename(pkg),)]
    buf = io.StringIO()
    try:
        write_pkg_info(pkg, buf)
        out.append("write_pkg_info ->\n%s" % buf.getvalue())
    except Exception as exc:
        out.append("write_pkg_info -> " + failure(exc).rstrip())

    root, top, _build, run, notes = _scratch_tree(pkg, "sdisttree")
    out.extend(notes)
    out.extend(_drop_glob_sections(pkg))
    rep = NodeRepresentation(run, top)
    try:
        rep.update_package(pkg)
    except Exception as exc:
        out.append("update_package -> " + failure(exc).rstrip())
        shutil.rmtree(root, ignore_errors=True)
        return "\n".join(out) + "\n"

    out.append("iter_source_files -> " + render(sorted(rep.iter_source_files())))
    archive_root = "%s-%s" % (pkg.name, pkg.version)
    # Both writers hand `iter_source_files`' first element straight to
    # `tarfile.add`/`ZipFile.write`, which resolve it against the process
    # directory.  The command runs with that directory set to the run node, so
    # the probe does too; without it every add raises FileNotFoundError and the
    # two writers -- the point of this probe -- never execute a line.
    here = os.getcwd()
    os.chdir(root)
    try:
        for fmt, ext in (("gztar", ".tar.gz"), ("zip", ".zip"), ("7z", ".7z")):
            name = sdist.archive_basename(pkg) + ext
            try:
                _root, node = sdist.create_archive(name, archive_root, rep, top,
                                                   run, fmt, "_sdist")
                out.append("create_archive %s -> %s" % (fmt, node.path_from(top)))
                out.extend(_archive_listing(node.abspath()))
            except Exception as exc:
                out.append("create_archive %s -> %s" % (fmt, failure(exc).rstrip()))
    finally:
        os.chdir(here)
    shutil.rmtree(root, ignore_errors=True)
    return "\n".join(out) + "\n"


@scratch("_scriptbuild")
def probe_api_scripts(_case):
    """Console-script generation, including the Windows launcher payload.

    `create_win32_script` reads `bento/commands/cli.exe` out of the installed
    distribution.  That file is package data, not a module, so this probe fails
    for any migration that ships only what `find_packages()` finds -- which is
    the single most common way to get a PEP 621 rewrite wrong.
    """
    from bento.core import PackageDescription
    from bento.core.node import create_base_nodes
    from bento.commands import script_utils

    pkg = PackageDescription.from_file("bento.info")
    out = []
    for arg in ("plain", "with space", "with\ttab", 'quote"inside',
                "back\\slash", "trailing\\", "ends with space ",
                'mixed "a b" \\\\'):
        out.append("nt_quote_arg %r -> %r" % (arg, script_utils.nt_quote_arg(arg)))
    for executable, wininst in ((script_utils.SYS_EXECUTABLE, False),
                                (script_utils.SYS_EXECUTABLE, True),
                                ("/opt/py thon/bin/python", False),
                                ("/opt/pythonw.exe", False)):
        try:
            out.append("get_script_header %r wininst=%s -> %r"
                       % (executable, wininst,
                          script_utils.get_script_header(executable, wininst)))
        except Exception as exc:
            out.append("get_script_header %r wininst=%s -> %s"
                       % (executable, wininst, failure(exc).rstrip()))

    here = os.getcwd()
    build_dir = os.path.join(here, "_scriptbuild")
    os.makedirs(build_dir, exist_ok=True)
    top, _build, _run = create_base_nodes(here, build_dir, here)
    scripts = top.make_node("_scriptbuild/scripts")
    scripts.mkdir()
    try:
        made = script_utils.create_scripts(pkg.executables, scripts)
        for name in sorted(made):
            out.append("create_scripts %s -> %r"
                       % (name, [n.path_from(top) for n in made[name]]))
            for node in made[name]:
                out.append("  %s:\n%s" % (node.path_from(top), node.read()))
    except Exception as exc:
        out.append("create_scripts -> " + failure(exc).rstrip())

    # The win32 branch never runs through create_scripts on this platform, and
    # it is the branch that depends on the shipped launcher.
    for name in sorted(pkg.executables):
        try:
            nodes = script_utils.create_win32_script(name, pkg.executables[name],
                                                     scripts)
            out.append("create_win32_script %s -> %r"
                       % (name, [n.path_from(top) for n in nodes]))
            for node in nodes:
                raw = node.read(flags="rb")
                if node.abspath().endswith(".exe"):
                    out.append("  %s: %d bytes, MZ=%s"
                               % (node.path_from(top), len(raw), raw[:2] == b"MZ"))
                else:
                    out.append("  %s:\n%s"
                               % (node.path_from(top),
                                  raw.decode("utf-8", "replace")))
        except Exception as exc:
            out.append("create_win32_script %s -> %s" % (name, failure(exc).rstrip()))
    shutil.rmtree(build_dir, ignore_errors=True)
    return "\n".join(out) + "\n"


# `get_inidata` stamps `time.ctime(time.time())` into the installer banner. The
# clock is not behaviour, and an expectation that carries one expires the moment
# the recording is a second old, so it is folded to a mark before comparison.
_CTIME_RE = re.compile(r"\b[A-Z][a-z]{2} [A-Z][a-z]{2} [ \d]\d "
                       r"\d{2}:\d{2}:\d{2} \d{4}\b")

# The same banner line also carries `bento.__version__`, and that string is
# expected to change: a checkout reports `nobuilt` because
# `bento/__package_info.py` is generated at install time, and the step that
# gives the version one declared source is the step that makes it print the
# real one. Pinning it here would turn that step into a false regression on
# 161 inherited observations, so it is folded to the same
# `<VERSION>` mark the global CLI shapes use, and the real string is scored by
# the metadata checks instead. Only the token after `with bento-` moves; the
# rest of the generated ini, including every field around it, stays pinned.
_WININST_VERSION_RE = re.compile(r"(with bento-)\S+")


@scratch("_wininstbuild")
def probe_api_wininst(case):
    """The Windows installer metadata, and the package data it is built from.

    Two functions of this module are *not* pinned here.
    `get_exe_bytes` imports `distutils.msvccompiler` inside its body and
    `create_exe` calls it; step 6's AST-level import gate
    requires that import to go, so whatever the candidate replaces
    `get_build_version()` with necessarily changes what those two return. An
    inherited expectation over them would be a step-1 test that step 6 makes
    unpassable. `wininst_filename` and `get_inidata` import nothing and are
    pinned in full.

    What a packaging migration can really break here is the *data*: the
    launcher and the four installer stubs under `bento/commands/` are binary
    files, not modules, and a wheel built by a `find_packages()`-shaped rewrite
    ships none of them. Those are asserted directly.
    """
    from bento.core import PackageDescription
    from bento.core.node import create_base_nodes
    from bento.commands import wininst_utils
    from bento._config import WININST_DIR, _CLI

    pkg = PackageDescription.from_file("bento.info")
    here = os.getcwd()
    build_dir = os.path.join(here, "_wininstbuild")
    os.makedirs(build_dir, exist_ok=True)
    top, _build, _run = create_base_nodes(here, build_dir, here)
    manifest = _pipeline_manifest(case, pkg, top)
    out = []

    try:
        from bento.core import PackageMetadata
        meta = PackageMetadata.from_build_manifest(manifest)
        for pyver in (None, "2.6", "3.9"):
            out.append("wininst_filename %r -> %r"
                       % (pyver, wininst_utils.wininst_filename(meta, pyver)))
    except Exception as exc:
        out.append("wininst_filename -> " + failure(exc).rstrip())

    try:
        inidata = _CTIME_RE.sub("<CTIME>", wininst_utils.get_inidata(manifest))
        out.append("get_inidata ->\n%s"
                   % _WININST_VERSION_RE.sub(r"\1<VERSION>", inidata))
    except Exception as exc:
        out.append("get_inidata -> " + failure(exc).rstrip())

    for label, path in (("launcher", _CLI),):
        try:
            with open(path, "rb") as fid:
                raw = fid.read()
            out.append("%s %s -> %d bytes, MZ=%s"
                       % (label, os.path.basename(path), len(raw), raw[:2] == b"MZ"))
        except Exception as exc:
            out.append("%s %s -> %s"
                       % (label, os.path.basename(path), failure(exc).rstrip()))
    try:
        stubs = []
        for name in sorted(os.listdir(WININST_DIR)):
            full = os.path.join(WININST_DIR, name)
            with open(full, "rb") as fid:
                raw = fid.read()
            stubs.append((name, len(raw), raw[:2] == b"MZ"))
        out.append("wininst stubs -> " + render(stubs))
    except Exception as exc:
        out.append("wininst stubs -> " + failure(exc).rstrip())
    shutil.rmtree(build_dir, ignore_errors=True)
    return "\n".join(out) + "\n"


@scratch("_installroot")
def probe_api_install(case):
    """The install transaction journal, including its rollback path.

    `bentomaker install` never reaches this code on the baseline, and it is the
    only part of the tree that writes outside the build directory, so its
    behaviour is pinned here directly.
    """
    from bento.core import PackageDescription
    from bento.commands import install

    pkg = PackageDescription.from_file("bento.info")
    here = os.getcwd()
    root = os.path.join(here, "_installroot")
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    out = ["executables -> " + render(sorted(pkg.executables))]

    source = os.path.join(root, "source.txt")
    with open(source, "w") as fh:
        fh.write("payload\n")

    def listing():
        found = []
        for base, dirs, files in os.walk(root):
            dirs.sort()
            for name in sorted(dirs) + sorted(files):
                found.append(os.path.relpath(os.path.join(base, name), root))
        return sorted(found)

    journal = os.path.join(root, "transaction.log")
    try:
        trans = install.TransactionLog(journal)
        trans.makedirs(os.path.join(root, "a", "b", "c"))
        trans.copy(source, os.path.join(root, "a", "b", "c", "one.txt"), "pythonfiles")
        trans.copy(source, os.path.join(root, "a", "b", "c", "two.txt"), "executables")
        trans.close()
        out.append("after transaction -> " + render(listing()))
        with open(journal) as fh:
            out.append("journal ->\n%s" % fh.read().replace(root, "<ROOT>"))
    except Exception as exc:
        out.append("transaction -> " + failure(exc).rstrip())

    try:
        install.TransactionLog(journal)
    except Exception as exc:
        out.append("second TransactionLog on same file -> " + failure(exc).rstrip())

    try:
        install.rollback_transaction(journal)
        out.append("after rollback_transaction -> " + render(listing()))
    except Exception as exc:
        out.append("rollback_transaction -> " + failure(exc).rstrip())

    # The rollback that TransactionLog drives itself, reached through the
    # "target already exists" refusal in copy().
    journal2 = os.path.join(root, "second.log")
    existing = os.path.join(root, "d", "exists.txt")
    os.makedirs(os.path.dirname(existing))
    with open(existing, "w") as fh:
        fh.write("already here\n")
    try:
        trans = install.TransactionLog(journal2)
        trans.makedirs(os.path.join(root, "e", "f"))
        trans.copy(source, existing, "pythonfiles")
        out.append("copy over existing -> no error (unexpected)")
    except Exception as exc:
        out.append("copy over existing -> " + failure(exc).rstrip())
    out.append("after self-rollback -> " + render(listing()))

    for line in ("MKDIR %s" % os.path.join(root, "e"), "COPY /no/such/file",
                 "BOGUS thing", "malformed"):
        try:
            install._rollback_operation(line)
            out.append("_rollback_operation %r -> ok" % (line.replace(root, "<ROOT>"),))
        except Exception as exc:
            out.append("_rollback_operation %r -> %s"
                       % (line.replace(root, "<ROOT>"), failure(exc).rstrip()))

    for kind in ("pythonfiles", "executables"):
        target = os.path.join(root, "installed", kind, "file.txt")
        try:
            install.copy_installer(source, target, kind)
            out.append("copy_installer %s -> mode %o"
                       % (kind, os.stat(target).st_mode & 0o777))
        except Exception as exc:
            out.append("copy_installer %s -> %s" % (kind, failure(exc).rstrip()))

    # `unix_installer` announces each file through `bento.utils.utils.pprint`,
    # whose default sink is sys.stderr, not sys.stdout. The banner is part of
    # the behaviour, so it is captured rather than left to leak out of the
    # observation process; TERM=dumb in the runner environment turns the colour
    # escapes off, which the recorded text also pins.
    for kind in ("pythonfiles", "executables"):
        target = os.path.join(root, "unix", kind, "file.txt")
        printed = io.StringIO()
        saved = sys.stderr
        sys.stderr = printed
        try:
            install.unix_installer(source, target, kind)
            result = "mode %o" % (os.stat(target).st_mode & 0o777,)
        except Exception as exc:
            result = failure(exc).rstrip()
        finally:
            sys.stderr = saved
        out.append("unix_installer %s -> %s  printed %r"
                   % (kind, result, printed.getvalue().replace(root, "<ROOT>")))

    out.append("final tree -> " + render(listing()))
    shutil.rmtree(root, ignore_errors=True)
    return "\n".join(out) + "\n"


@scratch("_regbuild")
def probe_api_registries(case):
    """The five registries the command layer is assembled out of.

    Every one of them is populated from this document's own command and option
    names where it has them, so the ordering the registries report is pinned
    against real input rather than against a constant.
    """
    from bento.commands import registries
    from bento.core import PackageDescription
    from bento.core.node import create_base_nodes

    pkg = PackageDescription.from_file("bento.info")
    names = sorted(pkg.executables) or ["alpha", "beta"]
    out = []

    class Dummy(object):
        def __init__(self, tag):
            self.tag = tag

        def __repr__(self):
            return "Dummy(%r)" % (self.tag,)

    commands = registries.CommandRegistry()
    for index, name in enumerate(names):
        commands.register(name, Dummy(name), public=bool(index % 2 == 0))
    try:
        commands.register(names[0], Dummy("again"))
    except Exception as exc:
        out.append("duplicate register -> " + failure(exc).rstrip())
    out.append("command_names -> %r" % (sorted(commands.command_names()),))
    out.append("public_command_names -> %r" % (sorted(commands.public_command_names()),))
    out.append("is_registered known/unknown -> %s/%s"
               % (commands.is_registered(names[0]), commands.is_registered("nope")))
    out.append("retrieve -> %r" % (commands.retrieve(names[0]),))
    try:
        commands.retrieve("nope")
    except Exception as exc:
        out.append("retrieve unknown -> " + failure(exc).rstrip())

    contexts = registries.ContextRegistry(default=Dummy("default"))
    contexts.register(names[0], Dummy("ctx"))
    out.append("context retrieve known -> %r" % (contexts.retrieve(names[0]),))
    out.append("context retrieve unknown -> %r" % (contexts.retrieve("nope"),))
    out.append("context is_registered -> %s/%s"
               % (contexts.is_registered(names[0]), contexts.is_registered("nope")))
    try:
        contexts.register(names[0], Dummy("dup"))
    except Exception as exc:
        out.append("context duplicate -> " + failure(exc).rstrip())
    bare = registries.ContextRegistry()
    try:
        out.append("bare context default -> %r" % (bare.retrieve("nope"),))
    except Exception as exc:
        out.append("bare context default -> " + failure(exc).rstrip())

    options = registries.OptionsRegistry()
    options.register(names[0], Dummy("opts"))
    out.append("options retrieve known -> %r" % (options.retrieve(names[0]),))
    out.append("options is_registered -> %s/%s"
               % (options.is_registered(names[0]), options.is_registered("nope")))
    try:
        options.retrieve("nope")
    except Exception as exc:
        out.append("options retrieve unknown -> " + failure(exc).rstrip())
    try:
        options.register(names[0], Dummy("dup"))
    except Exception as exc:
        out.append("options duplicate -> " + failure(exc).rstrip())

    for klass in (registries.BuilderRegistry, registries.ISectionRegistry):
        reg = klass()
        reg.register_category("packages", lambda *a, **kw: ("default", a, kw))
        reg.register_callback("packages", names[0], lambda *a, **kw: ("named", a, kw))
        out.append("%s callback named -> %r"
                   % (klass.__name__, reg.callback("packages", names[0])(1)))
        out.append("%s callback fallback -> %r"
                   % (klass.__name__, reg.callback("packages", "other")(2)))
        out.append("%s default_callback -> %r"
                   % (klass.__name__, reg.default_callback("packages", 3)))
        for bad in ("no-such-category",):
            for label, call in (("register_callback",
                                 lambda: reg.register_callback(bad, "x", None)),
                                ("callback", lambda: reg.callback(bad, "x")),
                                ("default_callback",
                                 lambda: reg.default_callback(bad))):
                try:
                    call()
                    out.append("%s %s %s -> ok" % (klass.__name__, label, bad))
                except Exception as exc:
                    out.append("%s %s %s -> %s"
                               % (klass.__name__, label, bad, failure(exc).rstrip()))

    here = os.getcwd()
    build_dir = os.path.join(here, "_regbuild")
    os.makedirs(build_dir, exist_ok=True)
    top, build, _run = create_base_nodes(here, build_dir, here)
    outputs = registries.OutputRegistry()
    outputs.register_category("packages", "pythonfiles")
    nodes = [top.find_node(name.split("/")) for name in sorted(case["files"])]
    nodes = [n for n in nodes if n is not None]
    try:
        outputs.register_outputs("packages", names[0], nodes, top, "$sitedir")
        out.append("iter_category -> " + render(sorted(
            (name, sorted(n.path_from(top) for n in found),
             from_node.path_from(top), target)
            for name, found, from_node, target
            in outputs.iter_category("packages"))))
        out.append("iter_over_category -> " + render(sorted(
            (category, name, sorted(n.path_from(top) for n in found),
             from_node.path_from(top), target)
            for category, name, found, from_node, target
            in outputs.iter_over_category())))
    except Exception as exc:
        out.append("OutputRegistry -> " + failure(exc).rstrip())
    for category, name in (("no-such-category", "x"), ("packages", names[0])):
        try:
            outputs.register_outputs(category, name, nodes, top, "$sitedir")
            out.append("register_outputs %r %r -> ok" % (category, name))
        except Exception as exc:
            out.append("register_outputs %r %r -> %s"
                       % (category, name, failure(exc).rstrip()))
    try:
        outputs.register_category("packages", "pythonfiles")
    except Exception as exc:
        out.append("duplicate register_category -> " + failure(exc).rstrip())
    try:
        list(outputs.iter_category("no-such-category"))
    except Exception as exc:
        out.append("iter_category unknown -> " + failure(exc).rstrip())
    out.append("OutputRegistry(categories=...) -> " + render(
        sorted(registries.OutputRegistry(
            [("a", "pythonfiles"), ("b", "datafiles")]).installed_categories.items())))
    shutil.rmtree(build_dir, ignore_errors=True)
    return "\n".join(out) + "\n"


@scratch("subpkgtree")
def probe_api_subpackage(_case):
    """Subpackage flattening: the `Recurse:` layer, name by name.

    A `src/` layout is exactly a change of `rdir`, so every one of these
    translations is a thing step 2 can break silently.
    """
    from bento.core import PackageDescription
    from bento.core import subpackage

    pkg = PackageDescription.from_file("bento.info")
    root, top, _build, _run, notes = _scratch_tree(pkg, "subpkgtree")
    out = list(notes)
    out.append("subpackages -> %r" % (sorted(pkg.subpackages),))
    for rdir in sorted(pkg.subpackages):
        spkg = pkg.subpackages[rdir]
        out.append("repr %s -> %s" % (rdir, spkg))
        for helper in ("flatten_subpackage_packages",
                       "flatten_subpackage_extensions",
                       "flatten_subpackage_compiled_libraries"):
            try:
                out.append("%s %s -> %s"
                           % (helper, rdir, render(getattr(subpackage, helper)(spkg, top))))
            except Exception as exc:
                out.append("%s %s -> %s" % (helper, rdir, failure(exc).rstrip()))
    for helper in ("get_extensions", "get_compiled_libraries", "get_packages"):
        try:
            out.append("%s -> %s" % (helper, render(getattr(subpackage, helper)(pkg, top))))
        except Exception as exc:
            out.append("%s -> %s" % (helper, failure(exc).rstrip()))
    out.append("bare SubPackageDescription -> %s"
               % (subpackage.SubPackageDescription("sub"),))
    shutil.rmtree(root, ignore_errors=True)
    return "\n".join(out) + "\n"


# The build context writes console scripts into `scripts-<sys.version[:3]>`,
# which is the running interpreter and not the candidate's work. It is not the
# same spelling as any of the marks the canonicaliser already knows -- 3.11
# renders as "3.1" there, not "3.11" -- so the probe folds it itself.
_SCRIPTS_DIR_RE = re.compile(r"scripts-\d+\.\d+")


@scratch("ctxtree")
def probe_api_contexts(_case):
    """The command context layer: what a command is handed, and what it builds.

    `bentomaker build` cannot complete on this interpreter, so
    the contexts the build and sdist commands are constructed with are reached
    directly. This is the surface that turns a parsed document into the
    installed-section manifest: the output and installed-section registries, the
    recursion into subpackages, and the metadata template substitution that
    generates a package's `__package_info.py`. Step 2 moves the version into
    that generated module, so the substitution has to keep working across it.

    Both the build and the sdist context call `update_package` from their
    constructor, and that resolves globs through `ant_glob`, a PEP 479 defect
    site on this interpreter. Every construction is guarded so the crash becomes
    one line of the observation instead of the end of it.
    """
    from bento.commands.build import BuildCommand
    from bento.commands.command_contexts import (
        BuildContext, CmdContext, ConfigureContext, ContextWithBuildDirectory,
        HelpContext, SdistContext, _generic_iregistrer,
        fill_metadata_template, write_template)
    from bento.commands.configure import ConfigureCommand
    from bento.commands.contexts import GlobalContext
    from bento.commands.core import HelpCommand
    from bento.commands.options import OptionsContext
    from bento.commands.utils import has_compiled_code, has_cython_code
    from bento.core import PackageDescription
    from bento.core.meta import PackageMetadata
    from bento.core.options import PackageOptions

    pkg = PackageDescription.from_file("bento.info")
    package_options = PackageOptions.from_file("bento.info")
    out = []
    out.append("has_cython_code -> %r" % (has_cython_code(pkg),))
    out.append("has_compiled_code -> %r" % (has_compiled_code(pkg),))

    # ---- the metadata template substitution, on its own ----------------
    # Pure: content in, content out. Every metadata attribute is referenced
    # exactly once, so a field that stopped being exposed surfaces as a KeyError
    # here rather than as a missing line in a generated module nobody reads.
    meta = PackageMetadata.from_package(pkg)
    attributes = sorted(meta.metadata_attributes)
    template = "".join("%s = $%s\n" % (name, name.upper()) for name in attributes)
    values = dict((name, getattr(meta, name)) for name in attributes)
    try:
        out.append("fill_metadata_template ->\n"
                   + fill_metadata_template(template, values))
    except Exception as exc:
        out.append("fill_metadata_template -> " + failure(exc).rstrip())
    # The ways a template can be wrong, and the two forms that are not wrong.
    for label, content in (("unknown key", "x = $NOT_A_FIELD\n"),
                           ("trailing dollar", "x = $\n"),
                           ("bad identifier", "x = ${}\n"),
                           ("escaped dollar", "x = $$NAME\n"),
                           ("braced key", "x = ${NAME}\n")):
        try:
            out.append("fill_metadata_template %s -> %r"
                       % (label, fill_metadata_template(content, values)))
        except Exception as exc:
            out.append("fill_metadata_template %s -> %s"
                       % (label, failure(exc).rstrip()))
    # `_safe_repr` switches on string-ness and on line count, and repr()s
    # everything else: all three branches, over values whose types a document
    # cannot influence, so the switch is pinned even for documents with no
    # multi-line metadata of their own.
    shapes = {"one": "single line", "two": "first\nsecond", "num": 3,
              "seq": ["a", "b"], "none": None, "quote": 'has "quotes"'}
    out.append("fill_metadata_template shapes -> %r"
               % (fill_metadata_template(
                   "".join("%s=$%s;" % (k, k.upper()) for k in sorted(shapes)),
                   shapes),))

    # ---- the contexts, over the materialised tree ----------------------
    root, top, build, run, notes = _scratch_tree(pkg, "ctxtree")
    out.extend(notes)
    if os.path.isfile("bento.info"):
        shutil.copyfile("bento.info", os.path.join(root, "bento.info"))
    # `pre_recurse` resolves a subpackage by finding its bento.info under the
    # top node and looking the path up in `pkg.subpackages`; it never reads the
    # file. The scratch tree carries the declared sources but not the
    # sub-documents, so the lookup needs them put back.
    sub_dirs = []
    for key in sorted(pkg.subpackages):
        rdir = os.path.dirname(key)
        if not rdir or not _SAFE_RELPATH.match(rdir):
            out.append("skipped subpackage key %r" % (key,))
            continue
        os.makedirs(os.path.join(root, rdir), exist_ok=True)
        with open(os.path.join(root, rdir, "bento.info"), "w") as fh:
            fh.write("# %s\n" % key)
        sub_dirs.append((key, rdir))

    global_context = GlobalContext(None)
    global_context.register_package_options(package_options)
    configure_argv = ["--prefix=/opt/x"]
    global_context.save_command_argv("configure", configure_argv)
    for name, command in (("configure", ConfigureCommand()),
                          ("build", BuildCommand()),
                          ("help", HelpCommand())):
        global_context.register_command(name, command, public=(name != "help"))
        global_context.register_options_context(
            name, OptionsContext.from_command(command))
    out.append("command_names public/all -> %r / %r"
               % (sorted(global_context.command_names()),
                  sorted(global_context.command_names(public_only=False))))
    out.append("retrieve_command_argv configure/absent -> %r / %r"
               % (global_context.retrieve_command_argv("configure"),
                  global_context.retrieve_command_argv("nope")))
    try:
        out.append("retrieve_scheme -> " + render(global_context.retrieve_scheme()))
    except Exception as exc:
        out.append("retrieve_scheme -> " + failure(exc).rstrip())
    # `retrieve_configured_scheme` is given an explicit --prefix. With none,
    # `set_scheme_unix` falls through to
    # `distutils.command.install.INSTALL_SCHEMES`; step 6 removes distutils, so
    # pinning what that branch returns would freeze the shape of a stdlib module
    # the candidate is required to stop importing.
    try:
        out.append("retrieve_configured_scheme(--prefix) -> "
                   + render(global_context.retrieve_configured_scheme(configure_argv)))
    except Exception as exc:
        out.append("retrieve_configured_scheme(--prefix) -> " + failure(exc).rstrip())

    build_options = OptionsContext.from_command(BuildCommand())
    base = CmdContext(global_context, [], build_options, pkg, run)
    out.append("CmdContext nodes -> run=%r top=%r cur=%r local=%r pkg=%r"
               % (base.run_node.path_from(top), base.top_node.path_from(top),
                  base.cur_node.path_from(top), base.local_node, base.local_pkg))
    parsed, positional = base.get_parsed_arguments()
    out.append("get_parsed_arguments [] -> %r args=%r"
               % (sorted(vars(parsed).items()), positional))
    base.init()
    base.configure()
    base.finish()
    out.append("base lifecycle -> ok")

    ctx = ContextWithBuildDirectory(global_context, [], build_options, pkg, run)
    out.append("ContextWithBuildDirectory -> build_root=%r top=%r build=%r "
               "top_or_sub=%r build_or_sub=%r"
               % (ctx.build_root.path_from(top), ctx.top_node.path_from(top),
                  ctx.build_node.path_from(build),
                  ctx.top_or_sub_directory_node.path_from(top),
                  ctx.build_or_sub_directory_node.path_from(build)))
    out.append("make_build_node -> %r"
               % (ctx.make_build_node("gen/out.txt").path_from(build),))
    for label, path in (("existing", "bento.info"), ("missing", "no/such.txt")):
        try:
            out.append("make_source_node %s -> %r"
                       % (label, ctx.make_source_node(path).path_from(top)))
        except Exception as exc:
            out.append("make_source_node %s -> %s" % (label, failure(exc).rstrip()))

    # pre_recurse on the run node is the documented no-op; on a subdirectory it
    # resolves the sub-package; on a node outside the source tree it is an
    # error. The context-manager form must leave exactly the state the explicit
    # pair leaves.
    ctx.pre_recurse(run)
    out.append("pre_recurse(run) -> local=%r pkg=%r"
               % (ctx.local_node.path_from(top), ctx.local_pkg))
    ctx.post_recurse()
    out.append("post_recurse -> local=%r pkg=%r" % (ctx.local_node, ctx.local_pkg))
    for key, rdir in sub_dirs:
        node = top.find_node(rdir)
        if node is None:
            out.append("recurse %s -> no node" % key)
            continue
        try:
            with ctx.recurse_manager(node):
                out.append("recurse %s -> local=%r resolved=%r"
                           % (key, ctx.local_node.path_from(top),
                              ctx.local_pkg is not None))
            out.append("recurse %s exit -> local=%r pkg=%r"
                       % (key, ctx.local_node, ctx.local_pkg))
        except Exception as exc:
            out.append("recurse %s -> %s" % (key, failure(exc).rstrip()))
    try:
        ctx.pre_recurse(build.make_node("elsewhere"))
        out.append("pre_recurse(build node) -> no error")
    except Exception as exc:
        out.append("pre_recurse(build node) -> " + failure(exc).rstrip())
    ctx.post_recurse()

    out.append("ConfigureContext -> %r"
               % (ConfigureContext(global_context, [], build_options, pkg,
                                   run).build_root.path_from(top),))
    help_ctx = HelpContext(global_context, [], build_options, pkg, run)
    out.append("HelpContext short_descriptions -> " + render(help_ctx.short_descriptions))
    out.append("HelpContext options registered -> %r / %r"
               % (help_ctx.is_options_context_registered("build"),
                  help_ctx.is_options_context_registered("nope")))
    out.append("HelpContext retrieve_options_context -> %r"
               % (type(help_ctx.retrieve_options_context("build")).__name__,))

    # ---- _generic_iregistrer, on a real node ---------------------------
    info_node = top.find_node("bento.info")
    nodes = [n for n in (info_node,) if n is not None]
    out.append("_generic_iregistrer -> %r"
               % (_generic_iregistrer("datafiles", "probe", nodes, top, "$sitedir"),))

    # ---- write_template ------------------------------------------------
    os.makedirs(os.path.join(root, "tpl"), exist_ok=True)
    with open(os.path.join(root, "tpl", "info.py.in"), "w") as fh:
        fh.write("NAME = $NAME\nVERSION = $VERSION\nEXTRA = $EXTRA\n")
    try:
        written = write_template(top, "tpl/info.py.in", pkg, {"extra": "probe"})
        out.append("write_template -> %s\n%s"
                   % (written.path_from(top), written.read()))
    except Exception as exc:
        out.append("write_template -> " + failure(exc).rstrip())
    try:
        write_template(top, "tpl/missing.py.in", pkg)
        out.append("write_template missing -> no error")
    except Exception as exc:
        out.append("write_template missing -> " + failure(exc).rstrip())

    def summarise_outputs(registry):
        return render(sorted(
            (category, name, sorted(n.path_from(top) for n in output_nodes),
             from_node.path_from(top), target_dir)
            for category, name, output_nodes, from_node, target_dir
            in registry.iter_over_category()))

    # ---- what `update_package` cannot resolve here, and why -------------
    # Both context constructors end in `NodeRepresentation.update_package`, and
    # four of its six steps resolve declared paths through `ant_glob`:
    # extensions, compiled libraries, data files and extra sources. `ant_glob`
    # is 100% dead on this interpreter -- `_ant_iter` (bento/core/node.py:392)
    # ends in a bare `raise StopIteration`, which PEP 479 turns into a
    # RuntimeError -- and `extra_source_files` defaults to ['bento.info'] for
    # *every* document, so with these four fields in place no case reaches a
    # context at all.
    #
    # api_node_update pins that crash already. Repeating it here would buy no
    # coverage and would cost all of it: freeze.py routes an observation to the
    # defect ledger when the marker text appears anywhere in the body, so one
    # RuntimeError at the bottom would drag the metadata substitution, the
    # registries and the recursion into the ledger with it.
    #
    # So the four fields are lifted off the package, what they held is
    # recorded, and the entities they would have produced are registered back
    # through `find_node`. The corpus declares literal paths -- `_scratch_tree`
    # materialises only those -- which is the case `ant_glob` reduces to, so
    # the result is the registry `update_package` would have built, and
    # `compile`, `post_compile` and `section_writer.store` run over this
    # document's own content instead of over nothing.
    # Data files and extra sources are the two declared categories
    # `_scratch_tree` leaves off disk: it materialises what a build compiles,
    # not what it copies. Both are declared as ant patterns as often as as
    # literal paths, and a pattern is exactly what cannot be resolved here, so
    # each one is stood in for by a single concrete path it would have matched
    # -- `**/` becomes `deep/`, `*` becomes `star`, `?` becomes `q`. This does
    # not reimplement the pattern language: `ant_glob`'s own behaviour is
    # pinned, as the crash it is, by api_node. What it arranges is that a
    # section which declares content ends up holding nodes, so the builders and
    # the section writer run over something rather than over an empty list.
    def stand_in(base, relpath):
        candidate = (relpath.replace("**/", "deep/").replace("*", "star")
                     .replace("?", "q"))
        if not _SAFE_RELPATH.match(candidate):
            return None
        target = os.path.join(base, candidate)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w") as fh:
            fh.write("# %s\n" % candidate)
        return candidate

    data_specs = []
    for name in sorted(pkg.data_files):
        section = pkg.data_files[name]
        if section.source_dir in ("", "."):
            sdir, base = ".", root
        elif _SAFE_RELPATH.match(section.source_dir):
            sdir = section.source_dir
            base = os.path.join(root, section.source_dir)
        else:
            out.append("skipped data section %r source_dir %r"
                       % (name, section.source_dir))
            continue
        os.makedirs(base, exist_ok=True)
        files = []
        for relpath in section.files:
            candidate = stand_in(base, relpath)
            if candidate is None:
                out.append("skipped data file %r in section %r" % (relpath, name))
                continue
            if candidate != relpath:
                out.append("data file %r stands in for %r in section %r"
                           % (candidate, relpath, name))
            files.append(candidate)
        data_specs.append((name, sdir, files, section.target_dir))

    # `MetaTemplateFile` is resolved by `write_template`, not by a glob, so it
    # survives context construction -- but `_scratch_tree` does not materialise
    # it either, and the miss branch of `write_template` is itself broken: it
    # formats `package.meta_template_file`, an attribute spelled without the
    # plural that PackageDescription has never had, so the AttributeError
    # escapes instead of the InvalidPackage. That is pinned once above, on
    # "tpl/missing.py.in". Left unmaterialised here it would fire again from
    # inside `compile`, `post_compile`'s inplace branch and `SdistContext
    # .configure`, truncating all three for exactly the documents that declare
    # a template -- so the declared templates are put on disk and the hit
    # branch is what these three record.
    for template in pkg.meta_template_files:
        if not _SAFE_RELPATH.match(template) or not template.endswith(".in"):
            out.append("skipped meta template %r" % (template,))
            continue
        target = os.path.join(root, template)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w") as fh:
            fh.write("NAME = $NAME\nVERSION = $VERSION\nEXTRA = $EXTRA\n")
        out.append("materialised meta template %r" % (template,))

    saved_extensions = pkg.extensions
    saved_libraries = pkg.compiled_libraries
    saved_extra = []
    for relpath in pkg.extra_source_files:
        candidate = stand_in(root, relpath)
        if candidate is None:
            out.append("skipped extra source %r" % (relpath,))
            continue
        if candidate != relpath:
            out.append("extra source %r stands in for %r" % (candidate, relpath))
        saved_extra.append(candidate)
    out.append("lifted for context construction -> " + render(
        {"extensions": sorted(saved_extensions),
         "compiled_libraries": sorted(saved_libraries),
         "data_files": [spec[0] for spec in data_specs],
         "extra_source_files": list(pkg.extra_source_files)}))
    pkg.extensions = {}
    pkg.compiled_libraries = {}
    pkg.data_files = {}
    pkg.extra_source_files = []
    # `_run_in_subpackage` reads the *subpackage's* dicts, so emptying the
    # top-level ones is not enough.
    saved_sub = {}
    for key in sorted(pkg.subpackages):
        sub = pkg.subpackages[key]
        saved_sub[key] = (sub.extensions, sub.compiled_libraries)
        if sub.extensions or sub.compiled_libraries:
            out.append("lifted from subpackage %r -> %r / %r"
                       % (key, sorted(sub.extensions),
                          sorted(sub.compiled_libraries)))
        sub.extensions = {}
        sub.compiled_libraries = {}

    def restore_registry(node_pkg):
        """Register back what the four glob-resolved steps would have produced.

        Same entity classes, same constructor arguments and same registry keys
        as `update_package`; only the resolution differs, and only where a
        literal path makes the difference nil. Returns the lines describing
        what did not resolve, which is itself part of the observation.
        """
        from bento.core.node_package import (NodeDataFiles, NodeExtension,
                                             translate_name)
        report = []

        def add_entities(extensions, libraries, ref_node):
            for category, declared in (("extensions", extensions),
                                       ("compiled_libraries", libraries)):
                for key in sorted(declared):
                    entity = declared[key]
                    nodes, missing = [], []
                    for relpath in entity.sources:
                        node = ref_node.find_node(relpath)
                        if node is None:
                            missing.append(relpath)
                        else:
                            nodes.append(node)
                    if missing:
                        report.append("%s %r unresolved -> %r"
                                      % (category, key, missing))
                    if entity.include_dirs:
                        # `to_node_extension` resolves these and then drops
                        # them on the floor -- NodeExtension is constructed
                        # without them. Recorded, not reproduced.
                        report.append("%s %r include_dirs ignored -> %r"
                                      % (category, key, entity.include_dirs))
                    node_ext = NodeExtension(entity.name, nodes,
                                             node_pkg.top_node, ref_node,
                                             node_pkg.sub_directory_node)
                    if category == "extensions" and ref_node == node_pkg.top_node:
                        name = node_ext.full_name
                    else:
                        name = translate_name(key, ref_node, node_pkg.top_node)
                    node_pkg.register_entity(category, name, node_ext)

        add_entities(saved_extensions, saved_libraries, top)
        for key, rdir in sub_dirs:
            ref_node = top.find_node(rdir)
            saved = saved_sub.get(key)
            if ref_node is None or saved is None:
                continue
            add_entities(saved[0], saved[1], ref_node)

        for name, sdir, files, target_dir in data_specs:
            ref_node = top if sdir == "." else top.find_node(sdir)
            if ref_node is None:
                report.append("datafiles %r -> source_dir %r not found"
                              % (name, sdir))
                continue
            nodes = [n for n in (ref_node.find_node(f) for f in files)
                     if n is not None]
            node_pkg.register_entity("datafiles", name,
                                     NodeDataFiles(name, nodes, ref_node,
                                                   target_dir))

        for relpath in saved_extra:
            node = top.find_node(relpath)
            if node is None:
                report.append("extra source %r unresolved" % (relpath,))
            else:
                node_pkg._extra_source_nodes.append(node)
        return report

    def summarise_registry(node_pkg):
        # `iter_category` over every category the representation carries: what
        # the build is about to walk, spelled as paths under the source tree.
        return render(sorted(
            (category, name,
             sorted(n.path_from(top) for n in
                    ([entity] if category == "modules" else entity.nodes)))
            for category in ("modules", "packages", "extensions",
                             "compiled_libraries", "datafiles")
            for name, entity in node_pkg.iter_category(category)))

    # ---- the build context ---------------------------------------------
    try:
        bld = BuildContext(global_context, [], build_options, pkg, run)
    except Exception as exc:
        bld = None
        out.append("BuildContext -> " + failure(exc).rstrip())
    if bld is not None:
        out.append("BuildContext reconstruction -> "
                   + render(restore_registry(bld._node_pkg)))
        out.append("BuildContext registry -> " + summarise_registry(bld._node_pkg))
        out.append("BuildContext source nodes -> %r"
                   % (sorted(n.path_from(top)
                             for n in bld._node_pkg.iter_source_nodes()),))
        out.append("BuildContext inplace -> %r" % (bld.inplace,))
        out.append("BuildContext output categories -> %r"
                   % (sorted(bld.outputs_registry.categories),))
        out.append("BuildContext installed categories -> %r"
                   % (sorted(bld.outputs_registry.installed_categories.items()),))
        out.append("BuildContext builder categories -> %r"
                   % (sorted(bld.builder_registry._callbacks),))
        out.append("BuildContext isection categories -> %r"
                   % (sorted(bld.isection_registry._callbacks),))
        bld.register_metadata("extra", "probe")
        bld.register_category("manuals", "datafiles")
        out.append("after register_category -> %r"
                   % (sorted(bld.outputs_registry.categories),))
        bld.register_outputs_simple(nodes, top, "$datadir")
        bld.register_outputs("manuals", "man1", nodes, top, "$mandir")
        for label, call in (
                ("duplicate category", lambda: bld.register_category("manuals")),
                ("unknown category", lambda: bld.register_outputs(
                    "nope", "x", nodes, top, "$sitedir")),
                ("duplicate outputs", lambda: bld.register_outputs(
                    "manuals", "man1", nodes, top, "$mandir")),
                # `extensions` and `compiled_libraries` are output categories
                # but not *builder* categories: the build command registers
                # those from the backend it selects, so on a bare context every
                # extension hook is a rejection.
                ("register_builder", lambda: bld.register_builder("ext", None)),
                ("tweak_extension", lambda: bld.tweak_extension("ext", debug=True)),
                ("tweak_library", lambda: bld.tweak_library("lib", debug=True)),
                ("disable_extension", lambda: bld.disable_extension("ext")),
                ("default_builder", lambda: bld.default_builder(None)),
                ("default_library_builder", lambda: bld.default_library_builder(None)),
                ("compiled_library_builder",
                 lambda: bld.register_compiled_library_builder("lib", None)),
                ("_compute_extension_name", lambda: bld._compute_extension_name("ext"))):
            try:
                out.append("BuildContext %s -> %r" % (label, call()))
            except Exception as exc:
                out.append("BuildContext %s -> %s" % (label, failure(exc).rstrip()))
        bld.pre_recurse(run)
        out.append("_compute_extension_name at top -> %r"
                   % (bld._compute_extension_name("ext"),))
        for key, rdir in sub_dirs:
            node = top.find_node(rdir)
            if node is None:
                continue
            bld.post_recurse()
            try:
                bld.pre_recurse(node)
                out.append("_compute_extension_name in %s -> %r"
                           % (rdir, bld._compute_extension_name("ext")))
            except Exception as exc:
                out.append("_compute_extension_name in %s -> %s"
                           % (rdir, failure(exc).rstrip()))
        bld.post_recurse()
        bld.pre_recurse(run)
        try:
            bld.compile()
            out.append("compile -> ok")
        except Exception as exc:
            out.append("compile -> " + failure(exc).rstrip())
        out.append("outputs after compile -> "
                   + _SCRIPTS_DIR_RE.sub("scripts-<PYSHORT>",
                                         summarise_outputs(bld.outputs_registry)))
        try:
            bld.post_compile()
            out.append("post_compile sections -> " + _SCRIPTS_DIR_RE.sub(
                "scripts-<PYSHORT>", render(sorted(
                    (category, name, section.source_dir, section.target_dir,
                     section.files)
                    for category, sections in bld.section_writer.sections.items()
                    for name, section in sections.items()))))
        except Exception as exc:
            out.append("post_compile -> " + failure(exc).rstrip())
        # `store` turns the sections into the build manifest the install and
        # egg commands read back, so this is the same object the api_egg and
        # api_install probes consume -- reached the way a build reaches it.
        manifest_path = os.path.join(root, "_build", "manifest", "build_manifest.info")
        try:
            bld.section_writer.store(manifest_path, pkg)
            with open(manifest_path) as fh:
                out.append("section_writer.store ->\n"
                           + _SCRIPTS_DIR_RE.sub("scripts-<PYSHORT>", fh.read()))
        except Exception as exc:
            out.append("section_writer.store -> " + failure(exc).rstrip())
        bld.post_recurse()

    # ---- the same thing with --inplace ---------------------------------
    # `post_compile`'s inplace branch rewrites the scheme to the run node and
    # copies every built node into the source tree. The tree listing after is
    # the observation: it is what `bentomaker build -i` leaves behind.
    # `post_compile`'s inplace branch copies with `copy_installer(node
    # .path_from(self.run_node), ...)` -- a path relative to the run node, used
    # as a path relative to the process's cwd. They are the same directory only
    # when the command is run from the source tree, which is how `bentomaker`
    # runs it and is not where this probe starts.
    cwd = os.getcwd()
    try:
        os.chdir(root)
        inplace = BuildContext(global_context, ["--inplace"], build_options, pkg, run)
        restore_registry(inplace._node_pkg)
        # The template substitution reads `$EXTRA` out of the context's own
        # metadata, so this context needs the same registration the first one
        # got or `compile` raises a KeyError on the declared template.
        inplace.register_metadata("extra", "probe")
        inplace.pre_recurse(run)
        inplace.compile()
        inplace.post_compile()
        inplace.post_recurse()
        out.append("inplace -> %r tree %r"
                   % (inplace.inplace,
                      [_SCRIPTS_DIR_RE.sub("scripts-<PYSHORT>", p)
                       for p in tree_listing(root)]))
    except Exception as exc:
        out.append("inplace -> " + failure(exc).rstrip())
    finally:
        os.chdir(cwd)

    # ---- the sdist context ---------------------------------------------
    try:
        sdist_ctx = SdistContext(global_context, [], build_options, pkg, run)
    except Exception as exc:
        sdist_ctx = None
        out.append("SdistContext -> " + failure(exc).rstrip())
    if sdist_ctx is not None:
        out.append("SdistContext reconstruction -> "
                   + render(restore_registry(sdist_ctx._node_pkg)))
        sdist_ctx.register_metadata("extra", "probe")
        if info_node is not None:
            sdist_ctx.register_source_node(info_node, "renamed.info")
        out.append("SdistContext extra -> %r aliased -> %r"
                   % ([n.path_from(top)
                       for n in sdist_ctx._node_pkg._extra_source_nodes],
                      sorted((n.path_from(top), alias) for n, alias
                             in sdist_ctx._node_pkg._aliased_source_nodes.items())))
        try:
            sdist_ctx.configure()
            out.append("SdistContext configure -> ok, extra %r"
                       % ([n.path_from(top)
                           for n in sdist_ctx._node_pkg._extra_source_nodes],))
        except Exception as exc:
            out.append("SdistContext configure -> " + failure(exc).rstrip())

    return "\n".join(out) + "\n"

API_TABLE = {
    "api_package": probe_api_package,
    "api_meta": probe_api_meta,
    "api_conv": probe_api_conv,
    "api_pypi": probe_api_pypi,
    "api_static": probe_api_static,
    "api_lex": probe_api_lex,
    "api_ipkg": probe_api_ipkg,
    "api_multipart": probe_api_multipart,
    "api_pypirc": probe_api_pypirc,
    "api_ast": probe_api_ast,
    "api_upload": probe_api_upload,
    "api_utils": probe_api_utils,
    "api_node": probe_api_node,
    "api_node_package": probe_api_node_package,
    "api_node_update": probe_api_node_update,
    "api_egg": probe_api_egg,
    "api_sdist": probe_api_sdist,
    "api_scripts": probe_api_scripts,
    "api_wininst": probe_api_wininst,
    "api_install": probe_api_install,
    "api_registries": probe_api_registries,
    "api_subpackage": probe_api_subpackage,
    "api_contexts": probe_api_contexts,
}


def run_cli(case_dir, argv, timeout):
    env = dict(os.environ)
    env.update(SUBPROCESS_ENV)
    env.pop("SOURCE_DATE_EPOCH", None)
    command = [sys.executable, "-m", "bentomakerlib.bentomaker"] + list(argv)
    try:
        proc = subprocess.run(command, cwd=case_dir, env=env, timeout=timeout,
                              stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        rc, out, err = proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired:
        rc, out, err = "TIMEOUT", b"", b""
    except Exception as exc:  # the interpreter could not even start it
        rc, out, err = "SPAWNFAIL", b"", failure(exc).encode()
    return rc, out, err


def tree_listing(root):
    """Relative paths of every file under root, sorted."""
    found = []
    for base, dirs, names in os.walk(root):
        dirs.sort()
        for name in sorted(names):
            found.append(os.path.relpath(os.path.join(base, name), root))
    return sorted(found)


# --------------------------------------------------------------------------
# driver
# --------------------------------------------------------------------------

def materialize(case, parent):
    case_dir = os.path.join(parent, case["id"])
    for relpath, encoded in sorted(case["files"].items()):
        target = os.path.join(case_dir, relpath)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as fh:
            fh.write(base64.b64decode(encoded))
    os.makedirs(case_dir, exist_ok=True)
    return case_dir


def observe(corpus_path, only=None, timeout=30.0, progress=False, keys=None,
            api_timeout=120.0):
    with open(corpus_path) as fh:
        corpus = json.load(fh)
    # The in-process probes read the same switches the subprocesses get, so an
    # API observation and a CLI observation describe the same configuration.
    os.environ.update(SUBPROCESS_ENV)
    os.environ.pop("SOURCE_DATE_EPOCH", None)
    # Import once up front so Canon can find where the distribution lives. A
    # failure here is itself an observation: every probe will record it.
    for module_name in ("bento", "bentomakerlib"):
        try:
            __import__(module_name)
        except Exception as exc:
            print("warning: cannot import %s: %s" % (module_name, exc), file=sys.stderr)
    # Warm up the ply parse table. bento regenerates and rewrites
    # `<package>/parsetab` the first time anything is parsed
    # (bento/_config.py:_PICKLED_PARSETAB), so without this the first case
    # observes the generation and every later case observes the cached read.
    # Both record and replay do it, so both see only the cached read.
    try:
        from bento.parser.parser import parse as _warm_parse
        _warm_parse("Name: warmup\n")
    except Exception as exc:
        print("warning: parser warm-up failed: %s" % exc, file=sys.stderr)

    observations = {}
    parent = tempfile.mkdtemp(prefix="bento-corpus-")
    try:
        for index, case in enumerate(corpus["cases"]):
            if only and case["stratum"] not in only:
                continue
            case_dir = materialize(case, parent)
            canon = Canon(case_dir)
            # `parent`, not the real cwd: the scorer runs unprivileged and its
            # starting directory is frequently one it cannot chdir back into.
            cwd = parent
            for probe in API_PROBES + STRATUM_API_PROBES.get(case["stratum"], []):
                key = "%s::%s" % (case["id"], probe)
                if keys is not None and key not in keys:
                    continue
                os.chdir(case_dir)
                # Generously bounded rather than unbounded: 120s is four times
                # the CLI probes' limit, so nothing the pinned tree does can
                # reach it, and a candidate that loops forever still produces a
                # complete observation file instead of no `reward.json` at all.
                previous_handler = signal.signal(signal.SIGALRM, _probe_alarm)
                signal.setitimer(signal.ITIMER_REAL, api_timeout)
                try:
                    text = API_TABLE[probe](case)
                except (KeyboardInterrupt, SystemExit):
                    raise
                except BaseException as exc:  # noqa: BLE001 - recording, not raising
                    # The class and message only: a traceback would pin the
                    # expectation to line numbers inside the tree the candidate
                    # is allowed to restructure.
                    text = failure(exc)
                finally:
                    signal.setitimer(signal.ITIMER_REAL, 0)
                    signal.signal(signal.SIGALRM, previous_handler)
                    os.chdir(cwd)
                observations[key] = canon(text)
            probes = dict(CLI_PROBES)
            if case["stratum"] in CLI_EXTRA_STRATA:
                probes.update(CLI_EXTRA_PROBES)
            for probe in sorted(probes):
                if keys is not None and "%s::%s" % (case["id"], probe) not in keys:
                    continue
                cli_dir = case_dir + "--" + probe
                shutil.copytree(case_dir, cli_dir)
                before = set(tree_listing(cli_dir))
                rc, out, err = run_cli(cli_dir, probes[probe], timeout)
                blob = "rc=%s\n--stdout--\n%s--stderr--\n%s" % (
                    rc, out.decode("utf-8", "replace"), err.decode("utf-8", "replace"))
                # What a command writes is as much its behaviour as what it
                # prints, and `configure` prints nothing when it succeeds.
                created = [p for p in tree_listing(cli_dir) if p not in before]
                blob += "--created--\n%s" % "".join(p + "\n" for p in created)
                cli_canon = Canon(cli_dir)
                observations["%s::%s" % (case["id"], probe)] = cli_canon(canon(blob))
                shutil.rmtree(cli_dir, ignore_errors=True)
            shutil.rmtree(case_dir, ignore_errors=True)
            if progress and index % 25 == 0:
                print("  ... %d/%d" % (index, len(corpus["cases"])), file=sys.stderr)
    finally:
        shutil.rmtree(parent, ignore_errors=True)
    return observations


def observe_globals(fixtures_path, timeout=60.0):
    """Record the global CLI shapes against each author-owned fixture tree."""
    with open(fixtures_path) as fh:
        fixtures = json.load(fh)["fixtures"]
    os.environ.update(SUBPROCESS_ENV)
    os.environ.pop("SOURCE_DATE_EPOCH", None)
    observations = {}
    parent = tempfile.mkdtemp(prefix="bento-globals-")
    try:
        for name in sorted(fixtures):
            seed = os.path.join(parent, name)
            os.makedirs(seed, exist_ok=True)
            for relpath, encoded in sorted(fixtures[name].items()):
                target = os.path.join(seed, relpath)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, "wb") as fh:
                    fh.write(base64.b64decode(encoded))
            for argv in GLOBAL_ARGV:
                label = " ".join(argv) if argv else "<noargs>"
                run_dir = os.path.join(parent, "%s--%s" % (name, label.replace(" ", "_")
                                                           .replace("/", "_")))
                shutil.copytree(seed, run_dir)
                before = set(tree_listing(run_dir))
                rc, out, err = run_cli(run_dir, argv, timeout)
                stdout = out.decode("utf-8", "replace")
                stderr = err.decode("utf-8", "replace")
                if "--version" in argv:
                    stdout = _LONE_TOKEN_RE.sub("<VERSION>", stdout.strip()) + "\n"
                stdout = _BANNER_VERSION_RE.sub(r"\1<VERSION>\2", stdout)
                stderr = _BANNER_VERSION_RE.sub(r"\1<VERSION>\2", stderr)
                blob = "rc=%s\n--stdout--\n%s--stderr--\n%s" % (rc, stdout, stderr)
                created = [p for p in tree_listing(run_dir) if p not in before]
                blob += "--created--\n%s" % "".join(p + "\n" for p in created)
                observations["%s::%s" % (name, label)] = Canon(run_dir)(blob)
                shutil.rmtree(run_dir, ignore_errors=True)
            shutil.rmtree(seed, ignore_errors=True)
    finally:
        shutil.rmtree(parent, ignore_errors=True)
    return observations


def compare(expected, actual):
    keys = sorted(expected)
    results = []
    for key in keys:
        want = expected[key]
        got = actual.get(key)
        results.append({"key": key, "ok": got == want,
                        "missing": got is None})
    passed = sum(1 for r in results if r["ok"])
    return {
        "total": len(keys),
        "passed": passed,
        "failed": len(keys) - passed,
        "score": (float(passed) / len(keys)) if keys else 0.0,
        "results": results,
    }


def _require_pinned_distutils():
    """Refuse to record or replay against an unpinned `distutils`.

    `SETUPTOOLS_USE_DISTUTILS` is consumed by setuptools' `.pth` at interpreter
    start, so `os.environ.update(SUBPROCESS_ENV)` inside `observe()` is far too
    late for the in-process probes -- it only reaches the CLI subprocesses.
    Every caller therefore has to put it in the environment itself, and the one
    that forgets would not fail: it would quietly record a different
    `to_distutils_meta` and the difference would surface as a candidate's lost
    points. Checked rather than assumed, and checked against the module that
    actually got imported rather than against the variable alone.
    """
    want = SUBPROCESS_ENV["SETUPTOOLS_USE_DISTUTILS"]
    have = os.environ.get("SETUPTOOLS_USE_DISTUTILS")
    if have != want:
        raise SystemExit(
            "SETUPTOOLS_USE_DISTUTILS=%s must be set before python starts "
            "(got %r); see SUBPROCESS_ENV." % (want, have))
    import distutils
    if "setuptools" in os.path.abspath(distutils.__file__).split(os.sep):
        raise SystemExit(
            "distutils resolved to setuptools' vendored copy (%s) despite "
            "SETUPTOOLS_USE_DISTUTILS=%s" % (distutils.__file__, want))


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_obs = sub.add_parser("observe")
    p_obs.add_argument("--corpus", required=True)
    p_obs.add_argument("--out", required=True)
    p_obs.add_argument("--strata", default="")
    p_obs.add_argument("--timeout", type=float, default=30.0)
    # The in-process half of --timeout. Generous on purpose: it exists to keep a
    # hang from costing the whole run, not to measure speed.
    p_obs.add_argument("--api-timeout", type=float, default=120.0)
    p_obs.add_argument("--progress", action="store_true")
    # Restrict the run to the keys of an existing observation file. Used to
    # measure coverage over exactly the frozen set, so no line can be credited
    # to an observation that the freeze dropped.
    p_obs.add_argument("--keys", default="")

    p_glb = sub.add_parser("globals")
    p_glb.add_argument("--fixtures", required=True)
    p_glb.add_argument("--out", required=True)
    p_glb.add_argument("--timeout", type=float, default=60.0)

    p_cmp = sub.add_parser("compare")
    p_cmp.add_argument("--expected", required=True)
    p_cmp.add_argument("--actual", required=True)
    p_cmp.add_argument("--out", required=True)
    p_cmp.add_argument("--diff-limit", type=int, default=12)

    args = parser.parse_args()
    # Resolved now, because observe() spends its life inside temporary
    # directories and a relative --out would land in one of them.
    for attr in ("corpus", "out", "expected", "actual", "keys", "fixtures"):
        value = getattr(args, attr, None)
        if value:
            setattr(args, attr, os.path.abspath(value))

    if args.cmd in ("observe", "globals"):
        _require_pinned_distutils()

    if args.cmd == "observe":
        only = set(s for s in args.strata.split(",") if s) or None
        keys = None
        if args.keys:
            with open(args.keys) as fh:
                keys = set(json.load(fh))
        observations = observe(args.corpus, only, args.timeout, args.progress, keys,
                               args.api_timeout)
        with open(args.out, "w") as fh:
            json.dump(observations, fh, indent=1, sort_keys=True)
            fh.write("\n")
        print("observations: %d" % len(observations))
        return 0

    if args.cmd == "globals":
        observations = observe_globals(args.fixtures, args.timeout)
        with open(args.out, "w") as fh:
            json.dump(observations, fh, indent=1, sort_keys=True)
            fh.write("\n")
        print("global observations: %d" % len(observations))
        return 0

    with open(args.expected) as fh:
        expected = json.load(fh)
    with open(args.actual) as fh:
        actual = json.load(fh)
    report = compare(expected, actual)
    with open(args.out, "w") as fh:
        json.dump(report, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print("equivalence: %d/%d (%.4f)" % (report["passed"], report["total"], report["score"]))
    shown = 0
    for result in report["results"]:
        if result["ok"] or shown >= args.diff_limit:
            continue
        shown += 1
        key = result["key"]
        print("  MISMATCH %s%s" % (key, " (missing)" if result["missing"] else ""))
        if not result["missing"]:
            want = expected[key].split("\n")
            got = actual[key].split("\n")
            import difflib
            for line in list(difflib.unified_diff(want, got, "expected", "actual",
                                                  lineterm="", n=1))[:14]:
                print("    " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
