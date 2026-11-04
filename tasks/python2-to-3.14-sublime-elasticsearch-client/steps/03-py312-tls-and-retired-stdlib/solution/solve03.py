# -*- coding: utf-8 -*-
"""Golden solution for rung 03, as a list of byte-anchored edits.

    python3 work/solve03.py <tree>

Applied on top of rung 02's tree. Same shape as ``work/solve01.py`` and
``work/solve02.py`` and for the same reasons: every edit anchors on the exact
bytes it expects, counts them, and refuses to continue when a count is wrong or
a replacement would be a no-op (INSTRUCTIONS trap 7).

Three kinds of change, in the order the rung's requirements list them:

  EDITS    byte-anchored replacements. Both copies of urllib3 get every urllib3
           edit -- ``lib/urllib3`` and the one requests vendors under
           ``lib/requests/packages/urllib3`` are byte-identical today and each
           is imported by a different half of the tree, so an edit to one and
           not the other is half a migration (NOTES section 4.7).

  RAW      the 26 string literals whose invalid escape sequences are a
           SyntaxWarning from 3.12's compiler. Each is re-spelled as a raw
           string and each is asserted to evaluate to the same object it did
           before, so a docstring that is scored for its first line cannot
           move. Done as a pass rather than 26 anchors because the edit is
           mechanical and the assertion is stronger than an anchor: it compares
           values, not bytes.

  DELETES  the vendored ``ssl_match_hostname`` backport, both copies. 3.12
           removed ``ssl.match_hostname``; the backport's first act is to try
           to import it, and what it falls back to is a hand-rolled matcher
           nobody should be running in 2026. Hostname verification moves into
           the handshake, where ``SSLContext.check_hostname`` does it.

What is deliberately *not* here:

  * ``resolve_ssl_version(None)`` still answers ``ssl.PROTOCOL_TLS`` (== 2).
    That is the function's published answer and this rung changes no
    documented semantics, so the recorded 3.8 observation stands. The
    deprecated constant is translated to ``PROTOCOL_TLS_CLIENT`` in
    ``create_urllib3_context``, the one place that builds a context -- which
    is what urllib3 itself did when it met the same deprecation.
  * ``contrib/pyopenssl.py`` is edited but untested: pyOpenSSL is not
    installed, so nothing imports the module and no case reaches it. It is
    edited because the gate reads it and because leaving a known-dead constant
    in a file is the thing the rung is about.
"""
from __future__ import print_function

import ast
import io
import os
import shutil
import sys
import tokenize
import warnings


# (relpath, old, new, count): ``old`` must appear exactly ``count`` times.
EDITS = []
#: (relpath, n_literals): every invalid-escape literal becomes raw.
RAW = []
#: relpaths removed outright.
DELETES = []

#: The two urllib3 copies. ``edit3`` writes every urllib3 edit to both.
U3 = ("lib/urllib3", "lib/requests/packages/urllib3")


def edit(rel, old, new, count=1):
    EDITS.append((rel, old, new, count))


def edit3(rel, old, new, count=1):
    """The same edit against both copies of urllib3."""
    for root in U3:
        EDITS.append((root + "/" + rel, old, new, count))


def raw(rel, n):
    RAW.append((rel, n))


def raw3(rel, n):
    for root in U3:
        RAW.append((root + "/" + rel, n))


#: Block replacements applied *only if the tree has them* (``rel`` relative to
#: each urllib3 root).
#:
#: Rung 02 has more than one correct end state, and the blind ladder produced a
#: different one from mine. `m03-step02-blind2`'s tree kept urllib3's
#: hand-rolled `SSLContext` fallback -- the `except ImportError:` arm labelled
#: "Platform-specific: Python 2 & 3.1" -- which is perfectly legal at rung 02:
#: on 3.11 `from ssl import SSLContext` succeeds, the fallback is never
#: defined, and every rung-02 gate and case passes. On 3.12 its body still
#: *names* the module-level `wrap_socket`, so a rung-03 tree that keeps it
#: fails `no-dead-ssl-names`. My own rung-01 solution had deleted the fallback,
#: so none of the anchors below covered it: the oracle applied all 35 edits to
#: that seed and scored **0.941667**, one of four new gates short, which is
#: how this list came to exist (NOTES section 9.7).
#:
#: Optional rather than required because the canonical predecessor does not
#: have the block at all. What keeps it honest is `_final_scan`: whichever
#: predecessor the tree came from, no name this rung retires may survive it.
OPTIONAL = []


