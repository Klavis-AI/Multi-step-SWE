#!/usr/bin/env python3
"""Scored conformance runner for the Go toolchain task.

The artifact is driven only through its published interface: the toolchain is
rebuilt from source with `make`, and every case runs `$GOROOT/bin/go` inside a
module directory whose go.mod carries the language version for that case.

Expected results were recorded from the reference gc toolchain at authoring
time (see NOTES-authoring.md); nothing here is hand written.
"""
import os, sys, re, json, time, shutil, subprocess, tempfile, argparse
import random
import stat
import resource
import concurrent.futures

APP = '/app'
BUILD = '/app'            # the build runs where the instruction says it runs
TOOLPATH = '/tests/toolpath'
LOGDIR = '/logs/verifier'

GOROUTINE_RE = re.compile(r'^goroutine \d+ \[')
EXITSTATUS_RE = re.compile(r'^exit status \d+$')
LIVENESS_GO = os.environ.get('LIVENESS_GO', '1.4')


def normalize(out):
    """Drop the goroutine dump and go run's exit-status line.

    Stack frames, addresses and goroutine ids are implementation detail; the
    first `panic:`/`fatal error:` line is specified behaviour and is kept.
    """
    res = []
    for line in out.splitlines():
        if GOROUTINE_RE.match(line):
            break
        if line.startswith('[signal '):
            continue
        if EXITSTATUS_RE.match(line):
            continue
        if line.startswith('# '):
            continue
        res.append(line)
    return '\n'.join(res) + ('\n' if res else '')


def is_elf(path):
    # Runs as root over a tree the candidate controls, before the build drops
    # privilege, so it must not open anything that can block or have side
    # effects: a FIFO here would hang the verifier on open() until the step
    # timeout, and a device node is worse. Only regular files are opened, and
    # the link itself is stat'd rather than its target.
    try:
        st = os.lstat(path)
        if not stat.S_ISREG(st.st_mode):
            return False
        with open(path, 'rb') as f:
            return f.read(4) == b'\x7fELF'
    except OSError:
        return False


def prepare_tree():
    """Delete build products in place so `make` has to rebuild from source."""
    removed = 0
    for sub in ('goroot/bin', 'goroot/pkg', 'bin', 'pkg'):
        p = os.path.join(BUILD, sub)
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)
            removed += 1
    for root, dirs, files in os.walk(BUILD):
        if '.git' in dirs:
            dirs.remove('.git')
        for name in files:
            p = os.path.join(root, name)
            if name.endswith(('.o', '.a', '.so', '.obj')) or is_elf(p):
                try:
                    os.unlink(p)
                    removed += 1
                except OSError:
                    pass
    return removed


