# -*- coding: utf-8 -*-
"""Throwaway CAs and server certificates, generated at run time.

Nothing here is checked in. A certificate carries a notBefore and a
notAfter; one baked into the task package would start failing on a date
nobody chose, and every TLS case would go red for a reason that has nothing to
do with the migration. Regenerating on every run means the validity window
always contains "now", so the clock cannot make this suite lie.

The cost of that choice is a handful of RSA keygens per process that needs TLS.
The driver generates once and hands the directory down through
``FIXTURE_CERTS``, so a full recording pays it once rather than once per case.

The subjectAltName of the default certificate deserves a note. The fixture
listens on 127.0.0.1, and the hostname matchers this repo can end up using
disagree about what that means:

* CPython's ``ssl.match_hostname`` (used up to 3.11) parses the hostname as an
  IP, and then reads *only* ``IP Address`` SAN entries.
* urllib3 1.10.4's vendored ``ssl_match_hostname/_implementation.py`` (which
  takes over at 3.12, when the stdlib function is removed) reads *only* ``DNS``
  entries, and falls back to commonName if there are none.
* ``SSLContext.check_hostname``, which is what a migrated tree ends up using,
  hands the name to OpenSSL: an IP literal is checked against IP SANs only.

A cert carrying just one of the three spellings would pass on one rung of the
ladder and fail on another, and the failure would look exactly like a broken
migration. So the default certificate carries all of them, and the scored
hostname cases use only the certificates all three agree about: ``MISMATCH``
(right issuer, wrong names) and ``UNTRUSTED`` (right names, unknown issuer).
The inputs where the matchers *disagree* -- a DNS-only SAN for an IP host, a
commonName-only certificate, partial wildcards -- are deliberately not scored,
because a correct step-03 migration is entitled to change those answers.

One leaf points the other way. ``client`` is issued by the fixture CA with
``extendedKeyUsage = clientAuth`` and no subjectAltName, and is what a case
hands to ``ssl_wrap_socket``'s ``certfile``/``keyfile``. Nothing matches a
hostname against a client certificate, so it needs no SAN; what it needs is an
EKU OpenSSL will accept in that role, which is why ``_CONF`` takes the EKU as a
parameter instead of hardcoding ``serverAuth``.
"""
import os
import subprocess
import tempfile

#: Ten years from whenever the fixture is built. Long enough that a single
#: recording/replay pair can never straddle the boundary; irrelevant beyond
#: that, because the next run generates a new one.
DAYS = 3650

#: The names the fixture host answers to, spelled for all three matchers.
DEFAULT_SAN = ("DNS:localhost", "DNS:127.0.0.1", "IP:127.0.0.1")

#: Extra leaf certificates, issued alongside the default one so that every
#: process finds them already on disk. ``(label, ca, cn, sans, eku)``.
EXTRA = (
    # Correctly issued by the fixture CA, but names nothing the fixture is
    # reachable as. Every matcher rejects it, for the same reason.
    ("mismatch", "ca", "wrong.example.invalid",
     ("DNS:wrong.example.invalid", "IP:10.0.0.1"), "serverAuth"),
    # Right names, wrong issuer: this one separates "does the client check the
    # chain" from "does the client check the name", which the mismatch case
    # alone cannot.
    ("untrusted", "ca2", "127.0.0.1", DEFAULT_SAN, "serverAuth"),
    # The other direction: a certificate the *client* presents. Issued by the
    # fixture CA so the fixture can verify it, ``clientAuth`` so OpenSSL will
    # let it be used that way, and no subjectAltName because nothing matches a
    # hostname against a client certificate. It exists so that
    # ``ssl_wrap_socket``'s ``certfile``/``keyfile`` arguments -- the one
    # branch of that function nothing else reaches -- are driven by a real
    # handshake rather than assumed to work.
    ("client", "ca", "harness-client", (), "clientAuth"),
)

#: Written last. ``ensure()`` keys off this rather than off the default chain,
#: so a directory caught half-built is rebuilt instead of used.
MARKER = "READY"

_CACHED = None

_CONF = """\
[req]
distinguished_name = dn
prompt = no
[dn]
CN = %(cn)s
[ext]
%(san)sbasicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = %(eku)s
"""


def _openssl(*args):
    p = subprocess.run(("openssl",) + args, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError("openssl %s failed (%d): %s"
                           % (" ".join(args), p.returncode,
                              p.stderr.decode("utf-8", "replace")[-1500:]))


def _ca(j, name, cn):
    # RSA-2048 with SHA-256: the weakest pair that OpenSSL 3's default security
    # level still accepts, and the strongest assumption every interpreter from
    # 3.8 to 3.14 can be relied on to make.
    _openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", j(name + ".key"), "-out", j(name + ".pem"),
             "-days", str(DAYS), "-sha256", "-subj", "/CN=" + cn)


def _leaf(j, label, ca, cn, sans, eku="serverAuth"):
    conf = j(label + ".cnf")
    with open(conf, "w") as fh:
        fh.write(_CONF % {
            "cn": cn,
            "san": ("subjectAltName = %s\n" % ", ".join(sans)) if sans else "",
            "eku": eku,
        })
    _openssl("req", "-new", "-newkey", "rsa:2048", "-nodes",
             "-keyout", j(label + ".key"), "-out", j(label + ".csr"),
             "-config", conf)
    _openssl("x509", "-req", "-in", j(label + ".csr"),
             "-CA", j(ca + ".pem"), "-CAkey", j(ca + ".key"), "-CAcreateserial",
             "-out", j(label + ".pem"), "-days", str(DAYS), "-sha256",
             "-extfile", conf, "-extensions", "ext")
    # load_cert_chain() takes one file holding both halves.
    with open(j(label + "-chain.pem"), "w") as out:
        for part in (label + ".pem", label + ".key"):
            with open(j(part)) as fh:
                out.write(fh.read())


def build(target):
    """Write both CAs, the default ``server-chain.pem`` and every EXTRA leaf."""
    if not os.path.isdir(target):
        os.makedirs(target)
    j = lambda n: os.path.join(target, n)  # noqa: E731

    _ca(j, "ca", "harness-throwaway-ca")
    _ca(j, "ca2", "harness-untrusted-ca")
    _leaf(j, "server", "ca", "127.0.0.1", DEFAULT_SAN)
    for label, ca, cn, sans, eku in EXTRA:
        _leaf(j, label, ca, cn, sans, eku)

    with open(j(MARKER), "w") as fh:
        fh.write("ok\n")
    return target


def ensure():
    """Return a directory holding the fixture certificates.

    Honours an inherited ``FIXTURE_CERTS`` so that a driver run generates the
    material once for all its case subprocesses; falls back to building a
    private copy, which is what a case run by hand gets.
    """
    global _CACHED
    if _CACHED:
        return _CACHED
    d = os.environ.get("FIXTURE_CERTS")
    if d and os.path.exists(os.path.join(d, MARKER)):
        _CACHED = d
        return d
    if not d:
        d = tempfile.mkdtemp(prefix="harness-certs-")
    _CACHED = build(d)
    os.environ["FIXTURE_CERTS"] = _CACHED
    return _CACHED


def chain(label="server"):
    """Path to one leaf's cert+key file, for ``SSLContext.load_cert_chain``."""
    return os.path.join(ensure(), "%s-chain.pem" % label)


if __name__ == "__main__":
    import sys
    print(build(sys.argv[1] if len(sys.argv) > 1
                else tempfile.mkdtemp(prefix="harness-certs-")))
