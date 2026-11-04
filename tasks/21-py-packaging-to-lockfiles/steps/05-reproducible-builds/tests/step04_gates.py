#!/usr/bin/env python3
"""Step 04 gates: wheels that are honest about who can install them.

Emits one tab-separated `family<TAB>label<TAB>ok<TAB>detail` line per check,
the same contract the earlier steps' gate scripts have with `test.sh`. Every
row here is recorded against family `04`; the earlier steps' rows are re-run
from their own gate scripts and keep families `01`, `02` and `03`.

Nothing here builds anything and nothing here executes candidate code. The
wheels have already been built by `test.sh`, unprivileged, and are read as
archives; the extension's C is read as text with step 02's `strip_c_noise` in
front of it, and `gil.declares` reads the branches the free-threaded build
compiles (step 02's `active_c_text`; the one subprocess asks the image's own
`python3.13t`, isolated and without `site`, for its `sys.hexversion`). The
three rows that need an interpreter to answer -- `abi3.serves`, `gil.stays-off`
and `ft.imports` -- are driven from `test.sh`, which installs the wheels into
throwaway environments and runs them.

Four groups, weighed by `test.sh` on the label prefix:

    abi3.   3 rows   the wheel built on the reference interpreter is tagged for
                     the stable ABI, that tag is true of the three interpreters
                     it claims -- one wheel carrying one built object, not one
                     per interpreter -- and the floor the distribution declares
                     is the same number as the floor the tag implies
    gil.    2 rows   the extension's own source declares that it does not need
                     the lock -- by either of the two spellings the C API has
                     for saying so -- and a free-threaded interpreter that
                     imports it stays free-threaded
    ft.     2 rows   the build on the free-threaded interpreter produces a wheel
                     tagged for it, and that wheel installs and runs there
    pure.   2 rows   the two distributions with no compiled code in them still
                     say so

Six of the nine are emitted here; the other three are recorded by `test.sh`
after it has run something.
"""
import os
import re
import sys

sys.path.insert(0, "/tests/lib")
import gatelib  # noqa: E402

# Step 02's, imported rather than copied: `locate_hello` is the definition of
# "the hello distribution" that step 02 scores against, and `strip_c_noise` is
# the reading of C that `02.hello.c-ported` already uses. A second copy here
# would let this step disagree with the step whose tree it inherits.
sys.path.insert(0, "/tests")
import step02_gates  # noqa: E402
import step03_gates  # noqa: E402

locate_hello = step02_gates.locate_hello
strip_c_noise = step02_gates.strip_c_noise

# The three distributions this step builds, each named where it was already
# named: `hello` by the pinned example's own `bento.info`, read by step 02's
# `hello_identity`; the root distribution by the recorded metadata every step
# checks its name against; and `yaku` by the constant step 03's own gates use.
# `test.sh` needs the same three names to drive `uv build --package`, and gets
# them from this file's stderr rather than writing a second copy of them.

ROWS = []


def row(label, ok, detail=""):
    ROWS.append(("04", label, "1" if ok else "0",
                 str(detail).replace("\t", " ")[:300]))


def flush():
    for family, label, ok, detail in ROWS:
        sys.stdout.write("\t".join((family, label, ok, detail)) + "\n")


# --------------------------------------------------------------------------
# finding a wheel and reading what it claims
# --------------------------------------------------------------------------

def only_wheel(dirpath, name):
    """The one wheel for `name` in `dirpath`, or None and why not.

    Each build in `test.sh` gets an output directory of its own, so "the wheel
    the 3.11 build produced" is a question about a directory and not about a
    filename pattern. More than one is a fault rather than a choice: a
    directory holding two wheels for the same distribution means the build
    produced something this row cannot name, and picking the newest would hide
    it.
    """
    if not os.path.isdir(dirpath):
        return None, "%s was not built" % os.path.basename(dirpath)
    canonical = step02_gates.canonical(name)
    found = []
    for entry in sorted(os.listdir(dirpath)):
        if not entry.endswith(".whl"):
            continue
        parts = entry[:-4].split("-")
        if parts and step02_gates.canonical(parts[0]) == canonical:
            found.append(entry)
    if not found:
        others = [e for e in sorted(os.listdir(dirpath))] or ["nothing"]
        return None, "no %s wheel; the directory holds %s" \
            % (name, ", ".join(others)[:120])
    if len(found) > 1:
        return None, "%d wheels for %s: %s" % (len(found), name,
                                               ", ".join(found)[:160])
    return os.path.join(dirpath, found[0]), found[0]


def wheel_file_tags(path):
    """The tag triples the WHEEL metadata file declares."""
    _metadata, wheel, _names = gatelib.wheel_metadata(path)
    out = []
    for line in wheel.get("Tag", []):
        parts = line.split("-")
        if len(parts) == 3:
            out.append(tuple(parts))
    return out


