package redis;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * The JSON.* family, as RedisJSON (module "ReJSON", type name ReJSON-RL)
 * exposes it in redis 8.0.
 *
 * Two path dialects live side by side: a JSONPath (anything starting with '$')
 * always replies with one element per match, while a legacy path (everything
 * else, and the default when the path argument is omitted) replies with a
 * single value and turns "no match" into an error. The two dialects also
 * disagree about which spelling of the path the error message quotes, so the
 * helpers below take the raw argument as well as the compiled path.
 */
public final class JsonCmds {
    private JsonCmds() {}

    private static Server S() { return Server.I; }

    static final String E_NOKEY = "ERR could not perform this operation on a key that doesn't exist";
    static final String E_WRONGREDIS = "Existing key has wrong Redis type";
    static final String E_ROOTONLY = "ERR new objects must be created at the root";

    // ------------------------------------------------------------------
    // lookup helpers
    // ------------------------------------------------------------------

    /** The document behind `key`, or null; a non-JSON key is an error. */
    private static RJson doc(Client c, byte[] key) {
        RObj o = S().lookupKeyRead(c, key);
        if (o == null) return null;
        if (o.type != RObj.MODULE || !(o.ptr instanceof RJson)) throw new RedisError(E_WRONGREDIS);
        return (RJson) o.ptr;
    }

    private static RJson docWrite(Client c, byte[] key) {
        RObj o = S().lookupKeyWrite(c, key);
        if (o == null) return null;
        if (o.type != RObj.MODULE || !(o.ptr instanceof RJson)) throw new RedisError(E_WRONGREDIS);
        return (RJson) o.ptr;
    }

    private static void store(Client c, byte[] key, RJson v) {
        RObj o = S().lookupKeyWrite(c, key);
        if (o != null && o.type == RObj.MODULE && o.ptr instanceof RJson) {
            o.ptr = v;
            S().signalModifiedKey(c.db, new Key(key));
        } else {
            S().setKey(c, key, ModTypes.obj(v));
        }
    }

    /** The root of `key` is replaced in place, so only the modification signal is due. */
    private static void touched(Client c, byte[] key, String event) {
        S().signalModifiedKey(c.db, new Key(key));
        S().notifyKeyspaceEvent('m', event, key, c.db);
        S().dirty++;
    }

    static RJson.Path path(byte[] raw) {
        try {
            return RJson.compile(raw);
        } catch (RJson.PathError e) {
            throw new RedisError(e.getMessage());
        }
    }

    static RJson parseValue(byte[] raw) { return parseValue(raw, true); }

    /**
     * JSON.SET reports a malformed document through RedisModule_ReplyWithError
     * with serde's own message, so it arrives without redis's "ERR " code;
     * every other command wraps it in one.
     */
    static RJson parseValue(byte[] raw, boolean prefix) {
        try {
            return RJson.parse(raw);
        } catch (RJson.ParseError e) {
            throw new RedisError(prefix ? "ERR " + e.getMessage() : e.getMessage());
        }
    }

    private static void wrongArity(String name) { throw RedisError.wrongArgs(name); }

    /** The spelling an error quotes: the original text for some commands. */
    private static String orig(byte[] raw) { return Util.s(raw); }

    // ------------------------------------------------------------------
    // JSON.SET / JSON.MSET / JSON.MERGE
    // ------------------------------------------------------------------

    private static void jsonSet(Client c) {
        if (c.argc < 4) wrongArity("json.set");
        if (c.argc > 5) throw RedisError.syntax();
        boolean nx = false, xx = false;
        if (c.argc == 5) {
            if (c.argIs(4, "NX")) nx = true;
            else if (c.argIs(4, "XX")) xx = true;
            else throw RedisError.syntax();
        }
        RJson.Path p = path(c.arg(2));
        RJson val = parseValue(c.arg(3), false);
        RJson cur = docWrite(c, c.arg(1));

        if (cur == null) {
            if (!p.isRoot()) throw new RedisError(E_ROOTONLY);
            if (xx) { c.addNil(); return; }
            store(c, c.arg(1), val);
            S().signalModifiedKey(c.db, new Key(c.arg(1)));
            S().notifyKeyspaceEvent('m', "json.set", c.arg(1), c.db);
            S().dirty++;
            c.addOk();
            return;
        }
        if (p.isRoot()) {
            if (nx) { c.addNil(); return; }
            store(c, c.arg(1), val);
            touched(c, c.arg(1), "json.set");
            c.addOk();
            return;
        }
        List<RJson.Loc> hits = cur.select(p);
        if (!hits.isEmpty()) {
            if (nx) { c.addNil(); return; }
            for (RJson.Loc l : hits) l.set(val.deepCopy());
            touched(c, c.arg(1), "json.set");
            c.addOk();
            return;
        }
        if (xx) { c.addNil(); return; }
        if (!create(cur, p, val)) { c.addNil(); return; }
        touched(c, c.arg(1), "json.set");
        c.addOk();
    }

