"""Execute an ordered corpus of operations against a Kubernetes control plane and
return one observation per scored case.

The same runner is used twice: once at authoring time against the real upstream
release (producing the recorded expectations) and once at scoring time against the
candidate.  Identical code on both sides is what makes the comparison meaningful.
"""
import json, os, re, ssl, subprocess, sys, time
import http.client

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from normalize import normalize_object, normalize_status  # noqa: E402

HASH_LABELS = ("pod-template-hash", "controller-uid", "batch.kubernetes.io/controller-uid")
HASH_RE = re.compile(r"[0-9a-z]{5,12}")


class Target:
    def __init__(self, host, port, tokens, kubectl=None, etcdctl=None, etcd_endpoint=None,
                 restart_cb=None, kubeconfig_dir=None, kubectl_env=None):
        self.host, self.port, self.tokens = host, int(port), tokens
        self.kubectl, self.etcdctl, self.etcd_endpoint = kubectl, etcdctl, etcd_endpoint
        self.restart_cb = restart_cb
        self.kubeconfig_dir = kubeconfig_dir
        # The whole environment kubectl runs with, or None for the caller's own.
        # A client finds its config, its cache and its plugins through what its
        # environment names, so a caller that runs kubectl as root passes a
        # fixed one rather than inheriting whatever it was started with.
        self.kubectl_env = kubectl_env
        self._ctx = ssl.create_default_context()
        self._ctx.check_hostname = False
        self._ctx.verify_mode = ssl.CERT_NONE

    def conn(self, timeout=60):
        return http.client.HTTPSConnection(self.host, self.port, context=self._ctx,
                                           timeout=timeout)

    def request(self, method, path, body=None, headers=None, user="admin", timeout=60,
                raw_body=None):
        h = {"Accept": "application/json", "Authorization": "Bearer " + self.tokens[user]}
        if headers:
            h.update(headers)
        data = raw_body
        if data is None and body is not None:
            data = json.dumps(body).encode()
            h.setdefault("Content-Type", "application/json")
        c = self.conn(timeout)
        try:
            c.request(method, path, body=data, headers=h)
            r = c.getresponse()
            payload = r.read()
            return r.status, dict(r.getheaders()), payload
        finally:
            c.close()


def _collect_hashes(obj, out):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in ("labels", "matchLabels", "selector") and isinstance(v, dict):
                for lk, lv in v.items():
                    if lk in HASH_LABELS and isinstance(lv, str):
                        out.add(lv)
            _collect_hashes(v, out)
    elif isinstance(obj, list):
        for v in obj:
            _collect_hashes(v, out)


def _apply_hashes(text, hashes):
    for hv in sorted(hashes, key=len, reverse=True):
        if hv:
            text = text.replace(hv, "<h>")
    return text


def _anon_names(obj):
    """Replace server-generated object names (generateName + random suffix) with a
    placeholder, using the generateName that the object still carries."""
    if isinstance(obj, dict):
        md = obj.get("metadata")
        if isinstance(md, dict) and isinstance(md.get("generateName"), str) and \
                isinstance(md.get("name"), str) and md["name"].startswith(md["generateName"]):
            md["name"] = md["generateName"] + "<gen>"
        for v in obj.values():
            _anon_names(v)
    elif isinstance(obj, list):
        for v in obj:
            _anon_names(v)


def _decode(payload, kind_hint, status):
    try:
        return json.loads(payload.decode("utf-8"))
    except Exception:
        return {"__raw__": payload.decode("utf-8", "replace")[:4000]}


_STATE = {"hashes": set(), "names": {}}

_SURFACE = None


def _surface():
    """The required API surface: discovery answers are compared against this
    documented subset, so group-versions outside it are filtered out on both
    sides of the comparison."""
    global _SURFACE
    if _SURFACE is None:
        path = os.environ.get("KCP_SURFACE") or os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "surface.json")
        try:
            doc = json.load(open(path))
        except Exception:
            doc = {"groupVersions": {}, "optionalGroupVersions": {}}
        gvs = {}
        for b in ("groupVersions", "optionalGroupVersions"):
            for gv, entries in (doc.get(b) or {}).items():
                gvs.setdefault(gv, set()).update(e["name"] for e in entries)
        _SURFACE = gvs
    return _SURFACE


def _dynamic_group(name):
    return name.endswith(".example.com") or ".example.com" in name