def requires_python_floor(metadata):
    """The lowest CPython a `Requires-Python` specifier admits, as (3, N).

    Only a lower bound is read, because only a lower bound is a claim about
    what an installer may take: `>=3.8` under a `cp311-abi3` tag tells pip on
    3.8 that this wheel is for it. An upper bound narrows the claim and is
    somebody else's row.
    """
    if metadata is None:
        return None, "the wheel has no METADATA"
    spec = metadata.get("Requires-Python")
    if not spec:
        return None, "the wheel declares no Requires-Python"
    best = None
    for clause in spec.split(","):
        match = re.match(r"\s*(>=|==|~=|>)\s*(\d+)\.(\d+)", clause)
        if not match:
            continue
        floor = (int(match.group(2)), int(match.group(3)))
        if match.group(1) == ">":
            floor = (floor[0], floor[1] + 1)
        if best is None or floor > best:
            best = floor
    if best is None:
        return None, "Requires-Python %r states no lower bound" % (spec,)
    return best, spec


def cpython_tag_floor(python_tag):
    """The lowest CPython a `cpXY` interpreter tag implies, as (3, N)."""
    match = re.match(r"^cp(\d)(\d+)$", python_tag or "")
    if not match:
        return None
    return (int(match.group(1)), int(match.group(2)))


# --------------------------------------------------------------------------
# the wheel the reference interpreter builds
# --------------------------------------------------------------------------

def gate_abi3_tag(wheel, note):
    """One wheel, tagged for the stable ABI, for a platform.

    The interpreter tag is read as `cp` plus any version rather than as a
    number this file writes down: which CPython the distribution decides to
    support from is the candidate's, and the instruction says so. What is
    fixed is the shape -- a CPython interpreter tag, the `abi3` ABI tag, and a
    platform that is not `any`, since a wheel carrying a compiled object is
    not installable everywhere.

    The filename and the `WHEEL` file have to agree. They are two statements of
    the same fact and an installer reads the first; a wheel whose name says
    `abi3` over a `WHEEL` file that says otherwise was renamed rather than
    built.
    """
    if wheel is None:
        row("abi3.tag", False, note)
        return
    name_tags = gatelib.wheel_tags(wheel)
    file_tags = wheel_file_tags(wheel)
    faults = []
    if not name_tags:
        faults.append("the filename carries no tag triple")
    for python_tag, abi, platform in name_tags:
        if cpython_tag_floor(python_tag) is None:
            faults.append("interpreter tag %r is not a CPython tag"
                          % (python_tag,))
        if abi != "abi3":
            faults.append("ABI tag is %r, not abi3" % (abi,))
        if platform == "any":
            faults.append("platform tag is 'any'")
    if sorted(name_tags) != sorted(file_tags):
        faults.append("the filename says %s and WHEEL says %s"
                      % (sorted(name_tags), sorted(file_tags)))
    row("abi3.tag", not faults,
        ("%s: %s" % (note, "; ".join(faults)))[:290] if faults
        else "%s is tagged %s and WHEEL agrees"
             % (note, ", ".join("-".join(t) for t in name_tags)))


def gate_abi3_floor(wheel, note):
    """The floor the distribution declares and the floor its tag implies agree.

    Equality, because that is what was asked for: "the version floor the
    distribution declares and the floor its tag implies are the same number".
    Each direction is a different defect and neither of them is the
    requirement.

    Declared BELOW the tag -- `">=3.8"` under `cp311-abi3` -- is a lie an
    installer acts on: pip on 3.8 reads `Requires-Python`, takes the wheel, and
    the import fails on an interpreter that has no symbol table for it.

    Declared ABOVE the tag -- `">=3.11"` under `cp39-abi3` -- is a wheel built
    against an older stable ABI than the distribution admits to supporting. The
    tag offers the file to interpreters the metadata says are not served, the
    metadata withholds it from interpreters the object would run on, and
    nothing in the tree says which of the two numbers was meant. `abi3.serves`
    cannot see it, because a `cp39-abi3` object loads perfectly well on 3.11,
    3.12 and 3.13, which is why the row is read off the pair and not off a run.
    """
    if wheel is None:
        row("abi3.floor-agrees", False, note)
        return
    metadata, _wheel, _names = gatelib.wheel_metadata(wheel)
    declared, spec = requires_python_floor(metadata)
    tags = gatelib.wheel_tags(wheel)
    implied = max([cpython_tag_floor(t[0]) for t in tags
                   if cpython_tag_floor(t[0])] or [None])
    if implied is None:
        row("abi3.floor-agrees", False,
            "%s: no CPython interpreter tag to imply a floor" % note)
        return
    if declared is None:
        row("abi3.floor-agrees", False,
            "%s: tagged for %d.%d and %s" % (note, implied[0], implied[1], spec))
        return
    ok = declared == implied
    row("abi3.floor-agrees", ok,
        "requires-python %s declares %d.%d, the tag implies %d.%d"
        % (spec, declared[0], declared[1], implied[0], implied[1]))


