package redis;

import java.util.ArrayList;
import java.util.List;

/**
 * Bitmap commands: a port of redis 8.0.6 src/bitops.c.
 *
 * SETBIT/GETBIT/BITCOUNT/BITPOS/BITOP/BITFIELD/BITFIELD_RO.
 */
public final class BitCmds {
    private BitCmds() {}

    private static final int BITOP_AND = 0, BITOP_OR = 1, BITOP_XOR = 2, BITOP_NOT = 3;

    private static final int BITFIELDOP_GET = 0, BITFIELDOP_SET = 1, BITFIELDOP_INCRBY = 2;

    private static final int BFOVERFLOW_WRAP = 0, BFOVERFLOW_SAT = 1, BFOVERFLOW_FAIL = 2;

    private static final int BITFIELD_FLAG_NONE = 0, BITFIELD_FLAG_READONLY = 1;

    private static final String OFFSET_ERR = "ERR bit offset is not an integer or out of range";
    private static final String TYPE_ERR = "ERR Invalid bitfield type. Use something like i16 u8. "
            + "Note that u64 is not supported but i64 is.";

    public static void register() {
        CommandTable.reg("setbit", 4, "write denyoom", 1, 1, 1, BitCmds::setbit);
        CommandTable.reg("getbit", 3, "readonly fast", 1, 1, 1, BitCmds::getbit);
        CommandTable.reg("bitcount", -2, "readonly", 1, 1, 1, BitCmds::bitcount);
        CommandTable.reg("bitpos", -3, "readonly", 1, 1, 1, BitCmds::bitpos);
        CommandTable.reg("bitop", -4, "write denyoom", 2, -1, 1, BitCmds::bitop);
        CommandTable.reg("bitfield", -2, "write denyoom", 1, 1, 1, BitCmds::bitfield);
        CommandTable.reg("bitfield_ro", -2, "readonly fast", 1, 1, 1, BitCmds::bitfieldRo);
    }

    // ------------------------------------------------------------------
    // low level bit helpers
    // ------------------------------------------------------------------

    /** redisPopcount(). */
    static long popcount(byte[] p, int off, int count) {
        long bits = 0;
        int i = off;
        int end = off + count;
        while (i + 8 <= end) {
            long w = ((long) (p[i] & 0xff) << 56) | ((long) (p[i + 1] & 0xff) << 48)
                   | ((long) (p[i + 2] & 0xff) << 40) | ((long) (p[i + 3] & 0xff) << 32)
                   | ((long) (p[i + 4] & 0xff) << 24) | ((long) (p[i + 5] & 0xff) << 16)
                   | ((long) (p[i + 6] & 0xff) << 8) | ((long) (p[i + 7] & 0xff));
            bits += Long.bitCount(w);
            i += 8;
        }
        while (i < end) { bits += Integer.bitCount(p[i] & 0xff); i++; }
        return bits;
    }

    /**
     * redisBitpos(): position of the first bit equal to 'bit' inside p[off..off+count).
     * Returns -1 when looking for a set bit and the range holds none; when looking for
     * a clear bit the range is considered zero padded on the right, so count*8 is
     * returned if every bit is set.
     */
    static long redisBitpos(byte[] p, int off, int count, int bit) {
        int skipval = (bit != 0) ? 0x00 : 0xff;
        for (int k = 0; k < count; k++) {
            int v = p[off + k] & 0xff;
            if (v != skipval) {
                for (int j = 7; j >= 0; j--) {
                    if (((v >> j) & 1) == bit) return (long) k * 8 + (7 - j);
                }
            }
        }
        return bit != 0 ? -1 : (long) count * 8;
    }

    /** redisBitpos() over a single byte value. */
    private static long bitposByte(int v, int bit) {
        v &= 0xff;
        for (int j = 7; j >= 0; j--) {
            if (((v >> j) & 1) == bit) return 7 - j;
        }
        return bit != 0 ? -1 : 8;
    }

