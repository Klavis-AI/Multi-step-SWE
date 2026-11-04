package lua;

/** A Lua coroutine. Each runs on its own Java thread with a strict one-at-a-time handoff. */
final class Coroutine {
    static final int SUSPENDED = 0, RUNNING = 1, NORMAL = 2, DEAD = 3;

    final Object lock = new Object();
    LuaFunction body;
    Thread thread;
    int status;
    boolean started;
    boolean turnIsCo;
    boolean kill;
    Object[] xfer = Interp.EMPTY;
    LuaError err;

    Coroutine resumer;
    Frame top;
    int depth;
    int nCcalls;
    int nonYieldable;
    Object handler;
    boolean inHandler;
    // Blocks with open to-be-closed variables, innermost last.  coroutine.close
    // walks them so a suspended coroutine's '__close' handlers still run.
    final java.util.List<Object> tbc = new java.util.ArrayList<>();
    final java.util.List<Frame> tbcFrames = new java.util.ArrayList<>();
    final java.util.List<Blk> tbcBlocks = new java.util.ArrayList<>();
    final boolean isMain;

    Coroutine(LuaFunction body) {
        this.body = body;
        this.isMain = false;
        this.status = SUSPENDED;
    }

    private Coroutine() { this.isMain = true; this.status = RUNNING; }

    static Coroutine newMain() { return new Coroutine(); }

    String statusName() {
        switch (status) {
            case SUSPENDED: return "suspended";
            case RUNNING: return this == Interp.current ? "running" : "normal";
            case NORMAL: return "normal";
            default: return "dead";
        }
    }

    /** Returns {true, results...} or {false, error}. */
    static final int MAXCCALLS = 200;

    static Object[] resume(Coroutine co, Object[] args) {
        Coroutine from = Interp.current;
        co.nCcalls = from.nCcalls + 1;
        if (co.nCcalls >= MAXCCALLS) {
            return new Object[]{Boolean.FALSE, LuaString.of("C stack overflow")};
        }
        if (co.status != SUSPENDED) {
            String s = co.status == DEAD ? "dead" : (co == Interp.current ? "non-suspended" : "non-suspended");
            return new Object[]{Boolean.FALSE, LuaString.of("cannot resume " + (co.status == DEAD ? "dead" : "non-suspended") + " coroutine")};
        }
        Coroutine me = from;
        co.resumer = me;
        me.status = NORMAL;
        co.status = RUNNING;
        co.xfer = args;
        co.err = null;
        Interp.current = co;
        try {
            synchronized (co.lock) {
                if (!co.started) {
                    co.started = true;
                    Gc.registerCoroutine(co);
                    co.thread = new Thread(null, () -> co.run(), "lua-coroutine", 1 << 26);
                    co.thread.setDaemon(true);
                    co.thread.start();
                }
                co.turnIsCo = true;
                co.lock.notifyAll();
                while (co.turnIsCo) {
                    try { co.lock.wait(); } catch (InterruptedException e) { throw new RuntimeException(e); }
                }
            }
        } finally {
            Interp.current = me;
            me.status = RUNNING;
        }
        if (co.err != null) {
            LuaError e = co.err;
            co.err = null;
            // The error is kept so that the first coroutine.close reports it.
            co.pendingError = e;
            return new Object[]{Boolean.FALSE, e.value};
        }
        Object[] r = co.xfer;
        Object[] out = new Object[r.length + 1];
        out[0] = Boolean.TRUE;
        System.arraycopy(r, 0, out, 1, r.length);
        return out;
    }

    private void run() {
        synchronized (lock) {
            while (!turnIsCo) {
                try { lock.wait(); } catch (InterruptedException e) { return; }
            }
        }
        try {
            Object[] r = Interp.callNoBarrier(body, xfer);
            xfer = r;
        } catch (LuaError e) {
            err = e;
            xfer = Interp.EMPTY;
        } catch (KillThread e) {
            status = DEAD;
            synchronized (lock) { turnIsCo = false; lock.notifyAll(); }
            return;
        } catch (StackOverflowError e) {
            err = new LuaError(LuaString.of("stack overflow"));
            xfer = Interp.EMPTY;
        } catch (Throwable t) {
            err = new LuaError(LuaString.of(String.valueOf(t)));
            xfer = Interp.EMPTY;
        }
        status = DEAD;
        synchronized (lock) { turnIsCo = false; lock.notifyAll(); }
    }

    /** Unblocks a suspended coroutine that has become unreachable so its thread ends. */
    void killThread() {
        synchronized (lock) {
            if (!started || status == DEAD) return;
            kill = true;
            turnIsCo = true;
            lock.notifyAll();
        }
    }

    /** coroutine.close: run the pending to-be-closed variables and kill the thread. */
    LuaError closeAll() {
        LuaError err = null;
        if (status == SUSPENDED && started) {
            killThread();
            // The coroutine's own thread is gone, so its still-open to-be-closed
            // variables are closed here, innermost first.
            for (int b = tbcBlocks.size() - 1; b >= 0; b--) {
                Blk blk = tbcBlocks.get(b);
                Frame f = tbcFrames.get(b);
                for (int i = blk.tbc.length - 1; i >= 0; i--) {
                    Object v = f.slots[blk.tbc[i]];
                    if (v instanceof Upvalue) v = ((Upvalue) v).v;
                    if (v == null || v == Boolean.FALSE) continue;
                    Object h = Interp.metamethod(v, "__close");
                    if (h == null) continue;
                    // The closing runs on the coroutine's own state, which has
                    // no message handler: an error here is reported raw.
                    Object savedH = Interp.current.handler;
                    Interp.current.handler = null;
                    try {
                        Interp.callFromC("close", h, new Object[]{v, err == null ? null : err.value});
                    } catch (LuaError e2) {
                        err = e2;
                    } finally {
                        Interp.current.handler = savedH;
                    }
                }
            }
        }
        tbcBlocks.clear();
        tbcFrames.clear();
        tbc.clear();
        status = DEAD;
        if (pendingError != null) { err = pendingError; pendingError = null; }
        return err;
    }

    LuaError pendingError;

    static Object[] yield(Object[] vals) {
        Coroutine co = Interp.current;
        if (co.isMain) throw Interp.runtimeError0("attempt to yield from outside a coroutine");
        if (co.nonYieldable > 0) throw Interp.runtimeError0("attempt to yield across metamethod/C-call boundary");
        co.xfer = vals;
        co.status = SUSPENDED;
        synchronized (co.lock) {
            co.turnIsCo = false;
            co.lock.notifyAll();
            while (!co.turnIsCo) {
                try { co.lock.wait(); } catch (InterruptedException e) { throw new KillThread(); }
            }
        }
        if (co.kill) throw new KillThread();
        co.status = RUNNING;
        return co.xfer;
    }
}
