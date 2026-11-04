package java.lang;
public final class Math {
    private Math() {}
    public static final double E = 2.718281828459045;
    public static final double PI = 3.141592653589793;
    public static int abs(int a) { return a < 0 ? -a : a; }
    public static long abs(long a) { return a < 0 ? -a : a; }
    public static float abs(float a) { return a <= 0.0f ? 0.0f - a : a; }
    public static double abs(double a) { return a <= 0.0 ? 0.0 - a : a; }
    public static int max(int a, int b) { return a >= b ? a : b; }
    public static long max(long a, long b) { return a >= b ? a : b; }
    public static float max(float a, float b) { return a >= b ? a : b; }
    public static double max(double a, double b) { return a >= b ? a : b; }
    public static int min(int a, int b) { return a <= b ? a : b; }
    public static long min(long a, long b) { return a <= b ? a : b; }
    public static float min(float a, float b) { return a <= b ? a : b; }
    public static double min(double a, double b) { return a <= b ? a : b; }
    public static native double sqrt(double a);
    public static native double pow(double a, double b);
    public static native double sin(double a);
    public static native double cos(double a);
    public static native double tan(double a);
    public static native double log(double a);
    public static native double exp(double a);
    public static native double floor(double a);
    public static native double ceil(double a);
    public static native double atan2(double y, double x);
    public static long round(double a) { return (long) floor(a + 0.5d); }
    public static int round(float a) { return (int) floor(a + 0.5f); }
    public static double random() { return 0.5d; }
    public static int floorDiv(int x, int y) { int r = x / y; if ((x ^ y) < 0 && r * y != x) r--; return r; }
    public static int floorMod(int x, int y) { return x - floorDiv(x, y) * y; }
    public static long floorDiv(long x, long y) { long r = x / y; if ((x ^ y) < 0 && r * y != x) r--; return r; }
    public static long floorMod(long x, long y) { return x - floorDiv(x, y) * y; }
    public static int addExact(int x, int y) {
        int r = x + y;
        if (((x ^ r) & (y ^ r)) < 0) throw new ArithmeticException("integer overflow");
        return r;
    }
    public static int multiplyExact(int x, int y) {
        long r = (long) x * (long) y;
        if ((int) r != r) throw new ArithmeticException("integer overflow");
        return (int) r;
    }
    public static int subtractExact(int x, int y) {
        int r = x - y;
        if (((x ^ y) & (x ^ r)) < 0) throw new ArithmeticException("integer overflow");
        return r;
    }
    public static int toIntExact(long v) {
        if ((int) v != v) throw new ArithmeticException("integer overflow");
        return (int) v;
    }
    public static double signum(double d) { return d == 0.0 || d != d ? d : (d > 0 ? 1.0 : -1.0); }
    public static double hypot(double x, double y) { return sqrt(x * x + y * y); }
}