    /** getBitOffsetFromArgument(). */
    static long getBitOffsetFromArgument(Client c, byte[] arg, boolean hash, int bits) {
        boolean usehash = arg.length > 0 && arg[0] == '#' && hash && bits > 0;
        Long v = usehash ? Util.string2ll(arg, 1, arg.length - 1) : Util.string2ll(arg);
        if (v == null) throw new RedisError(OFFSET_ERR);
        long loffset = v;
        if (usehash) loffset *= bits;
        if (loffset < 0
                || (!StringCmds.mustObey(c) && (loffset >> 3) >= Server.I.protoMaxBulkLen()))
            throw new RedisError(OFFSET_ERR);
        return loffset;
    }

    /** getBitfieldTypeFromArgument(): returns {sign, bits}. */
    static int[] getBitfieldType(byte[] arg) {
        if (arg.length == 0) throw new RedisError(TYPE_ERR);
        int sign;
        if (arg[0] == 'i') sign = 1;
        else if (arg[0] == 'u') sign = 0;
        else throw new RedisError(TYPE_ERR);
        Long v = Util.string2ll(arg, 1, arg.length - 1);
        if (v == null || v < 1 || (sign == 1 && v > 64) || (sign == 0 && v > 63))
            throw new RedisError(TYPE_ERR);
        return new int[]{sign, (int) (long) v};
    }

    /**
     * lookupStringForBitCommand(): create or zero-pad the value so that 'maxbit' is
     * addressable. sizes[0] receives the previous length, sizes[1] the growth.
     */
    static RObj lookupStringForBitCommand(Client c, long maxbit, long[] sizes) {
        Server s = Server.I;
        long byteIdx = maxbit >> 3;
        byte[] key = c.arg(1);
        RObj o = s.lookupKeyWrite(c, key);
        if (o != null && o.type != RObj.STRING) throw RedisError.wrongType();
        if (o == null) {
            o = new RObj(RObj.STRING, RObj.ENC_RAW, new byte[(int) (byteIdx + 1)]);
            s.dbAdd(c.db, key, o);
            sizes[0] = 0;
            sizes[1] = byteIdx + 1;
        } else {
            byte[] v = StringCmds.unshareRaw(o);
            sizes[0] = v.length;
            if (v.length < byteIdx + 1) {
                byte[] nv = new byte[(int) (byteIdx + 1)];
                System.arraycopy(v, 0, nv, 0, v.length);
                o.ptr = nv;
                sizes[1] = (byteIdx + 1) - v.length;
            } else {
                sizes[1] = 0;
            }
        }
        return o;
    }

    // ------------------------------------------------------------------
    // SETBIT / GETBIT
    // ------------------------------------------------------------------
    static void setbit(Client c) {
        Server s = Server.I;
        final String err = "ERR bit is not an integer or out of range";
        long bitoffset = getBitOffsetFromArgument(c, c.arg(2), false, 0);

        Long onL = Util.string2ll(c.arg(3));
        if (onL == null) throw new RedisError(err);
        long on = onL;
        if ((on & ~1L) != 0) throw new RedisError(err);

        long[] sizes = new long[2];
        RObj o = lookupStringForBitCommand(c, bitoffset, sizes);

        byte[] buf = (byte[]) o.ptr;
        int byteIdx = (int) (bitoffset >> 3);
        int byteval = buf[byteIdx] & 0xff;
        int bit = 7 - (int) (bitoffset & 7);
        int bitval = byteval & (1 << bit);

        if (sizes[1] != 0 || ((bitval != 0 ? 1 : 0) != on)) {
            byteval &= ~(1 << bit);
            byteval |= ((int) (on & 1)) << bit;
            buf[byteIdx] = (byte) byteval;
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            s.notifyKeyspaceEvent('$', "setbit", c.arg(1), c.db);
            s.dirty++;
        }
        c.addInt(bitval != 0 ? 1 : 0);
    }

