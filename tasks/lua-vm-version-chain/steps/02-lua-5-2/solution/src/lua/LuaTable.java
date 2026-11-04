package lua;

import java.util.ArrayList;
import java.util.HashMap;

/**
 * A Lua table: an array part for the dense integer keys 1..asize and a hash part,
 * kept in insertion order so that `next` has a stable successor relation even when
 * fields are set to nil during a traversal.
 */
public final class LuaTable {
    Object[] array = EMPTY;
    int asize;
    private HashMap<Object, Integer> hindex;
    private ArrayList<Object> hkeys;
    private ArrayList<Object> hvals;
    private int live;            // non-nil entries in the hash part
    public LuaTable metatable;

    private static final Object[] EMPTY = new Object[0];

    public LuaTable() { Gc.alloc(56); }

    public LuaTable(int narr, int nhash) {
        Gc.alloc(56 + 8 * narr + 24 * nhash);
        if (narr > 0) array = new Object[narr];
        if (nhash > 0) initHash(nhash);
    }

    private void initHash(int n) {
        hindex = new HashMap<>(Math.max(4, n * 2));
        hkeys = new ArrayList<>(Math.max(4, n));
        hvals = new ArrayList<>(Math.max(4, n));
    }

    /** Normalises a key so that a float with an exact integer value indexes the same slot. */
    static Object normKey(Object k) {
        if (k instanceof Double) {
            double d = (Double) k;
            long l = (long) d;
            if (l == d) {
                if (l >= Integer.MIN_VALUE && l <= Integer.MAX_VALUE) return (int) l;
                return (double) l;
            }
        }
        return k;
    }

    public Object rawget(Object key) {
        Object k = normKey(key);
        if (k instanceof Integer) {
            int i = (Integer) k;
            if (i >= 1 && i <= asize) return array[i - 1];
        }
        if (hindex == null) return null;
        Integer slot = hindex.get(k);
        return slot == null ? null : hvals.get(slot);
    }

    public Object rawgeti(int i) {
        if (i >= 1 && i <= asize) return array[i - 1];
        if (hindex == null) return null;
        Integer slot = hindex.get(i);
        return slot == null ? null : hvals.get(slot);
    }

    public Object rawget(LuaString s) {
        if (hindex == null) return null;
        Integer slot = hindex.get(s);
        return slot == null ? null : hvals.get(slot);
    }

    public Object rawget(String s) {
        return rawget(LuaString.of(s));
    }

    public void rawset(Object key, Object v) {
        Object k = normKey(key);
        if (k instanceof Integer) {
            int i = (Integer) k;
            if (i >= 1 && i <= asize) { array[i - 1] = v; return; }
            if (i == asize + 1 && v != null) { appendArray(v); return; }
        }
        hashSet(k, v);
    }

    public void rawseti(int i, Object v) {
        if (i >= 1 && i <= asize) { array[i - 1] = v; return; }
        if (i == asize + 1 && v != null) { appendArray(v); return; }
        hashSet(i, v);
    }

    public void rawset(String k, Object v) { hashSet(LuaString.of(k), v); }

    private void appendArray(Object v) {
        if (asize == array.length) {
            Object[] n = new Object[Math.max(4, array.length * 2)];
            System.arraycopy(array, 0, n, 0, asize);
            array = n;
        }
        array[asize++] = v;
        // migrate any following integer keys that now belong in the array part
        if (hindex != null && live > 0) {
            while (true) {
                Integer slot = hindex.get(asize + 1);
                if (slot == null) break;
                Object nv = hvals.get(slot);
                if (nv == null) break;
                hashSet(asize + 1, null);
                if (asize == array.length) {
                    Object[] n = new Object[Math.max(4, array.length * 2)];
                    System.arraycopy(array, 0, n, 0, asize);
                    array = n;
                }
                array[asize++] = nv;
            }
        }
    }