def wheel_extension_members(wheel):
    """The members of `wheel` this interpreter would import as extensions.

    The count behind the instruction's "one built object inside it": a wheel
    tagged for the stable ABI that carries a separate compiled extension for
    each interpreter installs on all three and imports on all three without
    the stable ABI having done any of the work, and the tag, the floor and the
    greeting on each interpreter all stay true while it does. `abi3.serves`
    is the row that prices it -- `test.sh` reads this count off stderr and
    folds it into that row's faults -- rather than a fourth `abi3.` row,
    because the claim is not a second claim: it is what "one wheel serves
    three interpreters" already means.

    `importlib.machinery.EXTENSION_SUFFIXES`, through step 02's reader, for
    the same reason `02.hello.extension-member` uses it: "importable here",
    read off the running interpreter, rather than a guess about a filename.
    `.dist-info/` is skipped, because metadata is not code.
    """
    if not wheel:
        return []
    suffixes = step02_gates.extension_suffixes()
    return sorted(m for m in gatelib.wheel_names(wheel)
                  if m.endswith(suffixes) and ".dist-info/" not in m)


# --------------------------------------------------------------------------
# the wheel the free-threaded interpreter builds
# --------------------------------------------------------------------------

def free_threaded_abi(abi):
    """The CPython a free-threaded ABI tag names, as (3, N), or None.

    `cp313t` is the free-threaded build's SOABI written as an ABI tag, which is
    what makes a wheel a wheel for an interpreter with no lock. The version is
    read rather than written down: the environment provides one free-threaded
    build today and this has to keep meaning the same thing when it provides a
    later one.
    """
    match = re.match(r"^cp(\d)(\d+)t$", abi or "")
    if not match:
        return None
    return (int(match.group(1)), int(match.group(2)))


def gate_ft_tag(wheel, note):
    """A second wheel, tagged for the free-threaded interpreter that built it.

    Two things, and they are the two fields: the ABI tag is a CPython
    free-threaded ABI (`cp3NNt`), and the interpreter tag is the CPython that
    ABI belongs to. Both spellings of the interpreter tag are accepted --

        hello-1.0-cp313-cp313t-linux_x86_64.whl
        hello-1.0-cp313t-cp313t-linux_x86_64.whl

    -- because they name the same artifact: one object, built by and for the
    free-threaded CPython 3.13, and the `t` is already carried by the ABI field
    that decides it. The first is what the tools in this image write; the second
    is what a candidate who spelled the interpreter tag out by hand gets, and
    the instruction asked for a wheel "tagged for that interpreter" without
    naming either. Whether an installer then takes the file is `ft.imports`'s
    question, not this row's -- the same claim/fact split `abi3.tag` and
    `abi3.serves` are.

    Everything the narrower reading rejected is still rejected: `cp313-cp313`
    is a wheel for the GIL build, `cp313-abi3` claims a stable ABI that does
    not exist on a free-threaded CPython, `cp312-cp313t` names an interpreter
    that is not the one the ABI belongs to, a non-CPython interpreter tag is
    not a CPython one, and `any` is not a platform for a file with a compiled
    object in it.
    """
    if wheel is None:
        row("ft.tag", False, note)
        return
    tags = gatelib.wheel_tags(wheel)
    faults = []
    if not tags:
        faults.append("the filename carries no tag triple")
    for python_tag, abi, platform in tags:
        version = free_threaded_abi(abi)
        if version is None:
            faults.append("ABI tag is %r, not a CPython free-threaded ABI tag "
                          "-- this is not a wheel for a free-threaded "
                          "interpreter" % (abi,))
        elif python_tag not in ("cp%d%d" % version, "cp%d%dt" % version):
            faults.append("interpreter tag %r is not the CPython the ABI tag "
                          "%r belongs to" % (python_tag, abi))
        if platform == "any":
            faults.append("platform tag is 'any'")
    row("ft.tag", not faults,
        ("%s: %s" % (note, "; ".join(faults)))[:290] if faults
        else "%s is tagged %s"
             % (note, ", ".join("-".join(t) for t in tags)))


# --------------------------------------------------------------------------
# the distributions with nothing compiled in them
# --------------------------------------------------------------------------