    static void getbit(Client c) {
        long bitoffset = getBitOffsetFromArgument(c, c.arg(2), false, 0);
        RObj o = Server.I.lookupKeyRead(c, c.arg(1));
        if (o == null) { c.addInt(0); return; }
        if (o.type != RObj.STRING) throw RedisError.wrongType();

        byte[] p = o.str();
        long byteIdx = bitoffset >> 3;
        int bit = 7 - (int) (bitoffset & 7);
        int bitval = 0;
        if (byteIdx < p.length) bitval = p[(int) byteIdx] & (1 << bit);
        c.addInt(bitval != 0 ? 1 : 0);
    }

    // ------------------------------------------------------------------
    // BITOP
    // ------------------------------------------------------------------
    static void bitop(Client c) {
        Server s = Server.I;
        int op;
        if (c.argIs(1, "and")) op = BITOP_AND;
        else if (c.argIs(1, "or")) op = BITOP_OR;
        else if (c.argIs(1, "xor")) op = BITOP_XOR;
        else if (c.argIs(1, "not")) op = BITOP_NOT;
        else throw RedisError.syntax();

        if (op == BITOP_NOT && c.argc != 4)
            throw new RedisError("ERR BITOP NOT must be called with a single source key.");

        byte[] targetkey = c.arg(2);
        int numkeys = c.argc - 3;
        byte[][] src = new byte[numkeys][];
        int[] len = new int[numkeys];
        int maxlen = 0;

        for (int j = 0; j < numkeys; j++) {
            RObj o = s.lookupKeyRead(c, c.arg(j + 3));
            if (o == null) { src[j] = null; len[j] = 0; continue; }
            if (o.type != RObj.STRING) throw RedisError.wrongType();
            src[j] = o.str();
            len[j] = src[j].length;
            if (len[j] > maxlen) maxlen = len[j];
        }

        byte[] res = null;
        if (maxlen > 0) {
            res = new byte[maxlen];
            for (int j = 0; j < maxlen; j++) {
                int output = (len[0] <= j) ? 0 : (src[0][j] & 0xff);
                if (op == BITOP_NOT) output = (~output) & 0xff;
                for (int i = 1; i < numkeys; i++) {
                    int b = (len[i] <= j) ? 0 : (src[i][j] & 0xff);
                    if (op == BITOP_AND) {
                        output &= b;
                        if (output == 0) break;
                    } else if (op == BITOP_OR) {
                        output |= b;
                        if (output == 0xff) break;
                    } else if (op == BITOP_XOR) {
                        output ^= b;
                    }
                }
                res[j] = (byte) output;
            }
        }

        if (maxlen > 0) {
            StringCmds.storeValue(c, targetkey, new RObj(RObj.STRING, RObj.ENC_RAW, res), false);
            s.notifyKeyspaceEvent('$', "set", targetkey, c.db);
            s.dirty++;
        } else {
            Key tk = new Key(targetkey);
            if (s.dbs[c.db].delete(tk)) {
                s.signalModifiedKey(c.db, tk);
                s.notifyKeyspaceEvent('g', "del", targetkey, c.db);
                s.dirty++;
            }
        }
        c.addInt(maxlen);
    }

