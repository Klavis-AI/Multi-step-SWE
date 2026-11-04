package lua;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.List;

/**
 * A deterministic mark-and-sweep over the interpreter's own object graph. The JVM
 * collector is invisible to Lua programs, so weak tables and finalisers need their
 * own reachability pass, run whenever the program asks for a collection.
 */
final class Gc {
    static final List<Object> finalizable = new ArrayList<>();
    static final List<WeakReference<LuaTable>> weakTables = new ArrayList<>();
    static final List<WeakReference<Coroutine>> liveCoroutines = new ArrayList<>();

    static void registerCoroutine(Coroutine co) {
        liveCoroutines.add(new WeakReference<>(co));
    }
    static boolean running = true;
    /** True while a collection is under way: a nested request does nothing. */
    static boolean collecting;
    static String mode = "generational";

    /**
     * The collector parameters reachable through collectgarbage("param", ...).
     * They have no effect on this collector, which runs to completion whenever
     * it runs, but the values round-trip and the defaults are the real ones.
     */
    static final Map<String, Long> params = new LinkedHashMap<>();
    static {
        params.put("pause", 250L);
        params.put("stepmul", 200L);
        params.put("stepsize", 9600L);
        params.put("minormul", 20L);
        params.put("majorminor", 50L);
        params.put("minormajor", 68L);
    }
    static long counter;

    /** Bytes allocated since the last collection, and the threshold that triggers one. */
    static long allocated;
    static long threshold = 400_000;
    static int lastLive;

    static void alloc(int n) { allocated += n; }

    static boolean due() { return running && allocated >= threshold; }

    /** One automatic collection, run only at a statement boundary. */
    static void step() {
        collect();
        allocated = 0;
        threshold = Math.max(400_000L, (long) lastLive * 200L);
    }

    private static IdentityHashMap<Object, Object> marked;
    private static final List<LuaTable> ephemeron = new ArrayList<>();

    static void registerWeak(LuaTable t) {
        for (WeakReference<LuaTable> r : weakTables) if (r.get() == t) return;
        weakTables.add(new WeakReference<>(t));
    }

    static void markFinalizable(Object o) {
        for (Object x : finalizable) if (x == o) return;
        finalizable.add(o);
    }

    static void collect() {
        if (collecting) return;
        collecting = true;
        try {
            collect0();
        } finally {
            collecting = false;
        }
    }

    private static void collect0() {
        marked = new IdentityHashMap<>();
        ephemeron.clear();
        markValue(Interp.globals);
        markValue(Interp.registry);
        markValue(Interp.stringMeta);
        markValue(Interp.numberMeta);
        markValue(Interp.booleanMeta);
        markValue(Interp.nilMeta);
        markValue(Interp.functionMeta);
        markValue(Interp.threadMeta);
        for (Coroutine c = Interp.current; c != null; c = c.resumer) markValue(c);
        markValue(Interp.main);
        propagateEphemerons();

        // The atomic phase, in PUC's order: weak values go before finalisers are
        // separated, weak keys after, so an object resurrected for its finaliser
        // keeps the weak-table entries it is the key of but loses those it is a
        // value of.
        clearWeak(true, false);

        List<Object> dead = new ArrayList<>();
        for (int i = finalizable.size() - 1; i >= 0; i--) {
            Object o = finalizable.get(i);
            if (!marked.containsKey(o)) { dead.add(o); finalizable.remove(i); }
        }
        for (Object o : dead) markValue(o);
        propagateEphemerons();

        // Values are cleared a second time as well: a table that only became
        // reachable through a resurrected object is weak-listed for the first
        // time here, and its dead values have to go before any finaliser runs.
        clearWeak(true, true);

        // a suspended coroutine nobody can reach will never run again: release its thread
        for (int i = liveCoroutines.size() - 1; i >= 0; i--) {
            Coroutine co = liveCoroutines.get(i).get();
            if (co == null) { liveCoroutines.remove(i); continue; }
            if (co.status == Coroutine.DEAD) { liveCoroutines.remove(i); continue; }
            if (!marked.containsKey(co)) {
                liveCoroutines.remove(i);
                co.killThread();
            }
        }

        lastLive = marked.size();
        marked = null;
        LuaError pending = null;
        for (Object o : dead) {
            Object h = Interp.metamethod(o, "__gc");
            if (h instanceof LuaFunction) {
                // A finaliser runs unprotected by any message handler.
                Object savedH = Interp.current.handler;
                Interp.current.handler = null;
                try {
                    Interp.call(h, new Object[]{o});
                } catch (LuaError e) {
                    if (pending == null) {
                        Object v = e.value;
                        String t = (v instanceof LuaString || LuaNum.isNum(v))
                                ? Interp.rawToString(v) : "?";
                        pending = new LuaError(LuaString.of("error in __gc metamethod (" + t + ")"));
                    }
                } finally {
                    Interp.current.handler = savedH;
                }
            }
        }
        counter++;
        if (pending != null) throw pending;
    }

