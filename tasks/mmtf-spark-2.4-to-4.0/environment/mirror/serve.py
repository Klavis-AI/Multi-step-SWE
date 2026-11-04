#!/usr/bin/env python3
"""Containerized stand-ins for every external service the pinned tree reaches.

Everything the pinned tree needs has to live inside the container, so these
services are reproduced rather than reached: each upstream is retired,
relocated or licence-gated, and none of them can be depended on to answer the
same way twice. Seventeen hosts are served. The fixtures are 2019-*shaped* --
built to the shape those services answered with around the tree's own commit
date -- rather than fetched from them.

    mmtf.rcsb.org        /v1.0/{full,reduced}/<ID>       gzipped MessagePack
    www.rcsb.org         /pdb/rest/search          POST  newline-separated IDs
    www.rcsb.org         /pdb/rest/customReport    POST  CSV
    pdbj.org             /rest/mine2_sql?q=<sql>         CSV
    dunbrack.fccc.edu    /Guoli/culledpdb_hh             HTML listing
    dunbrack.fccc.edu    /Guoli/culledpdb_hh/<file>      gzip
    myvariant.info       /v1/query?q=|scroll_id=         JSON
    g2s.genomenexus.org  /api/alignments/<q>/residueMapping   JSON
    www.drugbank.ca      /releases/latest/downloads/<n>  ZIP
    files.rcsb.org       /ligands/download/<ID>.cif      mmCIF chem-comp record
    files.rcsb.org       /dssp/**                        gone, as upstream 404s
    ftp.ebi.ac.uk        /pub/databases/msd/**           SIFTS, gzip (over HTTP)
    github.com           /sbl-sdsc/mmtf-data/raw/master/<f>  ORC mapping file
    www.compbio.dundee.ac.uk  /jpred/downloads/<file>    gzipped tar
    swissmodel.expasy.org     /repository/uniprot/<ac>.json    JSON
    swissmodel.expasy.org     /repository/uniprot/<ac>.pdb?..  PDB model
    pdb-redo.eu          /db/<id>/<id>_final.cif         mmCIF

Four more are not HTTP. BioJava's BlastClustReader, which BlastClusters wraps,
opens an ftp:// URL, so an FTP endpoint speaks enough of RFC 959 for the JDK's
sun.net.ftp client to retrieve a file, and three other hosts are reached the
same way:

    resources.rcsb.org          ftp:/sequence/clusters/bc-<N>.out
    ftp.uniprot.org             ftp:/pub/databases/uniprot/**
    ftp.wwpdb.org               ftp:/pub/pdb/data/status/<yyyymmdd>/*.pdb
    orengoftp.biochem.ucl.ac.uk ftp:/cath/releases/daily-release/newest/*

FTP carries no Host header, so each of those four is pinned to a loopback
address of its own and the FTP server routes on the address the connection
arrived at; see _ftp_routes(). Nothing here serves anything it was not asked
for -- there is no write side, no rename, and RETR resolves under one root.

The MMTF protocol was read from the bytecode of org.rcsb.mmtf.utils.CodecUtils
in mmtf-serialization-1.0.7.jar rather than from documentation:

    getMmtfEntryUrl(id, https, reduced)
        -> ("http://" | "https://") + "mmtf.rcsb.org/v1.0/"
           + ("reduced/" | "full/") + id

ReaderUtils.getByteArrayFromUrl opens that URL, gunzips the body and hands it to
MessagePackSerialization. MmtfReader.readSequenceFile applies exactly the same
two steps to the BytesWritable value of an MMTF-Hadoop sequence file, so the
sequence-file values are already the on-wire bodies and are served verbatim.

Both MMTF download entry points pass https=true, so TLS is mandatory; the CA
that signs this server's certificate is imported into each image's JDK
truststore, and its leaf carries a SAN for every host above.

Routing is strict: an unknown host, or a known host with an unknown path, is a
404. Only the *payload* of a query-keyed endpoint falls back to a shaped default,
because that is what keeps the spark.read().csv / .json pipelines runnable for
coverage; every such fallback is written to the request log so fixture coverage
stays auditable rather than invisible.

One exception. github.com is pinned to the
loopback to serve one recorded blob, but it is also where tools fetch their
own components at container start (a `git clone` of nvm, for one), and a
strict 404 there leaves such a tool unable to install at all. So here a
github.com request whose path is outside the tree's own data publisher
(LOCAL_PREFIXES) is passed through to the real github.com; everything under
it is still answered from the fixture tree, recorded or 404, under the strict
routing above. See _passthrough() below.
"""
import hashlib
import http.server
import os
import socket
import socketserver
import ssl
import struct
import sys
import threading
import urllib.parse

ROOT = os.environ.get('MMTF_MIRROR_ROOT', '/srv/mmtf')
SVC_ROOT = os.environ.get('MMTF_MIRROR_SVC_ROOT', '/srv/svc')

