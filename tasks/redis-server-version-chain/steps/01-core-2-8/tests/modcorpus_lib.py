"""Shared pieces of the module corpus: RESP client, reply encoding and the
normalisers that hide values no two servers can agree on.

Used by tools_modcorpus.py (records against the redis:8.0 reference) and
tools_modcheck.py (replays against any server).
"""

import socket

# --------------------------------------------------------------------------
# RESP2 client. Every byte string is carried as a latin-1 str so that a reply
# round-trips through JSON without losing a single byte.
# --------------------------------------------------------------------------


class Resp:
    def __init__(self, host='127.0.0.1', port=6379, timeout=20):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b''

    def _fill(self):
        d = self.sock.recv(1 << 16)
        if not d:
            raise EOFError('connection closed by the server')
        self.buf += d

    def _line(self):
        while b'\r\n' not in self.buf:
            self._fill()
        i = self.buf.index(b'\r\n')
        line, self.buf = self.buf[:i], self.buf[i + 2:]
        return line

    def _read(self):
        line = self._line()
        t, rest = line[:1], line[1:]
        if t == b'+':
            return ('status', rest.decode('latin-1'))
        if t == b'-':
            return ('error', rest.decode('latin-1'))
        if t == b':':
            return ('int', int(rest))
        if t == b',':
            return ('double', rest.decode('latin-1'))
        if t == b'#':
            return ('bool', rest == b't')
        if t == b'_':
            return ('null', None)
        if t == b'(':
            return ('bignum', rest.decode('latin-1'))
        if t in (b'$', b'='):
            n = int(rest)
            if n == -1:
                return ('null', None)
            while len(self.buf) < n + 2:
                self._fill()
            v, self.buf = self.buf[:n], self.buf[n + 2:]
            return ('bulk', v.decode('latin-1'))
        if t in (b'*', b'~', b'>'):
            n = int(rest)
            if n == -1:
                return ('null', None)
            return ('array', [self._read() for _ in range(n)])
        if t == b'%':
            n = int(rest)
            out = []
            for _ in range(n):
                out.append(self._read())
                out.append(self._read())
            return ('array', out)
        raise ValueError('unexpected RESP type byte %r' % (t,))

    def cmd(self, *args):
        out = b'*%d\r\n' % len(args)
        for a in args:
            if isinstance(a, str):
                a = a.encode('latin-1')
            elif not isinstance(a, bytes):
                a = str(a).encode('latin-1')
            out += b'$%d\r\n%s\r\n' % (len(a), a)
        self.sock.sendall(out)
        return self._read()

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


# --------------------------------------------------------------------------
# JSON encoding of a reply
# --------------------------------------------------------------------------


def enc(r):
    t, v = r
    if t == 'array':
        return [t, [enc(x) for x in v]]
    return [t, v]


def dec(j):
    t, v = j
    if t == 'array':
        return (t, [dec(x) for x in v])
    return (t, v)


def render(j):
    """A stable one-line rendering, used for sorting and for diff output."""
    t, v = j
    if t == 'array':
        return '[' + ', '.join(render(x) for x in v) + ']'
    return '%s(%r)' % (t, v)


# --------------------------------------------------------------------------
# Normalisers
#
# Every rule replaces the parts of a reply that cannot be reproduced by a
# second, independent server. Both the recorder and the checker run the same
# rule, so what ends up compared is only the part that is genuinely defined.
# --------------------------------------------------------------------------

MEM_FIELDS = ('memoryUsage',)
VINFO_FIELDS = ('max-level', 'vset-uid', 'hnsw-max-node-uid')


def _mask_after(j, names, marker):
    """In a flat [name, value, name, value, ...] array, blank out the values
    that follow any of `names`."""
    t, v = j
    if t != 'array':
        return j
    out = []
    mask_next = False
    for item in v:
        if mask_next:
            out.append(['bulk', marker])
            mask_next = False
            continue
        out.append(item)
        if item[0] in ('status', 'bulk') and item[1] in names:
            mask_next = True
    return [t, out]


def n_exact(j):
    return j


def n_sorted_array(j):
    t, v = j
    if t != 'array':
        return j
    return [t, sorted(v, key=render)]


def n_scan(j):
    t, v = j
    if t != 'array' or len(v) != 2:
        return j
    keys = v[1]
    if keys[0] == 'array':
        keys = ['array', sorted(keys[1], key=render)]
    return [t, [['bulk', '<cursor>'], keys]]


