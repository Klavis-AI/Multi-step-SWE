package redis;

import java.io.ByteArrayOutputStream;
import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The RDB file format: a port of redis src/rdb.c.
 *
 * saveFile() writes what redis 8.0 writes (RDB version 12, listpack based
 * container encodings). loadFile() accepts every RDB version from 6 (redis 2.6)
 * up to 12, including the ziplist / zipmap / intset / quicklist-v1 encodings and
 * every opcode rdb.h ever defined.
 */
public final class Rdb {
    private Rdb() {}

    public static final int VERSION = 12;

    /* Value types (rdb.h RDB_TYPE_*). */
    public static final int T_STRING = 0;
    public static final int T_LIST = 1;
    public static final int T_SET = 2;
    public static final int T_ZSET = 3;
    public static final int T_HASH = 4;
    public static final int T_ZSET_2 = 5;
    public static final int T_MODULE_PRE_GA = 6;
    public static final int T_MODULE_2 = 7;
    public static final int T_HASH_ZIPMAP = 9;
    public static final int T_LIST_ZIPLIST = 10;
    public static final int T_SET_INTSET = 11;
    public static final int T_ZSET_ZIPLIST = 12;
    public static final int T_HASH_ZIPLIST = 13;
    public static final int T_LIST_QUICKLIST = 14;
    public static final int T_STREAM_LISTPACKS = 15;
    public static final int T_HASH_LISTPACK = 16;
    public static final int T_ZSET_LISTPACK = 17;
    public static final int T_LIST_QUICKLIST_2 = 18;
    public static final int T_STREAM_LISTPACKS_2 = 19;
    public static final int T_SET_LISTPACK = 20;
    public static final int T_STREAM_LISTPACKS_3 = 21;
    public static final int T_HASH_METADATA_PRE_GA = 22;
    public static final int T_HASH_LISTPACK_EX_PRE_GA = 23;
    public static final int T_HASH_METADATA = 24;
    public static final int T_HASH_LISTPACK_EX = 25;

    /* Opcodes (rdb.h RDB_OPCODE_*). */
    static final int OP_SLOT_INFO = 244;
    static final int OP_FUNCTION2 = 245;
    static final int OP_FUNCTION_PRE_GA = 246;
    static final int OP_MODULE_AUX = 247;
    static final int OP_IDLE = 248;
    static final int OP_FREQ = 249;
    static final int OP_AUX = 250;
    static final int OP_RESIZEDB = 251;
    static final int OP_EXPIRETIME_MS = 252;
    static final int OP_EXPIRETIME = 253;
    static final int OP_SELECTDB = 254;
    static final int OP_EOF = 255;

    /* Module value sub opcodes. */
    static final int MOD_EOF = 0, MOD_SINT = 1, MOD_UINT = 2, MOD_FLOAT = 3,
                     MOD_DOUBLE = 4, MOD_STRING = 5;

    /* Length encodings. */
    static final int RDB_6BITLEN = 0, RDB_14BITLEN = 1;
    static final int RDB_32BITLEN = 0x80, RDB_64BITLEN = 0x81, RDB_ENCVAL = 3;
    static final int ENC_INT8 = 0, ENC_INT16 = 1, ENC_INT32 = 2, ENC_LZF = 3;

    /* quicklist.h */
    static final int QL_PLAIN = 1, QL_PACKED = 2;

    /* ================================================================ */
    /* public API                                                        */
    /* ================================================================ */

    public static void saveFile(Server s, Path p) throws Exception {
        Path dir = p.getParent();
        String tmpName = "temp-" + ProcessHandle.current().pid() + ".rdb";
        Path tmp = (dir == null) ? Path.of(tmpName) : dir.resolve(tmpName);
        boolean checksum = s == null || s.config == null || s.config.getBool("rdbchecksum");
        try {
            try (OutputStream os = new BufferedOutputStream(Files.newOutputStream(tmp), 1 << 16)) {
                W w = new W(os, checksum);
                writeRio(s, w);
            }
            Files.move(tmp, p, StandardCopyOption.REPLACE_EXISTING);
        } catch (Exception e) {
            try { Files.deleteIfExists(tmp); } catch (IOException ignored) {}
            throw e;
        }
        if (s != null) s.logNotice("DB saved on disk");
    }

    /** A load failure that redis reports with its own line and then exits on. */
    public static final class Fatal extends RuntimeException {
        public Fatal(String m) { super(m); }
    }

    /**
     * server.c loadDataFromDisk(): a missing RDB starts an empty server, every
     * other failure is fatal and the process exits after logging redis's line.
     */
    public static void loadOnStartup(Server s, Path p) {
        if (!Files.exists(p)) return;                       /* ENOENT */
        byte[] data;
        try {
            data = Files.readAllBytes(p);
        } catch (IOException e) {
            die(s, "Fatal error loading the DB: " + strerror(e) + ". Exiting.");
            return;
        }
        long start = System.currentTimeMillis();
        try {
            loadFromBytes(s, data);
        } catch (Fatal f) {
            die(s, f.getMessage());
            return;
        } catch (RedisError e) {
            String why = Util.s(e.msg).replaceFirst("^ERR ", "");
            if (why.startsWith("Wrong signature") || why.startsWith("Can't handle RDB")) {
                /* rdbLoad() logs the reason and returns with errno = EINVAL. */
                s.logWarn(why);
                die(s, "Fatal error loading the DB: Invalid argument. Exiting.");
            } else {
                die(s, "Short read or OOM loading DB. Unrecoverable error, aborting now.");
            }
            return;
        } catch (Exception e) {
            die(s, "Short read or OOM loading DB. Unrecoverable error, aborting now.");
            return;
        }
        s.logNotice(String.format("DB loaded from disk: %.3f seconds",
                (System.currentTimeMillis() - start) / 1000.0));
    }

    private static void die(Server s, String msg) {
        s.logWarn(msg);
        s.logStream.flush();
        Runtime.getRuntime().halt(1);
    }

    private static String strerror(IOException e) {
        if (e instanceof java.nio.file.AccessDeniedException) return "Permission denied";
        if (e instanceof java.nio.file.NoSuchFileException) return "No such file or directory";
        if (e instanceof java.io.FileNotFoundException) return "Permission denied";
        String m = e.getMessage();
        return m == null ? "Input/output error" : m;
    }

    public static void loadFile(Server s, Path p) throws Exception {
        if (!Files.exists(p)) return;
        byte[] data = Files.readAllBytes(p);
        long start = System.currentTimeMillis();
        loadFromBytes(s, data);
        if (s != null) {
            s.logNotice(String.format("DB loaded from disk: %.3f seconds",
                    (System.currentTimeMillis() - start) / 1000.0));
        }
    }

