#!/usr/bin/env python3
"""Replay a recorded corpus plan against a running control plane and score it.

Used unchanged by the visible conformance harness in the workspace and by the
scorer that grades the finished workspace; only the plan and the expectations differ.
"""
import json, re, os, signal, subprocess, sys, threading, time, shutil, glob, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import runner  # noqa: E402


def sh(c):
    return subprocess.run(c, shell=True, capture_output=True, text=True)


# Ceilings on everything this harness starts. A component that runs away is a
# failure of the implementation under test, not of the machine it runs on, so it
# is killed and the run continues. Both numbers are stated in SPEC/TEST_INTERFACE.md.
# Ceilings have to sit BELOW the container's own limit, and the sum of them does
# too: etcd and three components run at once, so a per-process ceiling that only
# the container could exceed lets the kernel kill the pod instead of the process.
# The container is 16 GiB; these total 8 GiB.
RSS_LIMIT_MB = int(os.environ.get("KCP_RSS_LIMIT_MB", "2048"))
RSS_TOTAL_MB = int(os.environ.get("KCP_RSS_TOTAL_MB", "8192"))
RUN_LIMIT_SEC = int(os.environ.get("KCP_RUN_LIMIT_SEC", "2100"))


def _rss_mb(pid):
    try:
        with open("/proc/%d/statm" % pid) as f:
            return int(f.read().split()[1]) * (os.sysconf("SC_PAGE_SIZE") // 1024) // 1024
    except Exception:
        return 0


# Candidate code gets an environment the package chose, not the one the scorer
# happened to be launched with. Everything a control plane needs is on its
# command line; what it must not receive is the grader's own variables.
KEEP_ENV = ("PATH", "HOME", "LANG", "LC_ALL", "TZ", "TMPDIR", "HOSTNAME", "TERM")


def clean_env():
    env = {k: v for k, v in os.environ.items() if k in KEEP_ENV}
    env.setdefault("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
    env.setdefault("HOME", "/tmp")
    if os.environ.get("KCP_ENV_PROBE"):
        env["KCP_ENV_PROBE"] = os.environ["KCP_ENV_PROBE"]
    return env


KUBE_USERS = ("admin", "alice", "bob", "ctl", "sched")


def kubeconfig_text(sport, user):
    """One user's clientcmd v1 kubeconfig for the API server on `sport`."""
    return ("apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n"
            f"    server: https://127.0.0.1:{sport}\n"
            "    insecure-skip-tls-verify: true\n"
            f"contexts:\n- name: local\n  context:\n    cluster: local\n    user: {user}\n"
            f"current-context: local\nusers:\n- name: {user}\n  user:\n    token: {user}-token\n")


# The only directories kubectl's PATH names when the scorer runs it: root-owned
# system directories the components' uid cannot write.
ROOT_PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"


def root_client(sport, parent="/run"):
    """The kubeconfigs and environment for the kubectl the scorer runs as root.

    A kubeconfig can name a credential plugin for its client to execute, and
    kubectl also reads its cache from $HOME and looks for plugins on $PATH. The
    run tree's kube/ cannot be what root reads: its parent belongs to the
    components' uid, which can put a directory of its own in kube/'s place. So
    the files are written fresh, from the scorer's own values (the port it chose
    and the tokens it issued, as certs() writes them), into a new directory only
    root can enter, and kubectl gets a HOME in there and a PATH of system
    directories, and nothing else. Returns (kubeconfig_dir, env)."""
    top = tempfile.mkdtemp(prefix="kcp-client.", dir=parent)    # mode 0700
    kube, home = os.path.join(top, "kube"), os.path.join(top, "home")
    os.mkdir(kube, 0o700)
    os.mkdir(home, 0o700)
    for u in KUBE_USERS:
        fd = os.open(os.path.join(kube, u + ".kubeconfig"),
                     os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, "w") as fh:
            fh.write(kubeconfig_text(sport, u))
    env = {"PATH": ROOT_PATH, "HOME": home,
           "KUBECONFIG": os.path.join(kube, "admin.kubeconfig")}
    return kube, env


class Cluster:
    def __init__(self, base, bindir, eport, sport, apiserver_args=None):
        self.base, self.bin = base, bindir
        # Where the components' stdout/stderr go. The scorer points this at a
        # directory only root can write; the run tree itself is handed to the
        # components' uid, and root must not open files a component can replace.
        self.logdir = os.environ.get("KCP_LOG_DIR") or base
        self.eport, self.sport = eport, sport
        self.extra = apiserver_args or []
        self.p = {}
        self.killed = {}
        self.deadline = time.time() + RUN_LIMIT_SEC
        self._watch = None

    def _spawn(self, name, argv, logpath, append=False):
        """Start one component in its own process group, under the watchdog, and
        as an unprivileged user when one is named.

        The components have no business reading the grader's own files: they are
        a control plane, and everything they need is the flags, the certificates
        and etcd. Dropping to a uid that cannot read them is what makes "answer
        the request" the only way to answer it."""
        pre = None
        uid = os.environ.get("KCP_DROP_UID")
        if uid and os.getuid() == 0:
            uid = int(uid)
            gid = int(os.environ.get("KCP_DROP_GID", uid))
            def pre():                                    # noqa: E306
                os.setgroups([])
                os.setgid(gid)
                os.setuid(uid)
        # never through a symlink in the last component
        log = os.fdopen(os.open(logpath, os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW |
                                (os.O_APPEND if append else os.O_TRUNC), 0o644),
                        "a" if append else "w")
        p = subprocess.Popen(argv, stdout=log,
                             stderr=subprocess.STDOUT, start_new_session=True,
                             preexec_fn=pre, env=clean_env())
        self.p[name] = p
        self._start_watchdog()
        return p

    def _start_watchdog(self):
        if self._watch:
            return
        def loop():
            while True:
                time.sleep(2)
                over = time.time() > self.deadline
                live = [(n, p) for n, p in self.p.items() if p.poll() is None]
                total = sum(_rss_mb(p.pid) for _n, p in live)
                for name, p in live:
                    if p.poll() is not None:
                        continue
                    mine = _rss_mb(p.pid)
                    if over or mine > RSS_LIMIT_MB or total > RSS_TOTAL_MB:
                        self.killed[name] = ("deadline" if over else
                                             "rss" if mine > RSS_LIMIT_MB else "rss-total")
                        try:
                            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
                        except Exception:
                            pass
                if all(p.poll() is not None for p in self.p.values()):
                    return
        self._watch = threading.Thread(target=loop, daemon=True)
        self._watch.start()

    def certs(self):
        b = self.base
        os.makedirs(b + "/certs", exist_ok=True)
        os.makedirs(b + "/kube", exist_ok=True)
        sh(f"openssl genrsa -out {b}/certs/sa.key 2048")
        sh(f"openssl rsa -in {b}/certs/sa.key -pubout -out {b}/certs/sa.pub")
        sh(f"openssl req -x509 -newkey rsa:2048 -nodes -keyout {b}/certs/ca.key "
           f"-out {b}/certs/ca.crt -days 3650 -subj /CN=test-ca")
        sh(f"openssl req -x509 -newkey rsa:2048 -nodes -keyout {b}/certs/tls.key "
           f"-out {b}/certs/tls.crt -days 3650 -subj /CN=kubernetes "
           f"-addext subjectAltName=IP:127.0.0.1,DNS:localhost")
        open(b + "/certs/tokens.csv", "w").write(
            'admin-token,admin,uid-admin,"system:masters"\n'
            'alice-token,alice,uid-alice,"devs"\n'
            'bob-token,bob,uid-bob,"ops"\n'
            'ctl-token,system:kube-controller-manager,uid-kcm,"system:masters"\n'
            'sched-token,system:kube-scheduler,uid-sched,"system:masters"\n')
        for u in KUBE_USERS:
            open(f"{b}/kube/{u}.kubeconfig", "w").write(kubeconfig_text(self.sport, u))

    def start_etcd(self, etcd="etcd", etcdctl="etcdctl"):
        os.makedirs(self.base + "/etcd", exist_ok=True)
        # the components run as KCP_DROP_UID; a data directory created here,
        # after the run tree was handed over, would still belong to root
        uid = os.environ.get("KCP_DROP_UID")
        if uid and os.getuid() == 0:
            try:
                os.chown(self.base + "/etcd", int(uid),
                         int(os.environ.get("KCP_DROP_GID", uid)))
            except OSError:
                pass
        self._spawn("etcd", [etcd, "--data-dir", self.base + "/etcd",
             "--listen-client-urls", "http://127.0.0.1:%d" % self.eport,
             "--advertise-client-urls", "http://127.0.0.1:%d" % self.eport,
             "--listen-peer-urls", "http://127.0.0.1:%d" % (self.eport + 1),
             "--initial-advertise-peer-urls", "http://127.0.0.1:%d" % (self.eport + 1),
             "--initial-cluster", "default=http://127.0.0.1:%d" % (self.eport + 1),
             "--log-level", "error"], self.logdir + "/etcd.log")
        for _ in range(120):
            if sh(f"{etcdctl} --endpoints http://127.0.0.1:{self.eport} endpoint health").returncode == 0:
                return True
            time.sleep(0.5)
        return False

    def start_apiserver(self):
        b = self.base
        self._spawn("api", [self.bin + "/kube-apiserver",
             "--etcd-servers=http://127.0.0.1:%d" % self.eport,
             "--secure-port=%d" % self.sport, "--bind-address=127.0.0.1",
             f"--tls-cert-file={b}/certs/tls.crt",
             f"--tls-private-key-file={b}/certs/tls.key",
             f"--client-ca-file={b}/certs/ca.crt",
             f"--token-auth-file={b}/certs/tokens.csv",
             "--authorization-mode=RBAC",
             "--storage-media-type=application/json",
             f"--service-account-key-file={b}/certs/sa.pub",
             f"--service-account-signing-key-file={b}/certs/sa.key",
             "--service-account-issuer=https://kubernetes.default.svc",
             "--api-audiences=https://kubernetes.default.svc"] + self.extra,
            self.logdir + "/apiserver.log", append=True)
        for _ in range(180):
            r = sh("curl -sk -o /dev/null -w '%%{http_code}' -H 'Authorization: Bearer "
                   "admin-token' https://127.0.0.1:%d/healthz" % self.sport)
            if r.stdout.strip() == "200":
                return True
            if self.p["api"].poll() is not None:
                return False
            time.sleep(0.5)
        return False

    def start_controllers(self):
        b = self.base
        self._spawn("kcm", [self.bin + "/kube-controller-manager",
                            f"--kubeconfig={b}/kube/ctl.kubeconfig", "--leader-elect=false"],
                    self.logdir + "/kcm.log")
        self._spawn("sched", [self.bin + "/kube-scheduler",
                              f"--kubeconfig={b}/kube/sched.kubeconfig", "--leader-elect=false"],
                    self.logdir + "/scheduler.log")

    def stop(self):
        self.deadline = 0          # let the watchdog retire
        for k in ("sched", "kcm", "api", "etcd"):
            p = self.p.get(k)
            if p and p.poll() is None:
                p.terminate()
        time.sleep(1)
        for k in ("sched", "kcm", "api", "etcd"):
            p = self.p.get(k)
            if p and p.poll() is None:
                try:
                    os.killpg(os.getpgid(p.pid), signal.SIGKILL)
                except Exception:
                    p.kill()


def replay(cl, plan_file, kubectl, etcdctl, kubeconfig_dir=None, kubectl_env=None):
    """kubeconfig_dir/kubectl_env: what kubectl reads, from root_client() when
    the caller is root; by default the run tree's kube/ and the caller's env."""
    doc = json.load(open(plan_file))

    def restart_cb(op):
        p = cl.p.get("api")
        if p:
            p.terminate()
            try:
                p.wait(timeout=30)
            except Exception:
                p.kill()
        return cl.start_apiserver()

    tokens = {"admin": "admin-token", "alice": "alice-token", "bob": "bob-token",
              "anon": "", "bogus": "no-such-token"}
    t = runner.Target("127.0.0.1", cl.sport, tokens, kubectl=kubectl, etcdctl=etcdctl,
                      etcd_endpoint="http://127.0.0.1:%d" % cl.eport,
                      restart_cb=restart_cb,
                      kubeconfig_dir=kubeconfig_dir or (cl.base + "/kube"),
                      kubectl_env=kubectl_env)
    r = runner.Runner(t, cl.base)
    return doc, r.run(doc["ops"])


_RS_KEY = re.compile(r"^(/registry/replicasets/[^/]+/.+)-([0-9a-z]{5,10})$")


def _scrub_key(k):
    """A ReplicaSet a Deployment owns is named after the deployment plus the
    pod-template hash, which is an implementation detail of the controller."""
    m = _RS_KEY.match(k)
    return (m.group(1) + "-<h>") if m else k


def _agg_groups_filter(names):
    """Aggregated discovery describes the same surface as /api and /apis, so the
    group list is held to the same required subset on both sides."""
    import runner as _r
    gvs = _r._surface()
    groups = {gv.split("/")[0] for gv in gvs if "/" in gv}
    return [n for n in names
            if (isinstance(n, str) and (n in groups or _r._dynamic_group(n)))]


# Case ids whose placement is a tie-break rather than a constraint: several nodes
# fit, so which one the scheduler picks is not a property of the request. Every
# other scheduler case has exactly one feasible node and is compared exactly.
_TIE_BREAK_IDS = {"S17.fits-anywhere"}
_TIE_BREAK_NODE = False


def scrub_for(cid):
    global _TIE_BREAK_NODE
    _TIE_BREAK_NODE = cid in _TIE_BREAK_IDS


_TOKEN_VOL = re.compile(r"kube-api-access-[a-z0-9]{5}")


def _agg_resource_filter(key, entry):
    """Aggregated discovery lists the same subresources as /apis, so it is held to
    the same required subset on both sides."""
    import runner as _r
    want = key[len("__agg__"):].split("|")
    gv = ("%s/%s" % (want[0], want[1])) if want[0] else want[1]
    allowed = _r._surface().get(gv)
    if allowed is None or not isinstance(entry.get("subresources"), list):
        return _final_scrub(entry)
    res = entry.get("resource")
    subs = [s for s in entry["subresources"]
            if "%s/%s" % (res, s.get("subresource")) in allowed]
    subs.sort(key=lambda s: s.get("subresource", ""))
    return _final_scrub(dict(entry, subresources=subs))


def _final_scrub(o):
    """Values that depend on where the control plane happens to run, plus the
    surface filtering that discovery answers are compared under."""
    if isinstance(o, dict):
        out = {}
        for k, v in o.items():
            if k == "serverAddress":
                out[k] = "<alloc>"
            elif k == "spec|nodeName" and isinstance(v, str) and _TIE_BREAK_NODE:
                out[k] = "<node>"
            elif k == "nodeName" and isinstance(v, str):
                # Which of several equally-good nodes the scheduler picks is a
                # tie-break, so an object's own nodeName is not compared; the
                # scheduler stratum asserts placement through its own picks.
                out[k] = "<node>"
            elif k == "__agggroups__" and isinstance(v, list):
                out[k] = _agg_groups_filter(v)
            elif k.startswith("__agg__") and isinstance(v, dict):
                out[k] = _agg_resource_filter(k, v)
            elif k == "keys" and isinstance(v, list):
                out[k] = [_scrub_key(x) if isinstance(x, str) else _final_scrub(x)
                          for x in v]
            else:
                out[k] = _final_scrub(v)
        return out
    if isinstance(o, list):
        return [_final_scrub(v) for v in o]
    if isinstance(o, str):
        # the projected token volume's generated name, in client text output too
        return _TOKEN_VOL.sub("kube-api-access-<gen>", o)
    return o


def score(doc, obs, expected):
    per, fails = {}, []
    for st, ids in sorted(doc["strata"].items()):
        ok = 0
        for cid in ids:
            scrub_for(cid)
            if json.dumps(_final_scrub(expected.get(cid)), sort_keys=True) == \
                    json.dumps(_final_scrub(obs.get(cid)), sort_keys=True):
                ok += 1
            else:
                fails.append(cid)
        per[st] = (ok, len(ids))
    return per, fails
