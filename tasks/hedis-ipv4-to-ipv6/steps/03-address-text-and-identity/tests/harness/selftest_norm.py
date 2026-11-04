#!/usr/bin/env python3
"""Check that every normalisation rule fires, and that none eats an address.

A rule that never matches is worse than no rule: it leaves a volatile field in
an expectation and the failure shows up as a mysterious flake much later. A
rule that matches too much is worse still, because it would erase exactly the
address text this task is about.

Every fixture below is a real shape produced by this tree -- INFO with CRLF
line endings, a CLIENT LIST row, a CLUSTER NODES row, the startup banner, the
MONITOR feed -- so "the rule fires" means "the rule fires on output the corpus
actually records", not "the rule fires on something invented to satisfy it".
"""

import os
import sys

# Before the first harness import, for the reason set out in runner.py: a
# `.pyc` beside a source file is a second copy of it that nothing can read.
sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "lib"))
import norm  # noqa: E402

CRLF_INFO = "\r\n".join([
    "# Server",
    "redis_version:3.1.999",
    "redis_git_sha1:00000000",
    "redis_git_dirty:0",
    "os:Linux 6.1.0-53-cloud-amd64 x86_64",
    "arch_bits:64",
    "multiplexing_api:epoll",
    "gcc_version:12.2.0",
    "run_id:bd2f0d5a7e8b6c4d3f2a1908e7d6c5b4a3928170",
    "process_id:4321",
    "tcp_port:7409",
    "uptime_in_seconds:12",
    "lru_clock:9541128",
    "hz:10",
    "config_file:/tmp/x/redis.conf",
    "executable:/tmp/x/src/redis-server",
    "# Clients",
    "connected_clients:3",
    "blocked_clients:0",
    "client_longest_output_list:0",
    "# Memory",
    "used_memory:812345",
    "used_memory_human:793.31K",
    "used_memory_lua:36864",
    "total_system_memory:33567932416",
    "mem_fragmentation_ratio:2.61",
    "mem_allocator:jemalloc-4.0.3",
    "maxmemory:0",
    "maxmemory_human:0B",
    "maxmemory_policy:noeviction",
    "# Persistence",
    "loading:0",
    "loading_start_time:1470000000",
    "loading_loaded_perc:12.34",
    "rdb_bgsave_in_progress:0",
    "rdb_last_save_time:1758000000",
    "aof_rewrite_in_progress:0",
    "aof_last_bgrewrite_status:ok",
    "# Stats",
    "total_connections_received:9",
    "instantaneous_ops_per_sec:0",
    "latest_fork_usec:271",
    "total_forks:2",
    "migrate_cached_sockets:0",
    "# Replication",
    "role:master",
    "connected_slaves:1",
    "slave0:ip=::1,port=7420,state=online,offset=4321,lag=0",
    "master_repl_offset:4321",
    "master_replid:bd2f0d5a7e8b6c4d3f2a1908e7d6c5b4a3928170",
    "repl_backlog_active:0",
    "repl_backlog_histlen:0",
    "# CPU",
    "used_cpu_sys:0.12",
    "# Cluster",
    "cluster_enabled:0",
    "# Keyspace",
    "db9:keys=3,expires=1,avg_ttl=0",
]) + "\r\n"

CLIENT_LIST = (
    "id=4 addr=[::1]:60620 fd=8 name= age=0 idle=0 flags=N db=9 sub=0 "
    "psub=0 multi=-1 qbuf=26 qbuf-free=32742 obl=0 oll=0 omem=0 "
    "events=r cmd=client\r\n"
    "id=5 addr=[::1]:7409 fd=9 name= age=1 idle=1 flags=N db=0 sub=0 "
    "psub=0 multi=-1 qbuf=0 qbuf-free=0 obl=0 oll=0 omem=0 "
    "events=r cmd=ping\r\n")

# This tree writes the peer unbracketed, which is exactly the defect step 3
# addresses; the rules must survive both that shape and a bracketed one.
CLUSTER_NODES = (
    "07c37dfeb235213a872192d90877d0cd55635b91 ::1:7450 myself,master - "
    "0 1470000000000 1 connected 0-5460\n"
    "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 ::1:7451 slave "
    "07c37dfeb235213a872192d90877d0cd55635b91 0 1470000000001 2 disconnected\n"
    "3f8c1e0b7a6d5c4b3a2918f7e6d5c4b3a2918f7e [::1]:7452 master - "
    "0 1470000000002 3 connected 5461-10922\n"
    "vars currentEpoch 6 lastVoteEpoch 4\n")