def optional3(rel, old, new):
    """A block replacement applied to whichever of the two copies has it."""
    for root in U3:
        OPTIONAL.append((root + "/" + rel, old, new))


#: urllib3 1.10.4's pre-2.7.9 `SSLContext` stand-in, verbatim, as the blind
#: rung-02 tree left it. Both copies carry it byte-identically.
SSLCONTEXT_FALLBACK = b"""try:
    from ssl import SSLContext  # Modern SSL?
except ImportError:
    class SSLContext(object):  # Platform-specific: Python 2 & 3.1
        supports_set_ciphers = True

        def __init__(self, protocol_version):
            self.protocol = protocol_version
            # Use default values from a real SSLContext
            self.check_hostname = False
            self.verify_mode = ssl.CERT_NONE
            self.ca_certs = None
            self.options = 0
            self.certfile = None
            self.keyfile = None
            self.ciphers = None

        def load_cert_chain(self, certfile, keyfile):
            self.certfile = certfile
            self.keyfile = keyfile

        def load_verify_locations(self, location):
            self.ca_certs = location

        def set_ciphers(self, cipher_suite):
            if not self.supports_set_ciphers:
                raise TypeError(
                    'Your version of Python does not support setting '
                    'a custom cipher suite. Please upgrade to Python '
                    '2.7, 3.2, or later if you need this functionality.'
                )
            self.ciphers = cipher_suite

        def wrap_socket(self, socket, server_hostname=None):
            warnings.warn(
                'A true SSLContext object is not available. This prevents '
                'urllib3 from configuring SSL appropriately and may cause '
                'certain SSL connections to fail. For more information, see '
                'https://urllib3.readthedocs.org/en/latest/security.html'
                '#insecureplatformwarning.',
                InsecurePlatformWarning
            )
            kwargs = {
                'keyfile': self.keyfile,
                'certfile': self.certfile,
                'ca_certs': self.ca_certs,
                'cert_reqs': self.verify_mode,
                'ssl_version': self.protocol,
            }
            if self.supports_set_ciphers:  # Platform-specific: Python 2.7+
                return wrap_socket(socket, ciphers=self.ciphers, **kwargs)
            else:  # Platform-specific: Python 2.6
                return wrap_socket(socket, **kwargs)
"""

optional3("util/ssl_.py", SSLCONTEXT_FALLBACK,
          b"from ssl import SSLContext\n")

# ------------------------------------------------ 1. the retired module ----
#
# ``imp`` is gone at 3.12. ``importlib.reload`` is the same function -- ``imp``
# had been a shim around it since 3.4 -- so this is the whole fix.
edit("ElasticsearchClient.py",
     b"from imp import reload\n",
     b"from importlib import reload\n")


# --------------------------------------------- 2. util/ssl_.py: imports ----
#
# The single most consequential line in the rung. This import is *atomic*:
#
#     from ssl import wrap_socket, CERT_NONE, PROTOCOL_SSLv23
#
# ``wrap_socket`` is the name 3.12 removed, and because the three names arrive
# on one statement, losing one loses all three. The ``except ImportError:
# pass`` then swallows it, the module imports cleanly, and the failure surfaces
# a long way away as ``NameError: CERT_NONE`` the first time anything resolves
# a certificate requirement. That is why the predecessor's TLS surface is not
# merely deprecated on 3.12 but broken, and why every case that touches it is
# this rung's own (NOTES section 9.2).
#
# The three pre-bound placeholders go with it: they exist so the module still
# imports on a Python built without ssl, a configuration this ladder does not
# have and 3.12 does not encourage. ``SSLContext`` is bound for real forty
# lines down; ``HAS_SNI`` is bound here; ``create_default_context`` was never
# read by anything in the tree (swept: only its own assignment and a docstring
# mention it).
#
# ``errno`` goes because the only reader of it is the dead OSError arm removed
# in edit 6.
edit3("util/ssl_.py",
      b"SSLContext = None\n"
      b"HAS_SNI = False\n"
      b"create_default_context = None\n"
      b"\n"
      b"import errno\n"
      b"import warnings\n"
      b"\n"
      b"try:  # Test for SSL features\n"
      b"    import ssl\n"
      b"    from ssl import wrap_socket, CERT_NONE, PROTOCOL_SSLv23\n"
      b"    from ssl import HAS_SNI  # Has SNI?\n"
      b"except ImportError:\n"
      b"    pass\n",
      b"import warnings\n"
      b"\n"
      b"import ssl\n"
      b"from ssl import CERT_NONE, HAS_SNI\n")