def gate_pure(label, wheel, note):
    """A wheel that claims no interpreter and no platform, and carries none.

    Both halves, because each without the other is satisfiable by a mistake:
    a pure tag on a wheel holding a `.so` is a wheel that will be installed
    where it cannot run, and a platform tag on a wheel holding no compiled
    code narrows who may install it for nothing.
    """
    if wheel is None:
        row(label, False, note)
        return
    tags = gatelib.wheel_tags(wheel)
    faults = []
    if not tags:
        faults.append("the filename carries no tag triple")
    for python_tag, abi, platform in tags:
        if not re.match(r"^py\d+$", python_tag):
            faults.append("interpreter tag %r is not a pure-Python tag"
                          % (python_tag,))
        if abi != "none":
            faults.append("ABI tag is %r, not none" % (abi,))
        if platform != "any":
            faults.append("platform tag is %r, not any" % (platform,))
    compiled = [n for n in gatelib.wheel_names(wheel)
                if n.endswith((".so", ".pyd", ".dylib"))]
    if compiled:
        faults.append("carries compiled members: %s" % ", ".join(compiled)[:80])
    row(label, not faults,
        ("%s: %s" % (note, "; ".join(faults)))[:290] if faults
        else "%s is %s and carries no compiled member"
             % (note, ", ".join("-".join(t) for t in tags)))


# --------------------------------------------------------------------------
# what the extension's C says about the lock
# --------------------------------------------------------------------------

def brace_span(text, start, opener="{", closer="}"):
    """(open, close) of the group at or after `start`, or None.

    A scanner and not a regex for the usual reason: the thing being matched
    nests, and a pattern that matches `{...}` matches the wrong closer as soon
    as an initialiser contains one. The text handed to this has already been
    through `strip_c_noise`, so a brace inside a comment or a character literal
    is not here to be counted. `opener`/`closer` are `()` when the group being
    spanned is a call's argument list rather than an initialiser.
    """
    open_at = text.find(opener, start)
    if open_at < 0:
        return None
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == opener:
            depth += 1
        elif text[i] == closer:
            depth -= 1
            if depth == 0:
                return (open_at, i)
    return None


def split_top_level(body):
    """`body` split on the commas that are not inside a nested group."""
    out, depth, current = [], 0, []
    for ch in body:
        if ch in "{([":
            depth += 1
        elif ch in "})]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(current))
            current = []
            continue
        current.append(ch)
    out.append("".join(current))
    return [part.strip() for part in out]


def slot_arrays(text):
    """Every `PyModuleDef_Slot <name>[] = { ... }` in `text`, by name."""
    out = {}
    for match in re.finditer(r"\bPyModuleDef_Slot\s+(\w+)\s*\[\s*\]\s*=", text):
        span = brace_span(text, match.end())
        if span:
            out[match.group(1)] = text[span[0] + 1:span[1]]
    return out


def gil_slot_entry(body):
    """The `{Py_mod_gil, ...}` entry in a slot array's body, or None."""
    for group in re.findall(r"\{[^{}]*\}", body):
        if re.search(r"\bPy_mod_gil\b", group):
            return group
    return None


def arrays_declaring_no_gil(arrays):
    """The names in `arrays` whose `Py_mod_gil` slot is `Py_MOD_GIL_NOT_USED`."""
    out = []
    for name in sorted(arrays):
        entry = gil_slot_entry(arrays[name])
        if entry and re.search(r"\bPy_MOD_GIL_NOT_USED\b", entry):
            out.append(name)
    return out


def resolve_slots_name(expression):
    """The array an `m_slots` initialiser names, through the spellings of it.

    `bar_slots`, `&bar_slots`, `&bar_slots[0]` and either of those behind a
    parenthesised cast are one pointer written four ways -- an array decays to
    the address of its first element, and `&a[0]` is that address spelled out.
    Returns the expression unchanged when it is none of them, which is the
    caller's signal that this file did not resolve it.
    """
    name = expression.strip()
    while True:
        cast = re.match(r"^\(\s*[\w\s]+\*+\s*\)\s*(.+)$", name, re.S)
        if not cast:
            break
        name = cast.group(1).strip()
    if name.startswith("&"):
        name = name[1:].strip()
    first = re.match(r"^(\w+)\s*\[\s*0\s*\]$", name)
    if first:
        name = first.group(1)
    return name


def module_def_fields(text):
    """The fields of the `PyModuleDef` initialiser in `text`.

    Returns `(fields, name, designated)`. `fields` answers both spellings: a
    designated initialiser (`.m_size = 0`) by name, and a positional one by its
    position in the struct, which is the order PEP 489 and
    `Include/moduleobject.h` fix -- HEAD_INIT, m_name, m_doc, m_size,
    m_methods, m_slots. `designated` is the set of fields that arrived by name,
    and it is returned because the two spellings mean different things by an
    absent field: a designated initialiser leaves out what it wants zeroed and
    C zeroes it, while a positional one that leaves a field out shifts every
    later field up by one. A caller reading an absent field has to know which
    it is looking at.
    """
    match = re.search(r"\bPyModuleDef\s+(\w+)\s*=", text)
    if not match:
        return None, "no PyModuleDef initialiser", set()
    span = brace_span(text, match.end())
    if span is None:
        return None, "the PyModuleDef initialiser does not close", set()
    parts = split_top_level(text[span[0] + 1:span[1]])
    positional = ["PyModuleDef_HEAD_INIT", "m_name", "m_doc", "m_size",
                  "m_methods", "m_slots", "m_traverse", "m_clear", "m_free"]
    fields = {}
    by_name = set()
    index = 0
    for part in parts:
        designated = re.match(r"^\.\s*(\w+)\s*=\s*(.*)$", part, re.S)
        if designated:
            fields[designated.group(1)] = designated.group(2).strip()
            by_name.add(designated.group(1))
            continue
        if index < len(positional):
            fields.setdefault(positional[index], part)
        index += 1
    return fields, match.group(1), by_name