LOGLINES = "\n".join([
    "4321:M 16 Sep 06:47:11.120 * Redis version=3.1.999, bits=64, "
    "commit=00000000, modified=0, pid=4321, just started",
    "4321:M 16 Sep 06:47:11.123 * Ready to accept connections",
    "4321:M 16 Sep 06:47:11.124 * Full resync from master: "
    "bd2f0d5a7e8b6c4d3f2a1908e7d6c5b4a3928170:17",
    "4321:S 16 Sep 06:47:11.125 * Synchronization with slave ::1:7420 succeeded",
    "4321:M 16 Sep 06:47:11.126 # Write error saving DB on disk: "
    "temp-4321.rdb",
    "4321:M 16 Sep 06:47:11.127 # Can't rename temp-rewriteaof-bg-4321.aof "
    "into appendonly.aof",
    "                PID: 4321",
    "Redis server v=3.1.999 sha=00000000:0 malloc=jemalloc-4.0.3 bits=64 "
    "build=6a1d1a3ee1b2c3d4",
])

MONITOR = ('1758000000.123456 [9 [::1]:60620] "SET" "k" "v"\n'
           '1758000000.223456 [9 ::1:60620] "GET" "k"\n')

DEBUG_OBJECT = ("Value at:0x7fef75835dc0 refcount:1 encoding:embstr "
                "serializedlength:3 lru:11158320 lru_seconds_idle:0")

# Values these defensive rules exist for. They do not appear in the shapes the
# corpus records today, but a bare epoch is the classic thing that slips into a
# reply when a new command is added, and masking one cannot damage an address.
MISC = "\n".join([
    "at 1758000000 the dump ran",
    "js timestamp 1758000000123",
    "saved as dump-4321.rdb",
])

# (label, input, must appear, must NOT appear)
CASES = [
    ("info", CRLF_INFO,
     ["run_id:<RUNID>", "process_id:<PID>", "uptime_in_seconds:<N>",
      "lru_clock:<N>", "used_memory:<N>", "used_memory_human:<N>",
      "total_system_memory:<N>", "mem_fragmentation_ratio:<N>",
      "connected_clients:<N>", "blocked_clients:<N>",
      "client_longest_output_list:<N>",
      "total_connections_received:<N>", "instantaneous_ops_per_sec:<N>",
      "latest_fork_usec:<N>", "total_forks:<N>", "migrate_cached_sockets:<N>",
      "loading_start_time:<N>", "loading_loaded_perc:<N>",
      "rdb_bgsave_in_progress:<N>", "rdb_last_save_time:<N>",
      "aof_rewrite_in_progress:<N>", "aof_last_bgrewrite_status:<N>",
      "master_repl_offset:<N>", "repl_backlog_active:<N>",
      "repl_backlog_histlen:<N>", "used_cpu_sys:<N>", "db9:<KEYSPACE>",
      "config_file:<PATH>", "executable:<PATH>",
      "redis_git_sha1:<ENV>", "redis_git_dirty:<ENV>", "os:<ENV>",
      "arch_bits:<ENV>", "gcc_version:<ENV>", "multiplexing_api:<ENV>",
      "slave0:ip=::1,port=7420,state=<S>,offset=<N>,lag=<N>",
      "master_replid:<REPLID>",
      # Configuration and identity are measurements of the *tree*, not of the
      # run, so they stay in the expectation.
      "role:master", "redis_version:3.1.999", "connected_slaves:1",
      "maxmemory:0", "maxmemory_human:0B", "maxmemory_policy:noeviction",
      "mem_allocator:jemalloc-4.0.3", "tcp_port:7409", "hz:10",
      "cluster_enabled:0", "loading:0"],
     ["run_id:bd2f", "4321", "812345", "offset=4321", "tcp_port:<ENV>",
      "maxmemory_policy:<N>", "1470000000", "1758000000"]),
    ("client-list", CLIENT_LIST,
     ["id=<N>", "addr=[::1]:<EPORT>", "fd=<N>", "age=<N>", "idle=<N>",
      "qbuf=<N>", "qbuf-free=<N>", "obl=<N>", "oll=<N>", "omem=<N>",
      "db=9", "cmd=client",
      # A port the corpus itself chose is not ephemeral and must survive.
      "addr=[::1]:7409"],
     ["addr=[::1]:60620", "id=4 ", "::<EPORT>", "fd=8"]),
    ("cluster-nodes", CLUSTER_NODES,
     ["<NODEID> ::1:7450 myself,master - <N> <N> <N> connected 0-5460",
      "<NODEID> ::1:7451 slave <NODEID> <N> <N> <N> disconnected",
      "<NODEID> [::1]:7452 master - <N> <N> <N> connected 5461-10922",
      "currentEpoch <N>"],
     ["07c37dfeb", "currentEpoch 6", "1470000000000"]),
    ("logs", LOGLINES,
     ["<LOGSTAMP> * Ready to accept connections",
      "Full resync from master: <NODEID>:<N>",
      "Synchronization with slave ::1:7420 succeeded",
      "commit=<SHA>", "pid=<PID>", "PID: <PID>", "sha=<SHA>", "build=<BUILD>",
      "v=3.1.999", "temp-<PID>.rdb", "temp-rewriteaof-bg-<PID>.aof"],
     ["4321", "6a1d1a3e"]),
    ("monitor", MONITOR,
     ['<TS> [9 [::1]:<EPORT>] "SET" "k" "v"',
      # The baseline brackets this peer, but the bracketing is precisely what
      # the migration may move, so the port must still be masked if it goes.
      '<TS> [9 ::1:<EPORT>] "GET" "k"'],
     ["1758000000", "60620"]),
    ("debug-object", DEBUG_OBJECT,
     ["at:<PTR>", "lru:<N>", "lru_seconds_idle:<N>",
      "refcount:1", "encoding:embstr", "serializedlength:3"],
     ["0x7fef", "11158320"]),
    ("misc", MISC,
     ["at <EPOCH> the dump ran", "js timestamp <EPOCHMS>",
      "saved as dump-<PID>.rdb"],
     ["1758000000", "4321"]),
    ("resolver",
     "# Creating Server TCP listening socket not-an-address:7410: "
     "Temporary failure in name resolution",
     ["not-an-address:7410: Name or service not known"],
     ["Temporary failure"]),
]

