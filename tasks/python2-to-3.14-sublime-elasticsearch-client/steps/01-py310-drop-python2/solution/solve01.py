# -*- coding: utf-8 -*-
"""Golden solution for rung 01, as a list of byte-anchored edits.

    python3 work/solve01.py <tree>

Written as a program rather than as a hand-edited tree so that it can be
re-derived if the pin ever moves, and so that every edit *asserts* it landed --
INSTRUCTIONS trap 7. An anchor that is not found, or found more than once, is a
hard error: a silent no-op here would show up much later as a gate finding with
no obvious cause.

The two vendored urllib3 copies (``lib/urllib3`` and
``lib/requests/packages/urllib3``) are byte-identical at the pin, so the edits
are written once against ``lib/urllib3`` and the whole directory is mirrored
afterwards. That is checked, not assumed: the mirror step refuses to run if the
two trees differed before the edits began.

Files are read and written as bytes throughout. ``lib/dateutil/parser.py``
carries ``# -*- coding:iso-8859-1 -*-`` and holds bytes that are not UTF-8;
decoding it would corrupt it.
"""
from __future__ import print_function

import os
import shutil
import sys


# (relpath, old, new, count): ``old`` must appear exactly ``count`` times.
EDITS = []
#: Files removed outright.
DELETES = []


def edit(rel, old, new, count=1):
    EDITS.append((rel, old, new, count))


def span(rel, start, end, new):
    """Replace everything from ``start`` through the end of ``end``.

    For the half-dozen places where the Python 2 arm is thirty or fifty lines
    long and retyping it as an anchor would be its own source of typos. Both
    markers still have to be unique, so this is no weaker than ``edit``.
    """
    EDITS.append((rel, (start, end), new, 1))


def drop(rel):
    DELETES.append(rel)


U3 = "lib/urllib3"
U3V = "lib/requests/packages/urllib3"


# ---------------------------------------------------------------- six ----
#
# Three vendored copies of six, plus the two ordered_dict backports that exist
# for Python 2.6. All five are deleted; the second urllib3 copy of each goes
# with the mirror at the end.
drop("lib/six.py")
drop(U3 + "/packages/six.py")
drop(U3 + "/packages/ordered_dict.py")


# ------------------------------------------------------- __future__ ----
edit("lib/urllib3/packages/__init__.py",
     b"from __future__ import absolute_import\n\nfrom . import ssl_match_hostname\n",
     b"from . import ssl_match_hostname\n")
edit("lib/requests/packages/__init__.py",
     b"from __future__ import absolute_import\n\nfrom . import urllib3\n",
     b"from . import urllib3\n")
edit("lib/requests/packages/chardet/chardetect.py",
     b"from __future__ import absolute_import, print_function, unicode_literals\n",
     b"")
edit("lib/elasticsearch/__init__.py",
     b"from __future__ import absolute_import\n\nVERSION",
     b"VERSION")
edit("lib/elasticsearch/connection/thrift.py",
     b"from __future__ import absolute_import\n", b"")
edit("lib/elasticsearch/client/__init__.py",
     b"from __future__ import unicode_literals\n", b"")
edit("lib/elasticsearch/client/utils.py",
     b"from __future__ import unicode_literals\n", b"")
edit("lib/dateutil/parser.py",
     b"from __future__ import unicode_literals\n\n", b"")


# --------------------------------------------------- urllib3 (both) ----
edit(U3 + "/connectionpool.py",
     b"from .packages import six\n", b"")
edit(U3 + "/connectionpool.py",
     b"\n\nxrange = six.moves.xrange\n\nlog = logging", b"\n\nlog = logging")
edit(U3 + "/connectionpool.py",
     b"        for _ in xrange(maxsize):", b"        for _ in range(maxsize):")