def _filter_discovery(doc, path):
    gvs = _surface()
    groups = {gv.split("/")[0] for gv in gvs if "/" in gv}
    if isinstance(doc, dict) and doc.get("kind") == "APIGroupList":
        keep = []
        for g in doc.get("groups") or []:
            nm = g.get("name", "")
            if nm not in groups and not _dynamic_group(nm):
                continue
            if nm in groups:
                vs = [v for v in g.get("versions") or []
                      if v.get("groupVersion") in gvs]
                if not vs:
                    continue
                g = dict(g, versions=vs)
            keep.append(g)
        doc = dict(doc, groups=keep)
    elif isinstance(doc, dict) and doc.get("kind") == "APIResourceList":
        gv = doc.get("groupVersion", "")
        allowed = gvs.get(gv)
        res = []
        for r in doc.get("resources") or []:
            if allowed is not None and r.get("name") not in allowed:
                continue
            r = {k: v for k, v in r.items() if k != "storageVersionHash"}
            res.append(r)
        res.sort(key=lambda x: x.get("name", ""))
        doc = dict(doc, resources=res)
    elif path and path.startswith("/openapi/v3") and isinstance(doc, dict) and "paths" in doc:
        keep = {}
        for k, v in (doc.get("paths") or {}).items():
            if k in ("api", "apis"):
                pass
            elif k.startswith("api/"):
                if k[4:] not in gvs:
                    continue
            elif k.startswith("apis/"):
                rest = k[5:]
                if rest not in gvs and rest not in groups and not _dynamic_group(rest):
                    continue
            else:
                continue
            if isinstance(v, dict) and isinstance(v.get("serverRelativeURL"), str):
                v = dict(v, serverRelativeURL=re.sub(r"\?hash=[^&]*", "?hash=<h>",
                                                     v["serverRelativeURL"]))
            keep[k] = v
        doc = dict(doc, paths=keep)
    return doc


def _collect_names(obj, out):
    if isinstance(obj, dict):
        md = obj.get("metadata")
        if isinstance(md, dict) and isinstance(md.get("generateName"), str) and \
                isinstance(md.get("name"), str) and md["name"].startswith(md["generateName"]):
            out[md["name"]] = md["generateName"] + "<gen>"
        for v in obj.values():
            _collect_names(v, out)
    elif isinstance(obj, list):
        for v in obj:
            _collect_names(v, out)


def _deep_sort(o):
    if isinstance(o, dict):
        return {k: _deep_sort(v) for k, v in o.items()}
    if isinstance(o, list):
        return sorted((_deep_sort(v) for v in o),
                      key=lambda x: json.dumps(x, sort_keys=True))
    return o


def _prep(doc, status, sort_items=False, deep_sort=False, path=None):
    """Common post-processing of one decoded API response."""
    if not isinstance(doc, dict):
        return doc
    if doc.get("kind") == "Status" and status >= 300:
        return normalize_status(doc)
    doc = _filter_discovery(doc, path)
    _collect_hashes(doc, _STATE["hashes"])
    _collect_names(doc, _STATE["names"])
    _anon_names(doc)
    doc = normalize_object(doc)
    if deep_sort:
        doc = _deep_sort(doc)
    elif sort_items and isinstance(doc.get("items"), list):
        doc["items"] = sorted(doc["items"], key=lambda x: json.dumps(x, sort_keys=True))
    txt = json.dumps(doc, sort_keys=True)
    txt = re.sub(r"kube-api-access-[a-z0-9]{5}", "kube-api-access-<gen>", txt)
    for nm, rep in sorted(_STATE["names"].items(), key=lambda kv: -len(kv[0])):
        txt = txt.replace(nm, rep)
    txt = _apply_hashes(txt, _STATE["hashes"])
    return json.loads(txt)


