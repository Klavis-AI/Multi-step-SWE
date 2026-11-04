package redis;

/** A value in the keyspace: type + encoding + payload, mirroring robj. */
public final class RObj {
    public static final int STRING = 0, LIST = 1, SET = 2, ZSET = 3, HASH = 4, MODULE = 5, STREAM = 6;

    // encodings (OBJECT ENCODING names)
    public static final int ENC_RAW = 0, ENC_INT = 1, ENC_EMBSTR = 8, ENC_HT = 2, ENC_ZIPMAP = 3,
            ENC_LINKEDLIST = 4, ENC_ZIPLIST = 5, ENC_INTSET = 6, ENC_SKIPLIST = 7,
            ENC_QUICKLIST = 9, ENC_STREAM = 10, ENC_LISTPACK = 11, ENC_LISTPACK_EX = 12;

    public int type;
    public int encoding;
    public Object ptr;
    public long lru;       // last access time, in seconds
    public int lfu = 5;    // LFU counter (LFU_INIT_VAL)
    /** LFU last decrement time, in minutes (robj.lru >> 8 in redis). */
    public int lfuLdt = (int) ((System.currentTimeMillis() / 60000) & 65535);
    /** Cached objectComputeSize() of this value; maintained by {@link Mem}. */
    public long memSize = 0;

    public RObj(int type, int encoding, Object ptr) {
        this.type = type; this.encoding = encoding; this.ptr = ptr;
        this.lru = System.currentTimeMillis() / 1000;
    }

    public static RObj string(byte[] v) {
        Long l = Util.string2ll(v);
        if (l != null && v.length <= 20) return new RObj(STRING, ENC_INT, l);
        return new RObj(STRING, v.length <= 44 ? ENC_EMBSTR : ENC_RAW, v);
    }

    /** A string object that keeps a raw (non-shared-integer) representation. */
    public static RObj rawString(byte[] v) { return new RObj(STRING, ENC_RAW, v); }

    public byte[] str() {
        if (type != STRING) throw new IllegalStateException("not a string");
        if (encoding == ENC_INT) return Util.ll2string((Long) ptr);
        return (byte[]) ptr;
    }

    public int strlen() {
        if (encoding == ENC_INT) return Util.ll2string((Long) ptr).length;
        return ((byte[]) ptr).length;
    }

    public String typeName() {
        switch (type) {
            case STRING: return "string";
            case LIST: return "list";
            case SET: return "set";
            case ZSET: return "zset";
            case HASH: return "hash";
            case STREAM: return "stream";
            case MODULE: return ptr instanceof ModVal ? ((ModVal) ptr).moduleTypeName() : "unknown";
            default: return "unknown";
        }
    }

    public String encodingName() {
        switch (encoding) {
            case ENC_RAW: return "raw";
            case ENC_INT: return "int";
            case ENC_EMBSTR: return "embstr";
            case ENC_HT: return "hashtable";
            case ENC_ZIPMAP: return "zipmap";
            case ENC_LINKEDLIST: return "linkedlist";
            case ENC_ZIPLIST: return "ziplist";
            case ENC_INTSET: return "intset";
            case ENC_SKIPLIST: return "skiplist";
            /* Redis 2.8 names the compact container encodings "ziplist" and the
             * large list encoding "linkedlist" (object.c strEncoding()). */
            case ENC_QUICKLIST: return "linkedlist";
            case ENC_STREAM: return "stream";
            case ENC_LISTPACK: return "ziplist";
            case ENC_LISTPACK_EX: return "ziplist";
            default: return "unknown";
        }
    }
}
