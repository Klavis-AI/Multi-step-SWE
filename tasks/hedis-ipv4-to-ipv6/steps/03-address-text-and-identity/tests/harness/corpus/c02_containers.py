"""List, hash, set, sorted-set and HyperLogLog surface, with edge strata."""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import cmdrun  # noqa: E402
from cmdrun import ADV, SORT, ADVSORT  # noqa: E402
import ctx as ctxmod  # noqa: E402

PORTS = 1
FAMILY = "behaviour"

I64MAX = 9223372036854775807
I64MIN = -9223372036854775808


def _lists():
    t = []
    a = t.append
    a(("rpush", ["RPUSH", "L", "a", "b", "c"]))
    a(("lpush", ["LPUSH", "L", "z"]))
    a(("lrange-all", ["LRANGE", "L", 0, -1]))
    a(("lrange-neg", ["LRANGE", "L", -2, -1]))
    a(("lrange-inverted", ["LRANGE", "L", 3, 1], ADV))
    a(("lrange-huge", ["LRANGE", "L", 0, I64MAX], ADV))
    a(("lrange-past-i64", ["LRANGE", "L", 0, "9223372036854775808"], ADV))
    a(("llen", ["LLEN", "L"]))
    a(("lindex-0", ["LINDEX", "L", 0]))
    a(("lindex-oob", ["LINDEX", "L", 99], ADV))
    a(("lindex-neg-oob", ["LINDEX", "L", -99], ADV))
    a(("lset", ["LSET", "L", 0, "Z"]))
    a(("lset-oob", ["LSET", "L", 99, "x"], ADV))
    a(("linsert-before", ["LINSERT", "L", "BEFORE", "b", "b0"]))
    a(("linsert-missing-pivot", ["LINSERT", "L", "AFTER", "nope", "x"], ADV))
    a(("linsert-bad-where", ["LINSERT", "L", "SIDEWAYS", "b", "x"], ADV))
    a(("lrem-positive", ["LREM", "L", 1, "b"]))
    a(("lrem-negative", ["LREM", "L", -1, "c"]))
    a(("lrem-zero", ["LREM", "L", 0, "b0"]))
    a(("lpop", ["LPOP", "L"]))
    a(("rpop", ["RPOP", "L"]))
    a(("lpop-empty-after", ["LPOP", "L"]))
    a(("exists-emptied-list", ["EXISTS", "L"], ADV))
    a(("rpoplpush", ["RPUSH", "L2", "1", "2", "3"]))
    a(("rpoplpush-do", ["RPOPLPUSH", "L2", "L3"]))
    a(("rpoplpush-self", ["RPOPLPUSH", "L2", "L2"], ADV))
    a(("rpoplpush-missing", ["RPOPLPUSH", "absent", "L3"], ADV))
    a(("ltrim", ["LTRIM", "L2", 0, 0]))
    a(("lrange-after-trim", ["LRANGE", "L2", 0, -1]))
    a(("ltrim-empty", ["LTRIM", "L2", 5, 1], ADV))
    a(("exists-after-ltrim-empty", ["EXISTS", "L2"], ADV))
    a(("rpush-empty-element", ["RPUSH", "L4", ""], ADV))
    a(("lindex-empty-element", ["LINDEX", "L4", 0], ADV))
    a(("rpush-no-values", ["RPUSH", "L5"], ADV))
    a(("list-encoding-small", ["OBJECT", "ENCODING", "L3"]))
    a(("rpush-many", ["RPUSH", "L6"] + [str(i) for i in range(200)]))
    a(("list-encoding-large", ["OBJECT", "ENCODING", "L6"]))
    a(("llen-large", ["LLEN", "L6"]))
    a(("lrange-large-tail", ["LRANGE", "L6", 195, -1]))
    a(("rpush-long-element", ["RPUSH", "L7", "y" * 100], ADV))
    a(("list-encoding-long-el", ["OBJECT", "ENCODING", "L7"], ADV))
    return t