def build(timeout):
    if not (os.path.exists(os.path.join(BUILD, 'Makefile'))
            or os.path.exists(os.path.join(BUILD, 'makefile'))
            or os.path.exists(os.path.join(BUILD, 'GNUmakefile'))):
        return False, 'no Makefile in /app'
    # HOME and TMPDIR must belong to the user the build RUNS as. This said
    # '/root' -- correct when `make` ran as root, wrong the moment it was
    # dropped to uid 65533 (NOTES 61), because /root is 0700 root:root. A
    # Makefile that staged anything under $HOME got `make failed` and a zero
    # with no case scored. The reference never touches $HOME, so the oracle
    # could not see it.
    home = os.path.join(BUILD, '.home')
    try:
        os.makedirs(home, exist_ok=True)
    except OSError:
        home = '/tmp'
    env = {
        'PATH': TOOLPATH,
        'HOME': home,
        'TMPDIR': os.path.join(BUILD, '.tmp'),
        'LANG': 'C',
        'LC_ALL': 'C',
        'TERM': 'dumb',
        'SHELL': '/bin/sh',
        'MAKEFLAGS': '',
        'http_proxy': 'http://127.0.0.1:1/',
        'https_proxy': 'http://127.0.0.1:1/',
        'GOPROXY': 'off',
    }
    # The build runs as the unprivileged case user too, not as root.
    #
    # The candidate writes the Makefile, and `make` is a program that runs
    # arbitrary shell. Relocating the expectations to a root-only directory
    # protects them from the *cases* and not from the *build*: a two-line
    # recipe that copies /grading/cases.json into /app and chmods it readable
    # defeats the whole thing, and it was measured doing exactly that before
    # this dropped. So /app is handed to the case user first -- it is the
    # candidate's own tree -- and the build runs with no more privilege than
    # the programs it produces.
    try:
        os.makedirs(env['TMPDIR'], exist_ok=True)
    except OSError:
        env['TMPDIR'] = '/tmp'
    _chown_tree(BUILD, CASE_UID, CASE_GID)
    try:
        p = subprocess.run(['make'], cwd=BUILD, env=env, capture_output=True,
                           text=True, timeout=timeout, preexec_fn=_build_limits)
    except subprocess.TimeoutExpired:
        return False, 'make timed out after %ds' % timeout
    if p.returncode != 0:
        return False, 'make failed (rc=%d)\n%s\n%s' % (p.returncode, p.stdout[-3000:], p.stderr[-3000:])
    go = os.path.join(BUILD, 'goroot', 'bin', 'go')
    if not os.path.isfile(go) or not os.access(go, os.X_OK):
        return False, 'make did not produce an executable goroot/bin/go'

    # NOT made read-only here, deliberately. Locking /app after the build does
    # stop seven concurrent cases racing inside one tree -- but the tree is
    # also what Harbor collects and what seeds the NEXT rung of the chain, and
    # a read-only workspace would arrive at the next agent unable to edit its
    # own source. It also makes the package non-idempotent: a second run over
    # the same tree cannot build. Both were measured. So the sharing is
    # documented in toolchain.md instead, and GOCACHE stays per-case.

    # No root-privileged recursive operation runs over the candidate's tree.
    # This used to be `chmod -R a+rX` as root, which is both unnecessary and a
    # liability: the tree was handed to the case user before the build, and the
    # build now runs as that user, so everything it produced is already owned
    # by the user the cases run as. A recursive chmod as root over paths the
    # candidate controls is exactly the kind of process this package spent the
    # afternoon removing.
    return True, p.stdout[-2000:]


def case_timeout(case, per_case_timeout):
    """This case's wall clock, never more than the budget that is left."""
    t = case.get('timeout', per_case_timeout)
    if DEADLINE is not None:
        t = max(1, min(t, int(DEADLINE - time.time())))
    return t


def _chown_tree(root, uid, gid):
    """Hand a tree to the case user. Only meaningful when running as root."""
    if os.getuid() != 0 or uid <= 0:
        return
    for base, dirs, files in os.walk(root):
        for name in dirs + files:
            try:
                os.lchown(os.path.join(base, name), uid, gid)
            except OSError:
                pass
    try:
        os.chown(root, uid, gid)
    except OSError:
        pass


def _build_limits():
    """The build's ceilings: no core dump, and the same unprivileged user the
    cases run as. The per-case CPU ceiling is deliberately not applied -- a
    toolchain build is allowed to be long, and its bound is the wall clock."""
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if os.getuid() == 0 and CASE_UID > 0:
        try:
            os.setgroups([])
        except Exception:
            pass
        os.setgid(CASE_GID)
        os.setuid(CASE_UID)


def _open_dir(d):
    """Make a case directory usable by the unprivileged case user."""
    try:
        os.chmod(d, 0o1777)
    except Exception:
        pass


def go_env():
    env = dict(os.environ)
    env['GOROOT'] = os.path.join(BUILD, 'goroot')
    env['GOFLAGS'] = ''
    env['GO111MODULE'] = 'on'
    env.pop('GOTOOLCHAIN', None)
    return env


# The liveness probe must not be answerable by echoing a literal.
#
# It used to compile `println("live-1.4")` and grep its own token back out of
# the output, which a toolchain passes by copying a string from its input to
# its output -- measured, with fifty lines of C that implement no Go at all.
# So the token is now *computed*: a slice, a range loop and integer arithmetic
# whose result appears nowhere in the source. Emitting it requires evaluating
# it. One of eight variants is chosen per run; all eight are verified against
# the reference in preflight, so the choice cannot make a correct toolchain
# flaky.
LIVE_VARIANTS = 8
LIVE_DETAIL = []