    private void hashSet(Object k, Object v) {
        if (hindex == null) {
            if (v == null) return;
            initHash(4);
        }
        Integer slot = hindex.get(k);
        if (slot != null) {
            Object old = hvals.get(slot);
            if (old == null && v != null) live++;
            else if (old != null && v == null) live--;
            hvals.set(slot, v);
            return;
        }
        if (v == null) return;
        if (hkeys.size() > 8 && live * 2 < hkeys.size()) compact();
        hindex.put(k, hkeys.size());
        hkeys.add(k);
        hvals.add(v);
        live++;
    }

    private void compact() {
        ArrayList<Object> nk = new ArrayList<>(live + 4);
        ArrayList<Object> nv = new ArrayList<>(live + 4);
        hindex.clear();
        for (int i = 0; i < hkeys.size(); i++) {
            Object v = hvals.get(i);
            if (v != null) {
                hindex.put(hkeys.get(i), nk.size());
                nk.add(hkeys.get(i));
                nv.add(v);
            }
        }
        hkeys = nk;
        hvals = nv;
    }

    /** Number of slots in the hash part (including tombstones); for the GC and `next`. */
    int hashSlots() { return hkeys == null ? 0 : hkeys.size(); }

    Object slotKey(int i) { return hkeys.get(i); }

    Object slotVal(int i) { return hvals.get(i); }

    void slotClear(int i) {
        if (hvals.get(i) != null) { hvals.set(i, null); live--; }
    }

    /** Internal integer keys become Lua numbers again on the way out. */
    static Object denorm(Object k) {
        return (k instanceof Integer) ? Double.valueOf((Integer) k) : k;
    }

    /** `next`: returns {key, value} or null at the end; throws for an invalid key. */
    public Object[] next(Object key) {
        int start;
        if (key == null) {
            start = 0;
        } else {
            Object k = normKey(key);
            if (k instanceof Integer) {
                int i = (Integer) k;
                if (i >= 1 && i <= asize) { start = i; }
                else start = -1;
            } else start = -1;
            if (start < 0) {
                Integer slot = hindex == null ? null : hindex.get(k);
                if (slot == null) throw new LuaError(LuaString.of("invalid key to 'next'"));
                start = asize + slot + 1;
            }
        }
        for (int i = start; i < asize; i++) {
            if (array[i] != null) return new Object[]{(double) (i + 1), array[i]};
        }
        int hs = start - asize;
        if (hs < 0) hs = 0;
        if (hkeys != null) {
            for (int i = hs; i < hkeys.size(); i++) {
                Object v = hvals.get(i);
                if (v != null) return new Object[]{denorm(hkeys.get(i)), v};
            }
        }
        return null;
    }

    public int length() {
        int j = asize;
        if (j > 0 && array[j - 1] == null) {
            int i = 0;
            while (j - i > 1) {
                int m = (i + j) >>> 1;
                if (array[m - 1] == null) j = m; else i = m;
            }
            return i;
        }
        if (hindex == null || live == 0) return j;
        int i = j;
        j = i + 1;
        while (rawgeti(j) != null) {
            i = j;
            if (j > Integer.MAX_VALUE / 2) {
                i = 1;
                while (rawgeti(i) != null) i++;
                return i - 1;
            }
            j *= 2;
        }
        while (j - i > 1) {
            int m = (i + j) >>> 1;
            if (rawgeti(m) == null) j = m; else i = m;
        }
        return i;
    }

    /** Inserts v at position pos, shifting up (table.insert). */
    public void insert(int pos, Object v) {
        int n = length();
        for (int i = n; i >= pos; i--) rawseti(i + 1, rawgeti(i));
        rawseti(pos, v);
    }

    public Object remove(int pos) {
        int n = length();
        Object v = rawgeti(pos);
        for (int i = pos; i < n; i++) rawseti(i, rawgeti(i + 1));
        if (pos <= n) rawseti(n, null);
        return v;
    }
}