def n_ts_info(j):
    return _mask_after(j, MEM_FIELDS, '<memory>')


def n_vinfo(j):
    return _mask_after(j, VINFO_FIELDS, '<counter>')


def n_opaque(j):
    """The value is implementation defined (an RDB/scan-dump payload)."""
    t, v = j
    if t == 'array':
        return [t, [n_opaque(x) for x in v]]
    if t in ('bulk', 'int'):
        return [t, '<opaque>']
    return j


def n_varies_int(j):
    """An integer taken from the wall clock (a TTL, a "*" timestamp)."""
    t, v = j
    if t == 'int':
        return [t, '<int>']
    if t == 'array':
        return [t, [n_varies_int(x) for x in v]]
    return j


def n_random_member(j):
    """VRANDMEMBER picks at random; only the shape is defined."""
    t, v = j
    if t == 'array':
        return [t, [n_random_member(x) for x in v]]
    if t == 'bulk':
        return [t, '<member>']
    return j


def n_vsim_top1(j):
    """VSIM is an approximate search: only the best match and the number of
    results are stable for well separated vectors."""
    t, v = j
    if t != 'array':
        return j
    head = v[0] if v else None
    return [t, [head, ['bulk', '<n=%d>' % len(v)]] if head else []]


def n_ts_sample_ts(j):
    """A sample whose timestamp came from the clock: [<int>, value]."""
    t, v = j
    if t != 'array':
        return j
    out = []
    for item in v:
        if item[0] == 'array' and len(item[1]) == 2 and item[1][0][0] == 'int':
            out.append(['array', [['int', '<int>'], item[1][1]]])
        elif item[0] == 'int':
            out.append(['int', '<int>'])
        else:
            out.append(item)
    return [t, out]


def n_keys_only(j):
    """A flat [name, value, ...] reply whose NAMES are defined and whose values
    are memory figures no two implementations agree on (MEMORY STATS)."""
    t, v = j
    if t != 'array':
        return j
    out = []
    for i, item in enumerate(v):
        out.append(item if i % 2 == 0 else ['bulk', '<value>'])
    return [t, out]


def n_any_string(j):
    """Only the type is defined: the text is advisory prose (MEMORY DOCTOR,
    MEMORY MALLOC-STATS, LATENCY DOCTOR)."""
    t, v = j
    if t in ('bulk', 'status', 'verbatim'):
        return [t, '<text>']
    return j


def n_shape(j):
    """Only the nesting and the reply types are defined, not any value."""
    t, v = j
    if t == 'array':
        return [t, [n_shape(x) for x in v]]
    return [t, '<%s>' % t]


# The MEMORY STATS fields redis 4.0 defines. Later releases add fields and never
# remove these, so keeping only these -- in order, values masked -- is a rule
# that a 4.0 server and an 8.0 server both satisfy, which is what lets one
# frozen reference answer for every rung.
MEMSTATS_CORE = (
    'peak.allocated', 'total.allocated', 'startup.allocated',
    'replication.backlog', 'clients.slaves', 'clients.normal', 'aof.buffer',
    'db.0', 'overhead.total', 'keys.count', 'keys.bytes-per-key',
    'dataset.bytes', 'dataset.percentage', 'peak.percentage', 'fragmentation',
)


def n_keys_core(j):
    t, v = j
    if t != 'array':
        return j
    out = []
    i = 0
    while i + 1 < len(v):
        name = v[i]
        if name[0] in ('status', 'bulk') and name[1] in MEMSTATS_CORE:
            out.append(name)
            out.append(['bulk', '<value>'])
        i += 2
    return [t, out]


def n_client_flags(j):
    """Only the `flags=` field of a CLIENT INFO / CLIENT LIST line: everything
    else on that line is addresses, ids, buffer sizes and clocks."""
    t, v = j
    if t not in ('bulk', 'status') or not isinstance(v, str):
        return j
    out = []
    for line in v.split('\n'):
        for tok in line.split():
            if tok.startswith('flags='):
                out.append(tok)
    return [t, ' '.join(out)]


def n_entry_fields(j):
    """An array of flat [name, value, ...] entries whose NAMES are defined and
    whose values are clocks, addresses and counters (ACL LOG, CLIENT LIST)."""
    t, v = j
    if t != 'array':
        return j
    out = []
    for item in v:
        if item[0] != 'array':
            out.append(item)
            continue
        fields = []
        for i, x in enumerate(item[1]):
            fields.append(x if i % 2 == 0 else ['bulk', '<value>'])
        out.append(['array', fields])
    return [t, out]