# The 2.6.4 tunnel branch: on every interpreter this task names, the `else`
# arm is the only reachable one.
edit(U3 + "/connectionpool.py",
     b"""        # Python 2.7+
        try:
            set_tunnel = conn.set_tunnel
        except AttributeError:  # Platform-specific: Python 2.6
            set_tunnel = conn._set_tunnel

        if sys.version_info <= (2, 6, 4) and not self.proxy_headers:   # Python 2.6.4 and older
            set_tunnel(self.host, self.port)
        else:
            set_tunnel(self.host, self.port, self.proxy_headers)
""",
     b"""        conn.set_tunnel(self.host, self.port, self.proxy_headers)
""")

edit(U3 + "/fields.py", b"\nfrom .packages import six\n", b"")
edit(U3 + "/fields.py",
     b"""    if not six.PY3:  # Python 2:
        value = value.encode('utf-8')
    value = email.utils.encode_rfc2231(value, 'utf-8')""",
     b"""    value = email.utils.encode_rfc2231(value, 'utf-8')""")

edit(U3 + "/filepost.py",
     b"from .packages import six\nfrom .packages.six import b\nfrom .fields import RequestField\n",
     b"from .fields import RequestField\n")
edit(U3 + "/filepost.py",
     b"        i = six.iteritems(fields)", b"        i = iter(fields.items())")
edit(U3 + "/filepost.py",
     b"        return ((k, v) for k, v in six.iteritems(fields))",
     b"        return ((k, v) for k, v in fields.items())")
edit(U3 + "/filepost.py",
     b"        if isinstance(data, six.text_type):",
     b"        if isinstance(data, str):")
edit(U3 + "/filepost.py",
     b"    body.write(b('--%s--\\r\\n' % (boundary)))",
     b"    body.write(('--%s--\\r\\n' % (boundary)).encode('latin-1'))")
edit(U3 + "/filepost.py",
     b"        body.write(b('--%s\\r\\n' % (boundary)))",
     b"        body.write(('--%s\\r\\n' % (boundary)).encode('latin-1'))")

edit(U3 + "/connection.py", b"import warnings\nfrom .packages import six\n",
     b"import warnings\n")
edit(U3 + "/connection.py",
     b"""        if six.PY3:  # Python 3
            kw.pop('strict', None)

        # Pre-set source_address in case we have an older Python like 2.6.
        self.source_address = kw.get('source_address')

        if sys.version_info < (2, 7):  # Python 2.6
            # _HTTPConnection on Python 2.6 will balk at this keyword arg, but
            # not newer versions. We can still use it when creating a
            # connection though, so we pop it *after* we have saved it as
            # self.source_address.
            kw.pop('source_address', None)
""",
     b"""        kw.pop('strict', None)

        self.source_address = kw.get('source_address')
""")

edit(U3 + "/response.py",
     b"from .packages.six import string_types as basestring, binary_type, PY3\n",
     b"")
edit(U3 + "/response.py",
     b"        if body and isinstance(body, (basestring, binary_type)):",
     b"        if body and isinstance(body, (str, bytes)):")
edit(U3 + "/response.py",
     b"        self._data = binary_type()", b"        self._data = b''")
edit(U3 + "/response.py",
     b"            buf = self._decoder.decompress(binary_type())",
     b"            buf = self._decoder.decompress(b'')")
edit(U3 + "/response.py",
     b"""            if PY3: # Python 3
                headers = HTTPHeaderDict(headers.items())
            else: # Python 2
                headers = HTTPHeaderDict.from_httplib(headers)
""",
     b"""            headers = HTTPHeaderDict(headers.items())
""")

edit(U3 + "/_collections.py",
     b"from collections import Mapping, MutableMapping\n",
     b"from collections.abc import Mapping, MutableMapping\n")
edit(U3 + "/_collections.py",
     b"""try:  # Python 2.7+
    from collections import OrderedDict
except ImportError:
    from .packages.ordered_dict import OrderedDict
from .packages.six import iterkeys, itervalues, PY3
""",
     b"""from collections import OrderedDict
""")
edit(U3 + "/_collections.py",
     b"            values = list(itervalues(self._container))",
     b"            values = list(self._container.values())")