class Runner:
    def __init__(self, target, workdir):
        self.t = target
        self.workdir = workdir
        self.obs = {}
        self.vars = {}

    # ---------- op implementations -------------------------------------------------
    def op_http(self, op):
        path = self._expand(op["path"])
        body = op.get("body")
        headers = dict(op.get("headers") or {})
        st, hdrs, payload = self.t.request(op.get("method", "GET"), path, body=body,
                                           headers=headers, user=op.get("as", "admin"),
                                           timeout=op.get("timeout", 60),
                                           raw_body=(op["raw_body"].encode() if isinstance(op.get("raw_body"), str) else op.get("raw_body")))
        ct = hdrs.get("Content-Type", "")
        doc = _decode(payload, op.get("expect"), st)
        doc = _prep(doc, st, sort_items=op.get("sort_items", False),
                    deep_sort=op.get("deep_sort", False), path=path)
        if op.get("pick"):
            doc = _pick(doc, op["pick"])
        rec = {"status": st, "body": doc}
        if op.get("keep_ct"):
            rec["content_type"] = ct.split(";")[0].strip()
        if op.get("save"):
            try:
                full = json.loads(payload.decode())
                for k, expr in op["save"].items():
                    self.vars[k] = _jget(full, expr)
            except Exception:
                pass
        return rec

    def op_wait(self, op):
        """Poll until `until` holds (or timeout), then record the final observation."""
        path = self._expand(op["path"])
        deadline = time.time() + op.get("timeout", 60)
        last = None
        while True:
            st, hdrs, payload = self.t.request("GET", path, user=op.get("as", "admin"))
            try:
                full = json.loads(payload.decode())
            except Exception:
                full = {}
            last = (st, full)
            if _cond(full, op.get("until")):
                break
            if time.time() > deadline:
                break
            time.sleep(op.get("interval", 0.5))
        st, full = last
        doc = _prep(full, st, sort_items=op.get("sort_items", True),
                    deep_sort=op.get("deep_sort", False))
        if op.get("pick"):
            doc = _pick(doc, op["pick"])
        return {"status": st, "body": doc}

    def op_watch(self, op):
        """Open a watch, run the embedded mutations, then drain whatever the
        server streamed.  Single-threaded: the events sit in the socket buffer
        while the mutations run, and are read back afterwards."""
        path = self._expand(op["path"])
        c = self.t.conn(timeout=op.get("timeout", 20))
        sock = None
        try:
            c.request("GET", path,
                      headers={"Accept": "application/json",
                               "Authorization": "Bearer " + self.t.tokens[op.get("as", "admin")]})
            sock = c.sock
            r = c.getresponse()
            status = r.status
            if status != 200:
                body = _prep(_decode(r.read(), None, status), status)
                return {"status": status, "body": body}
            for sub in op.get("ops", []):
                self._run_one(sub, record=False)
            time.sleep(op.get("settle", 2.0))
            events = []
            buf = b""
            try:
                (c.sock or sock).settimeout(op.get("drain", 1.5))
                deadline = time.time() + op.get("drain_total", 3.0)
                while time.time() < deadline:
                    chunk = r.read1(65536) if hasattr(r, "read1") else r.read(1)
                    if not chunk:
                        break
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        line = line.strip()
                        if line:
                            try:
                                events.append(json.loads(line.decode()))
                            except Exception:
                                pass
            except Exception:
                pass
        finally:
            try:
                c.close()
            except Exception:
                pass
        out = []
        for ev in events:
            t = ev.get("type")
            o = ev.get("object") or {}
            if t == "BOOKMARK":
                o = {"kind": o.get("kind")}
            elif op.get("full_objects"):
                o = _prep(o, 200)
            else:
                o = {"kind": o.get("kind"),
                     "name": (o.get("metadata") or {}).get("name"),
                     "ns": (o.get("metadata") or {}).get("namespace")}
            out.append({"type": t, "object": o})
        if op.get("drop_bookmarks"):
            out = [e for e in out if e["type"] != "BOOKMARK"]
        return {"status": status, "events": out}

    def op_kubectl(self, op):
        args = [self._expand(a) for a in op["args"]]
        kc = os.path.join(self.t.kubeconfig_dir, op.get("kubeconfig", "admin.kubeconfig"))
        cmd = [self.t.kubectl, "--kubeconfig", kc] + args
        env = dict(os.environ) if self.t.kubectl_env is None else dict(self.t.kubectl_env)
        env["KUBECTL_EXTERNAL_DIFF"] = "diff -u"
        p = subprocess.run(cmd, capture_output=True, timeout=op.get("timeout", 120),
                           input=(op.get("stdin") or "").encode(), env=env)
        out = p.stdout.decode("utf-8", "replace")
        err = p.stderr.decode("utf-8", "replace")
        rec = {"exit": p.returncode}
        if op.get("json_out"):
            try:
                doc = json.loads(out)
                rec["body"] = _prep(doc, 200, sort_items=op.get("sort_items", False))
            except Exception:
                rec["body"] = {"__raw__": out[:4000]}
        else:
            rec["stdout"] = _norm_text(out, op)
        if op.get("keep_stderr"):
            rec["stderr"] = _norm_text(err, op)
        if op.get("pick") and isinstance(rec.get("body"), dict):
            rec["body"] = _pick(rec["body"], op["pick"])
        return rec

    def op_kubelet(self, op):
        """Stand in for the node agent: move the pods selected by `selector` into
        `phase` and give them an address, exactly as a kubelet would report."""
        ns = op["namespace"]
        path = "/api/v1/namespaces/%s/pods?labelSelector=%s" % (ns, op.get("selector", ""))
        # A kubelet only ever sees pods the scheduler already placed on its node.
        deadline = time.time() + op.get("wait", 60)
        items = []
        while True:
            st, _h, payload = self.t.request("GET", path)
            try:
                items = json.loads(payload.decode()).get("items", [])
            except Exception:
                items = []
            items = [p for p in items if not p["metadata"].get("deletionTimestamp")]
            if items and all(p["spec"].get("nodeName") for p in items):
                break
            if time.time() > deadline:
                break
            time.sleep(0.5)
        items = [p for p in items if p["spec"].get("nodeName")]
        phase = op.get("phase", "Running")
        done = 0
        for idx, po in enumerate(sorted(items, key=lambda p: p["metadata"]["name"])):
            nm = po["metadata"]["name"]
            body = {"status": {
                "phase": phase,
                "podIP": "10.244.0.%d" % (idx + 10),
                "podIPs": [{"ip": "10.244.0.%d" % (idx + 10)}],
                "hostIP": "10.0.0.1",
                "conditions": [
                    {"type": "Initialized", "status": "True"},
                    {"type": "Ready", "status": "True" if phase == "Running" else "False"},
                    {"type": "ContainersReady",
                     "status": "True" if phase == "Running" else "False"},
                    {"type": "PodScheduled", "status": "True"}],
                "containerStatuses": [
                    {"name": c["name"], "ready": phase == "Running",
                     "restartCount": 0, "image": c["image"], "imageID": "",
                     "state": ({"running": {"startedAt": "2024-01-01T00:00:00Z"}}
                               if phase == "Running" else
                               {"terminated": {"exitCode": 0, "reason": "Completed",
                                               "finishedAt": "2024-01-01T00:00:00Z",
                                               "startedAt": "2024-01-01T00:00:00Z"}})}
                    for c in po["spec"]["containers"]]}}
            s2, _h2, _p2 = self.t.request(
                "PATCH", "/api/v1/namespaces/%s/pods/%s/status" % (ns, nm), body=body,
                headers={"Content-Type": "application/merge-patch+json"})
            if s2 < 300:
                done += 1
        return {"patched": done}

    def op_etcd(self, op):
        cmd = [self.t.etcdctl, "--endpoints", self.t.etcd_endpoint, "get",
               op.get("prefix", "/registry/"), "--prefix", "--keys-only"]
        p = subprocess.run(cmd, capture_output=True, timeout=60)
        text = p.stdout.decode()
        for nm, rep in sorted(_STATE["names"].items(), key=lambda kv: -len(kv[0])):
            text = text.replace(nm, rep)
        text = _apply_hashes(text, _STATE["hashes"])
        keys = [l for l in text.split("\n") if l.strip()]
        pat = op.get("filter")
        if pat:
            rx = re.compile(pat)
            keys = [k for k in keys if rx.search(k)]
        return {"keys": sorted(keys)}

    def op_restart(self, op):
        ok = self.t.restart_cb(op) if self.t.restart_cb else False
        return {"restarted": bool(ok)}

    def op_sleep(self, op):
        time.sleep(op.get("seconds", 1))
        return {}

    # ---------- driver -------------------------------------------------------------
    def _expand(self, s):
        if not isinstance(s, str):
            return s
        for k, v in self.vars.items():
            s = s.replace("{{%s}}" % k, str(v))
        return s

    def _run_one(self, op, record=True):
        try:
            fn = getattr(self, "op_" + op["op"])
            rec = fn(op)
        except Exception as e:
            rec = {"error": "%s: %s" % (type(e).__name__, str(e)[:200])}
        if record and op.get("id"):
            self.obs[op["id"]] = rec
        return rec

    # ---- node agent: confirm graceful pod deletions --------------------------------
    def start_reaper(self):
        import threading
        self._reap_stop = threading.Event()

        def loop():
            while not self._reap_stop.is_set():
                try:
                    st, _h, payload = self.t.request("GET", "/api/v1/pods", timeout=10)
                    if st == 200:
                        for po in json.loads(payload.decode()).get("items", []):
                            md = po.get("metadata", {})
                            if md.get("deletionTimestamp"):
                                self.t.request(
                                    "DELETE",
                                    "/api/v1/namespaces/%s/pods/%s?gracePeriodSeconds=0"
                                    % (md["namespace"], md["name"]), timeout=10)
                except Exception:
                    pass
                self._reap_stop.wait(0.7)

        self._reaper = threading.Thread(target=loop, daemon=True)
        self._reaper.start()

    def stop_reaper(self):
        if getattr(self, "_reap_stop", None):
            self._reap_stop.set()

    def run(self, plan, progress=None, trace=None):
        self.start_reaper()
        for i, op in enumerate(plan):
            t0 = time.time()
            self._run_one(op)
            dt = time.time() - t0
            if trace and dt > trace:
                print("  slow op %d %s %s %.1fs" % (i, op.get("op"),
                                                    op.get("id") or op.get("path", "")[:60], dt),
                      flush=True)
            if progress and i % 25 == 0:
                progress(i, len(plan))
        self.stop_reaper()
        return self.obs