def _live_program(lang, which):
    rnd = random.Random('liveness|%s|%d' % (lang, which))
    v = [rnd.randrange(-900000, 900000) for _ in range(8)]
    seed = rnd.randrange(1, 90000)
    m = 1 << 64
    s = seed
    for i, x in enumerate(v):
        s = (s * 131 + x + i) % m
    if s >= 1 << 63:
        s -= m
    src = ('package main\n\nfunc main() {\n'
           '\tv := []int{%s}\n'
           '\ts := %d\n'
           '\tfor i, x := range v {\n'
           '\t\ts = s*131 + x + i\n'
           '\t}\n'
           '\tprintln("live", s)\n}\n'
           % (', '.join(str(x) for x in v), seed))
    return src, str(s)


def liveness_one(lang, which=None):
    """Build and run a small program under one `go` directive, with the
    toolchain that `make` just produced -- `goroot/bin/go` was deleted before
    the build, and PATH holds only the C toolchain, so nothing else can answer.
    The expected output is computed by the program, not spelled in it."""
    env = go_env()
    go = os.path.join(BUILD, 'goroot', 'bin', 'go')
    d = tempfile.mkdtemp(prefix='golive')
    try:
        _open_dir(d)
        if which is None:
            which = random.randrange(LIVE_VARIANTS)
        src, token = _live_program(lang, which)
        with open(os.path.join(d, 'go.mod'), 'w') as f:
            f.write('module m\n\ngo %s\n' % lang)
        with open(os.path.join(d, 'x.go'), 'w') as f:
            f.write(src)
        env['GOPATH'] = os.path.join(d, '.gopath')
        env['HOME'] = d
        env['GOCACHE'] = os.path.join(d, '.cache')
        p = subprocess.run([go, 'run', '.'], cwd=d, env=env, capture_output=True,
                           text=True, timeout=120, preexec_fn=_limits)
        ok = p.returncode == 0 and token in (p.stdout + p.stderr)
        if not ok:
            # "it failed" and "it could not read its own source" are different
            # findings and must not look alike in the report
            st = os.stat(os.path.join(d, 'x.go'))
            LIVE_DETAIL.append(
                'go %s: rc=%d out=%r err=%r (source mode %o uid %d, case uid %d)'
                % (lang, p.returncode, p.stdout[-300:], p.stderr[-300:],
                   st.st_mode & 0o777, st.st_uid, CASE_UID))
        return ok
    except Exception as e:                       # noqa: BLE001
        LIVE_DETAIL.append('go %s: %r' % (lang, e))
        return False
    finally:
        shutil.rmtree(d, ignore_errors=True)


def liveness():
    """Every directive the step must answer to: this milestone's own, and the
    oldest one the chain still supports.

    Returns the list of directives that failed, so a zero can be told apart
    from the other two ways a step reaches zero -- a tree that never built, and
    a toolchain that built and was simply wrong. All three are 0.000000 in the
    reward and only the report distinguishes them."""
    bad = [l.strip() for l in LIVENESS_GO.split(',')
           if l.strip() and not liveness_one(l.strip())]
    return bad


# ---------------------------------------------------------------------------
# Artifact provenance.
#
# The milestone instruction forbids handing the work to an online
# implementation. The structural defences already in this runner force a wrap
# to build its toolchain from source here rather than drop in a binary: every
# build product is deleted before `make` (prepare_tree), the build sees no
# network (GOPROXY off, a dead proxy) and a C-only build PATH (TOOLPATH). The
# consequence worth checking is that a toolchain built that way, if it is
# upstream Go, is itself an upstream Go program -- its `go`, `gofmt` and the
# tools under pkg/tool are Go-compiled. The reference toolchain is written in
# C, so the `go` it produces is a C program and carries none of that. A
# produced toolchain whose own binaries are Go-compiled therefore did not come
# from this tree's C sources.
#
# Hard zero, not a gate: the step reports 0, Harbor still runs and averages
# every other step. Checked as root over goroot before any case runs, and only
# over the toolchain's OWN binaries (goroot/bin, goroot/pkg/tool) -- never over
# per-case `go build` outputs, which a correct toolchain legitimately stamps.
_GO_BINARY_MARK = b"\xff Go buildinf:"
# Older gc releases (before 1.13) write no build-info header, and an upstream
# Go 1.4 is buildable with nothing but a C compiler. Every gc-linked ELF since
# Go 1.0 names its pc-line table section, so that name is the second mark.
_GO_PCLNTAB_MARK = b".gopclntab"