edit(U3 + "/_collections.py",
     b"            return list(iterkeys(self._container))",
     b"            return list(self._container.keys())")
edit(U3 + "/_collections.py",
     b"    update = MutableMapping.update\n"
     b"    \n"
     b"    if not PY3: # Python 2\n"
     b"        iterkeys = MutableMapping.iterkeys\n"
     b"        itervalues = MutableMapping.itervalues\n"
     b"\n"
     b"    __marker",
     b"    update = MutableMapping.update\n"
     b"\n"
     b"    __marker")
# ``iteritems`` is this class's own generator, not the dict protocol: it yields
# one pair per header *line*, duplicates included, and ``items`` is defined in
# terms of it. Renamed rather than deleted, and both call sites move with it.
edit(U3 + "/_collections.py",
     b"            for key, val in other.iteritems():",
     b"            for key, val in other.iter_lines():")
edit(U3 + "/_collections.py",
     b"    def iteritems(self):\n"
     b"        \"\"\"Iterate over all header lines, including duplicate ones.\"\"\"",
     b"    def iter_lines(self):\n"
     b"        \"\"\"Iterate over all header lines, including duplicate ones.\"\"\"")
edit(U3 + "/_collections.py",
     b"        return list(self.iteritems())",
     b"        return list(self.iter_lines())")

edit(U3 + "/util/retry.py", b"from ..packages import six\n", b"")
# ``six.reraise(type(e), e, tb)`` is ``raise e.with_traceback(tb)`` on py3 --
# same exception object, same traceback, so the recorded observation is
# unchanged. The outer ``raise`` was always dead: reraise never returns.
edit(U3 + "/util/retry.py",
     b"raise six.reraise(type(error), error, _stacktrace)",
     b"raise error.with_traceback(_stacktrace)", count=3)

edit(U3 + "/util/request.py",
     b"from base64 import b64encode\n\nfrom ..packages.six import b\n",
     b"from base64 import b64encode\n")
edit(U3 + "/util/request.py",
     b"            b64encode(b(basic_auth)).decode('utf-8')",
     b"            b64encode(basic_auth.encode('latin-1')).decode('utf-8')")
edit(U3 + "/util/request.py",
     b"            b64encode(b(proxy_basic_auth)).decode('utf-8')",
     b"            b64encode(proxy_basic_auth.encode('latin-1')).decode('utf-8')")

# The fallback SSLContext exists for Python 2.6/3.1, where ``ssl.SSLContext``
# is absent. Every interpreter on this ladder has it, so the except arm is
# unreachable and the ``supports_set_ciphers`` version test inside it with it.
span(U3 + "/util/ssl_.py",
     b"try:\n    from ssl import SSLContext  # Modern SSL?\n",
     b"            else:  # Platform-specific: Python 2.6\n"
     b"                return wrap_socket(socket, **kwargs)\n",
     b"from ssl import SSLContext\n")


# ------------------------------------------------- elasticsearch ----
# ``(2, 7) <= sys.version_info < (3, 2)`` is false on anything this ladder
# runs, so the NullHandler was never installed and removing it changes no
# observable logging behaviour.
span("lib/elasticsearch/__init__.py",
     b"import sys\n",
     b"    logger.addHandler(logging.NullHandler())\n\n",
     b"")

span("lib/elasticsearch/compat.py",
     b"import sys\n",
     b"    map = map",
     b"string_types = str, bytes\n"
     b"from urllib.parse import quote_plus, urlencode, urlparse")
# ``map`` was re-exported only so Python 2 callers got the iterator form.
edit("lib/elasticsearch/helpers/__init__.py",
     b"from ..compat import map\n", b"")

edit("lib/elasticsearch/connection/esthrift/Rest.py",
     b"      for key, value in self.__dict__.iteritems()]",
     b"      for key, value in self.__dict__.items()]", count=2)