class Gone(object):
    """An error status upstream itself returns, checked on a stated date.

    The status is carried rather than assumed because the two probed cases
    disagree: RCSB's retired /dssp/ tree answers 404, PDB-REDO answers 500 for
    an entry it does not have. Both surface in the client as an IOException out
    of getInputStream, so nothing downstream can tell them apart -- which is
    exactly why the mirror has to, or a marker that says 404 where upstream
    says 500 is one nobody ever checked.
    """

    def __init__(self, status):
        self.status = status


# A route's third possible answer. None means "no fixture, and that is a gap";
# a Gone means "this status is what upstream says, and it was checked". Two
# 404s that mean opposite things must not share a log line.
GONE = Gone(404)
CERT = os.environ.get('MMTF_MIRROR_CERT', '/srv/mmtf-tls/server.pem')
HTTP_PORT = int(os.environ.get('MMTF_MIRROR_HTTP_PORT', '80'))
HTTPS_PORT = int(os.environ.get('MMTF_MIRROR_HTTPS_PORT', '443'))
FTP_PORT = int(os.environ.get('MMTF_MIRROR_FTP_PORT', '21'))
# An FTP control connection carries no Host header, so a single listener cannot
# be told which of several mirrored ftp:// hosts a client meant.
#
# The address the connection arrived on is the missing header. mmtf-mirror-up
# pins each FTP-served host to its own loopback address (127.0.0.2 and up) and
# leaves everything else on 127.0.0.1, so /etc/hosts -- the file that actually
# does the routing -- is also the map, and there is no second list to fall out
# of step with it. A host pinned to a bare 127.0.0.1 is an HTTP host and is
# routed by its Host header.
FTP_HOST = os.environ.get('MMTF_MIRROR_FTP_HOST', 'resources.rcsb.org')
HOSTS_FILE = os.environ.get('MMTF_MIRROR_HOSTS', '/etc/hosts')

# The date column of an ls -l style LIST line. A real server prints each
# entry's mtime; this prints one constant, because the mtimes here are the
# fixture files' own and would move every time the image is rebuilt. An
# expectation recorded against a listing that carries a build date expires
# on the next build.
LIST_STAMP = 'Jan  1  2019'


def _ftp_routes():
    """{loopback address: host} for every host pinned off 127.0.0.1."""
    routes = {'127.0.0.1': FTP_HOST}
    try:
        text = open(HOSTS_FILE).read()
    except OSError:
        return routes
    for line in text.splitlines():
        line = line.split('#', 1)[0].split()
        if len(line) < 2 or not line[0].startswith('127.') or line[0] == '127.0.0.1':
            continue
        for name in line[1:]:
            routes[line[0]] = name
            break
    return routes
REQLOG = os.environ.get('MMTF_MIRROR_REQLOG', '/var/log/mmtf-mirror-requests.log')
# Fixture-capture aid: when set, every query-keyed request is written out as
# <CAPTURE>/<host>/<endpoint>/<key>.request. Fixture payloads are then taken
# from the client that actually issues them rather than reconstructed by eye.
CAPTURE = os.environ.get('MMTF_MIRROR_CAPTURE', '')

_reqlog_lock = threading.Lock()

OCTET = 'application/octet-stream'


def fixture_key(payload):
    """Fixture filename for a query-keyed endpoint.

    The key is a digest of the exact request payload, so a fixture is bound to
    the request it answers and cannot be silently reused for another.
    """
    return hashlib.sha256(payload.encode('utf-8', 'replace')).hexdigest()[:16]


_served_cache = None


def _served_hosts():
    """Hosts this mirror actually answers for, read off the fixture tree.

    mmtf.rcsb.org has no directory under SVC_ROOT -- it is served from the blob
    corpus -- so it is added explicitly; everything else is whatever was baked
    into the image.
    """
    global _served_cache
    if _served_cache is None:
        try:
            names = set(os.listdir(SVC_ROOT))
        except OSError:
            names = set()
        _served_cache = names | {'mmtf.rcsb.org'}
    return _served_cache


# -- github.com passthrough (see the module docstring) -----------------------
#
# The host whose unrecorded paths go to the real service, and the paths that
# never do. Both github.com URLs the tree names (PdbToUniProt:83-84, the live
# one and the commented-out one) are under /sbl-sdsc/, so the whole of the
# tree's data publisher stays on the fixture tree: the recorded ORC blob is
# served from it, and any other path under it 404s here. Everything else on
# github.com -- a tool's git clone, a release download -- is relayed to the
# real host.
PASSTHROUGH_HOST = 'github.com'
LOCAL_PREFIXES = ('/sbl-sdsc/',)
_UPSTREAM_TIMEOUT = 120
_HOP_BY_HOP = frozenset(('connection', 'keep-alive', 'proxy-connection',
                         'proxy-authorization', 'te', 'upgrade', 'expect'))