# ------------------------------------- 3. util/ssl_.py: resolve_ssl_version --
#
# ``PROTOCOL_SSLv23`` is still a live alias on 3.14 -- it resolves to
# ``PROTOCOL_TLS``, value 2 -- so this is a naming fix, not a breakage fix, and
# the value the function returns does not move. It is the published answer for
# "the caller asked for nothing", it is what the 3.8 recording holds, and
# rewriting it would be inventing a semantic change this rung does not have.
edit3("util/ssl_.py",
      b"    if candidate is None:\n"
      b"        return PROTOCOL_SSLv23\n",
      b"    if candidate is None:\n"
      b"        return ssl.PROTOCOL_TLS\n")


# --------------------------------- 4. util/ssl_.py: create_urllib3_context --
#
# One place in the tree builds an SSLContext, so one place translates the
# deprecated protocol constant. ``SSLContext(PROTOCOL_TLS)`` warns from 3.10
# on; ``PROTOCOL_TLS_CLIENT`` is its client-side replacement and is what every
# caller here wants. Doing the translation here rather than in
# ``resolve_ssl_version`` keeps that function's published answer intact.
edit3("util/ssl_.py",
      b"    context = SSLContext(ssl_version or ssl.PROTOCOL_SSLv23)\n",
      b"    # ``PROTOCOL_TLS`` -- and its older spelling ``PROTOCOL_SSLv23`` --\n"
      b"    # is deprecated from 3.10 on: constructing a context with it warns.\n"
      b"    # ``PROTOCOL_TLS_CLIENT`` is the replacement for a client, which is\n"
      b"    # the only thing this library is.\n"
      b"    if not ssl_version or ssl_version == ssl.PROTOCOL_TLS:\n"
      b"        ssl_version = ssl.PROTOCOL_TLS_CLIENT\n"
      b"\n"
      b"    context = SSLContext(ssl_version)\n")

# ``PROTOCOL_TLS_CLIENT`` arrives with ``check_hostname`` already true, and
# assigning ``verify_mode = CERT_NONE`` while it is true raises ValueError --
# which is every unverified pool in the tree. So the two assignments swap
# order. The 3.2 ``getattr`` guard goes: there is no interpreter on this ladder
# whose SSLContext lacks the attribute.
#
# What this function does *not* do any more is decide the hostname policy. It
# only stops the protocol's default from deciding; the caller that knows about
# ``assert_hostname`` sets the flag (edit 8).
edit3("util/ssl_.py",
      b"    context.verify_mode = cert_reqs\n"
      b"    if getattr(context, 'check_hostname', None) is not None:  "
      b"# Platform-specific: Python 3.2\n"
      b"        # We do our own verification, including fingerprints and alternative\n"
      b"        # hostnames. So disable it here\n"
      b"        context.check_hostname = False\n"
      b"    return context\n",
      b"    # Order is load-bearing: ``check_hostname`` must be cleared before\n"
      b"    # ``verify_mode`` can be lowered, or CERT_NONE raises ValueError\n"
      b"    # under PROTOCOL_TLS_CLIENT. The caller re-raises the flag when it\n"
      b"    # wants the handshake to check the name.\n"
      b"    context.check_hostname = False\n"
      b"    context.verify_mode = cert_reqs\n"
      b"    return context\n")