    // ------------------------------------------------------------------
    // BITCOUNT
    // ------------------------------------------------------------------
    static void bitcount(Client c) {
        Server s = Server.I;
        long start, end;
        boolean isbit = false;
        int firstByteNegMask = 0, lastByteNegMask = 0;
        RObj o;
        byte[] p;
        long strlen;

        if (c.argc == 4 || c.argc == 5) {
            Long sv = Util.string2ll(c.arg(2));
            if (sv == null) throw RedisError.notInt();
            start = sv;
            Long ev = Util.string2ll(c.arg(3));
            if (ev == null) throw RedisError.notInt();
            end = ev;
            if (c.argc == 5) {
                if (c.argIs(4, "bit")) isbit = true;
                else if (c.argIs(4, "byte")) isbit = false;
                else throw RedisError.syntax();
            }
            o = s.lookupKeyRead(c, c.arg(1));
            if (o != null && o.type != RObj.STRING) throw RedisError.wrongType();
            p = o != null ? o.str() : null;
            strlen = p != null ? p.length : 0;
            long totlen = strlen;

            if (start < 0 && end < 0 && start > end) { c.addInt(0); return; }
            if (isbit) totlen <<= 3;
            if (start < 0) start = totlen + start;
            if (end < 0) end = totlen + end;
            if (start < 0) start = 0;
            if (end < 0) end = 0;
            if (end >= totlen) end = totlen - 1;
            if (isbit && start <= end) {
                firstByteNegMask = ~((1 << (8 - (int) (start & 7))) - 1) & 0xFF;
                lastByteNegMask = (1 << (7 - (int) (end & 7))) - 1;
                start >>= 3;
                end >>= 3;
            }
        } else if (c.argc == 2) {
            o = s.lookupKeyRead(c, c.arg(1));
            if (o != null && o.type != RObj.STRING) throw RedisError.wrongType();
            p = o != null ? o.str() : null;
            strlen = p != null ? p.length : 0;
            start = 0;
            end = strlen - 1;
        } else {
            throw RedisError.syntax();
        }

        if (o == null) { c.addInt(0); return; }

        if (start > end) {
            c.addInt(0);
        } else {
            int bytes = (int) (end - start + 1);
            long count = popcount(p, (int) start, bytes);
            if (firstByteNegMask != 0 || lastByteNegMask != 0) {
                int f = firstByteNegMask != 0 ? (p[(int) start] & firstByteNegMask) : 0;
                int l = lastByteNegMask != 0 ? (p[(int) end] & lastByteNegMask) : 0;
                count -= Integer.bitCount(f & 0xff) + Integer.bitCount(l & 0xff);
            }
            c.addInt(count);
        }
    }

    // ------------------------------------------------------------------
    // BITPOS
    // ------------------------------------------------------------------
    static void bitpos(Client c) {
        Server s = Server.I;
        Long bl = Util.string2ll(c.arg(2));
        if (bl == null) throw RedisError.notInt();
        long bit = bl;
        if (bit != 0 && bit != 1) throw new RedisError("ERR The bit argument must be 1 or 0.");
        int ibit = (int) bit;

        long start, end = 0;
        boolean isbit = false, endGiven = false;
        int firstByteNegMask = 0, lastByteNegMask = 0;
        RObj o;
        byte[] p;
        long strlen;

        if (c.argc == 4 || c.argc == 5 || c.argc == 6) {
            Long sv = Util.string2ll(c.arg(3));
            if (sv == null) throw RedisError.notInt();
            start = sv;
            if (c.argc == 6) {
                if (c.argIs(5, "bit")) isbit = true;
                else if (c.argIs(5, "byte")) isbit = false;
                else throw RedisError.syntax();
            }
            if (c.argc >= 5) {
                Long ev = Util.string2ll(c.arg(4));
                if (ev == null) throw RedisError.notInt();
                end = ev;
                endGiven = true;
            }
            o = s.lookupKeyRead(c, c.arg(1));
            if (o != null && o.type != RObj.STRING) throw RedisError.wrongType();
            p = o != null ? o.str() : null;
            strlen = p != null ? p.length : 0;
            long totlen = strlen;

            if (c.argc < 5) {
                if (isbit) end = (totlen << 3) + 7;
                else end = totlen - 1;
            }

            if (isbit) totlen <<= 3;
            if (start < 0) start = totlen + start;
            if (end < 0) end = totlen + end;
            if (start < 0) start = 0;
            if (end < 0) end = 0;
            if (end >= totlen) end = totlen - 1;
            if (isbit && start <= end) {
                firstByteNegMask = ~((1 << (8 - (int) (start & 7))) - 1) & 0xFF;
                lastByteNegMask = (1 << (7 - (int) (end & 7))) - 1;
                start >>= 3;
                end >>= 3;
            }
        } else if (c.argc == 3) {
            o = s.lookupKeyRead(c, c.arg(1));
            if (o != null && o.type != RObj.STRING) throw RedisError.wrongType();
            p = o != null ? o.str() : null;
            strlen = p != null ? p.length : 0;
            start = 0;
            end = strlen - 1;
        } else {
            throw RedisError.syntax();
        }

        /* A missing key is an infinite array of zero bits. */
        if (o == null) { c.addInt(ibit != 0 ? -1 : 0); return; }

        if (start > end) { c.addInt(-1); return; }

        long bytes = end - start + 1;
        long pos = 0;
        int tmpchar;

        scan:
        {
            if (firstByteNegMask != 0) {
                int v = p[(int) start] & 0xff;
                if (ibit != 0) tmpchar = v & ~firstByteNegMask;
                else tmpchar = v | firstByteNegMask;
                /* Special case, there is only one byte. */
                if (lastByteNegMask != 0 && bytes == 1) {
                    if (ibit != 0) tmpchar = tmpchar & ~lastByteNegMask;
                    else tmpchar = tmpchar | lastByteNegMask;
                }
                pos = bitposByte(tmpchar, ibit);
                if (bytes == 1 || (pos != -1 && pos != 8)) break scan;
                start++;
                bytes--;
            }
            long curbytes = bytes - (lastByteNegMask != 0 ? 1 : 0);
            if (curbytes > 0) {
                pos = redisBitpos(p, (int) start, (int) curbytes, ibit);
                if (bytes == curbytes || (pos != -1 && pos != (curbytes << 3))) break scan;
                start += curbytes;
                bytes -= curbytes;
            }
            int v = p[(int) end] & 0xff;
            if (ibit != 0) tmpchar = v & ~lastByteNegMask;
            else tmpchar = v | lastByteNegMask;
            pos = bitposByte(tmpchar, ibit);
        }

        /* With an explicit end the right of the range is not zero padded, so a
         * "first bit past the range" answer means "not found". */
        if (endGiven && ibit == 0 && pos == (bytes << 3)) { c.addInt(-1); return; }
        if (pos != -1) pos += start << 3;
        c.addInt(pos);
    }