def _hashes():
    t = []
    a = t.append
    a(("hset", ["HSET", "H", "f1", "v1"]))
    a(("hset-overwrite", ["HSET", "H", "f1", "v2"]))
    a(("hsetnx-existing", ["HSETNX", "H", "f1", "v3"]))
    a(("hsetnx-new", ["HSETNX", "H", "f2", "v2"]))
    a(("hmset", ["HMSET", "H", "f3", "3", "f4", "4"]))
    a(("hget", ["HGET", "H", "f1"]))
    a(("hget-missing", ["HGET", "H", "nope"]))
    a(("hmget", ["HMGET", "H", "f1", "nope", "f3"]))
    a(("hdel", ["HDEL", "H", "f4"]))
    a(("hdel-missing", ["HDEL", "H", "nope"]))
    a(("hlen", ["HLEN", "H"]))
    a(("hexists", ["HEXISTS", "H", "f1"]))
    a(("hstrlen-or-err", ["HSTRLEN", "H", "f1"], ADV))
    a(("hkeys", ["HKEYS", "H"], SORT))
    a(("hvals", ["HVALS", "H"], SORT))
    a(("hgetall", ["HGETALL", "H"], SORT))
    a(("hincrby", ["HINCRBY", "H", "n", 5]))
    a(("hincrby-overflow", ["HINCRBY", "H", "n", I64MAX], ADV))
    a(("hincrby-on-text", ["HINCRBY", "H", "f1", 1], ADV))
    a(("hincrbyfloat", ["HINCRBYFLOAT", "H", "fl", "1.5"]))
    a(("hincrbyfloat-inf", ["HINCRBYFLOAT", "H", "fl", "inf"], ADV))
    a(("hset-empty-field", ["HSET", "H", "", "empty-field"], ADV))
    a(("hget-empty-field", ["HGET", "H", ""], ADV))
    a(("hset-binary-field", ["HSET", "H", b"f\x00x", b"b\x00v"], ADV))
    a(("hget-binary-field", ["HGET", "H", b"f\x00x"], ADV))
    a(("hash-encoding-small", ["OBJECT", "ENCODING", "H"]))
    # 600 fields crosses the default hash-max-ziplist-entries (512).
    a(("hmset-many", ["HMSET", "H2"] + [x for i in range(600)
                                        for x in ("f%d" % i, str(i))]))
    a(("hash-encoding-large", ["OBJECT", "ENCODING", "H2"]))
    a(("hlen-large", ["HLEN", "H2"]))
    a(("hget-large-mid", ["HGET", "H2", "f499"], ADV))
    a(("hget-large-last", ["HGET", "H2", "f599"], ADV))
    a(("hgetall-large-sorted", ["HGETALL", "H2"], ADVSORT))
    a(("hmset-no-pairs", ["HMSET", "H3"], ADV))
    a(("hmset-odd-args", ["HMSET", "H3", "a"], ADV))
    return t


def _sets():
    t = []
    a = t.append
    a(("sadd", ["SADD", "S", "a", "b", "c"]))
    a(("sadd-dup", ["SADD", "S", "a"]))
    a(("scard", ["SCARD", "S"]))
    a(("sismember", ["SISMEMBER", "S", "a"]))
    a(("sismember-missing", ["SISMEMBER", "S", "zz"]))
    a(("smembers", ["SMEMBERS", "S"], SORT))
    a(("srem", ["SREM", "S", "c", "absent"]))
    a(("smove", ["SADD", "S2", "x"]))
    a(("smove-do", ["SMOVE", "S", "S2", "b"]))
    a(("smove-missing", ["SMOVE", "S", "S2", "nope"], ADV))
    a(("sunion", ["SUNION", "S", "S2"], SORT))
    a(("sinter", ["SINTER", "S", "S2"], SORT))
    a(("sdiff", ["SDIFF", "S", "S2"], SORT))
    a(("sinter-with-missing", ["SINTER", "S", "absent"], ADV))
    a(("sunionstore", ["SUNIONSTORE", "S3", "S", "S2"]))
    a(("sdiffstore-empty", ["SDIFFSTORE", "S4", "S", "S"], ADV))
    a(("exists-empty-store", ["EXISTS", "S4"], ADV))
    a(("sadd-ints", ["SADD", "SI", 1, 2, 3, -1, I64MAX, I64MIN]))
    a(("set-encoding-intset", ["OBJECT", "ENCODING", "SI"]))
    a(("smembers-ints", ["SMEMBERS", "SI"], SORT))
    a(("sadd-int-and-text", ["SADD", "SI", "notanint"], ADV))
    a(("set-encoding-after-text", ["OBJECT", "ENCODING", "SI"], ADV))
    a(("sadd-many", ["SADD", "SL"] + ["m%d" % i for i in range(300)]))
    a(("set-encoding-large", ["OBJECT", "ENCODING", "SL"]))
    a(("scard-large", ["SCARD", "SL"]))
    a(("spop-count-zero", ["SPOP", "SL", 0], ADV))
    a(("srandmember-zero", ["SRANDMEMBER", "SL", 0], ADV))
    a(("srandmember-all-sorted", ["SRANDMEMBER", "S3", 100], ADVSORT))
    a(("sadd-no-members", ["SADD", "S9"], ADV))
    return t