def _resolve_public(name):
    """IPv4 addresses for name, asked of the nameservers in /etc/resolv.conf.

    Not socket.getaddrinfo(): that reads /etc/hosts first, and /etc/hosts is
    where mmtf-mirror-up pinned this very name to the loopback, so it would
    hand back this mirror. A plain recursive A query is all that is needed.
    """
    servers = []
    try:
        with open('/etc/resolv.conf') as fh:
            for line in fh:
                fields = line.split()
                if len(fields) >= 2 and fields[0] == 'nameserver':
                    servers.append(fields[1])
    except OSError:
        return []
    qname = b''.join(bytes([len(p)]) + p.encode('ascii')
                     for p in name.strip('.').split('.')) + b'\x00'
    for ns in servers:
        qid = os.urandom(2)
        query = (qid + b'\x01\x00' + struct.pack('>HHHH', 1, 0, 0, 0)
                 + qname + struct.pack('>HH', 1, 1))
        family = socket.AF_INET6 if ':' in ns else socket.AF_INET
        try:
            sock = socket.socket(family, socket.SOCK_DGRAM)
            try:
                sock.settimeout(3)
                sock.sendto(query, (ns, 53))
                reply = sock.recv(4096)
            finally:
                sock.close()
            addrs = _parse_a_records(reply, qid)
        except (OSError, IndexError, struct.error):
            continue
        # A loopback answer would relay this mirror to itself.
        addrs = [a for a in addrs if not a.startswith('127.')]
        if addrs:
            return addrs
    return []


def _skip_dns_name(msg, off):
    while True:
        n = msg[off]
        if n == 0:
            return off + 1
        if n & 0xC0 == 0xC0:
            return off + 2
        off += 1 + n


def _parse_a_records(msg, qid):
    if len(msg) < 12 or msg[:2] != qid or (msg[3] & 0x0F) != 0:
        return []
    qdcount, ancount = struct.unpack('>HH', msg[4:8])
    off = 12
    for _ in range(qdcount):
        off = _skip_dns_name(msg, off) + 4
    addrs = []
    for _ in range(ancount):
        off = _skip_dns_name(msg, off)
        rtype, rclass, _ttl, rdlen = struct.unpack('>HHIH', msg[off:off + 10])
        off += 10
        if rtype == 1 and rclass == 1 and rdlen == 4:
            addrs.append(socket.inet_ntoa(msg[off:off + 4]))
        off += rdlen
    return addrs


def _upstream(host, tls):
    """A connected socket to the real host, verified as that host."""
    last = OSError('no public address for %s' % host)
    for addr in _resolve_public(host):
        try:
            raw = socket.create_connection((addr, 443 if tls else 80),
                                           timeout=15)
        except OSError as exc:
            last = exc
            continue
        raw.settimeout(_UPSTREAM_TIMEOUT)
        if not tls:
            return raw
        ctx = ssl.create_default_context()
        ctx.set_alpn_protocols(['http/1.1'])
        try:
            return ctx.wrap_socket(raw, server_hostname=host)
        except (OSError, ssl.SSLError) as exc:
            raw.close()
            last = exc
    raise last