    // ------------------------------------------------------------------
    // BITFIELD
    // ------------------------------------------------------------------
    static void setUnsignedBitfield(byte[] p, long offset, int bits, long value) {
        for (int j = 0; j < bits; j++) {
            int bitval = ((value & (1L << (bits - 1 - j))) != 0) ? 1 : 0;
            int byteIdx = (int) (offset >> 3);
            int bit = 7 - (int) (offset & 7);
            int byteval = p[byteIdx] & 0xff;
            byteval &= ~(1 << bit);
            byteval |= bitval << bit;
            p[byteIdx] = (byte) (byteval & 0xff);
            offset++;
        }
    }

    static long getUnsignedBitfield(byte[] p, long offset, int bits) {
        long value = 0;
        for (int j = 0; j < bits; j++) {
            int byteIdx = (int) (offset >> 3);
            int bit = 7 - (int) (offset & 7);
            int bitval = ((p[byteIdx] & 0xff) >> bit) & 1;
            value = (value << 1) | bitval;
            offset++;
        }
        return value;
    }

    static long getSignedBitfield(byte[] p, long offset, int bits) {
        long value = getUnsignedBitfield(p, offset, bits);
        if (bits < 64 && (value & (1L << (bits - 1))) != 0) value |= (-1L << bits);
        return value;
    }

    private static long wrapUnsigned(long value, long incr, int bits) {
        long mask = (bits >= 64) ? 0 : (-1L << bits);
        long res = value + incr;
        res &= ~mask;
        return res;
    }

    private static long wrapSigned(long value, long incr, int bits) {
        long msb = 1L << (bits - 1);
        long cc = value + incr;
        if (bits < 64) {
            long mask = -1L << bits;
            if ((cc & msb) != 0) cc |= mask;
            else cc &= ~mask;
        }
        return cc;
    }