edit("lib/elasticsearch/connection/esthrift/ttypes.py",
     b"      for key, value in self.__dict__.iteritems()]",
     b"      for key, value in self.__dict__.items()]", count=2)
edit("lib/elasticsearch/connection/esthrift/ttypes.py",
     b"          for _i4 in xrange(_size0):", b"          for _i4 in range(_size0):")
edit("lib/elasticsearch/connection/esthrift/ttypes.py",
     b"          for _i11 in xrange(_size7):", b"          for _i11 in range(_size7):")
edit("lib/elasticsearch/connection/esthrift/ttypes.py",
     b"          for _i22 in xrange(_size18):", b"          for _i22 in range(_size18):")


# ------------------------------------------------------- requests ----
# ``basestring``, ``str`` and ``bytes`` were aliases that only said anything on
# Python 2; ``is_py2``/``is_py3`` selected between the two arms below. All five
# go, and the four importers move to the builtins.
span("lib/requests/compat.py",
     b"import sys\n",
     b"    numeric_types = (int, float)",
     b"""try:
    import simplejson as json
except ImportError:
    import json

from urllib.parse import (urlparse, urlunparse, urljoin, urlsplit, urlencode,
                          quote, unquote, quote_plus, unquote_plus, urldefrag)
from urllib.request import parse_http_list, getproxies, proxy_bypass
from http import cookiejar as cookielib
from http.cookies import Morsel
from io import StringIO
from collections import OrderedDict

builtin_str = str
numeric_types = (int, float)""")

edit("lib/requests/auth.py",
     b"from .compat import urlparse, str\n", b"from .compat import urlparse\n")
edit("lib/requests/adapters.py",
     b"from .compat import urlparse, basestring\n",
     b"from .compat import urlparse\n")
edit("lib/requests/adapters.py",
     b"            if not isinstance(cert, basestring):",
     b"            if not isinstance(cert, (str, bytes)):")

edit("lib/requests/models.py",
     b"from .compat import (\n"
     b"    cookielib, urlunparse, urlsplit, urlencode, str, bytes, StringIO,\n"
     b"    is_py2, chardet, json, builtin_str, basestring)\n",
     b"from .compat import (\n"
     b"    cookielib, urlunparse, urlsplit, urlencode, StringIO,\n"
     b"    chardet, json, builtin_str)\n")
edit("lib/requests/models.py",
     b"                if isinstance(vs, basestring) or not hasattr(vs, '__iter__'):",
     b"                if isinstance(vs, (str, bytes)) or not hasattr(vs, '__iter__'):")
edit("lib/requests/models.py",
     b"        elif isinstance(data, basestring):",
     b"        elif isinstance(data, (str, bytes)):")
edit("lib/requests/models.py",
     b"            if isinstance(val, basestring) or not hasattr(val, '__iter__'):",
     b"            if isinstance(val, (str, bytes)) or not hasattr(val, '__iter__'):")
edit("lib/requests/models.py",
     b"            not isinstance(data, (basestring, list, tuple, dict))",
     b"            not isinstance(data, (str, bytes, list, tuple, dict))")
edit("lib/requests/models.py",
     b"                    if isinstance(data, basestring) or hasattr(data, 'read'):",
     b"                    if isinstance(data, (str, bytes)) or hasattr(data, 'read'):")
edit("lib/requests/models.py",
     b"            url = unicode(url) if is_py2 else str(url)",
     b"            url = str(url)")
edit("lib/requests/models.py",
     b"""        if is_py2:
            if isinstance(scheme, str):
                scheme = scheme.encode('utf-8')
            if isinstance(netloc, str):
                netloc = netloc.encode('utf-8')
            if isinstance(path, str):
                path = path.encode('utf-8')
            if isinstance(query, str):
                query = query.encode('utf-8')
            if isinstance(fragment, str):
                fragment = fragment.encode('utf-8')

        enc_params""",
     b"        enc_params")