def _read_head(sock, buf):
    """(one response head, the bytes after it) read off sock."""
    while b'\r\n\r\n' not in buf:
        chunk = sock.recv(65536)
        if not chunk:
            raise OSError('upstream closed inside a response head')
        buf += chunk
        if len(buf) > 262144:
            raise OSError('upstream response head too large')
    head, _, rest = buf.partition(b'\r\n\r\n')
    return head, rest


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    # Silence per-request logging; a full run of the tree makes thousands.
    def log_message(self, fmt, *args):
        pass

    # -- MMTF -------------------------------------------------------------

    def _mmtf(self):
        # /v1.0/full/<ID> or /v1.0/reduced/<ID>, nothing else.
        parts = self.path.split('?', 1)[0].strip('/').split('/')
        if len(parts) != 3 or parts[0] != 'v1.0' or parts[1] not in ('full', 'reduced'):
            return None
        pdb_id = parts[2]
        # The service is case-insensitive in the ID; reject anything that is not
        # a bare 4-character identifier so the mirror cannot be walked.
        if len(pdb_id) != 4 or not pdb_id.isalnum():
            return None
        path = os.path.join(ROOT, 'v1.0', parts[1], pdb_id.upper())
        if not os.path.isfile(path):
            # A .gone marker beside the blob, read the same way _static reads
            # one, so an id that is *meant* to be absent can say so with a
            # status instead of falling through to the unrecorded arm.
            #
            # The marker means something weaker here than it does under
            # pdb-redo or /dssp/, and the difference is worth keeping straight.
            # Those two record a fact probed against the live service. This
            # host has no DNS record at all -- it is retired -- so
            # there is no upstream left to probe and nothing to record about
            # it. What a marker declares here is the *mirror's own* contract:
            # this corpus is 82 entries, an id outside it names no structure,
            # and 404 is how that is said. The marker file carries that
            # distinction in its own text rather than leaving a later reader
            # to assume RCSB was asked.
            #
            # Per id, never a blanket rule for everything not in the corpus.
            # An unrecorded 404 from this route is how a fixture nobody wrote
            # shows up, and a route that answered every unknown id politely
            # would take that detector away -- a mistyped real id would 404 as
            # confidently as a deliberate one. The gap count only means
            # something while the gaps are still loud.
            return _gone_marker(path)
        return _read(path), OCTET, 'blob'

    # -- fixture-backed endpoints ------------------------------------------

    def _fixture(self, host, endpoint, payload, ctype):
        """Serve <SVC_ROOT>/<host>/<endpoint>/<key>, else .../_default."""
        if CAPTURE:
            _capture(host, endpoint, payload)
        base = os.path.join(SVC_ROOT, host, endpoint)
        keyed = os.path.join(base, fixture_key(payload))
        if os.path.isfile(keyed):
            return _read(keyed), ctype, 'keyed'
        # A recorded upstream error for this exact request, checked before the
        # default: a query upstream *rejects* is not an unrecorded query. It
        # has an answer and the answer is a status, and serving the default
        # instead would turn a 400 into a plausible-looking success.
        gone = _gone_marker(keyed)
        if gone is not None:
            return gone
        default = os.path.join(base, '_default')
        if os.path.isfile(default):
            return _read(default), ctype, 'default'
        return None

    def _static(self, host, relative, ctype):
        """Serve a named file under <SVC_ROOT>/<host>/, with no path escape."""
        if not relative or '..' in relative or relative.startswith('/'):
            return None
        path = os.path.join(SVC_ROOT, host, relative)
        if not os.path.isfile(path):
            return _gone_marker(path)
        return _read(path), ctype, 'static'

    def _route(self, host, body):
        path = self.path.split('?', 1)[0]
        query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)

        if host == 'mmtf.rcsb.org':
            return self._mmtf()

        if host == 'www.rcsb.org':
            # AdvancedQueryService and CustomReportService both POST a
            # URL-encoded payload and read the response stream.
            if path == '/pdb/rest/search':
                return self._fixture(host, 'search', body, 'text/plain')
            if path == '/pdb/rest/customReport':
                return self._fixture(host, 'customReport', body, 'text/csv')
            return None

        if host == 'pdbj.org':
            if path == '/rest/mine2_sql':
                sql = (query.get('q') or [''])[0]
                return self._fixture(host, 'mine2_sql', sql, 'text/csv')
            return None

        if host == 'dunbrack.fccc.edu':
            # PiscesDownloader reads the directory listing, then one file from
            # it. Every listing line must contain at least two quotes or
            # getFileName() throws StringIndexOutOfBoundsException.
            if path.rstrip('/') == '/Guoli/culledpdb_hh':
                return self._static(host, 'listing.html', 'text/html')
            prefix = '/Guoli/culledpdb_hh/'
            if path.startswith(prefix):
                name = path[len(prefix):]
                return self._static(host, os.path.join('files', name),
                                    'application/gzip')
            return None

        if host == 'ftp.ebi.ac.uk':
            # SIFTS, and over HTTP despite the hostname: PdbToUniProt builds
            # plain http:// URLs for both the chain-level CSV and the per-entry
            # split_xml, and never speaks FTP to this host.
            prefix = '/pub/databases/msd/'
            if path.startswith(prefix):
                return self._static(host, path[len(prefix):],
                                    'application/gzip')
            return None

        if host == 'github.com':
            # The cached residue mapping. Served as one file rather than a
            # directory because addFile() fetches a single file and the reader
            # then points ORC straight at it.
            prefix = '/sbl-sdsc/mmtf-data/raw/master/'
            if path.startswith(prefix):
                return self._static(host, path[len(prefix):], OCTET)
            return None

        if host == 'myvariant.info':
            if path == '/v1/query':
                payload = (query.get('scroll_id') or query.get('q') or [''])[0]
                return self._fixture(host, 'query', payload, 'application/json')
            return None

        if host == 'g2s.genomenexus.org':
            # G2SDataset builds the whole query into the path, so the path is
            # the payload. unquote() because the key is computed over the
            # characters the client meant: this variant id carries ':' and '>',
            # and whether a given JDK's URL.openStream() escapes them is not
            # something a fixture key should depend on.
            if path.startswith('/api/alignments/') \
                    and path.endswith('/residueMapping'):
                return self._fixture(host, 'alignments',
                                     urllib.parse.unquote(path),
                                     'application/json')
            return None

        if host == 'files.rcsb.org':
            # BioJava's chemical component dictionary. Nothing in this tree names
            # this host -- DownloadChemCompProvider does, from inside the jar, one
            # GET per distinct residue name in a PDB-format input. Unserved, the
            # ChemComp comes back with a null type and MmtfStructureWriter:105
            # NPEs, which turns PdbToMmtfFull and RosettaToMmtfFull red;
            # unpinned, the answer would come from the live RCSB service
            # instead, and what the tree printed would depend on the day.
            prefix = '/ligands/download/'
            if path.startswith(prefix):
                return self._static(host, os.path.join('ligands', 'download',
                                                       path[len(prefix):]),
                                    'text/plain')
            # MmtfUtils.calculateDsspSecondaryStructure falls back to
            # downloading a precomputed DSSP file whenever the local calculation
            # refuses the entry ("Not enough backbone groups ... 0 given,
            # minimum 5" -- nucleic acids, sugars, 11 entries of this corpus).
            # RCSB retired that tree: probed 2026-09-16, /dssp/hy/1hya/1hya.dssp.gz
            # and two others answer 404 with a 114-byte body over both http and
            # https. So 404 *is* the upstream answer here, not a fixture nobody
            # wrote, and BioJava's own catch turns it into "There will not be
            # secondary structure assignment for this structure". Returned as
            # GONE rather than None so the request log can say which of the two
            # kinds of 404 this was; a fixture tree that has stopped being
            # complete must not be able to hide behind a dead endpoint.
            if path.startswith('/dssp/'):
                return GONE
            return None

        if host == 'www.compbio.dundee.ac.uk':
            # One archive, fetched whole by JpredDataset and streamed through
            # gzip -> tar. Served under its own path rather than by digest
            # because the class builds a constant URL and never varies it.
            prefix = '/jpred/downloads/'
            if path.startswith(prefix):
                return self._static(host, os.path.join('jpred', 'downloads',
                                                       path[len(prefix):]),
                                    'application/gzip')
            return None

        if host == 'swissmodel.expasy.org':
            # SwissModelDataset builds
            #   /repository/uniprot/<ac>.json?provider=swissmodel
            # and hands the response stream straight to a temp file, so the
            # query string selects a provider rather than keying the answer:
            # the path alone addresses the record. Note what an unserved answer
            # costs here -- getSwissModelsRawData catches the IOException, prints
            # "WARNING: Could not load data for:" and carries on with a shorter
            # list, so a missing fixture is a quieter dataset rather than a red
            # job.
            prefix = '/repository/uniprot/'
            if path.startswith(prefix) and path.endswith('.json'):
                return self._static(host, os.path.join('repository', 'uniprot',
                                                       path[len(prefix):]),
                                    'application/json')
            # Coordinates, for MmtfImporter.downloadSwissModelsBy{UniProtIds,Urls}.
            # Here the query string *does* key the answer: range= and template=
            # pick one of the several models an entry has, and the three P24539
            # models differ by 63KB and 800 atoms. So the fixture is bound to the
            # whole request line rather than to the path, and there is
            # deliberately no _default under model/ -- an unkeyed request must
            # 404 rather than be answered with some other model, which is exactly
            # the mistake a path-keyed fixture would have made invisible.
            if path.startswith(prefix) and path.endswith('.pdb'):
                return self._fixture(host, 'model', self.path, 'chemical/x-pdb')
            return None

        if host == 'pdb-redo.eu':
            # MmtfImporter.downloadPdbRedo builds /db/<id>/<id>_final.cif with
            # the id lower-cased. Only two entries are mirrored; a third id in
            # the same request 404s on purpose, because getFromMmcifUrl's whole
            # error path is that 404 -- it catches, prints "WARNING: could not
            # parse", and returns null for the filter below it to drop. Upstream
            # answers a missing entry with 500 and a 21-byte {"error":"Not Found"}
            # body, which reaches the client as the same IOException; probed
            # 2026-09-16 for 1stp, 1pen and 4hhb. That 500 is declared in a
            # .gone marker beside the entry rather than left to fall through as
            # a miss, so this tree's count of unserved routes can stay at zero
            # and mean something: any entry in it is a fixture nobody wrote.
            # Lower-cased because upstream is case-insensitive in the entry
            # directory (probed 2026-09-16: /db/2ONX/2onx_final.cif answers 200),
            # and downloadPdbRedo lower-cases only the filename, not the
            # directory it builds from the caller's id.
            prefix = '/db/'
            if path.startswith(prefix) and path.endswith('_final.cif'):
                return self._static(host,
                                    os.path.join('db', path[len(prefix):].lower()),
                                    'chemical/x-cif')
            return None

        if host == 'www.drugbank.ca':
            prefix = '/releases/latest/downloads/'
            if path.startswith(prefix):
                rel = os.path.join('downloads', path[len(prefix):])
                hit = self._static(host, rel, 'application/zip')
                # Three of DrugBankDataset's four URL-building entry points
                # are behind a subscription, and the only thing separating
                # them from the open one in the client is the Base64
                # Authorization header getInputStream builds. Serving them unconditionally
                # would make that header dead weight: an edit that dropped it
                # would still be green. So the gate is reproduced, from a
                # .auth marker beside each gated fixture. It is
                # consulted only once the file has been found, so a gated name
                # that is missing stays a miss rather than becoming a 403.
                if hit is not None and not isinstance(hit, Gone):
                    gate = _gone_marker(os.path.join(SVC_ROOT, host, rel),
                                        '.auth')
                    if gate is not None and not self.headers.get(
                            'authorization'):
                        return gate
                return hit
            return None

        return None

    # -- passthrough --------------------------------------------------------

    def _passthrough(self):
        """Relay this request to the real host when it is not ours to answer.

        Returns False, having read nothing past the headers, for every request
        the fixture tree answers -- every host but PASSTHROUGH_HOST, and every
        path of it under LOCAL_PREFIXES. Otherwise relays the request, writes
        the real host's response back, and returns True.
        """
        host = (self.headers.get('Host') or '').split(':')[0].lower()
        if host != PASSTHROUGH_HOST:
            return False
        if self.path.split('?', 1)[0].startswith(LOCAL_PREFIXES):
            return False
        # One request per connection, both ways: the next request on this
        # socket could be a path the fixture tree answers, so it has to come
        # back through the router rather than down an open relay.
        self.close_connection = True
        tls = isinstance(self.connection, ssl.SSLSocket)
        try:
            up = _upstream(host, tls)
        except Exception as exc:
            _log('pass 502 %s %s %s %r' % (self.command, host, self.path, exc))
            self.send_response(502)
            self.send_header('Content-Length', '0')
            self.send_header('Connection', 'close')
            self.end_headers()
            return True
        _log('pass %s %s %s' % (self.command, host, self.path))
        try:
            head = [self.requestline]
            for name, value in self.headers.items():
                if name.lower() not in _HOP_BY_HOP:
                    head.append('%s: %s' % (name, value))
            head.append('Connection: close')
            up.sendall(('\r\n'.join(head) + '\r\n\r\n').encode('latin-1'))
            self._relay_body(up)
            buf = b''
            while True:
                status, buf = _read_head(up, buf)
                lines = status.split(b'\r\n')
                code = (lines[0].split(b' ', 2) + [b''])[1]
                if code.startswith(b'1') and code != b'101':
                    # An interim response (103 Early Hints): passed on as is.
                    self.wfile.write(status + b'\r\n\r\n')
                    continue
                break
            kept = [l for l in lines[1:]
                    if l.split(b':', 1)[0].strip().lower()
                    not in (b'connection', b'keep-alive')]
            self.wfile.write(b'\r\n'.join([lines[0]] + kept
                                          + [b'Connection: close'])
                             + b'\r\n\r\n')
            if buf:
                self.wfile.write(buf)
            while True:
                chunk = up.recv(65536)
                if not chunk:
                    break
                self.wfile.write(chunk)
        except Exception as exc:               # never take the mirror down
            _log('pass aborted %s %s %s %r' % (self.command, host, self.path,
                                               exc))
        finally:
            up.close()
        return True

    def _relay_body(self, up):
        """Copy the request body, framed as the client framed it, upstream."""
        if 'chunked' in (self.headers.get('Transfer-Encoding') or '').lower():
            while True:
                line = self.rfile.readline(65537)
                if not line:
                    raise OSError('client closed inside a chunked body')
                up.sendall(line)
                size = int(line.split(b';', 1)[0].strip(), 16)
                if size == 0:
                    # Trailers, then the blank line that ends the body.
                    while line not in (b'\r\n', b'\n', b''):
                        line = self.rfile.readline(65537)
                        up.sendall(line)
                    return
                self._relay_exact(up, size + 2)      # the chunk and its CRLF
        self._relay_exact(up, int(self.headers.get('Content-Length') or 0))

    def _relay_exact(self, up, left):
        while left > 0:
            data = self.rfile.read(min(left, 65536))
            if not data:
                raise OSError('client closed inside a request body')
            up.sendall(data)
            left -= len(data)

    # -- plumbing ---------------------------------------------------------

    def do_HEAD(self):
        if self._passthrough():
            return
        self._respond(b'', with_body=False)

    def do_GET(self):
        if self._passthrough():
            return
        self._respond(b'', with_body=True)

    def do_POST(self):
        if self._passthrough():
            return
        length = int(self.headers.get('Content-Length') or 0)
        self._respond(self.rfile.read(length) if length else b'', with_body=True)

    def _respond(self, raw_body, with_body):
        host = (self.headers.get('Host') or '').split(':')[0].lower()
        body = raw_body.decode('utf-8', 'replace')
        try:
            hit = self._route(host, body)
        except Exception as exc:               # never take the mirror down
            sys.stderr.write('mirror: %s %s: %r\n' % (host, self.path, exc))
            sys.stderr.flush()
            hit = None

        if isinstance(hit, Gone):
            # Deliberate, recorded, and not a gap: see the /dssp/ branch above
            # and the .gone markers in the fixture tree.
            _log('gone %d %s %s %s' % (hit.status, self.command, host,
                                       self.path))
            self.send_response(hit.status)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return

        if hit is None:
            # A host this mirror serves and a host it merely *pins* fail the same
            # way to the client -- 404 -- but they mean opposite things to
            # whoever maintains the fixtures. A 404 on a served host is a fixture
            # that was not written; a 404 on a pinned host is the intended answer
            # for a service nobody mirrored, and counting it as a gap would bury the one
            # signal that says the fixture tree is complete. The served set is
            # read off the tree, not hand-listed, so it cannot drift from it.
            if host and host not in _served_hosts():
                _log('unserved 404 %s %s %s' % (self.command, host, self.path))
            else:
                _log('404 %s %s %s' % (self.command, host, self.path))
            # The real services answered 404 for an unknown entry, which
            # surfaces in the client as FileNotFoundException.
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return

        data, ctype, kind = hit
        _log('200 %s %s %s %s' % (self.command, host, self.path, kind))
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        if with_body:
            self.wfile.write(data)


