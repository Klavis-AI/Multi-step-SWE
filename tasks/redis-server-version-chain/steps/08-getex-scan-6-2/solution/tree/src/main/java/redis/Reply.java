package redis;

/**
 * RESP2/RESP3 reply serialization. Every method appends to the supplied buffer;
 * the RESP version selects the wire form where the two protocols differ
 * (https://redis.io/docs/latest/develop/reference/protocol-spec/).
 */
public final class Reply {
    private Reply() {}

    public static void simple(Buf o, String s) { o.put((byte) '+').put(s).crlf(); }
    public static void simple(Buf o, byte[] s) { o.put((byte) '+').put(s).crlf(); }
    public static void ok(Buf o) { o.put(Util.b("+OK\r\n")); }
    public static void error(Buf o, String s) { o.put((byte) '-').put(s).crlf(); }
    public static void error(Buf o, byte[] s) { o.put((byte) '-').put(s).crlf(); }
    public static void integer(Buf o, long v) { o.put((byte) ':').putLong(v).crlf(); }

    public static void bulk(Buf o, byte[] v) {
        if (v == null) { nil(o, 2); return; }
        o.put((byte) '$').putLong(v.length).crlf().put(v).crlf();
    }
    public static void bulk(Buf o, String v) { bulk(o, v == null ? null : Util.b(v)); }

    public static void verbatim(Buf o, String fmt, byte[] v, int resp) {
        if (resp < 3) { bulk(o, v); return; }
        o.put((byte) '=').putLong(v.length + 4).crlf().put(fmt.substring(0, 3)).put((byte) ':').put(v).crlf();
    }

    /** Null: RESP2 uses $-1 for bulk and *-1 for array; RESP3 uses _. */
    public static void nil(Buf o, int resp) {
        if (resp >= 3) o.put(Util.b("_\r\n"));
        else o.put(Util.b("$-1\r\n"));
    }
    public static void nilArray(Buf o, int resp) {
        if (resp >= 3) o.put(Util.b("_\r\n"));
        else o.put(Util.b("*-1\r\n"));
    }

    public static void array(Buf o, int n) { o.put((byte) '*').putLong(n).crlf(); }
    public static void emptyArray(Buf o) { o.put(Util.b("*0\r\n")); }

    /** RESP3 map; RESP2 flattens to a 2n array. */
    public static void map(Buf o, int n, int resp) {
        if (resp >= 3) o.put((byte) '%').putLong(n).crlf();
        else o.put((byte) '*').putLong(2L * n).crlf();
    }
    /** RESP3 set; RESP2 uses an array. */
    public static void set(Buf o, int n, int resp) {
        if (resp >= 3) o.put((byte) '~').putLong(n).crlf();
        else o.put((byte) '*').putLong(n).crlf();
    }
    public static void push(Buf o, int n, int resp) {
        if (resp >= 3) o.put((byte) '>').putLong(n).crlf();
        else o.put((byte) '*').putLong(n).crlf();
    }

    public static void bool(Buf o, boolean v, int resp) {
        if (resp >= 3) o.put(v ? Util.b("#t\r\n") : Util.b("#f\r\n"));
        else integer(o, v ? 1 : 0);
    }

    /** RESP3 double; RESP2 bulk string with redis d2string formatting. */
    public static void dbl(Buf o, double v, int resp) {
        if (resp >= 3) {
            String s;
            if (Double.isInfinite(v)) s = v > 0 ? "inf" : "-inf";
            else if (Double.isNaN(v)) s = "nan";
            else s = Util.d2str(v);
            o.put((byte) ',').put(s).crlf();
        } else {
            bulk(o, Util.d2string(v));
        }
    }

    public static void bigNumber(Buf o, String v, int resp) {
        if (resp >= 3) o.put((byte) '(').put(v).crlf();
        else bulk(o, v);
    }
}