# ------------------------------------- 5. util/ssl_.py: ssl_wrap_socket ------
#
# ``IOError`` *is* ``OSError`` from Python 3.3 on, so the first handler already
# caught everything and the second -- the one written for 3.3's
# FileNotFoundError -- has been unreachable for the whole life of Python 3.
# Proved rather than assumed: a mutant that turns the second arm's body into
# ``raise AssertionError`` leaves both bad-ca-file probes reporting SSLError
# (NOTES section 9.2).
edit3("util/ssl_.py",
      b"        except IOError as e:  # Platform-specific: Python 2.6, 2.7, 3.2\n"
      b"            raise SSLError(e)\n"
      b"        # Py33 raises FileNotFoundError which subclasses OSError\n"
      b"        # These are not equivalent unless we check the errno attribute\n"
      b"        except OSError as e:  # Platform-specific: Python 3.3 and beyond\n"
      b"            if e.errno == errno.ENOENT:\n"
      b"                raise SSLError(e)\n"
      b"            raise\n",
      b"        except OSError as e:\n"
      b"            # ``IOError`` is ``OSError`` from 3.3 on, so the second arm\n"
      b"            # this replaces never ran: a missing or unreadable CA file\n"
      b"            # already left here as an SSLError.\n"
      b"            raise SSLError(e)\n")

# The docstring named the function 3.12 removed.
edit3("util/ssl_.py",
      b"    All arguments except for server_hostname and ssl_context have the same\n"
      b"    meaning as they do when using :func:`ssl.wrap_socket`.\n",
      b"    All arguments except for server_hostname and ssl_context have the same\n"
      b"    meaning as they do when using :meth:`ssl.SSLContext.wrap_socket`.\n")


# ---------------------------------------- 6. connection.py: the imports -----
#
# The backport is deleted, so its import goes. ``create_urllib3_context`` comes
# in instead: the connection now builds the context itself, because it is the
# only object that knows whether the handshake should check the name.
edit3("connection.py",
      b"from .packages.ssl_match_hostname import match_hostname\n"
      b"\n"
      b"from .util.ssl_ import (\n"
      b"    resolve_cert_reqs,\n"
      b"    resolve_ssl_version,\n"
      b"    ssl_wrap_socket,\n"
      b"    assert_fingerprint,\n"
      b")\n",
      b"from .util.ssl_ import (\n"
      b"    create_urllib3_context,\n"
      b"    resolve_cert_reqs,\n"
      b"    resolve_ssl_version,\n"
      b"    ssl_wrap_socket,\n"
      b"    assert_fingerprint,\n"
      b")\n")


# ------------------------- 7. connection.py: the unverified HTTPSConnection --
#
# ``ssl.wrap_socket(conn, keyfile, certfile)`` with no other argument checked
# neither chain nor name. The replacement says that in full rather than
# inheriting PROTOCOL_TLS_CLIENT's secure defaults by accident, because this
# class is the one the library keeps around precisely for callers who asked
# for no verification -- ``VerifiedHTTPSConnection`` is the other one.
edit3("connection.py",
      b"    def connect(self):\n"
      b"        conn = self._new_conn()\n"
      b"        self._prepare_conn(conn)\n"
      b"        self.sock = ssl.wrap_socket(conn, self.key_file, self.cert_file)\n",
      b"    def connect(self):\n"
      b"        conn = self._new_conn()\n"
      b"        self._prepare_conn(conn)\n"
      b"        # ``ssl.wrap_socket`` is gone at 3.12. This class verifies\n"
      b"        # nothing, and always did not; the context says so out loud\n"
      b"        # instead of relying on a default.\n"
      b"        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)\n"
      b"        context.check_hostname = False\n"
      b"        context.verify_mode = ssl.CERT_NONE\n"
      b"        if self.cert_file:\n"
      b"            context.load_cert_chain(self.cert_file, self.key_file)\n"
      b"        self.sock = context.wrap_socket(conn, server_hostname=self.host)\n")