def _zsets():
    t = []
    a = t.append
    a(("zadd", ["ZADD", "Z", 1, "a", 2, "b", 3, "c"]))
    a(("zadd-update", ["ZADD", "Z", 5, "a"]))
    a(("zadd-nx", ["ZADD", "Z", "NX", 9, "a"]))
    a(("zadd-xx-missing", ["ZADD", "Z", "XX", 9, "nope"]))
    a(("zadd-ch", ["ZADD", "Z", "CH", 6, "a"]))
    a(("zadd-incr", ["ZADD", "Z", "INCR", 1, "a"]))
    a(("zadd-nx-xx", ["ZADD", "Z", "NX", "XX", 1, "a"], ADV))
    a(("zscore", ["ZSCORE", "Z", "a"]))
    a(("zcard", ["ZCARD", "Z"]))
    a(("zrange", ["ZRANGE", "Z", 0, -1]))
    a(("zrange-withscores", ["ZRANGE", "Z", 0, -1, "WITHSCORES"]))
    a(("zrevrange", ["ZREVRANGE", "Z", 0, -1]))
    a(("zrank", ["ZRANK", "Z", "b"]))
    a(("zrank-missing", ["ZRANK", "Z", "nope"]))
    a(("zincrby", ["ZINCRBY", "Z", "2.5", "b"]))
    a(("zadd-inf", ["ZADD", "Z", "inf", "pinf", "-inf", "ninf"], ADV))
    a(("zscore-inf", ["ZSCORE", "Z", "pinf"], ADV))
    a(("zscore-ninf", ["ZSCORE", "Z", "ninf"], ADV))
    a(("zincrby-inf-minus-inf", ["ZINCRBY", "Z", "-inf", "pinf"], ADV))
    a(("zadd-nan", ["ZADD", "Z", "nan", "q"], ADV))
    a(("zadd-tiny", ["ZADD", "Z", "5e-324", "denorm"], ADV))
    a(("zscore-tiny", ["ZSCORE", "Z", "denorm"], ADV))
    a(("zadd-17digits", ["ZADD", "Z", "0.10000000000000001", "prec"], ADV))
    a(("zscore-17digits", ["ZSCORE", "Z", "prec"], ADV))
    a(("zrangebyscore-all", ["ZRANGEBYSCORE", "Z", "-inf", "+inf"]))
    a(("zrangebyscore-excl", ["ZRANGEBYSCORE", "Z", "(1", "(6"]))
    a(("zrangebyscore-bad", ["ZRANGEBYSCORE", "Z", "notanumber", 1], ADV))
    a(("zrangebyscore-limit", ["ZRANGEBYSCORE", "Z", "-inf", "+inf", "LIMIT", 1, 2]))
    a(("zrangebyscore-limit-neg", ["ZRANGEBYSCORE", "Z", "-inf", "+inf",
                                   "LIMIT", 0, -1], ADV))
    a(("zcount", ["ZCOUNT", "Z", "-inf", "+inf"]))
    a(("zremrangebyscore", ["ZREMRANGEBYSCORE", "Z", "(5", "+inf"]))
    a(("zremrangebyrank", ["ZREMRANGEBYRANK", "Z", 0, 0]))
    a(("zrem", ["ZREM", "Z", "b", "absent"]))
    a(("zadd-lex", ["ZADD", "ZL", 0, "a", 0, "b", 0, "c", 0, "d"]))
    a(("zrangebylex-all", ["ZRANGEBYLEX", "ZL", "-", "+"]))
    a(("zrangebylex-range", ["ZRANGEBYLEX", "ZL", "[b", "(d"]))
    a(("zrangebylex-bad", ["ZRANGEBYLEX", "ZL", "b", "d"], ADV))
    a(("zlexcount", ["ZLEXCOUNT", "ZL", "-", "+"]))
    a(("zremrangebylex", ["ZREMRANGEBYLEX", "ZL", "[a", "[a"]))
    a(("zunionstore", ["ZUNIONSTORE", "ZU", 2, "Z", "ZL"]))
    a(("zunionstore-weights", ["ZUNIONSTORE", "ZW", 2, "Z", "ZL",
                               "WEIGHTS", 2, 3]))
    a(("zunionstore-inf-weight", ["ZUNIONSTORE", "ZI", 1, "Z",
                                  "WEIGHTS", "inf"], ADV))
    a(("zunionstore-aggregate-min", ["ZUNIONSTORE", "ZM", 2, "Z", "ZL",
                                     "AGGREGATE", "MIN"]))
    a(("zunionstore-bad-numkeys", ["ZUNIONSTORE", "ZB", 0, "Z"], ADV))
    a(("zinterstore", ["ZINTERSTORE", "ZN", 2, "Z", "ZU"]))
    a(("zset-encoding-small", ["OBJECT", "ENCODING", "ZL"]))
    a(("zadd-many", ["ZADD", "ZBIG"] + [x for i in range(200)
                                        for x in (str(i), "m%d" % i)]))
    a(("zset-encoding-large", ["OBJECT", "ENCODING", "ZBIG"]))
    a(("zrange-large-tail", ["ZRANGE", "ZBIG", 197, -1, "WITHSCORES"]))
    return t