# The ACL LOG entry fields redis 6.2 defines. 8.0 adds `entry-id`,
# `timestamp-created` and `timestamp-last-updated` and removes none, so keeping
# only these -- names in order, values masked -- is a rule both satisfy.
ACLLOG_CORE = ('count', 'reason', 'context', 'object', 'username',
               'age-seconds', 'client-info')


def n_acl_log_core(j):
    t, v = j
    if t != 'array':
        return j
    out = []
    for item in v:
        if item[0] != 'array':
            out.append(item)
            continue
        fields = []
        pairs = item[1]
        for i in range(0, len(pairs) - 1, 2):
            name = pairs[i]
            if name[0] in ('status', 'bulk') and name[1] in ACLLOG_CORE:
                fields.append(name)
                fields.append(['bulk', '<value>'])
        out.append(['array', fields])
    return [t, out]


def n_info_fields(j):
    """An INFO section reduced to its field NAMES, in order, values dropped.

    Every value in INFO is a counter, an offset, an address or a clock; the
    field set and its order are the part a client can rely on."""
    t, v = j
    if t not in ('bulk', 'status', 'verbatim') or not isinstance(v, str):
        return j
    names = []
    for line in v.replace('\r\n', '\n').split('\n'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if ':' in line:
            names.append(line.split(':', 1)[0])
    return [t, ' '.join(names)]


# The `INFO replication` fields redis 2.8 defines. Later releases add fields and
# keep these (the `slave_*` names survive as the compatibility spelling), so
# keeping only these -- in order, values dropped -- is a rule a 2.8 server and an
# 8.0 server both satisfy.
INFOREPL_CORE = (
    'role', 'connected_slaves', 'master_repl_offset', 'repl_backlog_active',
    'repl_backlog_size', 'repl_backlog_first_byte_offset', 'repl_backlog_histlen',
    'master_host', 'master_port', 'master_link_status',
    'master_last_io_seconds_ago', 'master_sync_in_progress', 'slave_repl_offset',
    'slave_priority', 'slave_read_only',
)


def n_info_repl_core(j):
    t, v = j
    if t not in ('bulk', 'status', 'verbatim') or not isinstance(v, str):
        return j
    names = []
    for line in v.replace('\r\n', '\n').split('\n'):
        line = line.strip()
        if ':' in line and not line.startswith('#'):
            n = line.split(':', 1)[0]
            if n in INFOREPL_CORE:
                names.append(n)
    return [t, ' '.join(names)]


INFOSTATS_CORE = ('sync_full', 'sync_partial_ok', 'sync_partial_err',
                  'evicted_keys',
                  'total_connections_received', 'total_commands_processed',
                  'expired_keys', 'keyspace_hits', 'keyspace_misses',
                  'pubsub_channels', 'pubsub_patterns')


def n_info_stats_core(j):
    t, v = j
    if t not in ('bulk', 'status', 'verbatim') or not isinstance(v, str):
        return j
    names = [l.split(':', 1)[0] for l in v.replace('\r\n', '\n').split('\n')
             if ':' in l and not l.strip().startswith('#')]
    return [t, ' '.join(n for n in names if n in INFOSTATS_CORE)]


# Field sets for the INFO sections the instructions name. Each list holds the
# names that release defines and that 8.0 still defines, so the rule is
# era-invariant; the values are dropped, because every one of them is a counter,
# a clock or an address.
INFO_CORE = {
    'memory': ('used_memory', 'used_memory_human', 'used_memory_rss',
               'used_memory_peak', 'used_memory_lua', 'maxmemory',
               'maxmemory_policy', 'mem_fragmentation_ratio', 'mem_allocator',
               'lazyfree_pending_objects'),
    'persistence': ('loading', 'rdb_changes_since_last_save',
                    'rdb_bgsave_in_progress', 'rdb_last_save_time',
                    'rdb_last_bgsave_status', 'aof_enabled',
                    'aof_rewrite_in_progress', 'aof_last_bgrewrite_status',
                    'aof_last_write_status', 'aof_rewrites', 'aof_base_size',
                    'aof_current_size', 'aof_pending_rewrite'),
    'clients': ('connected_clients', 'blocked_clients',
                'client_recent_max_input_buffer',
                'client_recent_max_output_buffer'),
    'server': ('redis_version', 'redis_mode', 'os', 'arch_bits',
               'process_id', 'tcp_port', 'uptime_in_seconds', 'uptime_in_days',
               'executable', 'config_file', 'io_threads_active'),
}


def _info_core(section):
    names = INFO_CORE[section]

    def rule(j):
        t, v = j
        if t not in ('bulk', 'status', 'verbatim') or not isinstance(v, str):
            return j
        got = [l.split(':', 1)[0] for l in v.replace('\r\n', '\n').split('\n')
               if ':' in l and not l.strip().startswith('#')]
        return [t, ' '.join(n for n in got if n in names)]
    return rule


# The XINFO STREAM fields redis 5.0 defines; 8.0 adds more and removes none.
XINFO_CORE = ('length', 'radix-tree-keys', 'radix-tree-nodes', 'groups',
              'last-generated-id', 'first-entry', 'last-entry',
              'name', 'consumers', 'pending', 'idle', 'pel-count')


def n_xinfo_core(j):
    t, v = j
    if t != 'array':
        return j
    def one(item):
        if item[0] != 'array':
            return item
        out = []
        p = item[1]
        pairs = []
        for i in range(0, len(p) - 1, 2):
            if p[i][0] in ('bulk', 'status') and p[i][1] in XINFO_CORE:
                pairs.append(p[i][1])
        # sorted: XINFO's field order is not part of the contract and 8.0
        # reports it differently from 5.0
        for n in sorted(pairs):
            out.append(['bulk', n])
            out.append(['bulk', '<value>'])
        return ['array', out]
    # XINFO STREAM is one flat map; GROUPS/CONSUMERS are arrays of maps
    if v and v[0][0] == 'array':
        return [t, [one(x) for x in v]]
    return one([t, v])



def n_redirect(j):
    """`-MOVED <slot> <ip>:<port>` / `-ASK <slot> <ip>:<port>` with the address
    masked and the slot kept.

    The address is the recording's own port and cannot be an expectation; the
    kind of redirection and the slot number are the whole of what the protocol
    specifies, and they are what a candidate has to get right. Anything that is
    not a redirection passes through unchanged, so an implementation that
    answers `+OK` where redis answers `-ASK 0 ...` still fails.
    """
    t, v = j
    if t != 'error' or not isinstance(v, str):
        return j
    parts = v.split()
    if len(parts) == 3 and parts[0] in ('MOVED', 'ASK'):
        return [t, '%s %s <addr>' % (parts[0], parts[1])]
    return j


def n_cluster_myid(j):
    """A 40-character node id, which is different every run."""
    t, v = j
    if t == 'bulk' and isinstance(v, str) and len(v) == 40:
        return [t, '<nodeid:40>']
    return j


RULES = {
    None: n_exact,
    'exact': n_exact,
    'sorted-array': n_sorted_array,
    'scan': n_scan,
    'ts-info-memory': n_ts_info,
    'vinfo-counters': n_vinfo,
    'opaque-payload': n_opaque,
    'varies-int': n_varies_int,
    'random-member': n_random_member,
    'vsim-top1': n_vsim_top1,
    'ts-sample-clock': n_ts_sample_ts,
    'keys-only': n_keys_only,
    'any-string': n_any_string,
    'shape': n_shape,
    'redirect': n_redirect,
    'cluster-myid': n_cluster_myid,
    'keys-core': n_keys_core,
    'client-flags': n_client_flags,
    'entry-fields': n_entry_fields,
    'acl-log-core': n_acl_log_core,
    'info-fields': n_info_fields,
    'info-repl-core': n_info_repl_core,
    'info-stats-core': n_info_stats_core,
    'info-mem-core': _info_core('memory'),
    'info-pers-core': _info_core('persistence'),
    'info-clients-core': _info_core('clients'),
    'info-server-core': _info_core('server'),
    'xinfo-core': n_xinfo_core,
}


def normalize(j, rule):
    return RULES[rule](j)


# --------------------------------------------------------------------------
# Live-payload placeholders
#
# A DUMP payload is module specific, so the corpus cannot carry one: a RESTORE
# entry instead names the key whose payload the replay just captured.
# --------------------------------------------------------------------------

DUMP_REF = '\x00DUMP:'


def substitute(cmd, dumps):
    out = []
    for a in cmd:
        if isinstance(a, str) and a.startswith(DUMP_REF):
            out.append(dumps.get(a[len(DUMP_REF):], ''))
        else:
            out.append(a)
    return out