# ----------------------- 8. connection.py: hostname checking in the handshake -
#
# The old shape: wrap the socket with ``check_hostname`` off, then pull the
# peer certificate back out and run the vendored ``match_hostname`` over it.
# 3.12 removed ``ssl.match_hostname``, the backport it fell back to is deleted
# by this rung, and ``SSLContext.check_hostname`` has done the same job inside
# the handshake since 3.4.
#
# Two details the flag alone does not carry, both preserved:
#
#   * ``assert_hostname`` names a *different* name to check than the one
#     dialled. The handshake checks ``server_hostname``, so that is where the
#     override goes.
#   * ``assert_hostname is False`` and ``assert_fingerprint`` both mean "do not
#     check the name at all", and a fingerprint check must not be pre-empted by
#     a name failure -- so both clear the flag.
#
# The observable consequence is that the failure now arrives as
# ``ssl.SSLCertVerificationError`` from inside the handshake rather than as the
# backport's ``CertificateError`` after it. Both are ``ssl.SSLError``
# subclasses, both reach the same handler in connectionpool.py, and the cases
# record what survives that change: that it failed, that it failed inside
# urllib3's own hierarchy, and that the server saw nothing.
edit3("connection.py",
      b"        # Wrap socket using verification with the root certs in\n"
      b"        # trusted_root_certs\n"
      b"        self.sock = ssl_wrap_socket(conn, self.key_file, self.cert_file,\n"
      b"                                    cert_reqs=resolved_cert_reqs,\n"
      b"                                    ca_certs=self.ca_certs,\n"
      b"                                    server_hostname=hostname,\n"
      b"                                    ssl_version=resolved_ssl_version)\n"
      b"\n"
      b"        if self.assert_fingerprint:\n"
      b"            assert_fingerprint(self.sock.getpeercert(binary_form=True),\n"
      b"                               self.assert_fingerprint)\n"
      b"        elif resolved_cert_reqs != ssl.CERT_NONE \\\n"
      b"                and self.assert_hostname is not False:\n",
      b"        # Hostname verification happens in the handshake now:\n"
      b"        # ssl.match_hostname is gone at 3.12 and the vendored backport\n"
      b"        # with it. ``assert_hostname`` is honoured by checking the name\n"
      b"        # the caller named instead of the one that was dialled.\n"
      b"        check_hostname = (resolved_cert_reqs != ssl.CERT_NONE\n"
      b"                          and self.assert_hostname is not False\n"
      b"                          and not self.assert_fingerprint)\n"
      b"        if check_hostname and self.assert_hostname:\n"
      b"            server_hostname = self.assert_hostname\n"
      b"        else:\n"
      b"            server_hostname = hostname\n"
      b"\n"
      b"        context = create_urllib3_context(ssl_version=resolved_ssl_version,\n"
      b"                                         cert_reqs=resolved_cert_reqs)\n"
      b"        context.check_hostname = check_hostname\n"
      b"\n"
      b"        # Wrap socket using verification with the root certs in\n"
      b"        # trusted_root_certs\n"
      b"        self.sock = ssl_wrap_socket(conn, self.key_file, self.cert_file,\n"
      b"                                    cert_reqs=resolved_cert_reqs,\n"
      b"                                    ca_certs=self.ca_certs,\n"
      b"                                    server_hostname=server_hostname,\n"
      b"                                    ssl_version=resolved_ssl_version,\n"
      b"                                    ssl_context=context)\n"
      b"\n"
      b"        if self.assert_fingerprint:\n"
      b"            assert_fingerprint(self.sock.getpeercert(binary_form=True),\n"
      b"                               self.assert_fingerprint)\n"
      b"        elif check_hostname:\n")

# The call the flag replaces. The SecurityWarning above it stays: OpenSSL falls
# back to the common name when a certificate carries no subjectAltName, so a
# certificate that reaches this line without one is still worth complaining
# about, and that warning is scored.
edit3("connection.py",
      b"            match_hostname(cert, self.assert_hostname or hostname)\n",
      b"")