# Address text that must survive untouched, whatever form it takes.
ADDRESSES = [
    "bind ::1", "bind 2001:db8::1", "bind ::ffff:127.0.0.1", "bind fe80::1%lo",
    "bind ::", "bind 0.0.0.0", "bind 127.0.0.1",
    "Could not connect to Redis at [2001:db8::1]:7409",
    "Could not connect to Redis at 2001:db8::1:7409",
    "master_host:::1", "master_host:localhost", "MOVED 3999 [::1]:7451",
    "Creating Server TCP listening socket ::1%lo:7409",
    "# Creating Server TCP listening socket ::ffff:127.0.0.1:7410: bind: "
    "Invalid argument",
    "slave0:ip=2001:db8::1,port=7420",
    "cluster-announce-ip fd00:1234:5678:9abc::1",
]


def main():
    failures = []

    for label, text, want, unwanted in CASES:
        got = norm.normalize_text(text)
        for w in want:
            if w not in got:
                failures.append("%s: missing %r" % (label, w))
        for u in unwanted:
            if u in got:
                failures.append("%s: leaked %r" % (label, u))

    for a in ADDRESSES:
        got = norm.normalize_text(a)
        if got != a:
            failures.append("address mangled: %r -> %r" % (a, got))

    # Every rule must change at least one fixture. The rules chain, so this is
    # measured by applying them in order, not by matching each against the raw
    # text -- several rules only ever see the output of an earlier one.
    fired = set()
    for case in CASES:
        fired |= norm.rules_fired(case[1])
    for i, (rx, _rep) in enumerate(norm._COMPILED):
        if i not in fired:
            failures.append("rule never fires: %s" % rx.pattern)

    # A registered path prefix is replaced; the part under it is kept.
    norm.register_path("/tmp/recwork/w1/c01", "<WORK>")
    got = norm.normalize_text("dir /tmp/recwork/w1/c01/server/dump.rdb")
    if got != "dir <WORK>/server/dump.rdb":
        failures.append("path prefix: %r" % got)

    for f in failures:
        print("FAIL", f)
    print("%d checks failed" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