# What one wrap_check() call may read (hang-path sweep, 2026-10-13). It runs as
# root over a tree the candidate's `make` wrote, and without a bound a tree of
# many links to one large sparse ELF -- or many such files -- makes it read for
# longer than the step's timeout, which Harbor answers by dropping the step
# from the mean. So each file is read at most once per call, by inode, in one
# pass for every mark; the call reads at most _WRAP_READ_BUDGET bytes and looks
# at most _WRAP_ENTRY_BUDGET entries. A tree that needs more cannot be shown
# not to be a wrapper, and it scores 0 like one: an honest tree is a few
# hundred files and a few megabytes of ELF.
_WRAP_READ_BUDGET = 2 * 1024 * 1024 * 1024
_WRAP_ENTRY_BUDGET = 500000
_WRAP_STATE = {'memo': {}, 'bytes': _WRAP_READ_BUDGET,
               'entries': _WRAP_ENTRY_BUDGET}


class _WrapBudget(Exception):
    pass


def _wrap_entry(path):
    _WRAP_STATE['entries'] -= 1
    if _WRAP_STATE['entries'] < 0:
        raise _WrapBudget('more than %d entries (at %s)'
                          % (_WRAP_ENTRY_BUDGET, path))


def _file_marks(path, marks, cap=128 * 1024 * 1024):
    """The subset of *marks* found in the first *cap* bytes of *path*, a
    regular file. Reads in chunks with an overlap so a mark straddling a
    boundary is still found, and never opens a non-regular file -- a FIFO or
    device node in the candidate tree could otherwise block the verifier until
    the step timeout. Within one wrap_check() call each inode is read once,
    however many links lead to it."""
    try:
        st = os.lstat(path)
    except OSError:
        return frozenset()
    if not stat.S_ISREG(st.st_mode):
        return frozenset()
    key = (st.st_dev, st.st_ino)
    memo = _WRAP_STATE['memo']
    if key in memo:
        return memo[key]
    ov = max(len(m) for m in marks) - 1
    found = set()
    try:
        with open(path, 'rb') as f:
            prev = b''
            read = 0
            while read < cap and len(found) < len(marks):
                chunk = f.read(1 << 20)
                if not chunk:
                    break
                read += len(chunk)
                _WRAP_STATE['bytes'] -= len(chunk)
                if _WRAP_STATE['bytes'] < 0:
                    raise _WrapBudget('more than %d bytes of ELF content (at %s)'
                                      % (_WRAP_READ_BUDGET, path))
                window = prev + chunk
                for m in marks:
                    if m in window:
                        found.add(m)
                prev = chunk[-ov:] if ov else b''
    except OSError:
        pass
    memo[key] = frozenset(found)
    return memo[key]


def _go_compiled(path):
    """True when *path*, after following links, is a Go-compiled executable."""
    target = os.path.realpath(path)
    if target.startswith(('/proc/', '/sys/', '/dev/')):
        return False
    if not is_elf(target):
        return False
    found = _file_marks(target, _ALL_MARKS)
    return _GO_BINARY_MARK in found or _GO_PCLNTAB_MARK in found


def _walk_following_links(base):
    """Every non-directory entry under *base*, descending into linked
    directories too -- each real directory once, so a link cycle ends; never
    into /proc, /sys or /dev; and at most 200000 entries, so a link to / ends
    in bounded time."""
    seen, stack, budget = set(), [base], 200000
    while stack and budget > 0:
        d = stack.pop()
        real = os.path.realpath(d)
        if real in seen or real.startswith(('/proc', '/sys', '/dev')):
            continue
        seen.add(real)
        try:
            names = os.listdir(d)
        except OSError:
            continue
        for name in names:
            budget -= 1
            p = os.path.join(d, name)
            _wrap_entry(p)
            if os.path.isdir(p):
                if name != '.git':
                    stack.append(p)
            else:
                yield p