def _read(path):
    with open(path, 'rb') as fh:
        return fh.read()


def _gone_marker(path, suffix='.gone'):
    """A recorded upstream error, read off the tree beside the fixture.

    Written as <file><suffix>, first line "status: NNN" and the rest the probe
    that established it. Two suffixes are in use and they answer two different
    questions about the same kind of file:

      .gone  the fixture is absent on purpose -- upstream has no such entry,
             and this is the status it says so with.
      .auth  the fixture is present but upstream gates it, and this is the
             status it answers a request that arrives without credentials.

    Both are read off the same tree as the fixtures, for the same reason the
    served host set is (see the 404 arm below): a list of them kept anywhere
    else is a list that goes stale without anything going red.

    A malformed marker returns None -- i.e. counts as a gap, or as an ungated
    file. The failure that is loud is the one worth defaulting to.
    """
    try:
        text = open(path + suffix).read()
    except OSError:
        return None
    first = text.split('\n', 1)[0].strip()
    if not first.startswith('status:'):
        return None
    try:
        return Gone(int(first.split(':', 1)[1]))
    except ValueError:
        return None


def _capture(host, endpoint, payload):
    out = os.path.join(CAPTURE, host, endpoint)
    try:
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, fixture_key(payload) + '.request'), 'w') as fh:
            fh.write(payload)
    except OSError:
        pass