    /**
     * Create the single missing leaf a static path names; the parent must exist
     * already (RedisJSON never builds intermediate containers).
     */
    private static boolean create(RJson root, RJson.Path p, RJson val) {
        if (!p.isStatic() || p.len() == 0) return false;
        RJson cur = root;
        for (int i = 0; i < p.len() - 1; i++) {
            Object[] st = p.step(i);
            int op = (Integer) st[0];
            if (op == RJson.S_KEY) {
                if (cur.t != RJson.OBJECT) return false;
                cur = cur.obj.get(new Key(Util.b((String) st[1])));
            } else {
                if (cur.t != RJson.ARRAY) return false;
                long ix = (Long) st[1];
                if (ix < 0) ix += cur.arr.size();
                if (ix < 0 || ix >= cur.arr.size()) return false;
                cur = cur.arr.get((int) ix);
            }
            if (cur == null) return false;
        }
        Object[] last = p.step(p.len() - 1);
        if ((Integer) last[0] == RJson.S_KEY) {
            if (cur.t != RJson.OBJECT) return false;
            cur.obj.put(new Key(Util.b((String) last[1])), val);
            return true;
        }
        if (cur.t != RJson.ARRAY) return false;
        throw new RedisError("ERR array index out of range");
    }

    private static void jsonMset(Client c) {
        if (c.argc < 4 || (c.argc - 1) % 3 != 0) wrongArity("json.mset");
        /* Validate everything before touching the keyspace. */
        int n = (c.argc - 1) / 3;
        RJson.Path[] paths = new RJson.Path[n];
        RJson[] vals = new RJson[n];
        for (int i = 0; i < n; i++) {
            paths[i] = path(c.arg(2 + i * 3));
            vals[i] = parseValue(c.arg(3 + i * 3), false);
        }
        for (int i = 0; i < n; i++) {
            byte[] key = c.arg(1 + i * 3);
            RJson cur = docWrite(c, key);
            if (cur == null && !paths[i].isRoot()) throw new RedisError(E_ROOTONLY);
            if (cur != null && !paths[i].isRoot()) {
                List<RJson.Loc> hits = cur.select(paths[i]);
                if (hits.isEmpty() && !create(cur, paths[i], vals[i]))
                    throw new RedisError(E_ROOTONLY);
            }
        }
        for (int i = 0; i < n; i++) {
            byte[] key = c.arg(1 + i * 3);
            RJson cur = docWrite(c, key);
            if (cur == null || paths[i].isRoot()) {
                store(c, key, vals[i]);
            } else {
                List<RJson.Loc> hits = cur.select(paths[i]);
                for (RJson.Loc l : hits) l.set(vals[i].deepCopy());
            }
            S().signalModifiedKey(c.db, new Key(key));
            S().notifyKeyspaceEvent('m', "json.set", key, c.db);
            S().dirty++;
        }
        c.addOk();
    }

    private static void jsonMerge(Client c) {
        if (c.argc != 4) wrongArity("json.merge");
        RJson.Path p = path(c.arg(2));
        RJson val = parseValue(c.arg(3));
        RJson cur = docWrite(c, c.arg(1));
        if (cur == null) {
            if (!p.isRoot()) throw new RedisError(E_ROOTONLY);
            store(c, c.arg(1), val);
            S().signalModifiedKey(c.db, new Key(c.arg(1)));
            S().notifyKeyspaceEvent('m', "json.merge", c.arg(1), c.db);
            S().dirty++;
            c.addOk();
            return;
        }
        if (p.isRoot()) {
            RJson merged = mergePatch(cur, val);
            store(c, c.arg(1), merged);
            touched(c, c.arg(1), "json.merge");
            c.addOk();
            return;
        }
        List<RJson.Loc> hits = cur.select(p);
        if (hits.isEmpty()) {
            if (!create(cur, p, val)) throw new RedisError(E_ROOTONLY);
        } else {
            for (RJson.Loc l : hits) l.set(mergePatch(l.val, val.deepCopy()));
        }
        touched(c, c.arg(1), "json.merge");
        c.addOk();
    }

    /** RFC 7386 JSON Merge Patch. */
    private static RJson mergePatch(RJson target, RJson patch) {
        if (patch.t != RJson.OBJECT) return patch;
        RJson out = (target != null && target.t == RJson.OBJECT) ? target : RJson.object();
        for (Map.Entry<Key, RJson> e : patch.obj.entrySet()) {
            if (e.getValue().t == RJson.NULL) RJson.swapRemove(out.obj, e.getKey());
            else out.obj.put(e.getKey(), mergePatch(out.obj.get(e.getKey()), e.getValue()));
        }
        return out;
    }

    // ------------------------------------------------------------------
    // JSON.GET / JSON.MGET
    // ------------------------------------------------------------------