edit("lib/requests/models.py", b"import collections\n", b"import collections.abc\n")
edit("lib/requests/models.py",
     b"        if isinstance(hook, collections.Callable):",
     b"        if isinstance(hook, collections.abc.Callable):")
edit("lib/requests/models.py",
     b"            self.hooks[event].extend(h for h in hook if isinstance(h, collections.Callable))",
     b"            self.hooks[event].extend(h for h in hook if isinstance(h, collections.abc.Callable))")

edit("lib/requests/utils.py",
     b"from .compat import (quote, urlparse, bytes, str, OrderedDict, unquote, is_py2,\n"
     b"                     builtin_str, getproxies, proxy_bypass, urlunparse,\n"
     b"                     basestring)\n",
     b"from .compat import (quote, urlparse, OrderedDict, unquote,\n"
     b"                     builtin_str, getproxies, proxy_bypass, urlunparse)\n")
edit("lib/requests/utils.py",
     b"    if (name and isinstance(name, basestring) and name[0] != '<' and",
     b"    if (name and isinstance(name, (str, bytes)) and name[0] != '<' and")
edit("lib/requests/utils.py", b"import collections\n", b"import collections.abc\n")
edit("lib/requests/utils.py",
     b"    if isinstance(value, collections.Mapping):",
     b"    if isinstance(value, collections.abc.Mapping):")
edit("lib/requests/utils.py",
     b"""    if isinstance(string, builtin_str):
        out = string
    else:
        if is_py2:
            out = string.encode(encoding)
        else:
            out = string.decode(encoding)
""",
     b"""    if isinstance(string, builtin_str):
        out = string
    else:
        out = string.decode(encoding)
""")

edit("lib/requests/structures.py", b"import collections\n",
     b"import collections.abc\n")
edit("lib/requests/structures.py",
     b"class CaseInsensitiveDict(collections.MutableMapping):",
     b"class CaseInsensitiveDict(collections.abc.MutableMapping):")
edit("lib/requests/structures.py",
     b"    ``collections.MutableMapping`` as well as dict's ``copy``. Also",
     b"    ``collections.abc.MutableMapping`` as well as dict's ``copy``. Also")
edit("lib/requests/structures.py",
     b"        if isinstance(other, collections.Mapping):",
     b"        if isinstance(other, collections.abc.Mapping):")

edit("lib/requests/sessions.py", b"from collections import Mapping\n",
     b"from collections.abc import Mapping\n")

edit("lib/requests/cookies.py", b"import collections\n", b"import collections.abc\n")
edit("lib/requests/cookies.py",
     b"class RequestsCookieJar(cookielib.CookieJar, collections.MutableMapping):",
     b"class RequestsCookieJar(cookielib.CookieJar, collections.abc.MutableMapping):")
# The three ``iter*`` generators were the Python 2 half of this class's
# dict-like surface; ``keys``/``values``/``items`` keep working because the
# loops move into them.
span("lib/requests/cookies.py",
     b"    def iterkeys(self):\n",
     b"        return list(self.iteritems())\n",
     b"""    def keys(self):
        \"\"\"Dict-like keys() that returns a list of names of cookies from the
        jar. See values() and items().\"\"\"
        return [cookie.name for cookie in iter(self)]

    def values(self):
        \"\"\"Dict-like values() that returns a list of values of cookies from the
        jar. See keys() and items().\"\"\"
        return [cookie.value for cookie in iter(self)]

    def items(self):
        \"\"\"Dict-like items() that returns a list of name-value tuples from the
        jar. See keys() and values(). Allows client-code to call
        ``dict(RequestsCookieJar)`` and get a vanilla python dict of key value
        pairs.\"\"\"
        return [(cookie.name, cookie.value) for cookie in iter(self)]
""")

span("lib/requests/packages/chardet/__init__.py",
     b"from sys import version_info\n",
     b"        raise ValueError('Expected a bytes object, not a unicode object')\n",
     b"""
def detect(aBuf):
    if not isinstance(aBuf, bytes):
        raise ValueError('Expected a bytes object, not a unicode object')
""")
span("lib/requests/packages/chardet/compat.py",
     b"import sys\n",
     b"        return a\n",
     b"""base_str = (bytes, str)


def wrap_ord(a):
    return a
""")


