package redis;

/**
 * CRC-64/Jones as used by redis (src/crc64.c + deps/crcspeed.c).
 *
 * Width = 64, Poly = 0xad93d23594c935a9, reflected in and out, init 0, xorout 0.
 * The reflected form of the polynomial is 0x95ac9329ac4bc9b5; the tables below
 * are the "slicing by 8" tables crcspeed64little_init() builds at runtime.
 *
 * crc64(0, "123456789", 9) == 0xe9c6d914c4b8d9caL
 */
public final class Crc64 {
    private Crc64() {}

    /** Reflected polynomial: the bit-reverse of 0xad93d23594c935a9. */
    private static final long POLY_REFLECTED = 0x95ac9329ac4bc9b5L;

    private static final long[][] TABLE = new long[8][256];

    static {
        for (int n = 0; n < 256; n++) {
            long crc = n;
            for (int k = 0; k < 8; k++) {
                if ((crc & 1) != 0) crc = (crc >>> 1) ^ POLY_REFLECTED;
                else crc >>>= 1;
            }
            TABLE[0][n] = crc;
        }
        for (int n = 0; n < 256; n++) {
            long crc = TABLE[0][n];
            for (int k = 1; k < 8; k++) {
                crc = TABLE[0][(int) (crc & 0xff)] ^ (crc >>> 8);
                TABLE[k][n] = crc;
            }
        }
    }

    /** Incremental CRC64 over b[off..off+len). Pass 0 as the initial crc. */
    public static long crc64(long crc, byte[] b, int off, int len) {
        int i = off;
        int end = off + len;
        while (end - i >= 8) {
            crc ^= ((long) (b[i] & 0xff))
                 | ((long) (b[i + 1] & 0xff) << 8)
                 | ((long) (b[i + 2] & 0xff) << 16)
                 | ((long) (b[i + 3] & 0xff) << 24)
                 | ((long) (b[i + 4] & 0xff) << 32)
                 | ((long) (b[i + 5] & 0xff) << 40)
                 | ((long) (b[i + 6] & 0xff) << 48)
                 | ((long) (b[i + 7] & 0xff) << 56);
            crc = TABLE[7][(int) (crc & 0xff)]
                ^ TABLE[6][(int) ((crc >>> 8) & 0xff)]
                ^ TABLE[5][(int) ((crc >>> 16) & 0xff)]
                ^ TABLE[4][(int) ((crc >>> 24) & 0xff)]
                ^ TABLE[3][(int) ((crc >>> 32) & 0xff)]
                ^ TABLE[2][(int) ((crc >>> 40) & 0xff)]
                ^ TABLE[1][(int) ((crc >>> 48) & 0xff)]
                ^ TABLE[0][(int) (crc >>> 56)];
            i += 8;
        }
        while (i < end) {
            crc = TABLE[0][(int) ((crc ^ b[i]) & 0xff)] ^ (crc >>> 8);
            i++;
        }
        return crc;
    }

    public static long crc64(long crc, byte[] b) { return crc64(crc, b, 0, b.length); }
}