    public static byte[] serializeDataset(Server s) {
        ByteArrayOutputStream bos = new ByteArrayOutputStream(1 << 16);
        boolean checksum = s == null || s.config == null || s.config.getBool("rdbchecksum");
        try {
            W w = new W(bos, checksum);
            writeRio(s, w);
        } catch (IOException e) {
            throw new RuntimeException(e);   // ByteArrayOutputStream never throws
        }
        return bos.toByteArray();
    }

    /**
     * Load an RDB that is only the *prefix* of `data` (an AOF base file, or the
     * RDB preamble of an old style AOF), and return the number of bytes the RDB
     * itself occupied so the caller can go on with the RESP tail.
     */
    public static int loadPreamble(Server s, byte[] data) throws Exception {
        boolean wasLoading = (s != null) && s.loading;
        if (s != null) s.loading = true;
        try {
            R r = new R(data);
            loadRio(s, r);
            return r.p;
        } finally {
            if (s != null) s.loading = wasLoading;
        }
    }

    public static void loadFromBytes(Server s, byte[] rdb) throws Exception {
        boolean wasLoading = (s != null) && s.loading;
        if (s != null) s.loading = true;
        try {
            loadRio(s, new R(rdb));
        } finally {
            if (s != null) s.loading = wasLoading;
        }
    }

