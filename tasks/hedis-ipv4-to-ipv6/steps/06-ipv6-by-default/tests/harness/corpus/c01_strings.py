"""String, numeric and bit command surface, with the numeric edge strata."""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import cmdrun  # noqa: E402
from cmdrun import ADV, SORT  # noqa: E402
import ctx as ctxmod  # noqa: E402

PORTS = 1
FAMILY = "behaviour"

I64MAX = 9223372036854775807
I64MIN = -9223372036854775808


def _table():
    t = []
    a = t.append

    # -- plain string surface ---------------------------------------------
    a(("set", ["SET", "s", "hello"]))
    a(("get", ["GET", "s"]))
    a(("get-missing", ["GET", "nope"]))
    a(("setnx-existing", ["SETNX", "s", "x"]))
    a(("setnx-new", ["SETNX", "s2", "y"]))
    a(("setex", ["SETEX", "s3", 100, "v"]))
    a(("ttl-setex", ["TTL", "s3"]))
    a(("psetex", ["PSETEX", "s4", 100000, "v"]))
    a(("mset", ["MSET", "m1", "a", "m2", "b", "m3", "c"]))
    a(("mget", ["MGET", "m1", "m2", "m3", "absent"]))
    a(("msetnx-conflict", ["MSETNX", "m1", "z", "m9", "z"]))
    a(("getset", ["GETSET", "s", "world"]))
    a(("append", ["APPEND", "s", "!!"]))
    a(("strlen", ["STRLEN", "s"]))
    a(("setrange", ["SETRANGE", "s", 2, "XY"]))
    a(("getrange-full", ["GETRANGE", "s", 0, -1]))
    a(("getrange-neg", ["GETRANGE", "s", -3, -1]))
    a(("getrange-inverted", ["GETRANGE", "s", 5, 2], ADV))
    a(("getrange-past-end", ["GETRANGE", "s", 0, 1000], ADV))
    a(("set-ex-opt", ["SET", "s5", "v", "EX", 50]))
    a(("set-xx-missing", ["SET", "s6", "v", "XX"]))
    a(("set-nx-present", ["SET", "s5", "w", "NX"]))
    a(("set-px-nx", ["SET", "s7", "v", "PX", 50000, "NX"]))
    a(("set-bad-opt", ["SET", "s8", "v", "BOGUS"], ADV))
    a(("set-ex-zero", ["SET", "s9", "v", "EX", 0], ADV))
    a(("set-ex-negative", ["SET", "s9", "v", "EX", -1], ADV))
    a(("setex-zero", ["SETEX", "s10", 0, "v"], ADV))

    # -- binary safety and encodings --------------------------------------
    a(("set-empty", ["SET", "e", ""]))
    a(("get-empty", ["GET", "e"]))
    a(("strlen-empty", ["STRLEN", "e"]))
    a(("set-embedded-nul", ["SET", b"k\x00k", b"v\x00v"], ADV))
    a(("get-embedded-nul", ["GET", b"k\x00k"], ADV))
    a(("set-invalid-utf8", ["SET", "iu", b"\xff\xfe\xfd"], ADV))
    a(("get-invalid-utf8", ["GET", "iu"], ADV))
    a(("set-non-bmp", ["SET", "nb", "\U0001F600\U0001F4A9"], ADV))
    a(("get-non-bmp", ["GET", "nb"], ADV))
    a(("set-crlf", ["SET", "crlf", "a\r\nb"], ADV))
    a(("get-crlf", ["GET", "crlf"], ADV))
    a(("set-long-64k", ["SET", "long", "q" * 65536], ADV))
    a(("strlen-long", ["STRLEN", "long"], ADV))
    a(("append-long", ["APPEND", "long", "z" * 65536], ADV))
    a(("strlen-long2", ["STRLEN", "long"], ADV))
    a(("set-long-key", ["SET", "K" * 4096, "v"], ADV))
    a(("get-long-key", ["GET", "K" * 4096], ADV))
    a(("set-key-with-space", ["SET", "a b c", "v"], ADV))
    a(("get-key-with-space", ["GET", "a b c"], ADV))

    # -- integer strata ----------------------------------------------------
    a(("set-i64max", ["SET", "n", I64MAX]))
    a(("incr-at-i64max", ["INCR", "n"], ADV))
    a(("object-enc-i64max", ["OBJECT", "ENCODING", "n"], ADV))
    a(("set-i64min", ["SET", "n2", I64MIN]))
    a(("decr-at-i64min", ["DECR", "n2"], ADV))
    a(("set-i64max-minus1", ["SET", "n3", I64MAX - 1]))
    a(("incr-to-i64max", ["INCR", "n3"], ADV))
    a(("incrby-overflow", ["INCRBY", "n3", 1], ADV))
    a(("set-past-i64max", ["SET", "n4", "9223372036854775808"], ADV))
    a(("incr-past-i64max", ["INCR", "n4"], ADV))
    a(("object-enc-past-i64max", ["OBJECT", "ENCODING", "n4"], ADV))
    a(("set-zero", ["SET", "z", 0]))
    a(("incr-zero", ["INCR", "z"]))
    a(("decrby-zero", ["DECRBY", "z", 0]))
    a(("set-minus-zero", ["SET", "mz", "-0"], ADV))
    a(("incr-minus-zero", ["INCR", "mz"], ADV))
    a(("set-leading-zero", ["SET", "lz", "007"], ADV))
    a(("incr-leading-zero", ["INCR", "lz"], ADV))
    a(("set-leading-space", ["SET", "ls", " 1"], ADV))
    a(("incr-leading-space", ["INCR", "ls"], ADV))
    a(("set-trailing-space", ["SET", "ts", "1 "], ADV))
    a(("incr-trailing-space", ["INCR", "ts"], ADV))
    a(("set-plus", ["SET", "pp", "+1"], ADV))
    a(("incr-plus", ["INCR", "pp"], ADV))
    a(("incrby-i64min", ["INCRBY", "z", I64MIN], ADV))
    a(("incrby-past-range", ["INCRBY", "z", "9223372036854775808"], ADV))
    a(("decrby-i64min", ["DECRBY", "z", I64MIN], ADV))
    a(("incr-on-nonnumber", ["INCR", "s"], ADV))
    a(("incr-on-empty", ["INCR", "e"], ADV))

    # -- float strata ------------------------------------------------------
    a(("incrbyfloat", ["INCRBYFLOAT", "f", "3.0e3"]))
    a(("incrbyfloat-neg", ["INCRBYFLOAT", "f", "-1.5"]))
    a(("incrbyfloat-tiny", ["INCRBYFLOAT", "f", "0.000000001"], ADV))
    a(("incrbyfloat-inf", ["INCRBYFLOAT", "f", "inf"], ADV))
    a(("incrbyfloat-nan", ["INCRBYFLOAT", "f2", "nan"], ADV))
    a(("incrbyfloat-hex", ["INCRBYFLOAT", "f3", "0x10"], ADV))
    a(("incrbyfloat-denormal", ["INCRBYFLOAT", "f4", "5e-324"], ADV))
    a(("incrbyfloat-17digits", ["INCRBYFLOAT", "f5", "0.10000000000000001"], ADV))
    a(("incrbyfloat-round", ["INCRBYFLOAT", "f5", "0.2"], ADV))
    a(("incrbyfloat-on-int", ["SET", "fi", 10]))
    a(("incrbyfloat-int-result", ["INCRBYFLOAT", "fi", "0.1"], ADV))
    a(("incrbyfloat-back", ["INCRBYFLOAT", "fi", "-0.1"], ADV))
    a(("incrbyfloat-empty", ["INCRBYFLOAT", "e", "1"], ADV))

    # -- bit operations ----------------------------------------------------
    a(("setbit-0", ["SETBIT", "b", 0, 1]))
    a(("setbit-7", ["SETBIT", "b", 7, 1]))
    a(("getbit-0", ["GETBIT", "b", 0]))
    a(("getbit-past-end", ["GETBIT", "b", 100000], ADV))
    a(("setbit-far", ["SETBIT", "b", (1 << 26) - 1, 1], ADV))
    a(("strlen-after-far-setbit", ["STRLEN", "b"], ADV))
    a(("setbit-past-limit", ["SETBIT", "b2", 4294967296, 1], ADV))
    a(("setbit-negative", ["SETBIT", "b2", -1, 1], ADV))
    a(("setbit-bad-value", ["SETBIT", "b2", 0, 2], ADV))
    a(("del-b", ["DEL", "b"]))
    a(("set-bits", ["SET", "bs", "foobar"]))
    a(("bitcount-all", ["BITCOUNT", "bs"]))
    a(("bitcount-range", ["BITCOUNT", "bs", 1, 1]))
    a(("bitcount-neg-range", ["BITCOUNT", "bs", -5, -1], ADV))
    a(("bitcount-inverted", ["BITCOUNT", "bs", 3, 1], ADV))
    a(("bitcount-out-of-range", ["BITCOUNT", "bs", 100, 200], ADV))
    a(("bitpos-1", ["BITPOS", "bs", 1]))
    a(("bitpos-0", ["BITPOS", "bs", 0]))
    a(("bitpos-0-from", ["BITPOS", "bs", 0, 2]))
    a(("bitpos-missing", ["BITPOS", "nokey", 1], ADV))
    a(("bitop-and", ["BITOP", "AND", "dst", "bs", "bs"]))
    a(("bitop-xor-self", ["BITOP", "XOR", "dst2", "bs", "bs"], ADV))
    a(("get-dst2", ["GET", "dst2"], ADV))
    a(("bitop-not-two-src", ["BITOP", "NOT", "dst3", "bs", "bs"], ADV))
    a(("bitop-unknown-op", ["BITOP", "NOPE", "dst4", "bs"], ADV))
    a(("bitop-missing-src", ["BITOP", "OR", "dst5", "absent1", "absent2"], ADV))
    a(("exists-dst5", ["EXISTS", "dst5"], ADV))

    # -- type errors and arity --------------------------------------------
    a(("lpush-for-type-error", ["LPUSH", "alist", "x"]))
    a(("get-on-list", ["GET", "alist"], ADV))
    a(("incr-on-list", ["INCR", "alist"], ADV))
    a(("append-on-list", ["APPEND", "alist", "x"], ADV))
    a(("setrange-on-list", ["SETRANGE", "alist", 0, "x"], ADV))
    a(("getrange-on-list", ["GETRANGE", "alist", 0, 1], ADV))
    a(("set-no-args", ["SET"], ADV))
    a(("set-one-arg", ["SET", "k"], ADV))
    a(("get-two-args", ["GET", "a", "b"], ADV))
    a(("unknown-command", ["NOSUCHCOMMAND", "a"], ADV))
    a(("unknown-command-binary", ["NOSUCH\x00CMD"], ADV))
    a(("empty-command-name", [""], ADV))

    return t


def run(ctx):
    port = ctx.port(0)
    s = ctx.server("strings", port, ctxmod.default_directives(port))
    s.start().wait_ready()
    with s.client() as c:
        c.cmd("SELECT", 9)
        c.cmd("FLUSHALL")
        cmdrun.run_table(ctx, c, "str", _table())
        ctx.emit_reply("str/dbsize", c.cmd("DBSIZE"))
        ctx.emit_reply("str/keys-sorted", c.cmd("KEYS", "*"), sort=True)
    s.stop()