# --------------------------- 9. connectionpool.py: the exception it caught ---
#
# ``CertificateError`` came from the deleted backport. On 3.2+ it was
# ``ssl.CertificateError``, which 3.7 made an alias of
# ``ssl.SSLCertVerificationError`` -- itself an ``ssl.SSLError``, which this
# handler already names as ``BaseSSLError``. So the tuple collapses to one
# entry and catches exactly what it caught before.
edit3("connectionpool.py",
      b"from .packages.ssl_match_hostname import CertificateError\n",
      b"")
edit3("connectionpool.py",
      b"        except (BaseSSLError, CertificateError) as e:\n",
      b"        except BaseSSLError as e:\n")


# ------------------------------------ 10. packages/__init__.py: the import ---
edit3("packages/__init__.py",
      b"from . import ssl_match_hostname\n",
      b"")


# ---------------------------------------- 11. contrib/pyopenssl.py -----------
#
# Unreachable in this tree -- pyOpenSSL is not installed -- but the constants
# are as dead here as anywhere. ``ssl.PROTOCOL_SSLv3`` has not existed since
# 3.6, so the try/except around it could only ever take the except arm.
# ``PROTOCOL_TLS_CLIENT`` joins the table because that is now what
# ``create_urllib3_context`` is handed.
edit3("contrib/pyopenssl.py",
      b"_openssl_versions = {\n"
      b"    ssl.PROTOCOL_SSLv23: OpenSSL.SSL.SSLv23_METHOD,\n"
      b"    ssl.PROTOCOL_TLSv1: OpenSSL.SSL.TLSv1_METHOD,\n"
      b"}\n"
      b"\n"
      b"try:\n"
      b"    _openssl_versions.update({ssl.PROTOCOL_SSLv3: OpenSSL.SSL.SSLv3_METHOD})\n"
      b"except AttributeError:\n"
      b"    pass\n",
      b"_openssl_versions = {\n"
      b"    ssl.PROTOCOL_TLS: OpenSSL.SSL.SSLv23_METHOD,\n"
      b"    ssl.PROTOCOL_TLS_CLIENT: OpenSSL.SSL.SSLv23_METHOD,\n"
      b"    ssl.PROTOCOL_TLSv1: OpenSSL.SSL.TLSv1_METHOD,\n"
      b"}\n")


# --------------------------------------------- 12. the naive UTC clocks ------
#
# ``utcnow()`` and ``utcfromtimestamp()`` are deprecated at 3.12 and return a
# *naive* datetime that claims nothing about its offset -- which is the bug,
# not the deprecation. Each site keeps the instant it produced.
#
# The two analytics sites already attach ``tzutc()`` by hand one call later, so
# ``datetime.now(tzutc())`` is the same value with the same tzinfo class and
# one fewer step. ``requests`` only ever subtracts its two readings, so the
# offset it carries is invisible in the result; it gets the stdlib UTC.
# ``dateutil``'s is a constant: the proleptic ordinal of 1970-01-01.
edit("lib/analytics/client.py",
     b"            timestamp = datetime.utcnow().replace(tzinfo=tzutc())\n",
     b"            timestamp = datetime.now(tzutc())\n")
edit("lib/analytics/request.py",
     b'    body["sentAt"] = datetime.utcnow().replace(tzinfo=tzutc()).isoformat()\n',
     b'    body["sentAt"] = datetime.now(tzutc()).isoformat()\n')
edit("lib/requests/sessions.py",
     b"from datetime import datetime\n",
     b"from datetime import datetime, timezone\n")
edit("lib/requests/sessions.py",
     b"        start = datetime.utcnow()\n",
     b"        start = datetime.now(timezone.utc)\n")
edit("lib/requests/sessions.py",
     b"        r.elapsed = datetime.utcnow() - start\n",
     b"        r.elapsed = datetime.now(timezone.utc) - start\n")
edit("lib/dateutil/tz.py",
     b"EPOCHORDINAL = datetime.datetime.utcfromtimestamp(0).toordinal()\n",
     b"EPOCHORDINAL = datetime.datetime(1970, 1, 1).toordinal()\n")