    /** checkUnsignedBitfieldOverflow(): returns {0|1|-1, limit}. */
    static long[] checkUnsignedBitfieldOverflow(long value, long incr, int bits, int owtype) {
        long max = (bits == 64) ? -1L : ((1L << bits) - 1);
        long maxincr = max - value;
        long minincr = -value;

        if (Long.compareUnsigned(value, max) > 0 || (incr > 0 && incr > maxincr)) {
            if (owtype == BFOVERFLOW_WRAP) return new long[]{1, wrapUnsigned(value, incr, bits)};
            if (owtype == BFOVERFLOW_SAT) return new long[]{1, max};
            return new long[]{1, 0};
        } else if (incr < 0 && incr < minincr) {
            if (owtype == BFOVERFLOW_WRAP) return new long[]{-1, wrapUnsigned(value, incr, bits)};
            if (owtype == BFOVERFLOW_SAT) return new long[]{-1, 0};
            return new long[]{-1, 0};
        }
        return new long[]{0, 0};
    }

    /** checkSignedBitfieldOverflow(): returns {0|1|-1, limit}. */
    static long[] checkSignedBitfieldOverflow(long value, long incr, int bits, int owtype) {
        long max = (bits == 64) ? Long.MAX_VALUE : ((1L << (bits - 1)) - 1);
        long min = (-max) - 1;
        long maxincr = max - value;
        long minincr = min - value;

        if (value > max || (bits != 64 && incr > maxincr) || (value >= 0 && incr > 0 && incr > maxincr)) {
            if (owtype == BFOVERFLOW_WRAP) return new long[]{1, wrapSigned(value, incr, bits)};
            if (owtype == BFOVERFLOW_SAT) return new long[]{1, max};
            return new long[]{1, 0};
        } else if (value < min || (bits != 64 && incr < minincr) || (value < 0 && incr < 0 && incr < minincr)) {
            if (owtype == BFOVERFLOW_WRAP) return new long[]{-1, wrapSigned(value, incr, bits)};
            if (owtype == BFOVERFLOW_SAT) return new long[]{-1, min};
            return new long[]{-1, 0};
        }
        return new long[]{0, 0};
    }

    private static final class BfOp {
        long offset;
        long i64;
        int opcode;
        int owtype;
        int bits;
        int sign;
    }

    static void bitfield(Client c) { bitfieldGeneric(c, BITFIELD_FLAG_NONE); }

    static void bitfieldRo(Client c) { bitfieldGeneric(c, BITFIELD_FLAG_READONLY); }

