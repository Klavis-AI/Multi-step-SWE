package lua;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.List;

/**
 * A deterministic mark-and-sweep over the interpreter's own object graph. The JVM
 * collector is invisible to Lua programs, so weak tables and finalisers need their
 * own reachability pass, run whenever the program asks for a collection.
 */
final class Gc {
    static final List<Object> finalizable = new ArrayList<>();
    static final List<WeakReference<LuaTable>> weakTables = new ArrayList<>();
    static boolean running = true;
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

    static void registerWeak(LuaTable t) {
        for (WeakReference<LuaTable> r : weakTables) if (r.get() == t) return;
        weakTables.add(new WeakReference<>(t));
    }

    static void markFinalizable(Object o) {
        for (Object x : finalizable) if (x == o) return;
        finalizable.add(o);
    }

    static void collect() {
        marked = new IdentityHashMap<>();
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

        // clear dead entries from weak tables
        for (int i = weakTables.size() - 1; i >= 0; i--) {
            LuaTable t = weakTables.get(i).get();
            if (t == null) { weakTables.remove(i); continue; }
            if (!marked.containsKey(t)) continue;
            String mode = modeOf(t);
            if (mode == null) continue;
            boolean wk = mode.indexOf('k') >= 0, wv = mode.indexOf('v') >= 0;
            if (!wk && !wv) continue;
            for (int s = 0; s < t.hashSlots(); s++) {
                Object k = t.slotKey(s), v = t.slotVal(s);
                if (v == null) continue;
                if ((wk && dead(k)) || (wv && dead(v))) t.slotClear(s);
            }
            if (wv) {
                for (int a = 0; a < t.asize; a++) {
                    Object v = t.array[a];
                    if (v != null && dead(v)) t.array[a] = null;
                }
            }
        }

        // finalisers, in reverse order of marking
        List<Object> dead = new ArrayList<>();
        for (int i = finalizable.size() - 1; i >= 0; i--) {
            Object o = finalizable.get(i);
            if (!marked.containsKey(o)) { dead.add(o); finalizable.remove(i); }
        }
        lastLive = marked.size();
        marked = null;
        for (Object o : dead) {
            Object h = Interp.metamethod(o, "__gc");
            if (h instanceof LuaFunction) {
                try { Interp.call(h, new Object[]{o}); } catch (LuaError e) { /* ignored, as in PUC */ }
            }
        }
        counter++;
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
            for (int s = 0; s < t.hashSlots(); s++) {
                Object v = t.slotVal(s);
                if (v == null) continue;
                if (!wk) markValue(t.slotKey(s));
                if (!wv) markValue(v);
            }
        } else if (o instanceof LuaClosure) {
            LuaClosure c = (LuaClosure) o;
            markValue(c.env);
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
    static long count() {
        return ((long) lastLive * 72L + allocated) / 1024L + 1L;
    }
}