# ------------------------------------------------------ analytics ----
# ``six.string_types`` is the one-tuple ``(str,)`` on Python 3, and ``require``
# formats whatever it is handed into its AssertionError message, so the shape
# has to survive the move off six.
edit("lib/analytics/client.py",
     b"from dateutil.tz import tzutc\nfrom six import string_types\n",
     b"from dateutil.tz import tzutc\n")
edit("lib/analytics/client.py",
     b"\nID_TYPES = (numbers.Number, string_types)",
     b"\n#: Quoted verbatim in the assertion messages raised by require(), so it\n"
     b"#: stays a tuple rather than collapsing to a bare ``str``.\n"
     b"string_types = (str,)\n"
     b"\nID_TYPES = (numbers.Number, string_types)")

edit("lib/analytics/utils.py", b"\nimport six\n", b"")
edit("lib/analytics/utils.py",
     b"    if isinstance(item, (six.string_types, bool, numbers.Number, datetime,",
     b"    if isinstance(item, (str, bool, numbers.Number, datetime,")
edit("lib/analytics/utils.py",
     b"    for k, v in six.iteritems(dict_):", b"    for k, v in dict_.items():")

# ``Consumer.next`` is deliberately left alone. It is not the iterator
# protocol -- the class is a ``Thread`` with neither ``__iter__`` nor
# ``__next__``, and it returns a whole batch of queue items -- so it is an
# ordinary Python 3 method name and instruction item 3 does not ask for it.
# The golden solution used to rename it to ``next_batch``; that made the
# oracle do work no requirement called for, which is exactly the shape a
# calibration cannot see and a blind agent pays for. Section 7.9.

edit("lib/analytics/test/client.py", b"import six\n", b"")
edit("lib/analytics/test/client.py",
     b"        Client(six.u('unicode_key'))", b"        Client('unicode_key')")
edit("lib/analytics/test/utils.py", b"import six\n", b"")
edit("lib/analytics/test/utils.py",
     b"            'unicode': six.u('woo'),", b"            'unicode': 'woo',")
edit("lib/analytics/test/utils.py",
     b"        if six.PY3:\n"
     b"            item = bytes(10)\n"
     b"        else:\n"
     b"            item = bytearray(10)\n",
     b"        item = bytes(10)\n")


# -------------------------------------------------------- dateutil ----
edit("lib/dateutil/tz.py", b"\nfrom six import string_types, PY3\n", b"")
span("lib/dateutil/tz.py",
     b"def tzname_in_python2(myfunc):\n",
     b"    return inner_func\n\n",
     b"")
edit("lib/dateutil/tz.py", b"    @tzname_in_python2\n", b"", count=6)
edit("lib/dateutil/tz.py",
     b"        if isinstance(fileobj, string_types):",
     b"        if isinstance(fileobj, str):", count=2)

edit("lib/dateutil/relativedelta.py", b"from six import integer_types\n", b"")
edit("lib/dateutil/relativedelta.py",
     b"            if isinstance(weekday, integer_types):",
     b"            if isinstance(weekday, int):")

edit("lib/dateutil/rrule.py",
     b"from fractions import gcd\n\n"
     b"from six import advance_iterator, integer_types\n"
     b"from six.moves import _thread\n",
     b"from math import gcd\n\nimport _thread\n")
edit("lib/dateutil/rrule.py", b"advance_iterator(", b"next(", count=6)
edit("lib/dateutil/rrule.py", b", integer_types):", b", int):", count=12)
edit("lib/dateutil/rrule.py",
     b"            if isinstance(byweekday, integer_types) or hasattr(byweekday, \"n\"):",
     b"            if isinstance(byweekday, int) or hasattr(byweekday, \"n\"):")

