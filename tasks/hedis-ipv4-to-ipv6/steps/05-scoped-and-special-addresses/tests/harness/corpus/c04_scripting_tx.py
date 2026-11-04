"""Transactions, scripting, pub/sub and blocking-command surface."""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import cmdrun  # noqa: E402
from cmdrun import ADV, SORT  # noqa: E402
import ctx as ctxmod  # noqa: E402
import resp  # noqa: E402

PORTS = 1
FAMILY = "behaviour"


def _scripts():
    t = []
    a = t.append
    a(("eval-return-int", ["EVAL", "return 1", 0]))
    a(("eval-return-string", ["EVAL", "return 'hi'", 0]))
    a(("eval-return-table", ["EVAL", "return {1,2,3,'x'}", 0]))
    a(("eval-return-nested", ["EVAL", "return {1,{2,{3}}}", 0]))
    a(("eval-return-nil", ["EVAL", "return nil", 0], ADV))
    a(("eval-return-false", ["EVAL", "return false", 0], ADV))
    a(("eval-return-true", ["EVAL", "return true", 0], ADV))
    a(("eval-return-float", ["EVAL", "return 3.999", 0], ADV))
    a(("eval-return-negative-float", ["EVAL", "return -3.999", 0], ADV))
    a(("eval-status-reply", ["EVAL", "return redis.status_reply('FINE')", 0]))
    a(("eval-error-reply", ["EVAL", "return redis.error_reply('BROKE')", 0], ADV))
    a(("eval-keys-argv", ["EVAL", "return {KEYS[1],KEYS[2],ARGV[1]}", 2,
                          "ka", "kb", "va"]))
    a(("eval-negative-numkeys", ["EVAL", "return 1", -1], ADV))
    a(("eval-numkeys-too-many", ["EVAL", "return 1", 3, "only-one"], ADV))
    a(("eval-call-set", ["EVAL", "return redis.call('SET', KEYS[1], ARGV[1])",
                         1, "sk", "sv"]))
    a(("eval-call-get", ["EVAL", "return redis.call('GET', KEYS[1])", 1, "sk"]))
    a(("eval-call-missing-key", ["EVAL", "return redis.call('GET','nope')", 0],
       ADV))
    a(("eval-call-bad-command", ["EVAL", "return redis.call('NOPE')", 0], ADV))
    a(("eval-pcall-bad-command",
       ["EVAL", "local ok, e = pcall(function() return redis.call('NOPE') end) "
                "return type(e)", 0], ADV))
    a(("eval-syntax-error", ["EVAL", "this is not lua", 0], ADV))
    a(("eval-runtime-error", ["EVAL", "return nosuchfunction()", 0], ADV))
    a(("eval-cjson-encode", ["EVAL", "return cjson.encode({1,2,'x'})", 0]))
    a(("eval-cjson-decode", ["EVAL", "return cjson.decode('[1,2,3]')[2]", 0]))
    a(("eval-cjson-bad", ["EVAL", "return cjson.decode('{')", 0], ADV))
    a(("eval-sha1hex", ["EVAL", "return redis.sha1hex('')", 0]))
    a(("eval-tostring-large", ["EVAL", "return tostring(2^53)", 0], ADV))
    a(("eval-int-truncation", ["EVAL", "return 2^53", 0], ADV))
    a(("eval-string-with-nul", ["EVAL", "return 'a\\0b'", 0], ADV))
    a(("eval-breakout", ["EVAL", "return type(os)", 0], ADV))
    a(("eval-loadstring", ["EVAL", "return type(loadstring)", 0], ADV))
    a(("eval-global-write", ["EVAL", "someglobal = 1 return 1", 0], ADV))
    a(("eval-random-guard",
       ["EVAL", "redis.call('SET',KEYS[1],'x') return redis.call('TIME')[1] ~= nil",
        1, "rk"], ADV))
    a(("script-exists-unknown",
       ["SCRIPT", "EXISTS", "0000000000000000000000000000000000000000"], ADV))
    a(("evalsha-unknown",
       ["EVALSHA", "0000000000000000000000000000000000000000", 0], ADV))
    return t


