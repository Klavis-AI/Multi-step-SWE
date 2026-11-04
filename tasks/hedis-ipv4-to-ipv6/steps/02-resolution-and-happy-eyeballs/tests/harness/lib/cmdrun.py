"""Run a table of command vectors against one connection and emit each reply."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ctx as ctxmod  # noqa: E402


def run_table(ctx, client, prefix, table):
    """table: iterable of (label, args) or (label, args, opts).

    opts keys: adv (adversarial), sort (order-insensitive reply), w (weight).
    Every entry produces exactly one scored observation.
    """
    for entry in table:
        if len(entry) == 2:
            label, args = entry
            opts = {}
        else:
            label, args, opts = entry
        name = "%s/%s" % (prefix, label)
        try:
            reply = client.cmd(*args)
        except Exception as e:  # noqa: BLE001 - the failure itself is the observation
            ctx.emit(name, ["exception", type(e).__name__], adv=opts.get("adv", False),
                     w=opts.get("w", 1.0))
            continue
        ctx.emit_reply(name, reply, adv=opts.get("adv", False),
                       w=opts.get("w", 1.0), sort=opts.get("sort", False))


ADV = {"adv": True}
SORT = {"sort": True}
ADVSORT = {"adv": True, "sort": True}