# Outside goroot/bin and goroot/pkg/tool the tree may legitimately hold
# programs a correct toolchain produced: toolchain.md lets the toolchain write
# into its own tree at run time, and a correct toolchain may stamp what it
# builds with the same marks gc uses. So there a Go mark alone is not enough;
# the program must also be one of gc's OWN commands. Each one gc needs to build
# anything -- go, compile, link, asm -- carries its own source path; a user
# program or a compiled standard library never does.
_GC_TOOLCHAIN_MARKS = (b"cmd/go/", b"cmd/compile/", b"cmd/link/", b"cmd/asm/")
_ALL_MARKS = (_GO_BINARY_MARK, _GO_PCLNTAB_MARK) + _GC_TOOLCHAIN_MARKS


def _gc_toolchain_command(path):
    """True when *path*, after following links, is a Go-compiled executable
    that is itself one of the upstream gc toolchain's own commands."""
    if not _go_compiled(path):
        return False
    found = _file_marks(os.path.realpath(path), _ALL_MARKS)
    return any(m in found for m in _GC_TOOLCHAIN_MARKS)


def _scan_app_for_go_binary():
    """Return a relative offending path under /app, or '' -- any upstream gc
    toolchain command anywhere under /app. os.walk does not descend directory
    symlinks (so a link to / cannot trap it), and each regular file is judged by
    its realpath target, so a file symlinked to a gc command elsewhere is still
    that command."""
    for root, dirs, files in os.walk(BUILD):
        if '.git' in dirs:
            dirs.remove('.git')
        for name in files:
            p = os.path.join(root, name)
            _wrap_entry(p)
            if _gc_toolchain_command(p):
                return os.path.relpath(p, BUILD)
    return ''


def wrap_check():
    """Return the offending path (relative to /app) when the produced toolchain
    is a wrapped upstream Go, else ''. Scans the toolchain binaries `make`
    produced for the signature every Go-compiled executable carries; the
    reference's C-built toolchain has none of it.

    A link is judged by what it points at -- a `goroot/bin/go` that is a symlink
    to a Go-compiled `go` elsewhere is that program. The whole of /app is then
    scanned too, and unconditionally: an upstream gc toolchain left ANYWHERE
    under /app -- not only when `goroot/bin/go` is itself a script -- is one the
    produced `go` can hand its work to. There, only gc's own commands count (see
    _gc_toolchain_command), because the rest of the tree may hold programs a
    correct toolchain built. (Gating the /app scan on `go` being a
    non-executable image used to leave the scan off whenever a small native
    launcher stood in for `go`.)

    This runs before the liveness programs are built, so nothing in the tree is
    yet a program the toolchain compiled for the verifier; main() runs it again
    after liveness, when a toolchain unpacked on first use is on disk.

    Both scans together read and visit at most the _WRAP_* budgets; a tree that
    needs more is reported as not shown to be unwrapped, with the reason."""
    _WRAP_STATE.update(memo={}, bytes=_WRAP_READ_BUDGET,
                       entries=_WRAP_ENTRY_BUDGET)
    try:
        goroot = os.path.join(BUILD, 'goroot')
        for base in (os.path.join(goroot, 'bin'),
                     os.path.join(goroot, 'pkg', 'tool')):
            if not os.path.isdir(base):
                continue
            for p in _walk_following_links(base):
                if _go_compiled(p):
                    return os.path.relpath(p, BUILD)
        return _scan_app_for_go_binary()
    except _WrapBudget as err:
        return '(wrapper scan stopped: %s)' % err


# Every scored case runs under a hard address-space ceiling and a wall clock,
# so one runaway program cannot take the whole verifier down with it. The
# limits are stated in the agent-facing toolchain notes.
CASE_AS_LIMIT = 4 * 1024 * 1024 * 1024
CASE_CPU_LIMIT = 300


