package java.util;
public final class Arrays {
    static boolean objEquals(Object a, Object b) { return (a == b) || (a != null && a.equals(b)); }
    static int objHash(Object o) { return o != null ? o.hashCode() : 0; }

    private Arrays() {}
    public static int[] copyOf(int[] a, int n) { int[] r = new int[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static long[] copyOf(long[] a, int n) { long[] r = new long[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static short[] copyOf(short[] a, int n) { short[] r = new short[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static byte[] copyOf(byte[] a, int n) { byte[] r = new byte[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static char[] copyOf(char[] a, int n) { char[] r = new char[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static boolean[] copyOf(boolean[] a, int n) { boolean[] r = new boolean[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static float[] copyOf(float[] a, int n) { float[] r = new float[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static double[] copyOf(double[] a, int n) { double[] r = new double[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static <T> T[] copyOf(T[] a, int n) {
        T[] r = (T[]) java.lang.reflect.Array.newInstance(a.getClass().getComponentType(), n);
        System.arraycopy(a, 0, r, 0, Math.min(n, a.length));
        return r;
    }
    public static int[] copyOfRange(int[] a, int from, int to) { int[] r = new int[to - from]; System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from)); return r; }
    public static byte[] copyOfRange(byte[] a, int from, int to) { byte[] r = new byte[to - from]; System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from)); return r; }
    public static char[] copyOfRange(char[] a, int from, int to) { char[] r = new char[to - from]; System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from)); return r; }
    public static <T> T[] copyOfRange(T[] a, int from, int to) {
        T[] r = (T[]) java.lang.reflect.Array.newInstance(a.getClass().getComponentType(), to - from);
        System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from));
        return r;
    }
    public static void fill(int[] a, int v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(long[] a, long v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(byte[] a, byte v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(char[] a, char v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(boolean[] a, boolean v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(double[] a, double v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(Object[] a, Object v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static boolean equals(int[] a, int[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static boolean equals(byte[] a, byte[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static boolean equals(char[] a, char[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static boolean equals(Object[] a, Object[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (!objEquals(a[i], b[i])) return false;
        return true;
    }
    public static int hashCode(Object[] a) {
        if (a == null) return 0;
        int r = 1;
        for (int i = 0; i < a.length; i++) r = 31 * r + objHash(a[i]);
        return r;
    }
    public static int hashCode(int[] a) {
        if (a == null) return 0;
        int r = 1;
        for (int i = 0; i < a.length; i++) r = 31 * r + a[i];
        return r;
    }
    public static String toString(int[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(long[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(char[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(byte[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(double[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(boolean[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(Object[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(String.valueOf(a[i])); }
        return sb.append(']').toString();
    }
    public static void sort(int[] a) { sort(a, 0, a.length); }
    public static void sort(int[] a, int from, int to) {
        for (int i = from + 1; i < to; i++) {
            int v = a[i], j = i - 1;
            while (j >= from && a[j] > v) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    public static void sort(long[] a) {
        for (int i = 1; i < a.length; i++) {
            long v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    public static void sort(char[] a) {
        for (int i = 1; i < a.length; i++) {
            char v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    public static void sort(Object[] a) {
        for (int i = 1; i < a.length; i++) {
            Object v = a[i];
            int j = i - 1;
            while (j >= 0 && ((Comparable<Object>) a[j]).compareTo(v) > 0) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    public static <T> void sort(T[] a, Comparator<? super T> c) {
        for (int i = 1; i < a.length; i++) {
            T v = a[i];
            int j = i - 1;
            while (j >= 0 && c.compare(a[j], v) > 0) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
    }
    public static int binarySearch(int[] a, int key) {
        int lo = 0, hi = a.length - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >>> 1;
            if (a[mid] < key) lo = mid + 1;
            else if (a[mid] > key) hi = mid - 1;
            else return mid;
        }
        return -(lo + 1);
    }
    public static <T> List<T> asList(T... a) {
        ArrayList<T> l = new ArrayList<T>();
        for (int i = 0; i < a.length; i++) l.add(a[i]);
        return l;
    }
}