def _norm_text(s, op):
    s = s.replace("\r\n", "\n")
    for rx, rep in (op.get("sub") or []):
        s = re.sub(rx, rep, s)
    lines = [l.rstrip() for l in s.split("\n")]
    while lines and not lines[-1]:
        lines.pop()
    if op.get("sort_lines"):
        lines = sorted(lines)
    return "\n".join(lines)


def _jget(doc, expr):
    cur = doc
    for part in expr.split("|"):
        if part.endswith("]") and "[" in part:
            name, idx = part[:-1].split("[")
            cur = cur.get(name, []) if name else cur
            cur = cur[int(idx)] if isinstance(cur, list) and len(cur) > int(idx) else None
        else:
            cur = cur.get(part) if isinstance(cur, dict) else None
        if cur is None:
            return None
    return cur


def _pick(doc, paths):
    out = {}
    for p in paths:
        if p == "__groups__":
            out[p] = sorted((doc.get("paths") or {}).keys()) if isinstance(doc, dict) else None
            continue
        if p == "__names__":
            items = doc.get("items") if isinstance(doc, dict) else None
            out[p] = sorted((i.get("metadata") or {}).get("name") for i in items) \
                if isinstance(items, list) else None
            continue
        if p == "__count__":
            items = doc.get("items") if isinstance(doc, dict) else None
            out[p] = len(items) if isinstance(items, list) else None
            continue
        if p == "__agggroups__":
            it = doc.get("items") if isinstance(doc, dict) else None
            out[p] = sorted((g.get("metadata") or {}).get("name", "") for g in (it or [])) \
                if it is not None else None
            continue
        if p.startswith("__agg__"):
            want = p[len("__agg__"):].split("|")
            g, v, res = (want + ["", "", ""])[:3]
            found = None
            for grp in (doc.get("items") or []) if isinstance(doc, dict) else []:
                if (grp.get("metadata") or {}).get("name", "") != g:
                    continue
                for ver in grp.get("versions") or []:
                    if ver.get("version") != v:
                        continue
                    for r in ver.get("resources") or []:
                        if r.get("resource") == res:
                            found = r
            out[p] = found
            continue
        if p == "__gvs__":
            gs = doc.get("groups") if isinstance(doc, dict) else None
            out[p] = sorted("%s/%s" % (g.get("name"), v.get("version"))
                            for g in (gs or []) for v in (g.get("versions") or [])) \
                if gs is not None else None
            continue
        if p.endswith("|len"):
            v = _jget(doc, p[:-4]) if isinstance(doc, dict) else None
            out[p] = len(v) if isinstance(v, (list, dict, str)) else None
            continue
        v = _jget(doc, p) if isinstance(doc, dict) else None
        out[p] = v
    return out


def _cond(doc, until):
    if not until:
        return True
    for expr, want in until.items():
        got = _jget(doc, expr)
        if isinstance(want, dict) and "len" in want:
            got = len(got) if isinstance(got, (list, dict, str)) else -1
            want = want["len"]
        if got != want:
            return False
    return True