# Every case subprocess also drops to an unprivileged user, because the
# recorded expectations are on the same filesystem as the toolchain being
# graded. A toolchain that can read them does not need to implement Go: it
# looks up the case whose source it was handed and prints the answer. Measured
# with 62 lines of C reading the expectations by absolute path: 62 of 133 `run`
# cases at step 1 with a crude matcher, and a careful one gets nearly all of
# them plus every errorcheck case from `want.lines`.
#
# So the expectations live in a root-only directory and nothing that the
# candidate supplies ever runs as root. Both halves are needed: either alone
# leaves the file reachable.
# A wall-clock budget for the whole verifier, well inside the step's own
# timeout_sec. The inner caps do fit -- 1200 s of build, 240 s of liveness and
# 883 s of cases at seven jobs, against 2700 s -- but only if the cases really
# do run seven at a time, and the box they run on is shared. Being killed on
# the step timeout is the one failure that costs more than this step: the
# verifier dies before it writes a score. So it stops itself first and reports
# what it has, with the cases it did not reach counted as failures.
DEADLINE = None
DEADLINE_MSG = 'verifier deadline reached before this case ran'


def past_deadline():
    return DEADLINE is not None and time.time() > DEADLINE


CASE_UID = int(os.environ.get('CASE_UID', '65533'))
CASE_GID = int(os.environ.get('CASE_GID', '65533'))


def _limits():
    resource.setrlimit(resource.RLIMIT_AS, (CASE_AS_LIMIT, CASE_AS_LIMIT))
    resource.setrlimit(resource.RLIMIT_CPU, (CASE_CPU_LIMIT, CASE_CPU_LIMIT))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if os.getuid() == 0 and CASE_UID > 0:
        try:
            os.setgroups([])
        except Exception:
            pass
        os.setgid(CASE_GID)
        os.setuid(CASE_UID)


def run_case(case, per_case_timeout):
    if past_deadline():
        return False, DEADLINE_MSG
    env = go_env()
    go = os.path.join(BUILD, 'goroot', 'bin', 'go')
    d = tempfile.mkdtemp(prefix='gocase')
    try:
        _open_dir(d)
        for fl in case['files']:
            path = os.path.join(d, fl['name'])
            if os.path.dirname(path):
                os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w') as f:
                f.write(fl['content'])
        if case.get('gomod') is not None:
            with open(os.path.join(d, 'go.mod'), 'w') as f:
                f.write(case['gomod'])
        env['GOPATH'] = os.path.join(d, '.gopath')
        env['HOME'] = d
        env['GOCACHE'] = os.path.join(d, '.cache')
        for k, v in case.get('env', {}).items():
            env[k] = v.replace('$DIR', d)
        cmd = case.get('cmd') or ['run', '.']
        want = case['want']
        act = case['action']
        p = subprocess.run([go] + cmd, cwd=d, env=env, capture_output=True,
                           text=True, timeout=case_timeout(case, per_case_timeout),
                           preexec_fn=_limits)
        if act == 'run':
            got = normalize(p.stdout + p.stderr)
            if case.get('normalize_durations'):
                got = re.sub(r'\d+\.\d+s', '0.00s', got)
            if p.returncode != want['exit']:
                return False, 'exit %d want %d; output=%r' % (p.returncode, want['exit'], (p.stdout + p.stderr)[:400])
            if got != want['out']:
                return False, 'output %r want %r' % (got[:400], want['out'][:400])
            return True, ''
        if act in ('compile', 'build'):
            ok = (p.returncode == 0)
            if ok != (want['exit'] == 0):
                return False, 'build rc=%d want %d: %s' % (p.returncode, want['exit'], p.stderr[:400])
            if want['exit'] == 0 and '-o' in cmd:
                # `go build -o X .` on a *main* package must leave a runnable
                # object behind: a toolchain that exits 0 having produced
                # nothing has compiled nothing, and the exit status alone
                # cannot tell those apart. Nothing is asserted about the
                # object's format -- a Go implementation is free to emit
                # bytecode for its own loader rather than a native binary --
                # and library packages are exempt because `go build` writes no
                # output for them, which is what gc does too.
                target = cmd[cmd.index('-o') + 1]
                ismain = any(re.search(r'^package\s+main\b', f['content'], re.M)
                             for f in case['files'])
                if ismain:
                    path = os.path.join(d, target)
                    if not os.path.exists(path) or os.path.getsize(path) == 0:
                        return False, 'build reported success but produced no %s' % target
            return True, ''
        if act == 'errorcheck':
            if p.returncode == 0:
                return False, 'compiled but must be rejected (errors expected on lines %s)' % want['lines']
            got = sorted({int(m.group(2)) for m in
                          (re.match(r'^\.?/?(?:.*/)?([^:\s]+\.go):(\d+)[:\s]', ln)
                           for ln in p.stderr.splitlines()) if m})
            if got != want['lines']:
                return False, 'diagnostics on lines %s want %s: %s' % (got, want['lines'], p.stderr[:400])
            return True, ''
        return False, 'unknown action %s' % act
    except subprocess.TimeoutExpired:
        return False, 'timeout'
    except Exception as e:                       # noqa: BLE001 - verifier must not crash
        return False, 'runner error: %r' % (e,)
    finally:
        shutil.rmtree(d, ignore_errors=True)


