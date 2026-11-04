package redis;

import java.util.*;

/** Sorted set: member -> score dict plus a (score, member) ordered index. */
public final class RZSet {
    public static final class Entry implements Comparable<Entry> {
        public final double score;
        public final Key member;
        public Entry(double s, Key m) { score = s; member = m; }
        @Override public int compareTo(Entry o) {
            int c = Double.compare(score, o.score);
            if (c != 0) return c;
            return member.compareTo(o.member);
        }
        @Override public boolean equals(Object o) {
            return o instanceof Entry && compareTo((Entry) o) == 0;
        }
        @Override public int hashCode() { return member.hashCode() * 31 + Double.hashCode(score); }
    }

    public final HashMap<Key, Double> dict = new HashMap<>();
    public final TreeSet<Entry> sorted = new TreeSet<>();

    public int size() { return dict.size(); }

    public Double score(byte[] m) { return dict.get(new Key(m)); }

    /** returns true when the member was newly added */
    public boolean add(byte[] m, double score) {
        Key k = new Key(m);
        Double old = dict.put(k, score);
        if (old != null) {
            sorted.remove(new Entry(old, k));
            sorted.add(new Entry(score, k));
            return false;
        }
        sorted.add(new Entry(score, k));
        return true;
    }

    public boolean remove(byte[] m) {
        Key k = new Key(m);
        Double old = dict.remove(k);
        if (old == null) return false;
        sorted.remove(new Entry(old, k));
        return true;
    }

    public int rank(byte[] m, boolean rev) {
        Key k = new Key(m);
        Double sc = dict.get(k);
        if (sc == null) return -1;
        Entry e = new Entry(sc, k);
        int r = sorted.headSet(e).size();
        return rev ? dict.size() - 1 - r : r;
    }

    public List<Entry> range(int start, int stop, boolean rev) {
        int n = sorted.size();
        if (start < 0) start += n;
        if (stop < 0) stop += n;
        if (start < 0) start = 0;
        if (start > stop || start >= n) return Collections.emptyList();
        if (stop >= n) stop = n - 1;
        List<Entry> all = rev ? new ArrayList<>(sorted.descendingSet()) : new ArrayList<>(sorted);
        return new ArrayList<>(all.subList(start, stop + 1));
    }

    public List<Entry> all(boolean rev) {
        return rev ? new ArrayList<>(sorted.descendingSet()) : new ArrayList<>(sorted);
    }
}