    private static void jsonGet(Client c) {
        if (c.argc < 2) wrongArity("json.get");
        RJson.Fmt f = new RJson.Fmt();
        List<byte[]> paths = new ArrayList<>();
        for (int i = 2; i < c.argc; i++) {
            if (c.argIs(i, "INDENT")) {
                if (i + 1 >= c.argc) wrongArity("json.get");
                f.indent = c.arg(++i);
            } else if (c.argIs(i, "NEWLINE")) {
                if (i + 1 >= c.argc) wrongArity("json.get");
                f.newline = c.arg(++i);
            } else if (c.argIs(i, "SPACE")) {
                if (i + 1 >= c.argc) wrongArity("json.get");
                f.space = c.arg(++i);
            } else if (c.argIs(i, "NOESCAPE")) {
                /* accepted and ignored, kept for RedisJSON v1 compatibility */
            } else {
                paths.add(c.arg(i));
            }
        }
        RJson d = doc(c, c.arg(1));
        if (d == null) { c.addNil(); return; }
        if (paths.isEmpty()) paths.add(Util.b("."));

        if (paths.size() == 1) {
            byte[] raw = paths.get(0);
            RJson.Path p = path(raw);
            List<RJson.Loc> hits = d.select(p);
            if (p.legacy) {
                if (hits.isEmpty()) throw new RedisError("ERR Path '" + p.text + "' does not exist");
                c.addBulk(hits.get(0).val.toBytes(f));
            } else {
                c.addBulk(arrayOf(hits).toBytes(f));
            }
            return;
        }
        /* Several paths: a JSON object keyed by the path text. RedisJSON keeps
         * them in a hash map, so the corpus records this shape as normalized. */
        RJson out = RJson.object();
        for (byte[] raw : paths) {
            RJson.Path p = path(raw);
            List<RJson.Loc> hits = d.select(p);
            RJson v;
            if (p.legacy) {
                if (hits.isEmpty()) throw new RedisError("ERR Path '" + p.text + "' does not exist");
                v = hits.get(0).val;
            } else {
                v = arrayOf(hits);
            }
            out.obj.put(new Key(raw), v);
        }
        c.addBulk(out.toBytes(f));
    }

    private static RJson arrayOf(List<RJson.Loc> hits) {
        RJson a = RJson.array();
        for (RJson.Loc l : hits) a.arr.add(l.val);
        return a;
    }

    private static void jsonMget(Client c) {
        if (c.argc < 3) wrongArity("json.mget");
        byte[] raw = c.arg(c.argc - 1);
        RJson.Path p = path(raw);
        int n = c.argc - 2;
        c.addArray(n);
        for (int i = 1; i <= n; i++) {
            RJson d;
            try { d = doc(c, c.arg(i)); }
            catch (RedisError e) { c.addNil(); continue; }
            if (d == null) { c.addNil(); continue; }
            List<RJson.Loc> hits = d.select(p);
            if (p.legacy) {
                if (hits.isEmpty()) c.addNil();
                else c.addBulk(hits.get(0).val.toBytes());
            } else {
                c.addBulk(arrayOf(hits).toBytes());
            }
        }
    }

    // ------------------------------------------------------------------
    // JSON.DEL / JSON.FORGET / JSON.TYPE / JSON.CLEAR
    // ------------------------------------------------------------------

    private static void jsonDel(Client c) {
        if (c.argc < 2 || c.argc > 3) wrongArity(c.argIs(0, "json.forget") ? "json.forget" : "json.del");
        byte[] raw = c.argc > 2 ? c.arg(2) : Util.b(".");
        RJson.Path p = path(raw);
        RJson d = docWrite(c, c.arg(1));
        if (d == null) { c.addInt(0); return; }
        if (p.isRoot()) {
            S().dbDelete(c.db, c.arg(1));
            S().notifyKeyspaceEvent('g', "del", c.arg(1), c.db);
            S().dirty++;
            c.addInt(1);
            return;
        }
        List<RJson.Loc> hits = d.select(p);
        int n = deleteAll(hits);
        if (n > 0) touched(c, c.arg(1), "json.del");
        c.addInt(n);
    }

    /** Remove matched nodes; array members go last-first so indexes stay valid. */
    private static int deleteAll(List<RJson.Loc> hits) {
        int n = 0;
        List<RJson.Loc> ordered = new ArrayList<>(hits);
        ordered.sort((a, b) -> Integer.compare(b.idx, a.idx));
        for (RJson.Loc l : ordered) {
            if (l.parent == null) continue;
            if (l.parent.t == RJson.ARRAY) {
                if (l.idx >= 0 && l.idx < l.parent.arr.size() && l.parent.arr.get(l.idx) == l.val) {
                    l.parent.arr.remove(l.idx);
                    n++;
                }
            } else if (RJson.swapRemove(l.parent.obj, l.key)) {
                n++;
            }
        }
        return n;
    }

    private static void jsonType(Client c) {
        if (c.argc < 2 || c.argc > 3) wrongArity("json.type");
        byte[] raw = c.argc > 2 ? c.arg(2) : Util.b(".");
        RJson.Path p = path(raw);
        RJson d = doc(c, c.arg(1));
        if (d == null) { c.addNil(); return; }
        List<RJson.Loc> hits = d.select(p);
        if (p.legacy) {
            if (hits.isEmpty()) { c.addNil(); return; }
            c.addBulk(hits.get(0).val.jsonTypeName());
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) c.addBulk(l.val.jsonTypeName());
    }

