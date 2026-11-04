"""Shared plumbing for the wire-level probe case modules."""
import traceback

import smbprobe as P
import runner as _runner


class Ctx(object):
    def __init__(self, host, port, user, password, share="tmp"):
        self.host, self.port = host, port
        self.user, self.password = user, password
        self.share = share
        self._open = []

    # A service that accepts the connection and then never answers costs this
    # much per case and no more. Without the budget term, a candidate whose
    # server listens but does not speak makes every probe wait the full timeout,
    # and a group of a hundred cases alone outlasts the step's whole budget.
    CASE_TIMEOUT = 20.0

    def client(self, unicode_flag=True):
        left = _runner.budget_left()
        timeout = self.CASE_TIMEOUT if left is None else max(2.0, min(self.CASE_TIMEOUT, left / 4.0))
        c = P.SmbClient(self.host, self.port, timeout=timeout)
        c.connect()
        self._open.append(c)
        return c

    def smb1(self, share=None, dialects=None):
        """A connected, authenticated SMB1 session with a tree connect."""
        c = self.client()
        c.smb1_negotiate(dialects)
        c.smb1_session_setup(self.user, self.password)
        r = c.smb1_tree_connect(share or self.share)
        return c, r

    def close(self):
        for c in self._open:
            try:
                c.close()
            except Exception:
                pass
        self._open = []


# How many cases in a row may go unanswered before a group gives up on the
# service. Six is comfortably more than any transient stall and far less than the
# hundred-odd a full group holds.
UNRESPONSIVE_AFTER = 6


def runner(cases, record_dialect=False):
    """Build a run_all(...) for a case table of (name, function).

    ``record_dialect`` adds the highest dialect any of the case's connections
    actually negotiated to its observation. A module that exercises one dialect
    generation needs it: without it, a server that only speaks the *previous*
    generation quietly negotiates down and still satisfies most of the
    observations, and the group stops measuring the thing it is named for.
    """
    def run_all(host, port, user, password, only=None, share="tmp"):
        out = {}
        # A service that accepts the connection and then answers nothing costs
        # one timeout per case. Left alone, a group of a hundred cases spends the
        # whole step budget discovering the same thing a hundred times, and the
        # groups after it -- the previous rung's regressions, which such a
        # candidate would otherwise pass -- are marked failed for want of time.
        # Once the first few cases have all timed out, stop asking.
        consecutive_timeouts, unresponsive = 0, False
        for name, fn in cases:
            if only is not None and name not in only:
                continue
            left = _runner.budget_left()
            if _runner.budget_exhausted() or (left is not None and left < 5):
                out[name] = {"exception": "scoring budget exhausted"}
                continue
            if unresponsive:
                out[name] = {"exception": "service did not answer earlier cases"}
                continue
            ctx = Ctx(host, port, user, password, share)
            try:
                obs = fn(ctx)
            except Exception as e:
                obs = {"exception": "%s: %s" % (type(e).__name__, e),
                       "trace": traceback.format_exc().splitlines()[-3:]}
                if "timeout" in str(e).lower() or "closed by peer" in str(e):
                    consecutive_timeouts += 1
                    if consecutive_timeouts >= UNRESPONSIVE_AFTER:
                        _runner.log("probe group: %d consecutive cases went "
                                    "unanswered; the service is not speaking this "
                                    "dialect, marking the rest without asking\n"
                                    % consecutive_timeouts)
                        unresponsive = True
                else:
                    consecutive_timeouts = 0
            else:
                consecutive_timeouts = 0
                if record_dialect and isinstance(obs, dict):
                    seen = [getattr(c, "dialect", None) for c in ctx._open]
                    seen = [d for d in seen if d]
                    if seen:
                        obs["_negotiated"] = max(seen)
            finally:
                ctx.close()
            out[name] = obs
        return out
    return run_all