# ----------------------------------------- 13. the invalid escape sequences --
raw3("connectionpool.py", 3)
raw3("poolmanager.py", 1)
raw("lib/elasticsearch/client/__init__.py", 1)
raw("lib/requests/api.py", 7)
raw("lib/requests/models.py", 1)
raw("lib/requests/sessions.py", 7)
raw("lib/dateutil/tzwin.py", 2)


# -------------------------------------------------- 14. the deleted files ----
for _root in U3:
    DELETES.append(_root + "/packages/ssl_match_hostname/__init__.py")
    DELETES.append(_root + "/packages/ssl_match_hostname/_implementation.py")


def _raw_pass(path, expected):
    """Re-spell every invalid-escape literal in ``path`` as a raw string.

    Asserts, per literal, that the raw form evaluates to the same value. A
    literal that also carries a *valid* escape -- ``\\n``, say -- would change
    value under an ``r`` prefix, and the assertion is what says none here do.
    """
    with open(path, "rb") as fh:
        src = fh.read().decode("utf-8")
    edits = []
    for tok in tokenize.generate_tokens(io.StringIO(src).readline):
        if tok.type != tokenize.STRING:
            continue
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            try:
                value = ast.literal_eval(tok.string)
            except Exception:
                continue
            if not any(issubclass(w.category, (SyntaxWarning,
                                               DeprecationWarning))
                       for w in caught):
                continue
        text = tok.string
        quote = min(i for i in (text.find("'"), text.find('"')) if i >= 0)
        prefix, rest = text[:quote], text[quote:]
        if "r" in prefix.lower():
            raise SystemExit("%s:%d: already raw and still warns"
                             % (path, tok.start[0]))
        new = prefix + "r" + rest
        if ast.literal_eval(new) != value:
            raise SystemExit("%s:%d: raw form changes the value"
                             % (path, tok.start[0]))
        edits.append((tok.start, tok.end, new))
    if len(edits) != expected:
        raise SystemExit("%s: %d invalid-escape literals, expected %d"
                         % (path, len(edits), expected))
    lines = src.splitlines(True)
    for (srow, scol), (erow, ecol), new in reversed(edits):
        head = lines[srow - 1][:scol]
        tail = lines[erow - 1][ecol:]
        lines[srow - 1:erow] = [head + new + tail]
    with open(path, "wb") as fh:
        fh.write("".join(lines).encode("utf-8"))
    return len(edits)


#: The names this rung retires: copies of ``gates.SSL_DEAD_NAMES`` and
#: ``gates.DEAD_312``, which this file cannot import -- it ships alone, beside
#: a tree, with no ``/tests``. The gates are the authority, and the names here
#: are deliberately ``_`` plus the gate constant's own name, because that is
#: what ``build/sync.sh`` matches on: it refuses to install a solver whose
#: copy of a gate constant has drifted from the gate.
_SSL_DEAD_NAMES = frozenset(("wrap_socket", "match_hostname", "CertificateError",
                       "PROTOCOL_SSLv2", "PROTOCOL_SSLv3", "PROTOCOL_SSLv23"))
_DEAD_312 = frozenset(("asynchat", "asyncore", "distutils", "imp",
                           "smtpd"))