    /** DUMP: the value bytes, then the 2 byte RDB version, then the CRC64. */
    public static byte[] dumpPayload(RObj o) {
        Server s = Server.I;
        ByteArrayOutputStream bos = new ByteArrayOutputStream(64);
        try {
            W w = new W(bos, false);
            int t = objectType(s, o);
            if (t < 0) throw new RedisError("ERR Unknown object type");
            w.b(t);
            saveObject(s, w, o, t);
            w.flush();
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        bos.write(VERSION & 0xff);
        bos.write((VERSION >> 8) & 0xff);
        byte[] body = bos.toByteArray();
        long crc = Crc64.crc64(0, body, 0, body.length);
        byte[] out = Arrays.copyOf(body, body.length + 8);
        for (int i = 0; i < 8; i++) out[body.length + i] = (byte) (crc >>> (8 * i));
        return out;
    }

    /** RESTORE: verify footer, then rebuild the value. */
    public static RObj restorePayload(byte[] payload) {
        if (payload == null || payload.length < 10) throw badPayload();
        int n = payload.length;
        int rdbver = (payload[n - 10] & 0xff) | ((payload[n - 9] & 0xff) << 8);
        if (rdbver > VERSION) throw badPayload();
        Server s = Server.I;
        boolean skipCrc = s != null && s.config != null
                && s.config.has("skip-checksum-validation") && s.config.getBool("skip-checksum-validation");
        if (!skipCrc) {
            long crc = Crc64.crc64(0, payload, 0, n - 8);
            for (int i = 0; i < 8; i++) {
                if ((byte) (crc >>> (8 * i)) != payload[n - 8 + i]) throw badPayload();
            }
        }
        R r = new R(payload);
        r.limit = n - 10;
        r.rdbver = rdbver;
        RObj o;
        try {
            int type = r.u8();
            if (!isObjectType(type)) throw badFormat();
            o = loadObject(s, r, type, Util.EMPTY);
        } catch (Exception e) {
            throw badFormat();   // restoreCommand() reports one error for every failure
        }
        if (o == null) throw badFormat();
        return o;
    }

    static RedisError badPayload() {
        return new RedisError("ERR DUMP payload version or checksum are wrong");
    }

    static RedisError badFormat() { return new RedisError("ERR Bad data format"); }

    public static boolean isObjectType(int t) {
        return (t >= 0 && t <= 7) || (t >= 9 && t <= 25);
    }

    /* ================================================================ */
    /* writer                                                            */
    /* ================================================================ */

    static final class W {
        final OutputStream os;
        final boolean checksum;
        long crc = 0;
        /** Output is staged here so the checksum runs over big chunks. */
        private final byte[] out = new byte[1 << 16];
        private int n = 0;
        private final byte[] buf = new byte[16];

        W(OutputStream os, boolean checksum) { this.os = os; this.checksum = checksum; }

        private void drain() throws IOException {
            if (n == 0) return;
            if (checksum) crc = Crc64.crc64(crc, out, 0, n);
            os.write(out, 0, n);
            n = 0;
        }

        void raw(byte[] b, int off, int len) throws IOException {
            if (len >= out.length) {
                drain();
                if (checksum) crc = Crc64.crc64(crc, b, off, len);
                os.write(b, off, len);
                return;
            }
            if (n + len > out.length) drain();
            System.arraycopy(b, off, out, n, len);
            n += len;
        }

        void raw(byte[] b) throws IOException { raw(b, 0, b.length); }

        void b(int v) throws IOException {
            if (n == out.length) drain();
            out[n++] = (byte) v;
        }

        /** Push everything staged to the underlying stream. */
        void flush() throws IOException { drain(); os.flush(); }

        /** rdbSaveLen(): 6, 14, 32 or 64 bit length prefix. */
        void len(long l) throws IOException {
            if (l < (1 << 6)) {
                b((int) (l & 0xff) | (RDB_6BITLEN << 6));
            } else if (l < (1 << 14)) {
                buf[0] = (byte) (((l >> 8) & 0xff) | (RDB_14BITLEN << 6));
                buf[1] = (byte) (l & 0xff);
                raw(buf, 0, 2);
            } else if (l <= 0xFFFFFFFFL) {
                buf[0] = (byte) RDB_32BITLEN;
                buf[1] = (byte) (l >>> 24);
                buf[2] = (byte) (l >>> 16);
                buf[3] = (byte) (l >>> 8);
                buf[4] = (byte) l;
                raw(buf, 0, 5);
            } else {
                buf[0] = (byte) RDB_64BITLEN;
                for (int i = 0; i < 8; i++) buf[1 + i] = (byte) (l >>> (56 - 8 * i));
                raw(buf, 0, 9);
            }
        }

        /** rdbSaveLen() with the argument taken as a uint64_t. */
        void ulen(long l) throws IOException {
            if (l >= 0 && l < (1 << 6)) {
                b((int) (l & 0xff) | (RDB_6BITLEN << 6));
            } else if (l >= 0 && l < (1 << 14)) {
                buf[0] = (byte) (((l >> 8) & 0xff) | (RDB_14BITLEN << 6));
                buf[1] = (byte) (l & 0xff);
                raw(buf, 0, 2);
            } else if (l >= 0 && l <= 0xFFFFFFFFL) {
                buf[0] = (byte) RDB_32BITLEN;
                buf[1] = (byte) (l >>> 24);
                buf[2] = (byte) (l >>> 16);
                buf[3] = (byte) (l >>> 8);
                buf[4] = (byte) l;
                raw(buf, 0, 5);
            } else {
                buf[0] = (byte) RDB_64BITLEN;
                for (int i = 0; i < 8; i++) buf[1 + i] = (byte) (l >>> (56 - 8 * i));
                raw(buf, 0, 9);
            }
        }

        /** rdbSaveMillisecondTime(): 8 byte little endian. */
        void msTime(long t) throws IOException {
            for (int i = 0; i < 8; i++) buf[i] = (byte) (t >>> (8 * i));
            raw(buf, 0, 8);
        }

        /** rdbSaveBinaryDoubleValue(). */
        void binDouble(double d) throws IOException {
            long bits = Double.doubleToRawLongBits(d);
            for (int i = 0; i < 8; i++) buf[i] = (byte) (bits >>> (8 * i));
            raw(buf, 0, 8);
        }

        /** rdbSaveDoubleValue(): the pre-RDB-8 string-with-length-byte form. */
        void strDouble(double d) throws IOException {
            if (Double.isNaN(d)) { b(253); return; }
            if (Double.isInfinite(d)) { b(d < 0 ? 255 : 254); return; }
            byte[] v = Util.d2string(d);
            b(v.length);
            raw(v);
        }

        boolean compression = true;

        /** rdbSaveRawString(). */
        void str(byte[] s) throws IOException {
            if (s.length <= 11) {
                Long v = Util.string2ll(s);
                if (v != null) {
                    byte[] enc = encodeInteger(v);
                    if (enc != null) { raw(enc); return; }
                }
            }
            if (compression && s.length > 20) {
                byte[] c = Lzf.compress(s);
                if (c != null) {
                    b((RDB_ENCVAL << 6) | ENC_LZF);
                    len(c.length);
                    len(s.length);
                    raw(c);
                    return;
                }
            }
            len(s.length);
            if (s.length > 0) raw(s);
        }

        /** rdbSaveLongLongAsStringObject(). */
        void longAsStr(long v) throws IOException {
            byte[] enc = encodeInteger(v);
            if (enc != null) { raw(enc); return; }
            byte[] d = Util.ll2string(v);
            len(d.length);
            raw(d);
        }

        void aux(String k, String v) throws IOException {
            b(OP_AUX);
            str(Util.b(k));
            str(Util.b(v));
        }
    }

    /** rdbEncodeInteger(): null when the value does not fit in 8/16/32 bits. */
    static byte[] encodeInteger(long v) {
        if (v >= -(1 << 7) && v <= (1 << 7) - 1) {
            return new byte[]{(byte) ((RDB_ENCVAL << 6) | ENC_INT8), (byte) v};
        } else if (v >= -(1 << 15) && v <= (1 << 15) - 1) {
            return new byte[]{(byte) ((RDB_ENCVAL << 6) | ENC_INT16), (byte) v, (byte) (v >> 8)};
        } else if (v >= -(1L << 31) && v <= (1L << 31) - 1) {
            return new byte[]{(byte) ((RDB_ENCVAL << 6) | ENC_INT32),
                    (byte) v, (byte) (v >> 8), (byte) (v >> 16), (byte) (v >> 24)};
        }
        return null;
    }

    static void writeRio(Server s, W w) throws IOException {
        w.compression = (s == null || s.config == null) || s.config.getBool("rdbcompression");
        w.raw(Util.b(String.format("REDIS%04d", VERSION)));
        w.aux("redis-ver", Server.VERSION);
        w.aux("redis-bits", "64");
        w.aux("ctime", Long.toString(System.currentTimeMillis() / 1000));
        w.aux("used-mem", Long.toString(Runtime.getRuntime().totalMemory()
                - Runtime.getRuntime().freeMemory()));
        w.aux("aof-base", "0");

        if (s != null && s.dbs != null) {
            for (Db d : s.dbs) {
                if (d == null || d.dict.isEmpty()) continue;
                w.b(OP_SELECTDB);
                w.len(d.id);
                w.b(OP_RESIZEDB);
                w.len(d.dict.size());
                w.len(d.expires.size());
                for (Map.Entry<Key, RObj> e : new ArrayList<>(d.dict.entrySet())) {
                    RObj o = e.getValue();
                    if (o == null) continue;
                    int t = objectType(s, o);
                    if (t < 0) continue;   // unsupported value type: skip the key
                    Long exp = d.expires.get(e.getKey());
                    if (exp != null) {
                        w.b(OP_EXPIRETIME_MS);
                        w.msTime(exp);
                    }
                    w.b(t);
                    w.str(e.getKey().b);
                    saveObject(s, w, o, t);
                }
            }
        }
        w.b(OP_EOF);
        w.flush();
        long crc = w.crc;
        byte[] c = new byte[8];
        for (int i = 0; i < 8; i++) c[i] = (byte) (crc >>> (8 * i));
        w.os.write(c);      // the trailer itself is not part of the checksum
        w.os.flush();
    }

    /* ---------------- encoding selection (rdbSaveObjectType) ---------- */

    static long cfg(Server s, String name, long dflt) {
        if (s == null || s.config == null) return dflt;
        String v = s.config.get(name);
        if (v == null || v.trim().isEmpty()) return dflt;
        try { return Long.parseLong(v.trim()); } catch (NumberFormatException e) { return dflt; }
    }

    /** The RDB type byte redis would use for this value, or -1 when unsupported. */
    public static int objectType(Server s, RObj o) {
        switch (o.type) {
            case RObj.STRING:
                return T_STRING;
            case RObj.LIST:
                return T_LIST_QUICKLIST_2;
            case RObj.SET: {
                RSet set = (RSet) o.ptr;
                int enc = setEncoding(s, set);
                if (enc == RObj.ENC_INTSET) return T_SET_INTSET;
                if (enc == RObj.ENC_LISTPACK) return T_SET_LISTPACK;
                return T_SET;
            }
            case RObj.ZSET: {
                RZSet z = (RZSet) o.ptr;
                return zsetEncoding(s, z) == RObj.ENC_LISTPACK ? T_ZSET_LISTPACK : T_ZSET_2;
            }
            case RObj.HASH: {
                RHash h = (RHash) o.ptr;
                boolean ttl = minExpire(h) != NO_EXPIRE;
                boolean lp = hashEncoding(s, h) == RObj.ENC_LISTPACK;
                if (ttl) return lp ? T_HASH_LISTPACK_EX : T_HASH_METADATA;
                return lp ? T_HASH_LISTPACK : T_HASH;
            }
            case RObj.STREAM:
                return T_STREAM_LISTPACKS_3;
            case RObj.MODULE:
                return (o.ptr instanceof ModVal) ? T_MODULE_2 : -1;
            default:
                return -1;
        }
    }

    public static int setEncoding(Server s, RSet set) {
        long maxIntset = cfg(s, "set-max-intset-entries", 512);
        boolean allInts = true;
        for (Key k : set.set) {
            if (Util.string2ll(k.b) == null) { allInts = false; break; }
        }
        if (allInts && set.size() <= maxIntset) return RObj.ENC_INTSET;
        /* 2.8 has no listpack set encoding; anything that is not an intset is a
         * hash table (t_set.c setTypeCreate()/setTypeAdd()). */
        return RObj.ENC_HT;
    }

    public static int zsetEncoding(Server s, RZSet z) {
        long maxEnt = cfg(s, "zset-max-listpack-entries", 128);
        long maxVal = cfg(s, "zset-max-listpack-value", 64);
        if (maxEnt == 0) return RObj.ENC_SKIPLIST;
        if (z.size() > maxEnt) return RObj.ENC_SKIPLIST;
        for (Key k : z.dict.keySet()) if (k.b.length > maxVal) return RObj.ENC_SKIPLIST;
        return RObj.ENC_LISTPACK;
    }

    /** hashTypeGetMinExpire(): the smallest TTL among the fields that still exist. */
    public static final long NO_EXPIRE = Long.MAX_VALUE;

    public static long minExpire(RHash h) {
        if (h.ttls == null || h.ttls.isEmpty()) return NO_EXPIRE;
        long min = NO_EXPIRE;
        for (Key f : h.map.keySet()) {
            Long t = h.ttls.get(f);
            if (t != null && t < min) min = t;
        }
        return min;
    }

    public static int hashEncoding(Server s, RHash h) {
        long maxEnt = cfg(s, "hash-max-listpack-entries", 128);
        long maxVal = cfg(s, "hash-max-listpack-value", 64);
        if (maxEnt == 0) return RObj.ENC_HT;
        if (h.size() > maxEnt) return RObj.ENC_HT;
        for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
            if (e.getKey().b.length > maxVal || e.getValue().length > maxVal) return RObj.ENC_HT;
        }
        return RObj.ENC_LISTPACK;
    }