def _log(line):
    # Fixture coverage has to be auditable: a default-served request that nobody
    # can see is indistinguishable from a recorded one.
    try:
        with _reqlog_lock:
            with open(REQLOG, 'a') as fh:
                fh.write(line + '\n')
    except OSError:
        pass


class Server(socketserver.ThreadingTCPServer):
    # Spark runs the downloads as a parallel map, so the mirror must be threaded.
    daemon_threads = True
    allow_reuse_address = True


# --------------------------------------------------------------------------
# FTP
# --------------------------------------------------------------------------

class FtpHandler(socketserver.StreamRequestHandler):
    """Enough of RFC 959 for sun.net.ftp.impl.FtpClient to do a passive RETR.

    The JDK's client logs in, tries EPSV, falls back to PASV when that is
    refused, changes to the file's directory and issues RETR. A URL that names a
    directory rather than a file -- HadoopUpdate's status/ root -- takes the
    other branch of FtpURLConnection.getInputStream(), which changes directory
    and reads the listing, so LIST and NLST are here too. Anything outside those
    paths answers 500: a mirror that guesses at commands it was not built for is
    a mirror whose failures look like data.
    """

    def handle(self):
        self.cwd = '/'
        self.data_sock = None
        # Fixed for the life of the connection: the client reached one address,
        # and every command on this socket is for the host pinned to it.
        local = self.request.getsockname()[0]
        self.host = _ftp_routes().get(local)
        if self.host is None:
            # Not a guess and not the default tree. An address nothing is pinned
            # to is a mirror that has been reconfigured out from under a fixture
            # set, and answering it from resources.rcsb.org would serve one
            # host's files under another host's name.
            _log('ftp misroute %s' % local)
            self._send(421, 'no host pinned to %s' % local)
            return
        self._send(220, 'mirror ready')
        while True:
            try:
                raw = self.rfile.readline()
            except OSError:
                return
            if not raw:
                return
            line = raw.decode('utf-8', 'replace').strip()
            verb, _, arg = line.partition(' ')
            verb = verb.upper()
            _log('ftp %s %s %s' % (self.host, verb, arg))
            if verb == 'QUIT':
                self._send(221, 'bye')
                return
            handler = getattr(self, '_cmd_' + verb, None)
            if handler is None:
                self._send(500, 'not understood')
                continue
            try:
                handler(arg)
            except Exception as exc:                 # never drop the listener
                sys.stderr.write('mirror: ftp %s: %r\n' % (verb, exc))
                sys.stderr.flush()
                self._send(550, 'failed')

    # -- session -----------------------------------------------------------

    def _cmd_USER(self, arg):
        self._send(331, 'need password')

    def _cmd_PASS(self, arg):
        self._send(230, 'logged in')

    def _cmd_SYST(self, arg):
        self._send(215, 'UNIX Type: L8')

    def _cmd_TYPE(self, arg):
        self._send(200, 'type set')

    def _cmd_NOOP(self, arg):
        self._send(200, 'ok')

    def _cmd_PWD(self, arg):
        self._send(257, '"%s"' % self.cwd)

    def _cmd_CWD(self, arg):
        target = arg if arg.startswith('/') else self.cwd.rstrip('/') + '/' + arg
        if os.path.isdir(self._resolve(target)):
            self.cwd = target
            self._send(250, 'ok')
        else:
            self._send(550, 'no such directory')

    # -- transfer ----------------------------------------------------------

    def _cmd_PASV(self, arg):
        if self.data_sock:
            self.data_sock.close()
        # Advertise the address this control connection already arrived on, not a
        # literal: the client reaches the mirror through /etc/hosts, and a PASV
        # reply naming a different interface is a hang, not an error.
        host = self.request.getsockname()[0]
        self.data_sock = socket.socket()
        self.data_sock.bind((host, 0))
        self.data_sock.listen(1)
        port = self.data_sock.getsockname()[1]
        self._send(227, 'Entering Passive Mode (%s,%d,%d)'
                   % (host.replace('.', ','), port >> 8, port & 0xFF))

    def _cmd_SIZE(self, arg):
        path = self._resolve(arg)
        if os.path.isfile(path):
            self._send(213, str(os.path.getsize(path)))
        else:
            self._send(550, 'no such file')

    def _cmd_RETR(self, arg):
        path = self._resolve(arg)
        if not os.path.isfile(path):
            _log('ftp 550 RETR %s %s' % (self.host, arg))
            self._send(550, 'no such file')
            return
        self._transfer(_read(path), 'RETR', arg)

    def _cmd_LIST(self, arg):
        self._listing(arg, 'LIST')

    def _cmd_NLST(self, arg):
        self._listing(arg, 'NLST')

    def _listing(self, arg, verb):
        # A bare LIST after CWD is what the JDK sends; a flag argument is what
        # some clients send instead, and it is not a path.
        target = self.cwd if not arg or arg.startswith('-') else arg
        path = self._resolve(target)
        if not os.path.isdir(path):
            _log('ftp 550 %s %s %s' % (verb, self.host, arg))
            self._send(550, 'no such directory')
            return
        lines = []
        for name in sorted(os.listdir(path)):
            if verb == 'NLST':
                lines.append(name)
                continue
            full = os.path.join(path, name)
            isdir = os.path.isdir(full)
            lines.append('%srwxr-xr-x 1 mirror mirror %12d %s %s'
                         % ('d' if isdir else '-',
                            0 if isdir else os.path.getsize(full),
                            LIST_STAMP, name))
        # No "total N" header, and the reason is a consumer rather than taste.
        # HadoopUpdate reads this listing line by line and takes
        # line.substring(line.length()-8) of every one of them, inside a try
        # whose catch swallows the whole listing. A seven-character "total 4"
        # would throw StringIndexOutOfBounds and the run would print no dates at
        # all, with nothing anywhere saying why.
        self._transfer(''.join(l + '\r\n' for l in lines).encode(), verb, target)

    def _transfer(self, payload, verb, arg):
        if self.data_sock is None:
            self._send(425, 'use PASV first')
            return
        self._send(150, 'opening data connection')
        conn, _ = self.data_sock.accept()
        try:
            conn.sendall(payload)
        finally:
            conn.close()
            self.data_sock.close()
            self.data_sock = None
        _log('ftp 226 %s %s %s static' % (verb, self.host, arg))
        self._send(226, 'transfer complete')

    # -- plumbing ----------------------------------------------------------

    def _resolve(self, arg):
        """Map an FTP path under one root, with no escape."""
        path = arg if arg.startswith('/') else self.cwd.rstrip('/') + '/' + arg
        root = os.path.join(SVC_ROOT, self.host)
        full = os.path.normpath(os.path.join(root, path.lstrip('/')))
        if full != root and not full.startswith(root + os.sep):
            return os.path.join(root, '\0')          # never a real path
        return full

    def _send(self, code, text):
        self.wfile.write(('%d %s\r\n' % (code, text)).encode())