# Each case has a role, taken from its id:
#
#   regress/   a fixed sample of an earlier milestone's own cases, run at that
#              milestone's `go` directive
#   current/   the language suite run at *this* milestone's `go` directive
#   (neither)  this milestone's own new behaviour
#
# Go's compatibility promise means almost everything an earlier milestone
# tested still passes at the next release. If all three shared one weighted
# pool, a toolchain that carried the previous rung forward and implemented
# nothing new would score most of the step. So the step score is a product
# (2026-10-13): T1, this milestone's own work, times T2, keeping everything
# that already worked (regress/ + current/). Each factor is its own weighted
# fraction in [0, 1]. A toolchain that only carries the previous rung forward
# earns T2 but is multiplied by a near-zero T1. A step with no carried cases
# (step 1) scores on its own work alone.


def role_of(case_id):
    if case_id.startswith('regress/'):
        return 'compat'
    if case_id.startswith('current/'):
        return 'compat'
    return 'milestone'


def compute_score(cases, results):
    part = {'milestone': [0.0, 0.0], 'compat': [0.0, 0.0]}
    for c in cases:
        w = c.get('weight', 1.0)
        d = part[role_of(c['id'])]
        d[1] += w
        if results.get(c['id'], (False, ''))[0]:
            d[0] += w
    m_got, m_tot = part['milestone']
    c_got, c_tot = part['compat']
    if not m_tot and not c_tot:
        return 0.0
    if not c_tot:
        return m_got / m_tot
    if not m_tot:
        return c_got / c_tot
    return (m_got / m_tot) * (c_got / c_tot)



