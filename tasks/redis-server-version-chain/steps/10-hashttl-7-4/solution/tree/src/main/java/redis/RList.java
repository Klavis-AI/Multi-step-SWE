package redis;

import java.util.*;

/** List value: an ordered sequence of binary strings. */
public final class RList {
    public final ArrayDeque<byte[]> l = new ArrayDeque<>();
    public int size() { return l.size(); }
    public List<byte[]> asList() { return new ArrayList<>(l); }
    public byte[] get(int i) {
        int n = l.size();
        if (i < 0) i += n;
        if (i < 0 || i >= n) return null;
        Iterator<byte[]> it = l.iterator();
        for (int k = 0; k < i; k++) it.next();
        return it.next();
    }
    public void set(int i, byte[] v) {
        List<byte[]> tmp = asList();
        int n = tmp.size();
        if (i < 0) i += n;
        tmp.set(i, v);
        l.clear();
        l.addAll(tmp);
    }
}