    public static int listEncoding(Server s, RList l) {
        long maxEnt = cfg(s, "list-max-ziplist-entries", 512);
        long maxVal = cfg(s, "list-max-ziplist-value", 64);
        if (l.size() > maxEnt) return RObj.ENC_QUICKLIST;
        for (byte[] e : l.l) if (e.length > maxVal) return RObj.ENC_QUICKLIST;
        return RObj.ENC_LISTPACK;
    }

    static final long[] OPT_LEVEL = {4096, 8192, 16384, 32768, 65536};

    static long negFillLimit(int fill) {
        int off = (-fill) - 1;
        if (off < 0) off = 0;
        if (off >= OPT_LEVEL.length) off = OPT_LEVEL.length - 1;
        return OPT_LEVEL[off];
    }

    static boolean isLargeElement(int sz, int fill) {
        if (fill >= 0) return sz > 8192;
        return sz > negFillLimit(fill);
    }

    /* ---------------- rdbSaveObject ---------------- */

    static void saveObject(Server s, W w, RObj o, int type) throws IOException {
        switch (type) {
            case T_STRING:
                if (o.encoding == RObj.ENC_INT) w.longAsStr((Long) o.ptr);
                else w.str((byte[]) o.ptr);
                return;
            case T_LIST_QUICKLIST_2:
                saveList(s, w, (RList) o.ptr);
                return;
            case T_SET_INTSET:
            case T_SET_LISTPACK:
            case T_SET:
                saveSet(w, (RSet) o.ptr, type);
                return;
            case T_ZSET_LISTPACK:
            case T_ZSET_2:
                saveZSet(w, (RZSet) o.ptr, type);
                return;
            case T_HASH_LISTPACK:
            case T_HASH:
            case T_HASH_LISTPACK_EX:
            case T_HASH_METADATA:
                saveHash(w, (RHash) o.ptr, type);
                return;
            case T_STREAM_LISTPACKS_3:
                saveStream(w, (RStream) o.ptr);
                return;
            case T_MODULE_2:
                saveModuleValue(w, (ModVal) o.ptr);
                return;
            default:
                throw new RedisError("ERR Unknown object type");
        }
    }

    /** RDB_TYPE_LIST_QUICKLIST_2: a sequence of listpack (or plain) nodes. */
    static void saveList(Server s, W w, RList l) throws IOException {
        int fill = (int) cfg(s, "list-max-listpack-size", 128);
        long sizeLimit = fill >= 0 ? Long.MAX_VALUE : negFillLimit(fill);
        long countLimit = fill >= 0 ? (fill == 0 ? 1 : fill) : Long.MAX_VALUE;

        // Build the nodes first: the node count is written before them.
        List<Object> nodes = new ArrayList<>();   // byte[] => plain, List<byte[]> => packed
        List<byte[]> cur = null;
        long curSz = 0;
        for (byte[] e : l.l) {
            if (isLargeElement(e.length, fill)) {
                if (cur != null) { nodes.add(cur); cur = null; curSz = 0; }
                nodes.add(e);
                continue;
            }
            if (cur != null) {
                long newSz = curSz + e.length + 11;
                boolean exceeds;
                if (sizeLimit != Long.MAX_VALUE) exceeds = newSz > sizeLimit;
                else exceeds = newSz > 8192 || (cur.size() + 1) > countLimit;
                if (exceeds) { nodes.add(cur); cur = null; curSz = 0; }
            }
            if (cur == null) { cur = new ArrayList<>(); curSz = 11; }
            cur.add(e);
            curSz += e.length + 11;
        }
        if (cur != null) nodes.add(cur);

        w.len(nodes.size());
        for (Object node : nodes) {
            if (node instanceof byte[]) {
                w.len(QL_PLAIN);
                w.str((byte[]) node);
            } else {
                @SuppressWarnings("unchecked")
                List<byte[]> items = (List<byte[]>) node;
                w.len(QL_PACKED);
                w.str(Listpack.encode(items));
            }
        }
    }

    static void saveSet(W w, RSet set, int type) throws IOException {
        if (type == T_SET_INTSET) {
            long[] vals = new long[set.size()];
            int i = 0;
            for (Key k : set.set) vals[i++] = Util.string2ll(k.b);
            w.str(Listpack.encodeIntset(vals));
        } else if (type == T_SET_LISTPACK) {
            w.str(Listpack.encode(set.members()));
        } else {
            w.len(set.size());
            for (Key k : set.set) w.str(k.b);
        }
    }