def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cases', default='/tests/cases.json')
    ap.add_argument('--jobs', type=int, default=7)
    ap.add_argument('--build-timeout', type=int, default=1200)
    ap.add_argument('--case-timeout', type=int, default=30)
    ap.add_argument('--deadline', type=int, default=2400,
                    help='whole-verifier wall clock, inside timeout_sec')
    a = ap.parse_args()
    # Every case runs as an unprivileged user, so the sources written for it
    # must be readable by that user -- and `open(path, 'w')` takes its mode
    # from the inherited umask. A verifier started with umask 0077 writes them
    # 0600 root-only, the case process cannot read its own source, and the
    # step scores 0.000000 with build.ok true and no case scored. Measured:
    # this package scored 1.000000 under umask 0022 and 0.000000 under 0077,
    # from the same image and the same tree. The umask is therefore set here
    # rather than inherited.
    os.umask(0o022)
    os.makedirs(LOGDIR, exist_ok=True)
    report = {'build': None, 'cases': {}, 'score': 0.0}
    t0 = time.time()
    global DEADLINE
    DEADLINE = t0 + a.deadline
    removed = prepare_tree()
    ok, msg = build(a.build_timeout)
    report['build'] = {'ok': ok, 'log': msg[-4000:], 'removed_products': removed}
    if not ok:
        report['score'] = 0.0
        json.dump(report, open(os.path.join(LOGDIR, 'report.json'), 'w'), indent=1)
        print(json.dumps({'reward': 0.0}))
        print('BUILD FAILED:', msg[:2000], file=sys.stderr)
        return 0.0
    wrapped = wrap_check()
    if wrapped:
        report['score'] = 0.0
        report['build']['wrapped'] = wrapped
        json.dump(report, open(os.path.join(LOGDIR, 'report.json'), 'w'), indent=1)
        print('WRAPPED TOOLCHAIN: %s is a Go-compiled binary; the toolchain must '
              'be built from this tree\'s own sources' % wrapped, file=sys.stderr)
        return 0.0
    bad_live = liveness()
    if bad_live:
        report['score'] = 0.0
        report['build']['liveness'] = False
        report['build']['liveness_failed'] = bad_live
        report['build']['liveness_detail'] = LIVE_DETAIL[-4:]
        json.dump(report, open(os.path.join(LOGDIR, 'report.json'), 'w'), indent=1)
        return 0.0
    # The toolchain has now run once (liveness). Scan /app again: a toolchain
    # shipped as a non-executable blob and unpacked under /app on first use was
    # not on disk at the pre-liveness scan but is now. liveness builds in a
    # throwaway /tmp dir, so a from-source toolchain still leaves nothing new
    # under /app here.
    wrapped = wrap_check()
    if wrapped:
        report['score'] = 0.0
        report['build']['wrapped'] = wrapped
        json.dump(report, open(os.path.join(LOGDIR, 'report.json'), 'w'), indent=1)
        print('WRAPPED TOOLCHAIN: %s is a Go-compiled binary; the toolchain must '
              'be built from this tree\'s own sources' % wrapped, file=sys.stderr)
        return 0.0
    cases = json.load(open(a.cases))
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {ex.submit(run_case, c, a.case_timeout): c['id'] for c in cases}
        for f in concurrent.futures.as_completed(futs):
            results[futs[f]] = f.result()
    score = compute_score(cases, results)
    report['cases'] = {k: {'pass': v[0], 'why': v[1][:500]} for k, v in results.items()}
    # Roles, so a reader can tell the milestone's own work from what the step
    # carries forward: `regress/` is an earlier version's sample, `current/` is
    # the language suite re-run under this version's directive, and everything
    # else is this milestone's own behaviour.
    roles = {}
    for c in cases:
        role = ('regression' if c['id'].startswith('regress/')
                else 'current-suite' if c['id'].startswith('current/')
                else 'milestone')
        _ = role_of(c['id'])
        w = c.get('weight', 1.0)
        d = roles.setdefault(role, {'n': 0, 'weight': 0.0, 'got': 0.0, 'npass': 0})
        d['n'] += 1
        d['weight'] += w
        if results.get(c['id'], (False, ''))[0]:
            d['got'] += w
            d['npass'] += 1
    for d in roles.values():
        d['score'] = round(d['got'] / d['weight'], 6) if d['weight'] else 0.0
        d['weight'] = round(d['weight'], 3)
        d['got'] = round(d['got'], 3)
    report['by_role'] = roles
    report['score'] = score
    report['npass'] = sum(1 for v in results.values() if v[0])
    report['ntotal'] = len(cases)
    report['seconds'] = round(time.time() - t0, 1)
    # only cases the deadline kept from running; a program whose own output
    # mentions a deadline is an ordinary failure
    late = sum(1 for v in results.values() if not v[0] and v[1] == DEADLINE_MSG)
    if late:
        report['deadline_hit'] = late
    json.dump(report, open(os.path.join(LOGDIR, 'report.json'), 'w'), indent=1)
    print('passed %d/%d in %.1fs' % (report['npass'], report['ntotal'], time.time() - t0), file=sys.stderr)
    for k, v in sorted(results.items()):
        if not v[0]:
            print('FAIL %s: %s' % (k, v[1][:300].replace('\n', ' | ')), file=sys.stderr)
    return score


if __name__ == '__main__':
    # An exception escaping main() must still produce a score this package
    # wrote, with a reason attached. test.sh has already written a provisional
    # 0.000000, so the chain survives either way -- but a zero whose only
    # explanation is a traceback in a log file is indistinguishable from a
    # defeated candidate, and that is the distinction every blind
    # classification depends on.
    try:
        s = main()
    except BaseException as e:                   # noqa: BLE001
        import traceback
        s = 0.0
        try:
            os.makedirs(LOGDIR, exist_ok=True)
            json.dump({'build': {'ok': False, 'log': 'verifier crashed'},
                       'cases': {}, 'score': 0.0,
                       'verifier_error': traceback.format_exc()[-4000:]},
                      open(os.path.join(LOGDIR, 'report.json'), 'w'), indent=1)
        except Exception:
            pass
        traceback.print_exc(file=sys.stderr)
    with open('/logs/verifier/score', 'w') as f:
        f.write('%.6f' % s)