    private static void bitfieldGeneric(Client c, int flags) {
        Server s = Server.I;
        List<BfOp> ops = new ArrayList<>();
        int owtype = BFOVERFLOW_WRAP;
        boolean readonly = true;
        long highestWriteOffset = 0;

        for (int j = 2; j < c.argc; j++) {
            int remargs = c.argc - j - 1;
            int opcode;
            long i64 = 0;

            if (c.argIs(j, "get") && remargs >= 2) {
                opcode = BITFIELDOP_GET;
            } else if (c.argIs(j, "set") && remargs >= 3) {
                opcode = BITFIELDOP_SET;
            } else if (c.argIs(j, "incrby") && remargs >= 3) {
                opcode = BITFIELDOP_INCRBY;
            } else if (c.argIs(j, "overflow") && remargs >= 1) {
                j++;
                if (c.argIs(j, "wrap")) owtype = BFOVERFLOW_WRAP;
                else if (c.argIs(j, "sat")) owtype = BFOVERFLOW_SAT;
                else if (c.argIs(j, "fail")) owtype = BFOVERFLOW_FAIL;
                else throw new RedisError("ERR Invalid OVERFLOW type specified");
                continue;
            } else {
                throw RedisError.syntax();
            }

            int[] st = getBitfieldType(c.arg(j + 1));
            int sign = st[0], bits = st[1];
            long bitoffset = getBitOffsetFromArgument(c, c.arg(j + 2), true, bits);

            if (opcode != BITFIELDOP_GET) {
                readonly = false;
                if (highestWriteOffset < bitoffset + bits - 1)
                    highestWriteOffset = bitoffset + bits - 1;
                Long v = Util.string2ll(c.arg(j + 3));
                if (v == null) throw RedisError.notInt();
                i64 = v;
            }

            BfOp op = new BfOp();
            op.offset = bitoffset;
            op.i64 = i64;
            op.opcode = opcode;
            op.owtype = owtype;
            op.bits = bits;
            op.sign = sign;
            ops.add(op);

            j += 3 - (opcode == BITFIELDOP_GET ? 1 : 0);
        }

        RObj o;
        long[] sizes = new long[2];
        if (readonly) {
            o = s.lookupKeyRead(c, c.arg(1));
            if (o != null && o.type != RObj.STRING) throw RedisError.wrongType();
        } else {
            if ((flags & BITFIELD_FLAG_READONLY) != 0)
                throw new RedisError("ERR BITFIELD_RO only supports the GET subcommand");
            o = lookupStringForBitCommand(c, highestWriteOffset, sizes);
        }
        long strGrowSize = sizes[1];

        int changes = 0;
        c.addArray(ops.size());

        for (BfOp thisop : ops) {
            if (thisop.opcode == BITFIELDOP_SET || thisop.opcode == BITFIELDOP_INCRBY) {
                byte[] buf = (byte[]) o.ptr;
                if (thisop.sign != 0) {
                    long oldval = getSignedBitfield(buf, thisop.offset, thisop.bits);
                    long newval, retval;
                    long[] ow;
                    if (thisop.opcode == BITFIELDOP_INCRBY) {
                        ow = checkSignedBitfieldOverflow(oldval, thisop.i64, thisop.bits, thisop.owtype);
                        newval = ow[0] != 0 ? ow[1] : oldval + thisop.i64;
                        retval = newval;
                    } else {
                        newval = thisop.i64;
                        ow = checkSignedBitfieldOverflow(newval, 0, thisop.bits, thisop.owtype);
                        if (ow[0] != 0) newval = ow[1];
                        retval = oldval;
                    }
                    if (!(ow[0] != 0 && thisop.owtype == BFOVERFLOW_FAIL)) {
                        c.addInt(retval);
                        setUnsignedBitfield(buf, thisop.offset, thisop.bits, newval);
                        if (strGrowSize != 0 || oldval != newval) changes++;
                    } else {
                        c.addNil();
                    }
                } else {
                    long oldval = getUnsignedBitfield(buf, thisop.offset, thisop.bits);
                    long newval, retval;
                    long[] ow;
                    if (thisop.opcode == BITFIELDOP_INCRBY) {
                        newval = oldval + thisop.i64;
                        ow = checkUnsignedBitfieldOverflow(oldval, thisop.i64, thisop.bits, thisop.owtype);
                        if (ow[0] != 0) newval = ow[1];
                        retval = newval;
                    } else {
                        newval = thisop.i64;
                        ow = checkUnsignedBitfieldOverflow(newval, 0, thisop.bits, thisop.owtype);
                        if (ow[0] != 0) newval = ow[1];
                        retval = oldval;
                    }
                    if (!(ow[0] != 0 && thisop.owtype == BFOVERFLOW_FAIL)) {
                        c.addInt(retval);
                        setUnsignedBitfield(buf, thisop.offset, thisop.bits, newval);
                        if (strGrowSize != 0 || oldval != newval) changes++;
                    } else {
                        c.addNil();
                    }
                }
            } else {
                /* GET: work on a zero padded copy so we can read across the end. */
                byte[] src = (o != null) ? o.str() : null;
                int strlen = src != null ? src.length : 0;
                byte[] buf = new byte[9];
                long byteIdx = thisop.offset >> 3;
                for (int i = 0; i < 9; i++) {
                    if (src == null || i + byteIdx >= strlen) break;
                    buf[i] = src[(int) (i + byteIdx)];
                }
                long off = thisop.offset - (byteIdx * 8);
                if (thisop.sign != 0) c.addInt(getSignedBitfield(buf, off, thisop.bits));
                else c.addInt(getUnsignedBitfield(buf, off, thisop.bits));
            }
        }

        if (changes != 0) {
            s.signalModifiedKey(c.db, new Key(c.arg(1)));
            s.notifyKeyspaceEvent('$', "setbit", c.arg(1), c.db);
            s.dirty += changes;
        }
    }
}