    static void saveZSet(W w, RZSet z, int type) throws IOException {
        if (type == T_ZSET_LISTPACK) {
            List<byte[]> items = new ArrayList<>(z.size() * 2);
            for (RZSet.Entry e : z.all(false)) {
                items.add(e.member.b);
                items.add(Util.d2string(e.score));
            }
            w.str(Listpack.encode(items));
        } else {
            w.len(z.size());
            // redis walks the skiplist backwards so the loader inserts in order.
            for (RZSet.Entry e : z.all(true)) {
                w.str(e.member.b);
                w.binDouble(e.score);
            }
        }
    }

    static void saveHash(W w, RHash h, int type) throws IOException {
        boolean lp = (type == T_HASH_LISTPACK || type == T_HASH_LISTPACK_EX);
        if (type == T_HASH_LISTPACK || type == T_HASH) {
            if (lp) {
                List<byte[]> items = new ArrayList<>(h.size() * 2);
                for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
                    items.add(e.getKey().b);
                    items.add(e.getValue());
                }
                w.str(Listpack.encode(items));
            } else {
                w.len(h.size());
                for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
                    w.str(e.getKey().b);
                    w.str(e.getValue());
                }
            }
            return;
        }

        long minExpire = minExpire(h);
        if (minExpire == NO_EXPIRE) minExpire = 0;

        if (lp) {
            /* RDB_TYPE_HASH_LISTPACK_EX: min expire, then field/value/ttl triplets
             * ordered by ttl, fields without a ttl (0) last. */
            List<Key> order = new ArrayList<>(h.map.keySet());
            final RHash hh = h;
            order.sort(Comparator.comparingLong(k -> {
                Long t = hh.ttl(k);
                return (t == null) ? NO_EXPIRE : t;
            }));
            List<byte[]> items = new ArrayList<>(h.size() * 3);
            for (Key f : order) {
                Long t = h.ttl(f);
                items.add(f.b);
                items.add(h.map.get(f));
                items.add(Util.ll2string(t == null ? 0 : t));
            }
            w.msTime(minExpire);
            w.str(Listpack.encode(items));
        } else {
            /* RDB_TYPE_HASH_METADATA: min expire, count, then ttl/field/value. */
            w.msTime(minExpire);
            w.len(h.size());
            for (Map.Entry<Key, byte[]> e : h.map.entrySet()) {
                Long t = h.ttl(e.getKey());
                long rel = (t == null) ? 0 : (t - minExpire + 1);
                w.len(rel);
                w.str(e.getKey().b);
                w.str(e.getValue());
            }
        }
    }

    /* ================================================================ */
    /* reader                                                            */
    /* ================================================================ */

    static final class R {
        final byte[] a;
        int p;
        int limit;
        int rdbver = VERSION;
        /** set by loadLen() */
        boolean isEncoded;

        R(byte[] a) { this.a = a; this.limit = a.length; }

        void need(int n) {
            if (n < 0 || p + n > limit) throw shortRead();
        }

        int u8() {
            need(1);
            return a[p++] & 0xff;
        }

        byte[] read(int n) {
            need(n);
            byte[] r = Arrays.copyOfRange(a, p, p + n);
            p += n;
            return r;
        }

        void skip(int n) { need(n); p += n; }

        long u32le() {
            need(4);
            long v = (a[p] & 0xffL) | ((a[p + 1] & 0xffL) << 8)
                   | ((a[p + 2] & 0xffL) << 16) | ((a[p + 3] & 0xffL) << 24);
            p += 4;
            return v;
        }

        long u64le() {
            need(8);
            long v = 0;
            for (int i = 0; i < 8; i++) v |= (a[p + i] & 0xffL) << (8 * i);
            p += 8;
            return v;
        }

        /** rdbLoadLen(). */
        long len() {
            isEncoded = false;
            int b0 = u8();
            int type = (b0 & 0xC0) >> 6;
            if (type == RDB_ENCVAL) {
                isEncoded = true;
                return b0 & 0x3F;
            } else if (type == RDB_6BITLEN) {
                return b0 & 0x3F;
            } else if (type == RDB_14BITLEN) {
                int b1 = u8();
                return ((b0 & 0x3F) << 8) | b1;
            } else if (b0 == RDB_32BITLEN) {
                need(4);
                long v = ((a[p] & 0xffL) << 24) | ((a[p + 1] & 0xffL) << 16)
                       | ((a[p + 2] & 0xffL) << 8) | (a[p + 3] & 0xffL);
                p += 4;
                return v;
            } else if (b0 == RDB_64BITLEN) {
                need(8);
                long v = 0;
                for (int i = 0; i < 8; i++) v = (v << 8) | (a[p + i] & 0xffL);
                p += 8;
                return v;
            }
            throw badFormat();
        }

        /** rdbGenericLoadStringObject(): returns the raw bytes. */
        byte[] str() {
            long l = len();
            if (isEncoded) {
                switch ((int) l) {
                    case ENC_INT8: {
                        need(1);
                        return Util.ll2string(a[p++]);
                    }
                    case ENC_INT16: {
                        need(2);
                        int v = (a[p] & 0xff) | ((a[p + 1] & 0xff) << 8);
                        p += 2;
                        return Util.ll2string((short) v);
                    }
                    case ENC_INT32: {
                        need(4);
                        int v = (a[p] & 0xff) | ((a[p + 1] & 0xff) << 8)
                              | ((a[p + 2] & 0xff) << 16) | ((a[p + 3] & 0xff) << 24);
                        p += 4;
                        return Util.ll2string(v);
                    }
                    case ENC_LZF: {
                        long clen = len();
                        long ulen = len();
                        if (clen < 0 || clen > Integer.MAX_VALUE || ulen < 0 || ulen > Integer.MAX_VALUE)
                            throw badFormat();
                        need((int) clen);
                        byte[] out = Lzf.decompress(a, p, (int) clen, (int) ulen);
                        p += (int) clen;
                        if (out == null) throw badFormat();
                        return out;
                    }
                    default:
                        throw badFormat();
                }
            }
            if (l < 0 || l > Integer.MAX_VALUE) throw badFormat();
            return read((int) l);
        }

        /** rdbLoadDoubleValue(): a length byte plus the printed representation. */
        double strDouble() {
            int l = u8();
            if (l == 255) return Double.NEGATIVE_INFINITY;
            if (l == 254) return Double.POSITIVE_INFINITY;
            if (l == 253) return Double.NaN;
            byte[] v = read(l);
            return parseDouble(Util.s(v));
        }

        double binDouble() { return Double.longBitsToDouble(u64le()); }

        float binFloat() {
            need(4);
            int bits = (a[p] & 0xff) | ((a[p + 1] & 0xff) << 8)
                     | ((a[p + 2] & 0xff) << 16) | ((a[p + 3] & 0xff) << 24);
            p += 4;
            return Float.intBitsToFloat(bits);
        }

        /** rdbLoadMillisecondTime(): 8 byte little endian (all versions on LE hosts). */
        long msTime() { return u64le(); }
    }

    static RedisError shortRead() {
        return new RedisError("ERR Unexpected EOF reading RDB file");
    }

    /** sscanf("%lg") semantics, enough for the doubles redis writes. */
    static double parseDouble(String t) {
        String x = t.trim();
        String low = x.toLowerCase();
        if (low.equals("inf") || low.equals("+inf") || low.equals("infinity")
                || low.equals("+infinity")) return Double.POSITIVE_INFINITY;
        if (low.equals("-inf") || low.equals("-infinity")) return Double.NEGATIVE_INFINITY;
        if (low.equals("nan") || low.equals("-nan") || low.equals("+nan")) return Double.NaN;
        try {
            return Double.parseDouble(x);
        } catch (NumberFormatException e) {
            throw badFormat();
        }
    }

    /* ---------------- rdbLoadRio ---------------- */

    static void loadRio(Server s, R r) throws Exception {
        byte[] magic = r.read(9);
        String m = Util.s(magic);
        if (!m.startsWith("REDIS")) throw new RedisError("ERR Wrong signature trying to load DB from file");
        int rdbver;
        try { rdbver = Integer.parseInt(m.substring(5).trim()); }
        catch (NumberFormatException e) { throw new RedisError("ERR Wrong signature trying to load DB from file"); }
        if (rdbver < 1 || rdbver > VERSION)
            throw new RedisError("ERR Can't handle RDB format version " + rdbver);
        r.rdbver = rdbver;

        long now = System.currentTimeMillis();
        long expiretime = -1;
        Db db = (s != null && s.dbs != null && s.dbs.length > 0) ? s.dbs[0] : null;
        long loaded = 0, expired = 0, emptySkipped = 0;

        while (true) {
            int type = r.u8();

            if (type == OP_EXPIRETIME) {
                expiretime = ((long) (int) r.u32le()) * 1000L;
                continue;
            } else if (type == OP_EXPIRETIME_MS) {
                expiretime = r.msTime();
                continue;
            } else if (type == OP_FREQ) {
                r.u8();
                continue;
            } else if (type == OP_IDLE) {
                r.len();
                continue;
            } else if (type == OP_EOF) {
                break;
            } else if (type == OP_SELECTDB) {
                long id = r.len();
                if (s != null && s.dbs != null) {
                    if (id < 0 || id >= s.dbs.length) {
                        throw new RedisError("ERR FATAL: Data file was created with a Redis server "
                                + "configured to handle more than " + s.dbs.length + " databases");
                    }
                    db = s.dbs[(int) id];
                }
                continue;
            } else if (type == OP_RESIZEDB) {
                r.len();
                r.len();
                continue;
            } else if (type == OP_SLOT_INFO) {
                r.len();
                r.len();
                r.len();
                continue;
            } else if (type == OP_AUX) {
                byte[] k = r.str();
                byte[] v = r.str();
                if (s != null) {
                    String ks = Util.s(k);
                    if (ks.startsWith("%")) s.logNotice("RDB '" + ks + "': " + Util.s(v));
                    else if (ks.equalsIgnoreCase("redis-ver"))
                        s.logNotice("Loading RDB produced by version " + Util.s(v));
                }
                continue;
            } else if (type == OP_MODULE_AUX) {
                r.len();                 // module id
                long whenOpcode = r.len();
                r.len();                 // when
                if (whenOpcode != MOD_UINT) throw badFormat();
                skipModuleValue(r);
                continue;
            } else if (type == OP_FUNCTION_PRE_GA) {
                r.str();                 // library name
                r.str();                 // engine name
                long hasDesc = r.len();
                if (hasDesc != 0) r.str();
                r.str();                 // blob
                continue;
            } else if (type == OP_FUNCTION2) {
                r.str();                 // library code
                continue;
            }

            if (!isObjectType(type)) throw new RedisError("ERR Unknown RDB encoding type " + type);

            byte[] key = r.str();
            RObj val = loadObject(s, r, type, key);

            if (val == null) {
                emptySkipped++;
            } else if (expiretime != -1 && expiretime < now) {
                expired++;
            } else if (db != null) {
                Key k = new Key(key);
                db.dict.put(k, val);
                if (expiretime != -1) db.expires.put(k, expiretime);
                else db.expires.remove(k);
                /* A hash restored with field TTLs must join db->hexpires, or the
                 * active cycle would never look at it again. */
                if (val.type == RObj.HASH && ((RHash) val.ptr).hasTtls()) db.hexpires.add(k);
                loaded++;
            }
            expiretime = -1;
        }

        if (rdbver >= 5) {
            long stored = r.u64le();
            boolean check = (s == null || s.config == null) || s.config.getBool("rdbchecksum");
            if (check && stored != 0) {
                long expect = Crc64.crc64(0, r.a, 0, r.p - 8);
                if (expect != stored) throw new Fatal("Wrong RDB checksum. Aborting now.");
            }
        }
        if (s != null) {
            s.logNotice("Done loading RDB, keys loaded: " + loaded + ", keys expired: " + expired
                    + (emptySkipped > 0 ? ", empty keys skipped: " + emptySkipped : "") + ".");
        }
    }

    /**
     * The four bundled data types (JSON, probabilistic filters, time series,
     * vector sets) are stored with our own payload rather than redis's module
     * specific opcodes, which no other server could read anyway. The frame
     * still has the shape rdbLoadObject() expects for RDB_TYPE_MODULE_2 - a
     * module id, a stream of typed elements and RDB_MODULE_OPCODE_EOF - so a
     * foreign reader skips it cleanly.
     */
    static final long MOD_OWN_MAGIC = 0x5253564DL;   /* "RSVM" */

    static void saveModuleValue(W w, ModVal v) throws IOException {
        w.len(MOD_OWN_MAGIC);
        w.len(MOD_UINT);
        w.len(v.moduleId());
        byte[] payload = v.moduleSave();
        w.len(MOD_STRING);
        w.len(payload.length);
        w.raw(payload);
        w.len(MOD_EOF);
    }

    static RObj loadModuleValue(R r) {
        if (r.len() != MOD_UINT) throw badFormat();
        int id = (int) r.len();
        if (r.len() != MOD_STRING) throw badFormat();
        byte[] payload = r.str();
        if (r.len() != MOD_EOF) throw badFormat();
        Object v = ModTypes.load(id, payload);
        if (!(v instanceof ModVal)) throw badFormat();
        return ModTypes.obj((ModVal) v);
    }

    static void skipModuleValue(R r) {
        while (true) {
            long op = r.len();
            if (op == MOD_EOF) return;
            if (op == MOD_SINT || op == MOD_UINT) r.len();
            else if (op == MOD_STRING) r.str();
            else if (op == MOD_FLOAT) r.binFloat();
            else if (op == MOD_DOUBLE) r.binDouble();
            else throw badFormat();
        }
    }

    /* ---------------- rdbLoadObject ---------------- */

    /** Returns null for the "empty key" case redis silently skips. */
    static RObj loadObject(Server s, R r, int type, byte[] key) {
        switch (type) {
            case T_STRING:
                return RObj.string(r.str());

            case T_LIST: {
                long n = r.len();
                if (n == 0) return null;
                RList l = new RList();
                for (long i = 0; i < n; i++) l.l.addLast(r.str());
                return listObj(s, l);
            }

            case T_SET: {
                long n = r.len();
                if (n == 0) return null;
                RSet set = new RSet();
                for (long i = 0; i < n; i++) set.add(r.str());
                return setObj(s, set);
            }

            case T_ZSET:
            case T_ZSET_2: {
                long n = r.len();
                if (n == 0) return null;
                RZSet z = new RZSet();
                for (long i = 0; i < n; i++) {
                    byte[] m = r.str();
                    double score = (type == T_ZSET_2) ? r.binDouble() : r.strDouble();
                    if (Double.isNaN(score)) throw badFormat();
                    z.add(m, score);
                }
                return zsetObj(s, z);
            }

            case T_HASH: {
                long n = r.len();
                if (n == 0) return null;
                RHash h = new RHash();
                for (long i = 0; i < n; i++) {
                    byte[] f = r.str();
                    byte[] v = r.str();
                    h.map.put(new Key(f), v);
                }
                return hashObj(s, h);
            }

            case T_HASH_METADATA:
            case T_HASH_METADATA_PRE_GA: {
                long minExpire = 0;
                if (type == T_HASH_METADATA) minExpire = r.msTime();
                long n = r.len();
                if (n == 0) return null;
                RHash h = new RHash();
                for (long i = 0; i < n; i++) {
                    long ttl = r.len();
                    long expireAt = (type == T_HASH_METADATA)
                            ? (ttl != 0 ? ttl + minExpire - 1 : 0)
                            : ttl;
                    byte[] f = r.str();
                    byte[] v = r.str();
                    Key fk = new Key(f);
                    h.map.put(fk, v);
                    if (expireAt != 0) h.setTtl(fk, expireAt);
                }
                return hashObj(s, h);
            }

            case T_LIST_QUICKLIST:
            case T_LIST_QUICKLIST_2: {
                long n = r.len();
                if (n == 0) return null;
                RList l = new RList();
                for (long i = 0; i < n; i++) {
                    long container = QL_PACKED;
                    if (type == T_LIST_QUICKLIST_2) {
                        container = r.len();
                        if (container != QL_PACKED && container != QL_PLAIN) throw badFormat();
                    }
                    byte[] data = r.str();
                    if (data.length == 0) throw badFormat();
                    if (container == QL_PLAIN) {
                        l.l.addLast(data);
                        continue;
                    }
                    List<byte[]> items = (type == T_LIST_QUICKLIST_2)
                            ? Listpack.decode(data) : Listpack.decodeZiplist(data);
                    for (byte[] e : items) l.l.addLast(e);
                }
                if (l.size() == 0) return null;
                return listObj(s, l);
            }

            case T_LIST_ZIPLIST: {
                List<byte[]> items = Listpack.decodeZiplist(r.str());
                if (items.isEmpty()) return null;
                RList l = new RList();
                for (byte[] e : items) l.l.addLast(e);
                return listObj(s, l);
            }

            case T_SET_INTSET: {
                List<byte[]> items = Listpack.decodeIntset(r.str());
                if (items.isEmpty()) return null;
                RSet set = new RSet();
                for (byte[] e : items) set.add(e);
                return setObj(s, set);
            }

            case T_SET_LISTPACK: {
                List<byte[]> items = Listpack.decode(r.str());
                if (items.isEmpty()) return null;
                RSet set = new RSet();
                for (byte[] e : items) set.add(e);
                return setObj(s, set);
            }

            case T_ZSET_ZIPLIST:
            case T_ZSET_LISTPACK: {
                byte[] blob = r.str();
                List<byte[]> items = (type == T_ZSET_LISTPACK)
                        ? Listpack.decode(blob) : Listpack.decodeZiplist(blob);
                if (items.isEmpty()) return null;
                if ((items.size() & 1) != 0) throw badFormat();
                RZSet z = new RZSet();
                for (int i = 0; i < items.size(); i += 2) {
                    z.add(items.get(i), parseDouble(Util.s(items.get(i + 1))));
                }
                return zsetObj(s, z);
            }

            case T_HASH_ZIPLIST:
            case T_HASH_LISTPACK: {
                byte[] blob = r.str();
                List<byte[]> items = (type == T_HASH_LISTPACK)
                        ? Listpack.decode(blob) : Listpack.decodeZiplist(blob);
                if (items.isEmpty()) return null;
                if ((items.size() & 1) != 0) throw badFormat();
                RHash h = new RHash();
                for (int i = 0; i < items.size(); i += 2) h.map.put(new Key(items.get(i)), items.get(i + 1));
                return hashObj(s, h);
            }

            case T_HASH_LISTPACK_EX:
            case T_HASH_LISTPACK_EX_PRE_GA: {
                if (type == T_HASH_LISTPACK_EX) r.msTime();   // min expire, informational
                List<byte[]> items = Listpack.decode(r.str());
                if (items.isEmpty()) return null;
                if (items.size() % 3 != 0) throw badFormat();
                RHash h = new RHash();
                for (int i = 0; i < items.size(); i += 3) {
                    Key f = new Key(items.get(i));
                    h.map.put(f, items.get(i + 1));
                    Long t = Util.string2ll(items.get(i + 2));
                    if (t != null && t != 0) h.setTtl(f, t);
                }
                return hashObj(s, h);
            }

            case T_HASH_ZIPMAP: {
                List<byte[]> items = Listpack.decodeZipmap(r.str());
                if (items.isEmpty()) return null;
                RHash h = new RHash();
                for (int i = 0; i + 1 < items.size(); i += 2) {
                    h.map.put(new Key(items.get(i)), items.get(i + 1));
                }
                return hashObj(s, h);
            }

            case T_STREAM_LISTPACKS:
            case T_STREAM_LISTPACKS_2:
            case T_STREAM_LISTPACKS_3:
                return loadStream(s, r, type);

            case T_MODULE_PRE_GA:
                throw new RedisError("ERR Pre-release module format not supported");

            case T_MODULE_2: {
                long id = r.len();     // module id
                if (id == MOD_OWN_MAGIC) return loadModuleValue(r);
                skipModuleValue(r);
                if (s != null) {
                    s.logWarn("Skipping module key '" + Util.s(key) + "': modules are not supported");
                }
                return null;
            }

            default:
                throw new RedisError("ERR Unknown RDB encoding type " + type);
        }
    }

    /* ---------------- RDB_TYPE_STREAM_LISTPACKS_3 ---------------- */

    /** rdbSaveObject() for OBJ_STREAM. */
    static void saveStream(W w, RStream s) throws IOException {
        w.len(s.rax.size());
        for (Map.Entry<RStream.Id, RStream.Node> e : s.rax.entrySet()) {
            w.str(e.getKey().encode());
            w.str(e.getValue().encode());
        }
        w.ulen(s.length);
        w.ulen(s.lastId.ms);
        w.ulen(s.lastId.seq);
        w.ulen(s.firstId.ms);
        w.ulen(s.firstId.seq);
        w.ulen(s.maxDeletedEntryId.ms);
        w.ulen(s.maxDeletedEntryId.seq);
        w.ulen(s.entriesAdded);

        int numCgroups = s.cgroups == null ? 0 : s.cgroups.size();
        w.len(numCgroups);
        if (numCgroups == 0) return;
        for (Map.Entry<Key, RStream.CG> ge : s.cgroups.entrySet()) {
            RStream.CG cg = ge.getValue();
            w.str(ge.getKey().b);
            w.ulen(cg.lastId.ms);
            w.ulen(cg.lastId.seq);
            w.ulen(cg.entriesRead);
            saveStreamPEL(w, cg.pel, true);
            w.len(cg.consumers.size());
            for (Map.Entry<Key, RStream.Consumer> ce : cg.consumers.entrySet()) {
                RStream.Consumer consumer = ce.getValue();
                w.str(consumer.name);
                w.msTime(consumer.seenTime);
                w.msTime(consumer.activeTime);
                saveStreamPEL(w, consumer.pel, false);
            }
        }
    }

    /** rdbSaveStreamPEL() */
    static void saveStreamPEL(W w, Map<RStream.Id, RStream.NACK> pel, boolean nacks) throws IOException {
        w.len(pel.size());
        for (Map.Entry<RStream.Id, RStream.NACK> e : pel.entrySet()) {
            w.raw(e.getKey().encode());
            if (nacks) {
                w.msTime(e.getValue().deliveryTime);
                w.ulen(e.getValue().deliveryCount);
            }
        }
    }

    /** rdbLoadObject() for the three stream encodings. */
    static RObj loadStream(Server s, R r, int type) {
        RStream st = new RStream();
        long listpacks = r.len();
        if (listpacks < 0) throw badFormat();
        for (long i = 0; i < listpacks; i++) {
            byte[] nodekey = r.str();
            if (nodekey.length != 16) throw badFormat();
            byte[] lp = r.str();
            List<byte[]> elems = Listpack.decode(lp);
            if (elems.isEmpty()) throw badFormat();
            RStream.Id master = RStream.Id.decode(nodekey, 0);
            if (st.rax.containsKey(master)) throw badFormat();
            st.rax.put(master, new RStream.Node(elems));
        }
        st.length = r.len();
        long ms = r.len(), seq = r.len();
        st.lastId = new RStream.Id(ms, seq);
        if (type >= T_STREAM_LISTPACKS_2) {
            ms = r.len(); seq = r.len();
            st.firstId = new RStream.Id(ms, seq);
            ms = r.len(); seq = r.len();
            st.maxDeletedEntryId = new RStream.Id(ms, seq);
            st.entriesAdded = r.len();
        } else {
            st.maxDeletedEntryId = new RStream.Id(0, 0);
            st.entriesAdded = st.length;
            st.firstId = st.getEdgeID(true, true);
        }
        if (st.length != 0 && st.rax.isEmpty()) throw badFormat();

        long cgroups = r.len();
        for (long g = 0; g < cgroups; g++) {
            byte[] cgname = r.str();
            long cms = r.len(), cseq = r.len();
            RStream.Id cgId = new RStream.Id(cms, cseq);
            long cgOffset;
            if (type >= T_STREAM_LISTPACKS_2) cgOffset = r.len();
            else cgOffset = st.estimateDistanceFromFirstEverEntry(cgId);
            RStream.CG cg = st.createCG(cgname, cgId, cgOffset);
            if (cg == null) throw badFormat();

            long pel = r.len();
            for (long i = 0; i < pel; i++) {
                byte[] rawid = r.read(16);
                RStream.Id id = RStream.Id.decode(rawid, 0);
                long deliveryTime = r.msTime();
                long deliveryCount = r.len();
                if (cg.pel.containsKey(id)) throw badFormat();
                cg.pel.put(id, new RStream.NACK(null, deliveryTime, deliveryCount));
            }

            long consumers = r.len();
            for (long ci = 0; ci < consumers; ci++) {
                byte[] cname = r.str();
                if (cg.consumers.containsKey(new Key(cname))) throw badFormat();
                RStream.Consumer consumer = new RStream.Consumer(cname);
                cg.consumers.put(new Key(cname), consumer);
                consumer.seenTime = r.msTime();
                if (type >= T_STREAM_LISTPACKS_3) consumer.activeTime = r.msTime();
                else consumer.activeTime = consumer.seenTime;
                long cpel = r.len();
                for (long i = 0; i < cpel; i++) {
                    byte[] rawid = r.read(16);
                    RStream.Id id = RStream.Id.decode(rawid, 0);
                    RStream.NACK nack = cg.pel.get(id);
                    if (nack == null) throw badFormat();
                    nack.consumer = consumer;
                    consumer.pel.put(id, nack);
                }
            }
        }
        return StreamCmds.streamObject(st);
    }

    /* ---------------- object construction ---------------- */

    public static RObj listObj(Server s, RList l) {
        return new RObj(RObj.LIST, listEncoding(s, l), l);
    }

    public static RObj setObj(Server s, RSet set) {
        return new RObj(RObj.SET, setEncoding(s, set), set);
    }

    public static RObj zsetObj(Server s, RZSet z) {
        return new RObj(RObj.ZSET, zsetEncoding(s, z), z);
    }

    public static RObj hashObj(Server s, RHash h) {
        int enc = hashEncoding(s, h);
        if (enc == RObj.ENC_LISTPACK && minExpire(h) != NO_EXPIRE) enc = RObj.ENC_LISTPACK_EX;
        return new RObj(RObj.HASH, enc, h);
    }
}