    private static void jsonClear(Client c) {
        if (c.argc < 2 || c.argc > 3) wrongArity("json.clear");
        byte[] raw = c.argc > 2 ? c.arg(2) : Util.b(".");
        RJson.Path p = path(raw);
        RJson d = docWrite(c, c.arg(1));
        if (d == null) throw new RedisError(E_NOKEY);
        int n = 0;
        for (RJson.Loc l : d.select(p)) {
            RJson v = l.val;
            switch (v.t) {
                case RJson.ARRAY: if (!v.arr.isEmpty()) { v.arr.clear(); n++; } break;
                case RJson.OBJECT: if (!v.obj.isEmpty()) { v.obj.clear(); n++; } break;
                case RJson.INT: if (v.i != 0) { v.i = 0; n++; } break;
                case RJson.UINT: if (v.i != 0) { v.t = RJson.INT; v.i = 0; n++; } break;
                case RJson.DOUBLE: if (v.d != 0) { v.t = RJson.INT; v.i = 0; n++; } break;
                default: break;
            }
        }
        if (n > 0) touched(c, c.arg(1), "json.clear");
        c.addInt(n);
    }

    private static void jsonToggle(Client c) {
        if (c.argc != 3) wrongArity("json.toggle");
        RJson.Path p = path(c.arg(2));
        RJson d = docWrite(c, c.arg(1));
        if (d == null) throw new RedisError(E_NOKEY);
        List<RJson.Loc> hits = d.select(p);
        int changed = 0;
        if (p.legacy) {
            RJson v = firstOfType(hits, RJson.BOOL);
            if (v == null) throw new RedisError("ERR Path '" + p.text + "' does not exist or not a bool");
            v.b = !v.b;
            touched(c, c.arg(1), "json.toggle");
            c.addBulk(v.b ? "true" : "false");
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.BOOL) { l.val.b = !l.val.b; changed++; c.addInt(l.val.b ? 1 : 0); }
            else c.addNil();
        }
        if (changed > 0) touched(c, c.arg(1), "json.toggle");
    }

    private static RJson firstOfType(List<RJson.Loc> hits, int t) {
        for (RJson.Loc l : hits) if (l.val.t == t) return l.val;
        return null;
    }

    // ------------------------------------------------------------------
    // numbers
    // ------------------------------------------------------------------

    private static void numOp(Client c, boolean mult) {
        String name = mult ? "json.nummultby" : "json.numincrby";
        if (c.argc != 4) wrongArity(name);
        RJson.Path p = path(c.arg(2));
        RJson by = parseValue(c.arg(3));
        if (!by.isNumber()) throw new RedisError("bad input number");
        RJson d = docWrite(c, c.arg(1));
        if (d == null) throw new RedisError(E_NOKEY);
        List<RJson.Loc> hits = d.select(p);
        boolean changed = false;
        if (p.legacy) {
            RJson target = null;
            for (RJson.Loc l : hits) if (l.val.isNumber()) { target = l.val; break; }
            if (target == null)
                throw new RedisError("ERR Path '" + p.text + "' does not exist or does not contains a number");
            apply(target, by, mult);
            touched(c, c.arg(1), mult ? "json.nummultby" : "json.numincrby");
            c.addBulk(target.toBytes());
            return;
        }
        RJson out = RJson.array();
        for (RJson.Loc l : hits) {
            if (l.val.isNumber()) { apply(l.val, by, mult); out.arr.add(l.val); changed = true; }
            else out.arr.add(RJson.nul());
        }
        if (changed) touched(c, c.arg(1), mult ? "json.nummultby" : "json.numincrby");
        c.addBulk(out.toBytes());
    }

    /** i64 arithmetic stays i64 (wrapping) while both sides are integral. */
    private static void apply(RJson target, RJson by, boolean mult) {
        boolean bothInt = (target.t == RJson.INT || target.t == RJson.UINT)
                && (by.t == RJson.INT || by.t == RJson.UINT);
        if (bothInt) {
            long a = target.i, b = by.i;
            target.t = RJson.INT;
            target.i = mult ? a * b : a + b;
            return;
        }
        double v = mult ? target.num() * by.num() : target.num() + by.num();
        target.t = RJson.DOUBLE;
        target.d = v;
    }

    // ------------------------------------------------------------------
    // strings
    // ------------------------------------------------------------------

    private static void jsonStrLen(Client c) {
        if (c.argc < 2 || c.argc > 3) wrongArity("json.strlen");
        byte[] raw = c.argc > 2 ? c.arg(2) : Util.b(".");
        RJson.Path p = path(raw);
        RJson d = doc(c, c.arg(1));
        if (d == null) {
            if (p.legacy) { c.addNil(); return; }
            throw new RedisError(E_NOKEY);
        }
        List<RJson.Loc> hits = d.select(p);
        if (p.legacy) {
            if (hits.isEmpty()) throw new RedisError("ERR Path '" + p.text + "' does not exist");
            RJson v = hits.get(0).val;
            if (v.t != RJson.STRING) throw wrongPathType("string", v);
            c.addInt(v.s.length);
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.STRING) c.addInt(l.val.s.length);
            else c.addNil();
        }
    }

    static RedisError wrongPathType(String want, RJson got) {
        return new RedisError("WRONGTYPE wrong type of path value - expected " + want
                + " but found " + got.jsonTypeName());
    }

    private static void jsonStrAppend(Client c) {
        if (c.argc < 3 || c.argc > 4) wrongArity("json.strappend");
        byte[] raw = c.argc > 3 ? c.arg(2) : Util.b(".");
        byte[] valRaw = c.arg(c.argc - 1);
        RJson.Path p = path(raw);
        RJson add = parseValue(valRaw);
        RJson d = docWrite(c, c.arg(1));
        if (d == null) throw new RedisError(E_NOKEY);
        if (add.t != RJson.STRING) throw wrongPathType("string", add);
        List<RJson.Loc> hits = d.select(p);
        boolean changed = false;
        if (p.legacy) {
            RJson v = firstOfType(hits, RJson.STRING);
            if (v == null) throw new RedisError("ERR Path '" + p.text + "' does not exist or not a string");
            v.s = Util.concat(v.s, add.s);
            touched(c, c.arg(1), "json.strappend");
            c.addInt(v.s.length);
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.STRING) {
                l.val.s = Util.concat(l.val.s, add.s);
                changed = true;
                c.addInt(l.val.s.length);
            } else {
                c.addNil();
            }
        }
        if (changed) touched(c, c.arg(1), "json.strappend");
    }

    // ------------------------------------------------------------------
    // objects
    // ------------------------------------------------------------------

    private static void jsonObjLen(Client c) {
        if (c.argc < 2 || c.argc > 3) wrongArity("json.objlen");
        byte[] raw = c.argc > 2 ? c.arg(2) : Util.b(".");
        RJson.Path p = path(raw);
        RJson d = doc(c, c.arg(1));
        if (d == null) {
            if (p.legacy) { c.addNil(); return; }
            throw new RedisError("ERR Path '" + p.text + "' does not exist or not an object");
        }
        List<RJson.Loc> hits = d.select(p);
        if (p.legacy) {
            if (hits.isEmpty()) { c.addNil(); return; }
            RJson v = hits.get(0).val;
            if (v.t != RJson.OBJECT) throw wrongPathType("object", v);
            c.addInt(v.obj.size());
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.OBJECT) c.addInt(l.val.obj.size());
            else c.addNil();
        }
    }

    private static void jsonObjKeys(Client c) {
        if (c.argc < 2 || c.argc > 3) wrongArity("json.objkeys");
        byte[] raw = c.argc > 2 ? c.arg(2) : Util.b(".");
        RJson.Path p = path(raw);
        RJson d = doc(c, c.arg(1));
        if (d == null) {
            if (p.legacy) { c.addNil(); return; }
            throw new RedisError(E_NOKEY);
        }
        List<RJson.Loc> hits = d.select(p);
        if (p.legacy) {
            if (hits.isEmpty()) { c.addNil(); return; }
            RJson v = hits.get(0).val;
            if (v.t != RJson.OBJECT)
                throw new RedisError("ERR Path '" + p.text + "' does not exist or not an object");
            c.addArray(v.obj.size());
            for (Key k : v.obj.keySet()) c.addBulk(k.b);
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.OBJECT) {
                c.addArray(l.val.obj.size());
                for (Key k : l.val.obj.keySet()) c.addBulk(k.b);
            } else {
                c.addNilArray();
            }
        }
    }

    // ------------------------------------------------------------------
    // arrays
    // ------------------------------------------------------------------

    private static void jsonArrLen(Client c) {
        if (c.argc < 2 || c.argc > 3) wrongArity("json.arrlen");
        boolean given = c.argc > 2;
        byte[] raw = given ? c.arg(2) : Util.b(".");
        RJson.Path p = path(raw);
        RJson d = doc(c, c.arg(1));
        if (d == null) {
            if (p.legacy) { c.addNil(); return; }
            throw new RedisError(E_NOKEY);
        }
        List<RJson.Loc> hits = d.select(p);
        if (p.legacy) {
            if (hits.isEmpty()) throw new RedisError("ERR Path '" + orig(raw) + "' does not exist");
            RJson v = hits.get(0).val;
            if (v.t != RJson.ARRAY)
                throw new RedisError("ERR Path '" + orig(raw) + "' does not exist or not an array");
            c.addInt(v.arr.size());
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.ARRAY) c.addInt(l.val.arr.size());
            else c.addNil();
        }
    }

    private static void jsonArrAppend(Client c) {
        if (c.argc < 4) wrongArity("json.arrappend");
        byte[] raw = c.arg(2);
        RJson.Path p = path(raw);
        List<RJson> add = new ArrayList<>();
        for (int i = 3; i < c.argc; i++) add.add(parseValue(c.arg(i)));
        RJson d = docWrite(c, c.arg(1));
        if (d == null) throw new RedisError(E_NOKEY);
        List<RJson.Loc> hits = d.select(p);
        boolean changed = false;
        if (p.legacy) {
            RJson v = firstOfType(hits, RJson.ARRAY);
            if (v == null)
                throw new RedisError("ERR Path '" + orig(raw) + "' does not exist or not an array");
            for (RJson a : add) v.arr.add(a.deepCopy());
            touched(c, c.arg(1), "json.arrappend");
            c.addInt(v.arr.size());
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.ARRAY) {
                for (RJson a : add) l.val.arr.add(a.deepCopy());
                changed = true;
                c.addInt(l.val.arr.size());
            } else {
                c.addNil();
            }
        }
        if (changed) touched(c, c.arg(1), "json.arrappend");
    }

    private static void jsonArrIndex(Client c) {
        if (c.argc < 4 || c.argc > 6) wrongArity("json.arrindex");
        byte[] raw = c.arg(2);
        RJson.Path p = path(raw);
        RJson needle = parseValue(c.arg(3));
        long start = c.argc > 4 ? c.argLong(4) : 0;
        long stop = c.argc > 5 ? c.argLong(5) : 0;
        RJson d = doc(c, c.arg(1));
        if (d == null) throw new RedisError("ERR Path '" + orig(raw) + "' does not exist");
        List<RJson.Loc> hits = d.select(p);
        if (p.legacy) {
            if (hits.isEmpty()) throw new RedisError("ERR Path '" + orig(raw) + "' does not exist");
            RJson v = hits.get(0).val;
            if (v.t != RJson.ARRAY) throw wrongPathType("array", v);
            c.addInt(indexOf(v, needle, start, stop));
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.ARRAY) c.addInt(indexOf(l.val, needle, start, stop));
            else c.addNil();
        }
    }

    private static long indexOf(RJson a, RJson needle, long start, long stop) {
        int n = a.arr.size();
        long s = start < 0 ? start + n : start;
        if (s < 0) s = 0;
        long e = stop == 0 ? n : (stop < 0 ? stop + n : stop);
        if (e > n) e = n;
        for (long i = s; i < e; i++) {
            if (i < 0 || i >= n) break;
            if (equalJson(a.arr.get((int) i), needle)) return i;
        }
        return -1;
    }

    static boolean equalJson(RJson a, RJson b) {
        if (a.isNumber() && b.isNumber()) return a.num() == b.num();
        if (a.t != b.t) return false;
        switch (a.t) {
            case RJson.NULL: return true;
            case RJson.BOOL: return a.b == b.b;
            case RJson.STRING: return java.util.Arrays.equals(a.s, b.s);
            case RJson.ARRAY: {
                if (a.arr.size() != b.arr.size()) return false;
                for (int i = 0; i < a.arr.size(); i++) if (!equalJson(a.arr.get(i), b.arr.get(i))) return false;
                return true;
            }
            default: {
                if (a.obj.size() != b.obj.size()) return false;
                for (Map.Entry<Key, RJson> e : a.obj.entrySet()) {
                    RJson o = b.obj.get(e.getKey());
                    if (o == null || !equalJson(e.getValue(), o)) return false;
                }
                return true;
            }
        }
    }

    private static void jsonArrInsert(Client c) {
        if (c.argc < 5) wrongArity("json.arrinsert");
        RJson.Path p = path(c.arg(2));
        long index = c.argLong(3);
        List<RJson> add = new ArrayList<>();
        for (int i = 4; i < c.argc; i++) add.add(parseValue(c.arg(i)));
        RJson d = docWrite(c, c.arg(1));
        if (d == null) throw new RedisError(E_NOKEY);
        List<RJson.Loc> hits = d.select(p);
        boolean changed = false;
        if (p.legacy) {
            RJson v = firstOfType(hits, RJson.ARRAY);
            if (v == null)
                throw new RedisError("ERR Path '" + p.text + "' does not exist or not an array");
            insertInto(v, index, add);
            touched(c, c.arg(1), "json.arrinsert");
            c.addInt(v.arr.size());
            return;
        }
        for (RJson.Loc l : hits) if (l.val.t == RJson.ARRAY) checkInsert(l.val, index);
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.ARRAY) {
                insertInto(l.val, index, add);
                changed = true;
                c.addInt(l.val.arr.size());
            } else {
                c.addNil();
            }
        }
        if (changed) touched(c, c.arg(1), "json.arrinsert");
    }

    private static void checkInsert(RJson a, long index) {
        long ix = index < 0 ? index + a.arr.size() : index;
        if (ix < 0 || ix > a.arr.size()) throw new RedisError("ERR index out of bounds");
    }

    private static void insertInto(RJson a, long index, List<RJson> add) {
        checkInsert(a, index);
        long ix = index < 0 ? index + a.arr.size() : index;
        int at = (int) ix;
        for (RJson v : add) a.arr.add(at++, v.deepCopy());
    }

    private static void jsonArrPop(Client c) {
        if (c.argc < 2 || c.argc > 4) wrongArity("json.arrpop");
        byte[] raw = c.argc > 2 ? c.arg(2) : Util.b(".");
        long index = c.argc > 3 ? c.argLong(3) : -1;
        RJson.Path p = path(raw);
        RJson d = docWrite(c, c.arg(1));
        if (d == null) throw new RedisError(E_NOKEY);
        List<RJson.Loc> hits = d.select(p);
        boolean changed = false;
        if (p.legacy) {
            RJson v = firstOfType(hits, RJson.ARRAY);
            if (v == null)
                throw new RedisError("ERR Path '" + p.text + "' does not exist or not an array");
            byte[] r = popFrom(v, index);
            if (r == null) { c.addNil(); return; }
            touched(c, c.arg(1), "json.arrpop");
            c.addBulk(r);
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.ARRAY) {
                byte[] r = popFrom(l.val, index);
                if (r == null) c.addNil();
                else { changed = true; c.addBulk(r); }
            } else {
                c.addNil();
            }
        }
        if (changed) touched(c, c.arg(1), "json.arrpop");
    }

    private static byte[] popFrom(RJson a, long index) {
        int n = a.arr.size();
        if (n == 0) return null;
        long ix = index < 0 ? index + n : index;
        if (ix < 0) ix = 0;
        if (ix >= n) ix = n - 1;
        return a.arr.remove((int) ix).toBytes();
    }

    private static void jsonArrTrim(Client c) {
        if (c.argc != 5) wrongArity("json.arrtrim");
        RJson.Path p = path(c.arg(2));
        long start = c.argLong(3);
        long stop = c.argLong(4);
        RJson d = docWrite(c, c.arg(1));
        if (d == null) throw new RedisError(E_NOKEY);
        List<RJson.Loc> hits = d.select(p);
        boolean changed = false;
        if (p.legacy) {
            RJson v = firstOfType(hits, RJson.ARRAY);
            if (v == null)
                throw new RedisError("ERR Path '" + p.text + "' does not exist or not an array");
            trim(v, start, stop);
            touched(c, c.arg(1), "json.arrtrim");
            c.addInt(v.arr.size());
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) {
            if (l.val.t == RJson.ARRAY) {
                trim(l.val, start, stop);
                changed = true;
                c.addInt(l.val.arr.size());
            } else {
                c.addNil();
            }
        }
        if (changed) touched(c, c.arg(1), "json.arrtrim");
    }

    private static void trim(RJson a, long start, long stop) {
        int n = a.arr.size();
        long s = start < 0 ? start + n : start;
        long e = stop < 0 ? stop + n : stop;
        if (s < 0) s = 0;
        if (e >= n) e = n - 1;
        if (s > e || s >= n) { a.arr.clear(); return; }
        List<RJson> keep = new ArrayList<>(a.arr.subList((int) s, (int) e + 1));
        a.arr.clear();
        a.arr.addAll(keep);
    }

    // ------------------------------------------------------------------
    // JSON.RESP / JSON.DEBUG
    // ------------------------------------------------------------------

    private static void jsonResp(Client c) {
        if (c.argc < 2 || c.argc > 3) wrongArity("json.resp");
        byte[] raw = c.argc > 2 ? c.arg(2) : Util.b(".");
        RJson.Path p = path(raw);
        RJson d = doc(c, c.arg(1));
        if (d == null) { c.addNil(); return; }
        List<RJson.Loc> hits = d.select(p);
        if (p.legacy) {
            if (hits.isEmpty()) throw new RedisError("ERR Path '" + p.text + "' does not exist");
            resp(c, hits.get(0).val);
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) resp(c, l.val);
    }

    private static void resp(Client c, RJson v) {
        switch (v.t) {
            case RJson.NULL: c.addNil(); return;
            case RJson.BOOL: c.addSimple(v.b ? "true" : "false"); return;
            case RJson.INT: c.addInt(v.i); return;
            case RJson.UINT:
                if (v.i >= 0) c.addInt(v.i);
                else c.addBulk(Long.toUnsignedString(v.i));
                return;
            case RJson.DOUBLE: c.addBulk(Util.d2string(v.d)); return;
            case RJson.STRING: c.addBulk(v.s); return;
            case RJson.ARRAY:
                c.addArray(v.arr.size() + 1);
                c.addSimple("[");
                for (RJson e : v.arr) resp(c, e);
                return;
            default:
                c.addArray(v.obj.size() * 2 + 1);
                c.addSimple("{");
                for (Map.Entry<Key, RJson> e : v.obj.entrySet()) {
                    c.addBulk(e.getKey().b);
                    resp(c, e.getValue());
                }
        }
    }

    private static void jsonDebug(Client c) {
        if (c.argc < 2) wrongArity("json.debug");
        String sub = c.argLower(1);
        if (sub.equals("help")) {
            c.addArray(2);
            c.addBulk("MEMORY <key> [path] - reports memory usage");
            c.addBulk("HELP                - this message");
            return;
        }
        if (!sub.equals("memory")) throw new RedisError("ERR unknown subcommand - try `JSON.DEBUG HELP`");
        if (c.argc < 3 || c.argc > 4) wrongArity("json.debug");
        byte[] raw = c.argc > 3 ? c.arg(3) : Util.b(".");
        RJson.Path p = path(raw);
        RJson d = doc(c, c.arg(2));
        if (d == null) {
            if (p.legacy) { c.addInt(0); return; }
            c.addEmptyArray();
            return;
        }
        List<RJson.Loc> hits = d.select(p);
        if (p.legacy) {
            if (hits.isEmpty()) throw new RedisError("ERR Path '" + p.text + "' does not exist");
            c.addInt(memory(hits.get(0).val));
            return;
        }
        c.addArray(hits.size());
        for (RJson.Loc l : hits) c.addInt(memory(l.val));
    }

    /** RedisJSON's heap accounting; recorded as a normalized field in the corpus. */
    private static long memory(RJson v) {
        switch (v.t) {
            case RJson.NULL: return 0;
            case RJson.BOOL: return 0;
            case RJson.INT: case RJson.UINT: case RJson.DOUBLE: return 8;
            case RJson.STRING: return 16 + v.s.length;
            case RJson.ARRAY: {
                long n = 24 + 8L * v.arr.size();
                for (RJson e : v.arr) n += memory(e);
                return n;
            }
            default: {
                long n = 24;
                for (Map.Entry<Key, RJson> e : v.obj.entrySet())
                    n += 40 + e.getKey().b.length + memory(e.getValue());
                return n;
            }
        }
    }

    // ------------------------------------------------------------------
    public static void register() {
        /* Modules register every command with arity -1 and check the count
         * themselves, which is why the wrong-arity error is raised inline. */
        CommandTable.reg("json.set", -1, "write denyoom module", 1, 1, 1, JsonCmds::jsonSet);
        CommandTable.reg("json.get", -1, "readonly module", 1, 1, 1, JsonCmds::jsonGet);
        CommandTable.reg("json.mget", -1, "readonly module", 1, 1, 1, JsonCmds::jsonMget);
        CommandTable.reg("json.mset", -1, "write denyoom module", 1, -1, 3, JsonCmds::jsonMset);
        CommandTable.reg("json.merge", -1, "write denyoom module", 1, 1, 1, JsonCmds::jsonMerge);
        CommandTable.reg("json.del", -1, "write module", 1, 1, 1, JsonCmds::jsonDel);
        CommandTable.reg("json.forget", -1, "write module", 1, 1, 1, JsonCmds::jsonDel);
        CommandTable.reg("json.type", -1, "readonly module", 1, 1, 1, JsonCmds::jsonType);
        CommandTable.reg("json.clear", -1, "write module", 1, 1, 1, JsonCmds::jsonClear);
        CommandTable.reg("json.toggle", -1, "write denyoom module", 1, 1, 1, JsonCmds::jsonToggle);
        CommandTable.reg("json.numincrby", -1, "write module", 1, 1, 1, c -> numOp(c, false));
        CommandTable.reg("json.nummultby", -1, "write module", 1, 1, 1, c -> numOp(c, true));
        CommandTable.reg("json.strlen", -1, "readonly module", 1, 1, 1, JsonCmds::jsonStrLen);
        CommandTable.reg("json.strappend", -1, "write denyoom module", 1, 1, 1, JsonCmds::jsonStrAppend);
        CommandTable.reg("json.objlen", -1, "readonly module", 1, 1, 1, JsonCmds::jsonObjLen);
        CommandTable.reg("json.objkeys", -1, "readonly module", 1, 1, 1, JsonCmds::jsonObjKeys);
        CommandTable.reg("json.arrlen", -1, "readonly module", 1, 1, 1, JsonCmds::jsonArrLen);
        CommandTable.reg("json.arrappend", -1, "write denyoom module", 1, 1, 1, JsonCmds::jsonArrAppend);
        CommandTable.reg("json.arrindex", -1, "readonly module", 1, 1, 1, JsonCmds::jsonArrIndex);
        CommandTable.reg("json.arrinsert", -1, "write denyoom module", 1, 1, 1, JsonCmds::jsonArrInsert);
        CommandTable.reg("json.arrpop", -1, "write module", 1, 1, 1, JsonCmds::jsonArrPop);
        CommandTable.reg("json.arrtrim", -1, "write module", 1, 1, 1, JsonCmds::jsonArrTrim);
        CommandTable.reg("json.resp", -1, "readonly module", 1, 1, 1, JsonCmds::jsonResp);
        CommandTable.reg("json.debug", -1, "readonly module", 2, 2, 1, JsonCmds::jsonDebug);
    }
}
