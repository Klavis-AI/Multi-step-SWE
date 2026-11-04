#!/usr/bin/env python3
"""Score topic-based subscriptions.

Each scenario installs its topic and subscription, performs its writes, and then
asserts what arrived at a receiver this module hosts.  Every scenario contains
both writes that must notify and writes that must not, so neither "notify on
everything" nor "notify on nothing" passes one.

The unit list per scenario is deliberately fine-grained -- the handshake, the
bundle type, the SubscriptionStatus type, the event numbers, the focus
references, the entry shape for the content level, and the two operations -- so
a server that gets the matching right but the notification shape wrong is scored
for what it got right.
"""
import http.server, json, re, socketserver, threading, time, urllib.parse, sys, os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fhirhttp import Client

_LOCK = threading.Lock()
_GOT = []


class _RX(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        try:
            doc = json.loads(raw.decode("utf-8"))
        except Exception:
            doc = None
        with _LOCK:
            _GOT.append({"ctype": self.headers.get("Content-Type"), "bundle": doc,
                         "raw": raw.decode("utf-8", "replace")[:200000]})
        body = b'{"resourceType":"OperationOutcome","issue":[]}'
        try:
            self.send_response(200)
            self.send_header("Content-Type", "application/fhir+json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except Exception:
            pass          # the server may close first; the bundle is already kept

    def log_message(self, *a):
        pass

    def handle_one_request(self):
        try:
            http.server.BaseHTTPRequestHandler.handle_one_request(self)
        except Exception:
            self.close_connection = True


class _Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def start_receiver(port):
    s = _Server(("127.0.0.1", port), _RX)
    t = threading.Thread(target=s.serve_forever, daemon=True)
    t.start()
    return s


def _drain():
    with _LOCK:
        got = list(_GOT)
        del _GOT[:]
    return got


def _num(v):
    """`notificationEvent.eventNumber` is a `string` in R4B and an `integer64`
    in R5, which JSON carries as a string; a JSON integer is accepted too.  The
    value compared is the number either way."""
    if isinstance(v, str) and re.fullmatch(r"[0-9]+", v):
        return int(v)
    return repr(v) if isinstance(v, bool) else v


def _wait(want, timeout=15.0):
    """Notifications are delivered out of band, so wait for the expected count
    rather than assuming the write response implies delivery."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        with _LOCK:
            if len(_GOT) >= want:
                break
        time.sleep(0.2)
    return _drain()


def _events_of(bundles):
    evs = []
    for b in bundles:
        doc = b.get("bundle")
        if not isinstance(doc, dict):
            continue
        ent = doc.get("entry") or []
        if not ent:
            continue
        ss = (ent[0] or {}).get("resource") or {}
        if ss.get("type") == "handshake":
            continue
        for e in ss.get("notificationEvent") or []:
            ref = ((e.get("focus") or {}).get("reference") or "")
            evs.append((_num(e.get("eventNumber")), ref.split("/")[-2] if ref.count("/") >= 1 else "",
                        ref.split("/")[-1] if ref else ""))
    return evs


def run(root, corpus_path, port=18899, log=print, deadline=None):
    exp = json.load(open(corpus_path))
    endpoint = "http://127.0.0.1:%d/notify" % port
    srv = start_receiver(port)
    units = {}

    def U(sid, name, ok, note=""):
        units["%s/%s" % (sid, name)] = (bool(ok), 1.0)
        if not ok:
            log("  SUB miss %-28s %-22s %s" % (sid, name, note))

    try:
        for sc in exp["scenarios"]:
            if deadline is not None and time.monotonic() > deadline:
                U(sc["id"], "reached", False, "not reached before the deadline")
                continue
            base = "%s/%s" % (root.rstrip("/"),
                              "baseR5" if sc["release"] == "R5" else "baseR4B")
            c = Client(base, timeout=120)
            # Scenarios share one store, and a Subscription left active by an
            # earlier scenario keeps firing on this one's writes.  Clear both
            # resource types first, so each scenario observes only its own.
            for t in ("Subscription", "SubscriptionTopic"):
                st, _, body = c.send("GET", "/%s?_count=200" % t, None, {})
                try:
                    bundle = json.loads(body.decode("utf-8"))
                except Exception:
                    bundle = {}
                for e in bundle.get("entry") or []:
                    rid = ((e.get("resource") or {}).get("id"))
                    if rid:
                        c.send("DELETE", "/%s/%s" % (t, rid), None, {})
            _drain()
            sub = json.loads(json.dumps(sc["subscription"]).replace("@ENDPOINT@", endpoint))
            sid = sub["id"]
            st, _, _ = c.send("PUT", "/SubscriptionTopic/%s" % sc["topic"]["id"],
                              sc["topic"], {"Content-Type": "application/fhir+json"})
            U(sc["id"], "topic-stored", st in (200, 201), "PUT topic -> %s" % st)
            st, _, _ = c.send("PUT", "/Subscription/%s" % sid, sub,
                              {"Content-Type": "application/fhir+json"})
            U(sc["id"], "subscription-stored", st in (200, 201), "PUT sub -> %s" % st)

            want_hs = not sc["expect"].get("no_handshake") and not sc["expect"].get("no_delivery")
            hs = _wait(1, 8.0) if want_hs else _drain()
            if want_hs:
                kinds = [((b.get("bundle") or {}).get("entry") or [{}])[0]
                         .get("resource", {}).get("type") for b in hs]
                U(sc["id"], "handshake", "handshake" in kinds, "got %s" % kinds)
            st, _, body = c.send("GET", "/Subscription/%s" % sid, None, {})
            cur = json.loads(body.decode("utf-8")) if body else {}
            want_status = sc["expect"].get("subscription_status",
                                           "off" if sc["expect"].get("no_handshake") else "active")
            U(sc["id"], "lifecycle", cur.get("status") == want_status,
              "status %s, wanted %s" % (cur.get("status"), want_status))

            for method, path, body_doc in sc["writes"]:
                c.send(method, "/" + path, body_doc,
                       {"Content-Type": "application/fhir+json"} if body_doc else {})
            want_ev = [tuple(x) for x in sc["expect"]["events"]]
            got = _wait(1 if want_ev else 0, 12.0 if want_ev else 3.0)
            evs = _events_of(got)
            U(sc["id"], "events", [(t, i) for _, t, i in evs] == want_ev,
              "got %s, wanted %s" % ([(t, i) for _, t, i in evs], want_ev))
            if want_ev:
                U(sc["id"], "event-numbers", [n for n, _, _ in evs] == list(range(1, len(evs) + 1)),
                  "numbers %s" % [n for n, _, _ in evs])
                first = next((b["bundle"] for b in got
                              if isinstance(b.get("bundle"), dict)
                              and ((b["bundle"].get("entry") or [{}])[0].get("resource") or {})
                              .get("type") != "handshake"), None)
                U(sc["id"], "bundle-type",
                  (first or {}).get("type") == sc["expect"]["bundle_type"],
                  "type %s" % (first or {}).get("type"))
                ss = ((first or {}).get("entry") or [{}])[0].get("resource") or {}
                U(sc["id"], "status-type", ss.get("type") == sc["expect"]["status_type"],
                  "type %s" % ss.get("type"))
                U(sc["id"], "status-no-fullurl",
                  "fullUrl" not in (((first or {}).get("entry") or [{}])[0]),
                  "the SubscriptionStatus entry must have no fullUrl")
                lvl = sc["expect"]["content"]
                # A server may send one notification per event or one carrying
                # several, so the entry count is checked across every event
                # notification rather than inside one of them: one
                # SubscriptionStatus per notification, plus one entry per event
                # unless the content level is `empty`.
                notes = [b["bundle"] for b in got
                         if isinstance(b.get("bundle"), dict)
                         and ((b["bundle"].get("entry") or [{}])[0].get("resource") or {})
                         .get("type") != "handshake"]
                total = sum(len(n.get("entry") or []) for n in notes)
                if lvl == "empty":
                    U(sc["id"], "content-empty", total == len(notes),
                      "%d entries over %d notifications, wanted %d"
                      % (total, len(notes), len(notes)))
                else:
                    U(sc["id"], "content-entries", total == len(notes) + len(want_ev),
                      "%d entries over %d notifications for %d events"
                      % (total, len(notes), len(want_ev)))
                    payload = [e for n in notes for e in (n.get("entry") or [])[1:]]
                    if payload:
                        e1 = payload[0]
                        U(sc["id"], "content-fullurl",
                          isinstance(e1.get("fullUrl"), str)
                          and e1["fullUrl"].startswith("http")
                          and e1["fullUrl"].endswith("/%s/%s" % want_ev[0]),
                          "fullUrl %s" % e1.get("fullUrl"))
                        has = isinstance(e1.get("resource"), dict)
                        U(sc["id"], "content-resource",
                          has == (lvl == "full-resource"),
                          "resource present=%s at content=%s" % (has, lvl))
                    else:
                        U(sc["id"], "content-fullurl", False, "no payload entry at all")
                        U(sc["id"], "content-resource", False, "no payload entry at all")
            else:
                U(sc["id"], "silence", not evs, "expected no notification, got %s" % evs)

            st, _, body = c.send("GET", "/Subscription/%s/$status" % sid, None, {})
            try:
                sb = json.loads(body.decode("utf-8"))
            except Exception:
                sb = {}
            ss = ((sb.get("entry") or [{}])[0].get("resource") or {})
            U(sc["id"], "status-op", st == 200 and sb.get("type") == "searchset"
              and ss.get("resourceType") == "SubscriptionStatus"
              and ss.get("type") == "query-status"
              and ss.get("eventsSinceSubscriptionStart") in (len(want_ev), str(len(want_ev))),
              "$status -> %s %s n=%s" % (st, ss.get("type"),
                                         ss.get("eventsSinceSubscriptionStart")))
            if sc["expect"].get("check_events_op"):
                st, _, body = c.send("GET", "/Subscription/%s/$events" % sid, None, {})
                try:
                    eb = json.loads(body.decode("utf-8"))
                except Exception:
                    eb = {}
                ess = ((eb.get("entry") or [{}])[0].get("resource") or {})
                nums = [_num(e.get("eventNumber")) for e in ess.get("notificationEvent") or []]
                U(sc["id"], "events-op", st == 200 and ess.get("type") == "query-event"
                  and nums == list(range(1, len(want_ev) + 1)),
                  "$events -> %s %s %s" % (st, ess.get("type"), nums))
                st, _, body = c.send("GET",
                                     "/Subscription/%s/$events?eventsSinceNumber=2"
                                     % sid, None, {})
                try:
                    eb = json.loads(body.decode("utf-8"))
                except Exception:
                    eb = {}
                ess = ((eb.get("entry") or [{}])[0].get("resource") or {})
                nums = [_num(e.get("eventNumber")) for e in ess.get("notificationEvent") or []]
                U(sc["id"], "events-op-since", nums == list(range(2, len(want_ev) + 1)),
                  "$events?since=2 -> %s" % nums)
            st, _, _ = c.send("GET", "/Subscription/no-such-id/$status", None, {})
            U(sc["id"], "status-op-404", st == 404, "$status on a missing id -> %s" % st)
    finally:
        # Everything this component wrote comes back out.  The scenarios share a
        # store with the REST corpora that run after them, and an Encounter left
        # behind turns up in another component's search results as an extra id --
        # a miss in a component that is not at fault.
        for sc in exp["scenarios"]:
            base = "%s/%s" % (root.rstrip("/"),
                              "baseR5" if sc["release"] == "R5" else "baseR4B")
            c = Client(base, timeout=30)
            for method, path, _ in sc["writes"]:
                c.send("DELETE", "/" + path, None, {})
            c.send("DELETE", "/SubscriptionTopic/%s" % sc["topic"]["id"], None, {})
            c.send("DELETE", "/Subscription/%s" % sc["subscription"]["id"], None, {})
        srv.shutdown()
        srv.server_close()
    return units


if __name__ == "__main__":
    u = run(sys.argv[1], sys.argv[2])
    print("SUBSCRIPTIONS: %d/%d" % (sum(1 for v, _ in u.values() if v), len(u)))