edit("lib/dateutil/tzwin.py", b"from six.moves import winreg\n", b"import winreg\n")

edit("lib/dateutil/parser.py",
     b"from six import text_type, binary_type, integer_types\n\n", b"")
edit("lib/dateutil/parser.py", b"import collections\n", b"import collections.abc\n")
edit("lib/dateutil/parser.py",
     b"            if (isinstance(tzinfos, collections.Callable) or",
     b"            if (isinstance(tzinfos, collections.abc.Callable) or")
edit("lib/dateutil/parser.py",
     b"                if isinstance(tzinfos, collections.Callable):",
     b"                if isinstance(tzinfos, collections.abc.Callable):")
edit("lib/dateutil/parser.py", b", text_type):", b", str):", count=2)
edit("lib/dateutil/parser.py", b", binary_type):", b", bytes):", count=2)
edit("lib/dateutil/parser.py", b", integer_types):", b", int):")


# --------------------------------------- the Python 2 iterator aliases ----
#
# ``_timelex`` and ``_genitem`` each define ``__next__`` and then alias
# ``next`` to it, which is how a single source drove the iterator protocol
# under both interpreters -- ``_timelex``'s even says so in the comment. On
# Python 3 nothing reads the alias: ``next(x)`` and ``for`` both go to
# ``__next__``. Both go, and with them the last way to spell ``x.next()`` in
# this tree.
#
# ``analytics.Consumer.next`` is not one of these and stays: its class has no
# ``__next__`` to alias, so it is an ordinary method that happens to be
# spelled ``next``. Sections 7.9 and 7.11.
edit("lib/dateutil/parser.py",
     b"        return token\n"
     b"\n"
     b"    def next(self):\n"
     b"        return self.__next__()  # Python 2.x support\n",
     b"        return token\n")
edit("lib/dateutil/rrule.py",
     b"                self.genlist.remove(self)\n"
     b"\n"
     b"        next = __next__\n",
     b"                self.genlist.remove(self)\n")


# ------------------------------------------------------ the plugin ----
edit("ElasticsearchClient.py", b'    "six",\n', b"")


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: solve01.py <tree>")
    tree = os.path.abspath(argv[1])

    # The mirror at the end is only sound if the two copies started identical.
    import filecmp
    cmp_ = filecmp.dircmp(os.path.join(tree, U3), os.path.join(tree, U3V))
    if cmp_.left_only or cmp_.right_only or cmp_.diff_files:
        raise SystemExit("the two urllib3 copies already differ; the mirror "
                         "step would silently pick one")

    applied = 0
    for rel, old, new, count in EDITS:
        path = os.path.join(tree, rel)
        with open(path, "rb") as fh:
            src = fh.read()
        if isinstance(old, tuple):
            start, end = old
            for marker in (start, end):
                if src.count(marker) != 1:
                    raise SystemExit(
                        "%s: span marker appears %d times, expected 1:\n%r"
                        % (rel, src.count(marker), marker[:120]))
            i = src.index(start)
            j = src.index(end, i) + len(end)
            out = src[:i] + new + src[j:]
        else:
            n = src.count(old)
            if n != count:
                raise SystemExit("%s: anchor appears %d times, expected %d:\n%r"
                                 % (rel, n, count, old[:120]))
            out = src.replace(old, new)
        if out == src:
            raise SystemExit("%s: edit changed nothing" % rel)
        with open(path, "wb") as fh:
            fh.write(out)
        applied += 1

    for rel in DELETES:
        path = os.path.join(tree, rel)
        if not os.path.exists(path):
            raise SystemExit("%s: nothing to delete" % rel)
        os.remove(path)

    # Mirror urllib3 -> requests.packages.urllib3.
    shutil.rmtree(os.path.join(tree, U3V))
    shutil.copytree(os.path.join(tree, U3), os.path.join(tree, U3V))

    print("solve01: %d edits, %d deletions, urllib3 mirrored"
          % (applied, len(DELETES)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