def run(ctx):
    port = ctx.port(0)
    s = ctx.server("scripting", port, ctxmod.default_directives(port))
    s.start().wait_ready()

    with s.client() as c:
        c.cmd("SELECT", 9)
        c.cmd("FLUSHALL")
        cmdrun.run_table(ctx, c, "lua", _scripts())

        # SCRIPT LOAD / EVALSHA round trip.
        kind, sha = c.cmd("SCRIPT", "LOAD", "return 41 + 1")
        ctx.emit("lua/script-load-kind", kind)
        ctx.emit("lua/script-load-len", len(sha))
        ctx.emit_reply("lua/evalsha", c.cmd("EVALSHA", sha, 0))
        ctx.emit_reply("lua/script-exists", c.cmd("SCRIPT", "EXISTS", sha))
        ctx.emit_reply("lua/script-flush", c.cmd("SCRIPT", "FLUSH"))
        ctx.emit_reply("lua/evalsha-after-flush",
                       c.cmd("EVALSHA", sha, 0), adv=True)

        # -- transactions --------------------------------------------------
        ctx.emit_reply("tx/multi", c.cmd("MULTI"))
        ctx.emit_reply("tx/queued-set", c.cmd("SET", "tk", "1"))
        ctx.emit_reply("tx/queued-incr", c.cmd("INCR", "tk"))
        ctx.emit_reply("tx/exec", c.cmd("EXEC"))
        ctx.emit_reply("tx/exec-without-multi", c.cmd("EXEC"), adv=True)
        ctx.emit_reply("tx/discard-without-multi", c.cmd("DISCARD"), adv=True)
        c.cmd("MULTI")
        ctx.emit_reply("tx/nested-multi", c.cmd("MULTI"), adv=True)
        ctx.emit_reply("tx/queue-bad-arity", c.cmd("SET"), adv=True)
        ctx.emit_reply("tx/exec-after-queue-error", c.cmd("EXEC"), adv=True)
        c.cmd("MULTI")
        c.cmd("SET", "tk2", "v")
        ctx.emit_reply("tx/discard", c.cmd("DISCARD"))
        ctx.emit_reply("tx/discard-effect", c.cmd("EXISTS", "tk2"))
        c.cmd("MULTI")
        c.cmd("LPUSH", "tk", "x")        # queues fine, fails at exec
        ctx.emit_reply("tx/exec-with-runtime-error", c.cmd("EXEC"), adv=True)

        # WATCH invalidated by another connection.
        c.cmd("SET", "wk", "1")
        ctx.emit_reply("tx/watch", c.cmd("WATCH", "wk"))
        with s.client() as other:
            other.cmd("SELECT", 9)
            other.cmd("SET", "wk", "2")
        c.cmd("MULTI")
        c.cmd("GET", "wk")
        ctx.emit_reply("tx/exec-after-touched-watch", c.cmd("EXEC"), adv=True)
        ctx.emit_reply("tx/unwatch", c.cmd("UNWATCH"))

        # -- pub/sub -------------------------------------------------------
        sub = resp.Client("::1", port, timeout=5.0)
        ctx.emit_reply("pubsub/subscribe", sub.cmd("SUBSCRIBE", "chan"))
        ctx.emit_reply("pubsub/psubscribe", sub.cmd("PSUBSCRIBE", "ch*"))
        ctx.emit_reply("pubsub/publish", c.cmd("PUBLISH", "chan", "payload"))
        ctx.emit_reply("pubsub/delivery-plain", sub.reader.read())
        ctx.emit_reply("pubsub/delivery-pattern", sub.reader.read())
        ctx.emit_reply("pubsub/forbidden-while-subscribed",
                       sub.cmd("GET", "x"), adv=True)
        ctx.emit_reply("pubsub/ping-while-subscribed", sub.cmd("PING"))
        ctx.emit_reply("pubsub/channels", c.cmd("PUBSUB", "CHANNELS"), sort=True)
        ctx.emit_reply("pubsub/numsub", c.cmd("PUBSUB", "NUMSUB", "chan"))
        ctx.emit_reply("pubsub/numpat", c.cmd("PUBSUB", "NUMPAT"))
        ctx.emit_reply("pubsub/unsubscribe", sub.cmd("UNSUBSCRIBE", "chan"))
        ctx.emit_reply("pubsub/punsubscribe", sub.cmd("PUNSUBSCRIBE", "ch*"))
        ctx.emit_reply("pubsub/publish-no-listener",
                       c.cmd("PUBLISH", "quiet", "x"), adv=True)
        sub.close()

        # -- blocking commands ---------------------------------------------
        blocker = resp.Client("::1", port, timeout=6.0)
        blocker.cmd("SELECT", 9)
        blocker.send_raw(resp.encode_command(["BLPOP", "bl", 0]))
        time.sleep(0.2)
        ctx.emit_reply("block/push-wakes", c.cmd("LPUSH", "bl", "woken"))
        ctx.emit_reply("block/blpop-result", blocker.reader.read())
        blocker.send_raw(resp.encode_command(["BRPOPLPUSH", "bl2", "bl3", 0]))
        time.sleep(0.2)
        c.cmd("LPUSH", "bl2", "moved")
        ctx.emit_reply("block/brpoplpush-result", blocker.reader.read())
        ctx.emit_reply("block/brpoplpush-dest", c.cmd("LRANGE", "bl3", 0, -1))
        ctx.emit_reply("block/blpop-timeout",
                       blocker.cmd("BLPOP", "never", 1), adv=True)
        ctx.emit_reply("block/blpop-negative-timeout",
                       blocker.cmd("BLPOP", "never", -1), adv=True)
        ctx.emit_reply("block/blpop-fractional-timeout",
                       blocker.cmd("BLPOP", "never", "0.5"), adv=True)
        blocker.close()
    s.stop()