def _final_scan(tree):
    """No name this rung retires survives anywhere in the finished tree.

    Byte anchors are exact, which makes them precise and makes them blind: an
    anchor that is missing is a loud failure, but a construct that is present
    and covered by no anchor is silent. It was silent once -- the blind
    rung-02 seed carried a second `wrap_socket` call site the canonical
    predecessor does not have, every anchor matched, and the oracle finished
    with a cheerful summary line and 0.941667.

    This is deliberately a *subset* of what `no-dead-ssl-names` and
    `no-dead-stdlib-312` score, never a superset: bare `Name` references,
    definitions of the retired name, and imports. The attribute-receiver rules
    the gates apply are not reproduced, because a solution that disagreed with
    the verifier in either direction would be worse than no check at all.
    `context.wrap_socket(sock)` is the named replacement and is an Attribute,
    so it is silent here exactly as it is there.
    """
    bad = []
    for dirpath, dirnames, filenames in os.walk(tree):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for fn in sorted(filenames):
            if not fn.endswith(".py"):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, tree)
            with open(path, "rb") as fh:
                src = fh.read()
            try:
                parsed = ast.parse(src)
            except SyntaxError as exc:
                bad.append("%s: does not parse: %s" % (rel, exc))
                continue
            for node in ast.walk(parsed):
                if isinstance(node, ast.Name) and node.id in _SSL_DEAD_NAMES:
                    bad.append("%s:%d: %s" % (rel, node.lineno, node.id))
                elif (isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                                        ast.ClassDef))
                        and node.name in _SSL_DEAD_NAMES):
                    bad.append("%s:%d: defines %s"
                               % (rel, node.lineno, node.name))
                elif isinstance(node, ast.ImportFrom):
                    root = (node.module or "").split(".")[0]
                    if root in _DEAD_312:
                        bad.append("%s:%d: from %s import"
                                   % (rel, node.lineno, node.module))
                    if node.module == "ssl":
                        for alias in node.names:
                            if alias.name in _SSL_DEAD_NAMES:
                                bad.append("%s:%d: from ssl import %s"
                                           % (rel, node.lineno, alias.name))
                elif isinstance(node, ast.Import):
                    for alias in node.names:
                        if alias.name.split(".")[0] in _DEAD_312:
                            bad.append("%s:%d: import %s"
                                       % (rel, node.lineno, alias.name))
    if bad:
        raise SystemExit(
            "solve03: %d retired name(s) survive the edits -- this tree is "
            "not the predecessor the anchors were written against:\n  %s"
            % (len(bad), "\n  ".join(bad[:20])))
    return True


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: solve03.py <tree>")
    tree = os.path.abspath(argv[1])

    applied = 0
    for rel, old, new, count in EDITS:
        path = os.path.join(tree, rel)
        with open(path, "rb") as fh:
            src = fh.read()
        n = src.count(old)
        if n != count:
            raise SystemExit("%s: anchor appears %d times, expected %d:\n%r"
                             % (rel, n, count, old[:160]))
        out = src.replace(old, new)
        if out == src:
            raise SystemExit("%s: edit changed nothing" % rel)
        with open(path, "wb") as fh:
            fh.write(out)
        applied += 1

    # Applied per file, then checked for agreement: the two urllib3 copies are
    # byte-identical in the pinned tree and a predecessor that collapsed the
    # fallback in one and not the other would be half a migration, which is
    # exactly the asymmetry `edit3` exists to prevent.
    optional = {}
    for rel, old, new in OPTIONAL:
        path = os.path.join(tree, rel)
        with open(path, "rb") as fh:
            src = fh.read()
        n = src.count(old)
        if n > 1:
            raise SystemExit("%s: optional block appears %d times" % (rel, n))
        if n:
            with open(path, "wb") as fh:
                fh.write(src.replace(old, new))
        optional.setdefault(old, []).append(n)
    for counts in optional.values():
        if len(set(counts)) != 1:
            raise SystemExit("optional block present in only %d of %d copies: "
                             "%r" % (sum(counts), len(counts), counts))
    n_optional = sum(sum(c) for c in optional.values())

    literals = 0
    for rel, n in RAW:
        literals += _raw_pass(os.path.join(tree, rel), n)

    removed = 0
    for rel in DELETES:
        path = os.path.join(tree, rel)
        if not os.path.exists(path):
            raise SystemExit("%s: nothing to delete" % rel)
        os.remove(path)
        removed += 1
    # The two package directories are empty now; leaving them behind would
    # leave an importable namespace package where a package used to be.
    for root in U3:
        d = os.path.join(tree, root, "packages", "ssl_match_hostname")
        if os.path.isdir(d):
            left = [n for n in os.listdir(d) if n != "__pycache__"]
            if left:
                raise SystemExit("%s: not empty: %r" % (d, left))
            shutil.rmtree(d)

    _final_scan(tree)
    print("solve03: %d edits (+%d optional), %d raw literals, %d files "
          "removed, 0 retired names left"
          % (applied, n_optional, literals, removed))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