def serve_ftp(port):
    srv = Server(('0.0.0.0', port), FtpHandler)
    srv.serve_forever()


def serve(port, tls):
    srv = Server(('0.0.0.0', port), Handler)
    if tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(CERT)
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    srv.serve_forever()


def main():
    full = os.path.join(ROOT, 'v1.0', 'full')
    reduced = os.path.join(ROOT, 'v1.0', 'reduced')
    # Markers are not blobs. Counting them here would let the banner report a
    # corpus one entry larger than the one it can serve.
    def n_blobs(d):
        if not os.path.isdir(d):
            return 0
        return len([f for f in os.listdir(d) if not f.endswith(('.gone', '.auth'))])
    n_full, n_red = n_blobs(full), n_blobs(reduced)
    if n_full == 0:
        sys.stderr.write('mmtf-mirror: no blobs under %s\n' % full)
        return 1
    hosts = sorted(os.listdir(SVC_ROOT)) if os.path.isdir(SVC_ROOT) else []
    sys.stderr.write('mmtf-mirror: full=%d reduced=%d svc=[%s] http=%d https=%d ftp=%d\n'
                     % (n_full, n_red, ' '.join(hosts), HTTP_PORT, HTTPS_PORT,
                        FTP_PORT))
    sys.stderr.flush()
    for target, args in ((serve, (HTTP_PORT, False)), (serve_ftp, (FTP_PORT,))):
        threading.Thread(target=target, args=args, daemon=True).start()
    serve(HTTPS_PORT, True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