# Directories a build DERIVES under the tree it is building, on top of the ones
# `gatelib.SKIP_DIRS` already prunes (`build`, `dist`, `.eggs`, `__pycache__`,
# `site-packages`, `.git`, `*.egg-info`) and the virtual environments
# `gatelib.iter_files` prunes by their `pyvenv.cfg`. Each is here because a
# build of THIS extension can write a generated copy of `src/hellomodule.c`
# into it:
#
#   _skbuild      scikit-build and scikit-build-core's in-tree build directory
#   CMakeFiles    what an in-source `cmake .` leaves beside CMakeLists.txt
#   .mesonpy-*    meson-python's scratch directory; its name carries a hash,
#                 so it is matched by prefix rather than listed
#
# Step-local rather than in `gatelib.SKIP_DIRS`: `gil.declares` is the only row
# in any of the five steps that walks a tree looking for a SECOND file, so it
# is the only row this hazard has. The hazard is real
# because the static gates run before `test.sh` builds anything: a candidate
# who tried a CMake-driven build inside `/app` while working leaves the
# generated copy behind, and without this it costs thirteen units on a row
# about a second copy maintained BY HAND.
DERIVED_BUILD_DIRS = ("_skbuild", "CMakeFiles")
DERIVED_BUILD_PREFIXES = (".mesonpy-",)


def extension_sources(hellodir):
    """Every C source under `hellodir` that defines `PyInit__bar`, by path.

    The instruction says both wheels come out of the one source file; a tree
    that keeps a second copy of the extension so that one wheel can be built
    from each is the shortcut that sentence exists to rule out, and this is
    where it is visible. Read through `strip_c_noise` for the same reason
    everything else here is: a `PyInit__bar` inside a comment explaining what
    was removed is not a second copy.

    Build scratch is pruned first -- see `DERIVED_BUILD_DIRS`. A copy of the
    extension's source that a build system generated is the build system's copy
    of the one file, not a second file; this row is about what the candidate
    maintains.
    """
    out = []
    for path in gatelib.iter_files(hellodir, skip_names=DERIVED_BUILD_DIRS):
        if any(part.startswith(DERIVED_BUILD_PREFIXES)
               for part in gatelib.relpath(path, hellodir).split("/")[:-1]):
            continue
        if not path.endswith((".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp")):
            continue
        with open(path, "rb") as fh:
            body = strip_c_noise(fh.read().decode("utf-8", "replace"))
        if re.search(r"\bPyInit__bar\b", body):
            out.append(path)
    return sorted(out)