def _hll():
    t = []
    a = t.append
    a(("pfadd", ["PFADD", "P", "a", "b", "c"]))
    a(("pfcount", ["PFCOUNT", "P"]))
    a(("pfadd-many", ["PFADD", "P2"] + ["e%d" % i for i in range(1000)]))
    a(("pfcount-many", ["PFCOUNT", "P2"]))
    a(("pfmerge", ["PFMERGE", "P3", "P", "P2"]))
    a(("pfcount-merged", ["PFCOUNT", "P3"]))
    a(("pfcount-multi", ["PFCOUNT", "P", "P2"]))
    a(("pfadd-empty", ["PFADD", "P4"], ADV))
    a(("pfcount-missing", ["PFCOUNT", "absent"], ADV))
    a(("pfadd-on-string", ["SET", "notahll", "x"]))
    a(("pfcount-on-string", ["PFCOUNT", "notahll"], ADV))
    a(("hll-strlen", ["STRLEN", "P2"], ADV))
    return t


def run(ctx):
    port = ctx.port(0)
    s = ctx.server("containers", port, ctxmod.default_directives(port))
    s.start().wait_ready()
    with s.client() as c:
        c.cmd("SELECT", 9)
        c.cmd("FLUSHALL")
        cmdrun.run_table(ctx, c, "list", _lists())
        cmdrun.run_table(ctx, c, "hash", _hashes())
        cmdrun.run_table(ctx, c, "set", _sets())
        cmdrun.run_table(ctx, c, "zset", _zsets())
        cmdrun.run_table(ctx, c, "hll", _hll())
        ctx.emit_reply("containers/dbsize", c.cmd("DBSIZE"))
    s.stop()
