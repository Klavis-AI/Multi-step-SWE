#!/usr/bin/env python3
"""Self-check runner for the conformance corpus in this workspace.

Usage:  python3 /app/conformance/selfcheck.py [--jobs N] [--filter SUBSTR]

Each case is a program from the Go project's own `test/` directory together
with the behaviour the reference toolchain produces for it. The runner drives
your toolchain the way /app/SPEC/toolchain.md describes: it builds a module
directory containing the program and a go.mod with the case's `go` directive,
then runs `$GOROOT/bin/go`.
"""
import os, re, sys, json, shutil, tempfile, argparse, subprocess
import resource
import concurrent.futures

GOROUTINE_RE = re.compile(r'^goroutine \d+ \[')
EXITSTATUS_RE = re.compile(r'^exit status \d+$')


def normalize(out):
    res = []
    for line in out.splitlines():
        if GOROUTINE_RE.match(line):
            break
        if line.startswith('[signal ') or EXITSTATUS_RE.match(line) or line.startswith('# '):
            continue
        res.append(line)
    return '\n'.join(res) + ('\n' if res else '')


# The per-case ceilings that /app/SPEC/toolchain.md section 7 states.
CASE_AS_LIMIT = 4 * 1024 * 1024 * 1024
CASE_CPU_LIMIT = 300


def _limits():
    resource.setrlimit(resource.RLIMIT_AS, (CASE_AS_LIMIT, CASE_AS_LIMIT))
    resource.setrlimit(resource.RLIMIT_CPU, (CASE_CPU_LIMIT, CASE_CPU_LIMIT))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run_case(goroot, case, timeout):
    d = tempfile.mkdtemp(prefix='gocase')
    try:
        for fl in case['files']:
            p = os.path.join(d, fl['name'])
            if os.path.dirname(p):
                os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'w').write(fl['content'])
        if case.get('gomod'):
            open(os.path.join(d, 'go.mod'), 'w').write(case['gomod'])
        env = dict(os.environ)
        env['GOROOT'] = goroot
        env['GOPATH'] = os.path.join(d, '.gopath')
        env['GOFLAGS'] = ''
        env['GO111MODULE'] = 'on'
        for k, v in case.get('env', {}).items():
            env[k] = v.replace('$DIR', d)
        cmd = case.get('cmd') or ['run', '.']
        p = subprocess.run([os.path.join(goroot, 'bin', 'go')] + cmd, cwd=d, env=env,
                           capture_output=True, text=True, timeout=case.get('timeout', timeout),
                           preexec_fn=_limits)
        want, act = case['want'], case['action']
        if act == 'run':
            if p.returncode != want['exit']:
                return False, 'exit %d want %d: %s' % (p.returncode, want['exit'], (p.stdout + p.stderr)[:300])
            got = normalize(p.stdout + p.stderr)
            if case.get('normalize_durations'):
                got = re.sub(r'\d+\.\d+s', '0.00s', got)
            if got != want['out']:
                return False, 'output %r want %r' % (got[:300], want['out'][:300])
            return True, ''
        if act in ('compile', 'build'):
            if (p.returncode == 0) != (want['exit'] == 0):
                return False, 'rc=%d want %d: %s' % (p.returncode, want['exit'], p.stderr[:300])
            return True, ''
        if act == 'errorcheck':
            if p.returncode == 0:
                return False, 'compiled but must be rejected on lines %s' % want['lines']
            got = sorted({int(m.group(2)) for m in
                          (re.match(r'^\.?/?(?:.*/)?([^:\s]+\.go):(\d+)[:\s]', ln) for ln in p.stderr.splitlines()) if m})
            if got != want['lines']:
                return False, 'diagnostics on %s want %s: %s' % (got, want['lines'], p.stderr[:300])
            return True, ''
        return False, 'unknown action'
    except subprocess.TimeoutExpired:
        return False, 'timeout'
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--goroot', default=os.environ.get('GOROOT', '/app/goroot'))
    ap.add_argument('--cases', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cases.json'))
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--timeout', type=int, default=30)
    ap.add_argument('--filter', default='')
    ap.add_argument('--show', type=int, default=25)
    a = ap.parse_args()
    cases = [c for c in json.load(open(a.cases)) if a.filter in c['id']]
    res = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {ex.submit(run_case, a.goroot, c, a.timeout): c['id'] for c in cases}
        for f in concurrent.futures.as_completed(futs):
            res[futs[f]] = f.result()
    npass = sum(1 for v in res.values() if v[0])
    shown = 0
    for k in sorted(res):
        if not res[k][0] and shown < a.show:
            print('FAIL %s: %s' % (k, res[k][1][:300].replace('\n', ' | ')))
            shown += 1
    print('passed %d/%d' % (npass, len(cases)))
    return 0 if npass == len(cases) else 1


sys.exit(main())