def multi_phase_faults(text):
    """What stops `text` from being a multi-phase declaration. [] means it is.

    Four clauses, and not one of them is a house style: each is a condition the
    interpreter acts on when the module is imported -- two of them by refusing
    the import outright, two by deciding whether the declaration is read at all
    -- so a file that fails one is a file that does not do what was asked, not
    a file written differently. The instruction puts the extension's C beyond
    these properties out of scope, and this is the line that draws.

    * `PyModuleDef_Init` rather than `PyModule_Create`. The import machinery
      runs a slot array only when the init function hands back a `PyModuleDef`
      for it to build from; hand back a built module and the slots are never
      read, so `Py_mod_gil` says nothing and the interpreter turns the lock
      back on -- `gil.stays-off` is the row that sees that one. Writing both is
      worse: `PyModule_Create` on a definition with slots raises `SystemError`,
      "module %s: PyModule_Create is incompatible with m_slots"
      (`_PyModule_CreateInitialized`, `Objects/moduleobject.c`; documented as
      "when using legacy single-phase initialization, m_slots must be NULL"),
      and the module does not import at all.
    * a slot array wired into `m_slots`. The interpreter reaches slots only
      through `def->m_slots` -- `PyModule_FromDefAndSpec2` walks that array and
      nothing else -- so an array nothing points at is never read, `Py_mod_gil`
      is never seen, and the lock comes back on with the module's name in the
      `RuntimeWarning`. No error, which is what makes it worth a row.
    * `Py_mod_gil` set to `Py_MOD_GIL_NOT_USED` in it, which is the declaration
      itself: absent it, the interpreter's default for an extension is that the
      lock is needed, and it enables it and says so on stderr.
    * `m_size` not negative. "Negative m_size is only allowed when using
      legacy single-phase initialization or when creating modules dynamically"
      (`PyModuleDef.m_size`, `c-api/module.html`): under multi-phase
      initialisation `PyModule_FromDefAndSpec2` raises `SystemError`, "module
      %s: m_size may not be negative for multi-phase initialization", and the
      import fails -- taking `abi3.serves`, `ft.imports` and `gil.stays-off`
      with it.

    Reading the declaration as text still stops short of importing it, and
    where it does the detail string says so rather than the fault list. An
    `m_slots` expression is resolved through the spellings that name an array
    in this file -- `bar_slots`, `&bar_slots`, `&bar_slots[0]`, either behind a
    cast -- and an expression past those, a macro or an array declared in
    another translation unit, is recorded as unresolved and not faulted, so
    long as this file does declare a slot array carrying `{Py_mod_gil,
    Py_MOD_GIL_NOT_USED}` somewhere: what the interpreter does with that wiring
    is a fact about the import, and `gil.stays-off` is the row that imports the
    module and watches. `NULL`, `0` and an absent `m_slots` stay faults,
    because they are not unresolved -- they resolve, to nothing, and no slot
    array is read at all. An absent `m_size` is a fault in a positional
    initialiser, where the field below it moves up into its place, and not in a
    designated one, where C zeroes what is not mentioned and zero is the value
    multi-phase initialisation wants.
    """
    faults = []
    if not re.search(r"\bPyModuleDef_Init\b", text):
        faults.append("PyInit__bar does not return a PyModuleDef_Init")
    if re.search(r"\bPyModule_Create2?\b", text):
        faults.append("still initialised in one phase (PyModule_Create)")

    arrays = slot_arrays(text)
    fields, note, designated = module_def_fields(text)
    slots_name = None
    slots_note = "?"
    m_size = "?"
    if fields is None:
        faults.append(note)
    else:
        m_size = fields.get("m_size", "")
        if not m_size:
            if designated:
                m_size = "0 (not mentioned in a designated initialiser)"
            else:
                faults.append("the PyModuleDef states no m_size")
        elif m_size.lstrip().startswith("-"):
            faults.append("m_size is %s" % m_size.strip())
        written = (fields.get("m_slots") or "").strip()
        slots_note = written or "unset"
        if not written or written in ("NULL", "0"):
            faults.append("m_slots is %r" % (written or "unset",))
        elif resolve_slots_name(written) in arrays:
            slots_name = resolve_slots_name(written)
        else:
            declaring = arrays_declaring_no_gil(arrays)
            if declaring:
                slots_note = ("m_slots is %s, which this file does not resolve "
                              "to an array it declares; %s does carry "
                              "{Py_mod_gil, Py_MOD_GIL_NOT_USED}, and "
                              "gil.stays-off is the row that can tell whether "
                              "the interpreter reads it"
                              % (written[:40], ", ".join(declaring)[:40]))
            else:
                faults.append("m_slots names %r, which is not a "
                              "PyModuleDef_Slot array in this file"
                              % (written,))

    if slots_name:
        entry = gil_slot_entry(arrays[slots_name])
        if entry is None:
            faults.append("%s has no Py_mod_gil slot" % slots_name)
        elif not re.search(r"\bPy_MOD_GIL_NOT_USED\b", entry):
            faults.append("the Py_mod_gil slot in %s is %s"
                          % (slots_name, " ".join(entry.split())[:60]))
        else:
            slots_note = ("%s carrying {Py_mod_gil, Py_MOD_GIL_NOT_USED}"
                          % slots_name)
    return faults, m_size, slots_note


def init_function_body(text, name="PyInit__bar"):
    """(open, close) of the braces around the body `name` is defined with.

    The first `{` after the name is not it: a prototype ends in `;` and the
    next brace in the file belongs to whatever follows. So the argument list is
    spanned first, and what comes after it has to be the body for this to be
    the definition rather than a declaration or a call.
    """
    for match in re.finditer(r"\b%s\b" % re.escape(name), text):
        args = brace_span(text, match.end(), "(", ")")
        if args is None or text[match.end():args[0]].strip():
            continue
        rest = text[args[1] + 1:]
        stripped = rest.lstrip()
        if not stripped.startswith("{"):
            continue
        body = brace_span(text, args[1] + 1 + (len(rest) - len(stripped)))
        if body is not None:
            return body
    return None


