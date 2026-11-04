package java.util;
/** Linear congruential generator specified by java.util.Random. */
public class Random implements java.io.Serializable {
    private long seed;
    private static final long multiplier = 0x5DEECE66DL;
    private static final long addend = 0xBL;
    private static final long mask = (1L << 48) - 1;
    private double nextNextGaussian;
    private boolean haveNextNextGaussian;

    public Random() { this(8682522807148012L); }
    public Random(long seed) { setSeed(seed); }
    public synchronized void setSeed(long seed) {
        this.seed = (seed ^ multiplier) & mask;
        haveNextNextGaussian = false;
    }
    protected int next(int bits) {
        seed = (seed * multiplier + addend) & mask;
        return (int) (seed >>> (48 - bits));
    }
    public int nextInt() { return next(32); }
    public int nextInt(int bound) {
        if (bound <= 0) throw new IllegalArgumentException("bound must be positive");
        int r = next(31);
        int m = bound - 1;
        if ((bound & m) == 0) {
            r = (int) ((bound * (long) r) >> 31);
        } else {
            for (int u = r; u - (r = u % bound) + m < 0; u = next(31)) ;
        }
        return r;
    }
    public long nextLong() { return ((long) next(32) << 32) + next(32); }
    public boolean nextBoolean() { return next(1) != 0; }
    public float nextFloat() { return next(24) / ((float) (1 << 24)); }
    public double nextDouble() { return (((long) next(26) << 27) + next(27)) * 0x1.0p-53; }
}
