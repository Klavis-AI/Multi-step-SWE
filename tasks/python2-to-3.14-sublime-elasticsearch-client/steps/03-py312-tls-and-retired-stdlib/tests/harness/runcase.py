# -*- coding: utf-8 -*-
"""Run exactly one case, in its own interpreter, and print its observation.

    python -m harness.runcase <tree> <case-id>

One process per case is what makes the suite deterministic: no connection
pool, module-level cache, installed warning filter or logging handler survives
from one scored input to the next.
"""
import io
import json
import logging
import os
import sys
import traceback
import warnings

from . import canon, env, normalize, registry, server

# Loggers the tree writes to. Their records are part of the observation: the
# elasticsearch trace logger reproduces the request as a curl command, which is
# the most sensitive single view of the str/bytes boundary in the whole tree.
WATCHED = ("elasticsearch", "elasticsearch.trace", "urllib3", "requests",
           "segment", "analytics")

# Warnings that are part of a library's own documented contract and must
# survive the migration unchanged. Everything else a run emits —
# DeprecationWarning, ResourceWarning, SyntaxWarning, EncodingWarning and
# friends — is a property of the *interpreter*, not of the tree's behaviour:
# it is supposed to change as the ladder is climbed, and scoring it here would
# make a correct migration fail an inherited behaviour case.
#
# They are scored statically instead, by the rung that retires the construct
# raising them: `compiles-without-warnings` compiles every module and records
# what the compiler itself says (invalid string escapes at 03, PEP 765 at 07),
# and `no-naive-utc`, `no-dead-stdlib-312` and `no-dead-stdlib-313` name the
# runtime deprecations directly. There is no second scored replay under
# `-X dev -W error::DeprecationWarning`: promoting the warning to an exception
# changes the answer the case returns, so every case touching a deprecated API
# would have to be flipped at the rung that deprecates it and flipped back at
# the rung that removes it -- and it would spend a second full replay's wall
# clock measuring a property the AST gates already measure exactly.
CONTRACT_WARNINGS = frozenset({
    "UserWarning",            # elasticsearch's own verify_certs=False warning
    "HTTPWarning",            # urllib3.exceptions base
    "SecurityWarning",
    "InsecureRequestWarning",
    "InsecurePlatformWarning",
    "SNIMissingWarning",
    "SystemTimeWarning",
    "DependencyWarning",
})


class Collector(logging.Handler):
    def __init__(self):
        logging.Handler.__init__(self, level=logging.DEBUG)
        self.records = []

    def emit(self, record):
        try:
            msg = record.getMessage()
        except Exception as exc:  # a broken format string is itself behaviour
            msg = "<unformattable: %s>" % exc
        self.records.append({
            "logger": record.name,
            "level": record.levelname,
            "message": msg,
            "exc": (record.exc_info[0].__name__ if record.exc_info else None),
        })


class Ctx(object):
    """What a case is handed. Everything it needs to reach the interface, and
    nothing that selects behaviour in the tree."""

    def __init__(self, tree, fixture):
        self.tree = tree
        self.fx = fixture

    # --- convenience constructors over the repo's own public API ----------
    def es(self, **kw):
        from elasticsearch import Elasticsearch
        kw.setdefault("hosts", [{"host": self.fx.host, "port": self.fx.port}])
        if self.fx.scheme == "https":
            kw.setdefault("use_ssl", True)
            kw.setdefault("verify_certs", True)
            kw.setdefault("ca_certs", server.ca_pem())
        hosts = kw.pop("hosts")
        return Elasticsearch(hosts, **kw)

    def es_custom_headers(self, headers, **kw):
        """The repo's own connection class, as commands/base.py builds it."""
        from elasticsearch import Elasticsearch
        from elasticsearch_connections import CustomHeadersConnection
        return Elasticsearch(
            self.fx.base,
            send_get_body_as="POST",
            connection_class=CustomHeadersConnection,
            headers=headers,
            **kw)

    def url(self, path=""):
        return self.fx.url(path)


def run(tree, cid):
    env.pin()
    registry.load_all()
    c = registry.CASES[cid]

    routes, default = {}, None
    if c.fixture is not None:
        spec = c.fixture()
        if isinstance(spec, tuple):
            routes, default = spec
        else:
            routes = spec
    tls = server.server_chain() if c.tls else None
    fx = server.Fixture(routes, default=default, tls=tls)

    sys.path.insert(0, os.path.join(tree, "lib"))
    sys.path.insert(0, tree)

    collector = Collector()
    for name in WATCHED:
        lg = logging.getLogger(name)
        lg.addHandler(collector)
        lg.setLevel(logging.DEBUG)
        lg.propagate = False

    obs = {}
    buf_out, buf_err = io.StringIO(), io.StringIO()
    real_out, real_err = sys.stdout, sys.stderr
    caught = []
    try:
        sys.stdout, sys.stderr = buf_out, buf_err
        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter("always")
            try:
                obs["result"] = canon.canon(c.fn(Ctx(tree, fx)))
            except BaseException as exc:  # the error path is scored too
                obs["result"] = None
                obs["raised"] = canon.exc(exc)
                obs["traceback_tail"] = traceback.format_exc().strip().splitlines()[-1]
            caught = sorted(
                ({"category": x.category.__name__, "message": str(x.message)}
                 for x in w if x.category.__name__ in CONTRACT_WARNINGS),
                key=lambda d: (d["category"], d["message"]))
    finally:
        sys.stdout, sys.stderr = real_out, real_err
        for name in WATCHED:
            logging.getLogger(name).removeHandler(collector)

    obs["wire"] = fx.wire()
    obs["logs"] = collector.records
    obs["stdout"] = buf_out.getvalue()
    obs["stderr"] = buf_err.getvalue()
    obs["warnings"] = caught
    port = fx.port
    fx.close()
    return normalize.walk(obs, port=port)


def main(argv):
    tree, cid = argv[1], argv[2]
    try:
        obs = run(tree, cid)
        print(json.dumps({"id": cid, "obs": obs}, sort_keys=True))
    except BaseException:
        print(json.dumps({"id": cid, "harness_error":
                          traceback.format_exc()}, sort_keys=True))
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