def set_gil_calls(text):
    """The `PyUnstable_Module_SetGIL(..., Py_MOD_GIL_NOT_USED)` calls in
    `text`, as (inside `PyInit__bar`, anywhere else); either may be None.

    Where the call is, is the whole of it. The interpreter runs what the init
    function runs, so a call in a static helper nothing calls declares nothing
    and the lock comes back on -- while a row that took the call's presence in
    the file as the declaration would hand thirteen units to dead code, and
    hand them for a file whose own comment could have earned them. Both
    answers are returned rather than one, so that the detail string can tell a
    file that never makes the call from a file that makes it out of reach.

    The argument list is spanned rather than matched, because the module
    argument is itself commonly a call.
    """
    body = init_function_body(text)
    inside = outside = None
    for match in re.finditer(r"\bPyUnstable_Module_SetGIL\s*\(", text):
        span = brace_span(text, match.start(), "(", ")")
        if span is None:
            continue
        args = text[span[0] + 1:span[1]]
        if not re.search(r"\bPy_MOD_GIL_NOT_USED\b", args):
            continue
        call = " ".join(("PyUnstable_Module_SetGIL(%s)" % args).split())
        if body is not None and body[0] < match.start() < body[1]:
            inside = inside or call
        else:
            outside = outside or call
    return inside, outside


def free_threaded_build():
    """The macros the free-threaded build compiles `hellomodule.c` with.

    The declaration `gil.declares` reads is read by one interpreter only, the
    free-threaded one, so the file is read the way that build compiles it
    (`step02_gates.active_c_text`) and not as every branch at once. A file that
    keeps single-phase `PyModule_Create` in the `#else` of `#ifdef
    Py_GIL_DISABLED`, for the limited-API build that has no `Py_mod_gil`, makes
    that call in a build that never reads the declaration: such a file imports
    on 3.13t with the lock off.

    `Py_GIL_DISABLED` is defined. `Py_LIMITED_API` is not: the 3.13 headers
    refuse the two together (`#error "The limited API is not currently
    supported in the free-threaded build"`, `Include/Python.h`), so a
    free-threaded build that compiles has it undefined. `Py_mod_gil` is then
    defined (`Include/moduleobject.h` defines it outside the limited API), and
    `PY_VERSION_HEX` is the free-threaded interpreter's own `sys.hexversion`,
    asked of it; if it cannot be asked, the version stays unknown. Every other
    macro is unknown, so every branch on one stays in the text -- the reading
    this row had before, for any condition this cannot decide. The one
    simplification is that `Py_GIL_DISABLED` holds from the first line, where
    the real build defines it in `pyconfig.h`: a file that tests it before any
    Python header has been read builds wrongly on that interpreter, and the
    `ft.` rows and `gil.stays-off` are the ones that see it.
    """
    config = {"Py_GIL_DISABLED": 1, "Py_LIMITED_API": step02_gates.PP_UNDEF,
              "Py_mod_gil": 4, "PY_MAJOR_VERSION": 3, "PY_MINOR_VERSION": 13}
    try:
        import shutil
        import subprocess
        exe = shutil.which("python3.13t")
        if exe:
            proc = subprocess.run(
                [exe, "-I", "-S", "-c", "import sys; print(sys.hexversion)"],
                cwd="/", stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                timeout=60)
            hexversion = int(proc.stdout.decode().strip())
            if proc.returncode == 0 and (hexversion >> 16) == 0x030D:
                config["PY_VERSION_HEX"] = hexversion
    except Exception:
        pass
    return config