    /** Removes entries of every reachable weak table whose weak side is dead. */
    private static void clearWeak(boolean values, boolean keys) {
        for (int i = weakTables.size() - 1; i >= 0; i--) {
            LuaTable t = weakTables.get(i).get();
            if (t == null) { weakTables.remove(i); continue; }
            if (!marked.containsKey(t)) continue;
            String mode = modeOf(t);
            if (mode == null) continue;
            boolean wk = mode.indexOf('k') >= 0, wv = mode.indexOf('v') >= 0;
            for (int s = 0; s < t.hashSlots(); s++) {
                Object k = t.slotKey(s), v = t.slotVal(s);
                if (v == null) continue;
                if (keys && wk && dead(k)) { t.slotClear(s); continue; }
                if (values && wv && dead(v)) t.slotClear(s);
            }
            if (values && wv) {
                for (int a = 0; a < t.asize; a++) {
                    Object v = t.array[a];
                    if (v != null && dead(v)) t.array[a] = null;
                }
            }
        }
    }

    private static String modeOf(LuaTable t) {
        if (t.metatable == null) return null;
        Object m = t.metatable.rawget("__mode");
        return m instanceof LuaString ? m.toString() : null;
    }

    static boolean collectable(Object o) {
        return o instanceof LuaTable || o instanceof LuaFunction || o instanceof Coroutine
                || o instanceof Userdata;
    }

    private static boolean dead(Object o) {
        return collectable(o) && !marked.containsKey(o);
    }

    private static final List<LuaTable> pendingEphemeron = new ArrayList<>();

    private static void markValue(Object o) {
        if (o == null || !collectable(o)) return;
        if (marked.put(o, Boolean.TRUE) != null) return;
        if (o instanceof LuaTable) {
            LuaTable t = (LuaTable) o;
            markValue(t.metatable);
            String mode = modeOf(t);
            boolean wk = mode != null && mode.indexOf('k') >= 0;
            boolean wv = mode != null && mode.indexOf('v') >= 0;
            if (wk || wv) registerWeak(t);
            for (int a = 0; a < t.asize; a++) {
                if (!wv) markValue(t.array[a]);
            }
            if (wk && !wv) {
                ephemeron.add(t);          // values wait until their key is known live
            }
            for (int s = 0; s < t.hashSlots(); s++) {
                Object v = t.slotVal(s);
                if (v == null) continue;
                if (!wk) markValue(t.slotKey(s));
                if (!wv && !wk) markValue(v);
            }
        } else if (o instanceof LuaClosure) {
            LuaClosure c = (LuaClosure) o;
            for (Upvalue u : c.upvals) if (u != null) markValue(u.v);
        } else if (o instanceof Userdata) {
            Userdata u = (Userdata) o;
            markValue(u.metatable);
        } else if (o instanceof Coroutine) {
            Coroutine c = (Coroutine) o;
            markValue(c.body);
            for (Object x : c.xfer) markValue(x);
            for (Frame f = c.top; f != null; f = f.prev) markFrame(f);
        }
    }

    private static void markFrame(Frame f) {
        markValue(f.cl);
        for (Object s : f.slots) {
            if (s instanceof Upvalue) markValue(((Upvalue) s).v);
            else markValue(s);
        }
        for (Object v : f.varargs) markValue(v);
        if (f.ret != null) for (Object v : f.ret) markValue(v);
        if (f.temps != null) {
            for (Object v : f.temps) {
                if (v instanceof Object[]) for (Object x : (Object[]) v) markValue(x);
                else markValue(v);
            }
        }
    }

    /** An estimate of the live bytes, in Kbytes, for collectgarbage("count"). */
    /**
     * Ephemeron tables: in a table with weak keys and strong values, a value is
     * reachable only while its key is, so marking has to reach a fixed point.
     */
    private static void propagateEphemerons() {
        boolean changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < ephemeron.size(); i++) {
                LuaTable t = ephemeron.get(i);
                if (!marked.containsKey(t)) continue;
                for (int s = 0; s < t.hashSlots(); s++) {
                    Object v = t.slotVal(s);
                    if (v == null) continue;
                    Object k = t.slotKey(s);
                    if (collectable(k) && !marked.containsKey(k)) continue;
                    int before = marked.size();
                    markValue(v);
                    if (marked.size() != before) changed = true;
                }
            }
        }
    }

    /** Runs every pending finaliser, as closing a state does. */
    static void closeState() {
        for (int i = finalizable.size() - 1; i >= 0; i--) {
            Object o = finalizable.get(i);
            Object h = Interp.metamethod(o, "__gc");
            if (h instanceof LuaFunction) {
                try { Interp.call(h, new Object[]{o}); } catch (LuaError e) { /* ignored */ }
            }
        }
        finalizable.clear();
    }

    static long count() {
        return ((long) lastLive * 72L + allocated) / 1024L + 1L;
    }
}