def gate_gil_declares(hellodir):
    """The extension declares, in its own source, that it does not need the GIL.

    The declaration, and not one spelling of it. CPython gives an extension two
    ways to say `Py_MOD_GIL_NOT_USED`, and the free-threading HOWTO this step's
    reference document names carries both:

    * multi-phase initialisation with `{Py_mod_gil, Py_MOD_GIL_NOT_USED}` in
      the slot array wired into `m_slots` -- the route that stays inside the
      limited API, and the one the reference solution takes; and
    * single-phase initialisation with `PyUnstable_Module_SetGIL(m,
      Py_MOD_GIL_NOT_USED)` in `PyInit__bar`, which is outside the limited API
      and so only compiles in the free-threaded build -- which is the only
      build that reads it. In `PyInit__bar`, and read nowhere else: the
      interpreter calls that one function, and a call it cannot reach from
      there is a comment with a semicolon after it.

    Both were measured on this image's 3.13t and both leave
    `sys._is_gil_enabled()` False, so a gate that took only the first would
    charge thirteen units for following the other half of the page the
    instruction points at. The instruction asks the module to declare its
    position; this asks whether it does.

    `src/hellomodule.c` by path, because that is the file `02.hello.c-ported`
    already requires to exist and to define `PyInit__bar`; a candidate who
    moved it has failed that row, and this one agreeing with it is better than
    the two of them looking in different places. A SECOND file under the
    distribution that also defines `PyInit__bar` is a fault here, because the
    instruction says both wheels come out of the one source file.

    The file is read as the free-threaded build compiles it -- see
    `free_threaded_build` -- because that build is the one that reads the
    declaration; what the limited-API build does in its own branches is the
    `abi3.` rows' business.
    """
    label = "gil.declares"
    if hellodir is None:
        row(label, False, "the hello distribution was not found")
        return
    path = os.path.join(hellodir, "src", "hellomodule.c")
    if not os.path.isfile(path):
        row(label, False, "src/hellomodule.c is not there")
        return
    with open(path, "rb") as fh:
        text = strip_c_noise(fh.read().decode("utf-8", "replace"))
    text, blanked = step02_gates.active_c_text(text, free_threaded_build())
    read_as = (", read as the free-threaded build compiles it (%d line(s) "
               "in branches it skips)" % blanked) if blanked else ""

    faults = []
    others = [gatelib.relpath(p, hellodir) for p in extension_sources(hellodir)
              if os.path.abspath(p) != os.path.abspath(path)]
    if others:
        faults.append("PyInit__bar is defined in %d files, not one: %s"
                      % (len(others) + 1, ", ".join(others)[:90]))

    set_gil, set_gil_elsewhere = set_gil_calls(text)
    mp_faults, m_size, slots_note = multi_phase_faults(text)
    if set_gil is None and mp_faults:
        faults.extend(mp_faults)
        if set_gil_elsewhere is not None:
            faults.append("the file calls %s, but not from PyInit__bar, so "
                          "the import never runs it"
                          % set_gil_elsewhere[:70])
        elif not re.search(r"\bPy_MOD_GIL_NOT_USED\b", text):
            faults.append("nothing in the file names Py_MOD_GIL_NOT_USED")
        else:
            faults.append("and no PyUnstable_Module_SetGIL(..., "
                          "Py_MOD_GIL_NOT_USED) either")

    if set_gil is not None:
        how = ("single-phase initialisation with %s in PyInit__bar"
               % set_gil[:90])
    else:
        how = ("PyModuleDef_Init, m_size %s, %s"
               % (str(m_size).strip(), slots_note))
    row(label, not faults,
        ("; ".join(faults) + read_as)[:290] if faults
        else "src/hellomodule.c: %s%s" % (how, read_as))


# --------------------------------------------------------------------------

SOURCE_LABELS = ["gil.declares"]
WHEEL_LABELS = ["abi3.tag", "abi3.floor-agrees", "ft.tag",
                "pure.bento", "pure.yaku"]


def check_emitted(mode, want):
    got = [label for _family, label, _ok, _detail in ROWS]
    if sorted(got) != sorted(want):
        sys.stderr.write(
            "GATE FAULT (%s): emitted %d rows, expected %d; missing %r; "
            "unexpected %r\n"
            % (mode, len(got), len(want), sorted(set(want) - set(got)),
               sorted(set(got) - set(want))))
        raise SystemExit(4)


def main(argv):
    mode = argv[1]

    if mode == "source":
        tree, pristine = argv[2], argv[3]
        hello_name, _version = step02_gates.hello_identity(pristine)
        hellodir, _note = locate_hello(tree, hello_name)
        gate_gil_declares(hellodir)
        check_emitted(mode, SOURCE_LABELS)
        flush()
        # What `test.sh` needs and cannot read for itself, on stderr so it
        # stays out of the row stream.
        sys.stderr.write("PKG hello %s\n" % hello_name)
        sys.stderr.write("PKG root %s\n" % step03_gates.root_name())
        sys.stderr.write("PKG yaku %s\n" % step03_gates.YAKU)
        return 0

    if mode == "wheels":
        abi3_dir, ft_dir, pure_dir, pristine = argv[2], argv[3], argv[4], argv[5]
        hello_name, _version = step02_gates.hello_identity(pristine)
        root_name, yaku_name = step03_gates.root_name(), step03_gates.YAKU
        abi3_wheel, abi3_note = only_wheel(abi3_dir, hello_name)
        gate_abi3_tag(abi3_wheel, abi3_note)
        gate_abi3_floor(abi3_wheel, abi3_note)
        ft_wheel, ft_note = only_wheel(ft_dir, hello_name)
        gate_ft_tag(ft_wheel, ft_note)
        for label, name in (("pure.bento", root_name), ("pure.yaku", yaku_name)):
            wheel, note = only_wheel(pure_dir, name)
            gate_pure(label, wheel, note)
        check_emitted(mode, WHEEL_LABELS)
        flush()
        # What `test.sh` needs and cannot read for itself, on stderr so it
        # stays out of the row stream.
        if abi3_wheel:
            sys.stderr.write("ABI3WHEEL %s\n" % abi3_wheel)
            exts = wheel_extension_members(abi3_wheel)
            sys.stderr.write("ABI3EXTS %d\n" % len(exts))
            sys.stderr.write("ABI3EXTNAMES %s\n" % (" ".join(exts) or "-"))
        if ft_wheel:
            sys.stderr.write("FTWHEEL %s\n" % ft_wheel)
        return 0

    raise SystemExit("unknown mode %r" % mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
