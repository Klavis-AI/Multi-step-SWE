package redis;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The value behind a ReJSON-RL key: a serde_json compatible JSON tree plus the
 * parser, the serializer and the JSONPath engine RedisJSON exposes.
 *
 * Numbers follow serde_json: a literal without '.'/'e' becomes an i64 (or a u64
 * when it does not fit but is positive), everything else an f64. Object members
 * keep insertion order (RedisJSON builds serde_json with `preserve_order`).
 */
public final class RJson implements ModVal {
    public static final int NULL = 0, BOOL = 1, INT = 2, UINT = 3, DOUBLE = 4,
                            STRING = 5, ARRAY = 6, OBJECT = 7;

    public int t;
    public boolean b;
    public long i;            // INT (signed) or UINT (unsigned bit pattern)
    public double d;
    public byte[] s;
    public List<RJson> arr;
    public LinkedHashMap<Key, RJson> obj;

    private RJson(int t) { this.t = t; }

    public static RJson nul() { return new RJson(NULL); }
    public static RJson bool(boolean v) { RJson r = new RJson(BOOL); r.b = v; return r; }
    public static RJson integer(long v) { RJson r = new RJson(INT); r.i = v; return r; }
    public static RJson uinteger(long v) { RJson r = new RJson(UINT); r.i = v; return r; }
    public static RJson dbl(double v) { RJson r = new RJson(DOUBLE); r.d = v; return r; }
    public static RJson str(byte[] v) { RJson r = new RJson(STRING); r.s = v; return r; }
    public static RJson array() { RJson r = new RJson(ARRAY); r.arr = new ArrayList<>(); return r; }
    public static RJson object() { RJson r = new RJson(OBJECT); r.obj = new LinkedHashMap<>(); return r; }

    public boolean isNumber() { return t == INT || t == UINT || t == DOUBLE; }

    public double num() {
        switch (t) {
            case INT: return (double) i;
            case UINT: return uToDouble(i);
            case DOUBLE: return d;
            default: return 0;
        }
    }

    static double uToDouble(long v) {
        if (v >= 0) return (double) v;
        return ((double) (v >>> 1)) * 2.0 + (double) (v & 1L);
    }

    /** JSON.TYPE names. */
    public String jsonTypeName() {
        switch (t) {
            case NULL: return "null";
            case BOOL: return "boolean";
            case INT: case UINT: return "integer";
            case DOUBLE: return "number";
            case STRING: return "string";
            case ARRAY: return "array";
            default: return "object";
        }
    }

    public RJson deepCopy() {
        switch (t) {
            case ARRAY: {
                RJson r = array();
                for (RJson e : arr) r.arr.add(e.deepCopy());
                return r;
            }
            case OBJECT: {
                RJson r = object();
                for (Map.Entry<Key, RJson> e : obj.entrySet()) r.obj.put(e.getKey(), e.getValue().deepCopy());
                return r;
            }
            case STRING: return str(s.clone());
            default: {
                RJson r = new RJson(t);
                r.b = b; r.i = i; r.d = d;
                return r;
            }
        }
    }

    @Override public String moduleTypeName() { return "ReJSON-RL"; }
    @Override public boolean moduleCopyable() { return true; }
    @Override public int moduleId() { return ModTypes.ID_JSON; }
    @Override public byte[] moduleSave() { return toBytes(); }

    public static Object moduleLoad(byte[] payload) { return parse(payload); }

    // ==================================================================
    // parser  (serde_json compatible, including its error messages)
    // ==================================================================

    /** serde_json reports failures as "<msg> at line L column C". */
    public static final class ParseError extends RuntimeException {
        public ParseError(String m) { super(m); }
    }

    public static RJson parse(byte[] in) {
        P p = new P(in);
        p.ws();
        RJson v = p.value();
        p.ws();
        if (p.pos < p.a.length) throw p.err("trailing characters");
        return v;
    }

    private static final class P {
        final byte[] a;
        int pos = 0;

        P(byte[] a) { this.a = a; }

        /** serde_json counts lines from 1 and columns from 1 at the failing byte. */
        ParseError err(String msg) {
            int line = 1, col = 0;
            for (int k = 0; k < pos && k < a.length; k++) {
                if (a[k] == '\n') { line++; col = 0; } else col++;
            }
            return new ParseError(msg + " at line " + line + " column " + col);
        }

        ParseError eof() {
            int line = 1, col = 0;
            for (int k = 0; k < a.length; k++) {
                if (a[k] == '\n') { line++; col = 0; } else col++;
            }
            return new ParseError("EOF while parsing a value at line " + line + " column " + col);
        }

        void ws() {
            while (pos < a.length) {
                byte c = a[pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos++;
                else break;
            }
        }

        int peek() { return pos < a.length ? (a[pos] & 0xff) : -1; }

        RJson value() {
            int c = peek();
            if (c < 0) throw eof();
            switch (c) {
                case '{': return objectVal();
                case '[': return arrayVal();
                case '"': return RJson.str(stringVal());
                case 't': return lit("true", RJson.bool(true));
                case 'f': return lit("false", RJson.bool(false));
                case 'n': return lit("null", RJson.nul());
                default:
                    if (c == '-' || (c >= '0' && c <= '9')) return number();
                    pos++;   /* serde points one past the offending byte */
                    throw err("expected ident");
            }
        }

        RJson lit(String word, RJson v) {
            for (int k = 0; k < word.length(); k++) {
                if (pos + k >= a.length || a[pos + k] != word.charAt(k)) {
                    pos = Math.min(a.length, pos + k + 1);
                    throw err("expected ident");
                }
            }
            pos += word.length();
            return v;
        }

        RJson objectVal() {
            pos++;                       /* '{' */
            RJson o = RJson.object();
            ws();
            if (peek() == '}') { pos++; return o; }
            while (true) {
                ws();
                if (peek() != '"') { pos++; throw err("key must be a string"); }
                byte[] k = stringVal();
                ws();
                if (peek() != ':') { pos++; throw err("expected `:`"); }
                pos++;
                ws();
                RJson v = value();
                o.obj.put(new Key(k), v);
                ws();
                int c = peek();
                if (c == ',') { pos++; continue; }
                if (c == '}') { pos++; return o; }
                if (c < 0) throw eof();
                pos++;
                throw err("expected `,` or `}`");
            }
        }

        RJson arrayVal() {
            pos++;                       /* '[' */
            RJson r = RJson.array();
            ws();
            if (peek() == ']') { pos++; return r; }
            while (true) {
                ws();
                r.arr.add(value());
                ws();
                int c = peek();
                if (c == ',') { pos++; continue; }
                if (c == ']') { pos++; return r; }
                if (c < 0) throw eof();
                pos++;
                throw err("expected `,` or `]`");
            }
        }

        byte[] stringVal() {
            pos++;                       /* '"' */
            Buf out = new Buf(16);
            while (true) {
                if (pos >= a.length) throw eof();
                int c = a[pos] & 0xff;
                if (c == '"') { pos++; break; }
                if (c == '\\') {
                    pos++;
                    if (pos >= a.length) throw eof();
                    int e = a[pos++] & 0xff;
                    switch (e) {
                        case '"': out.put((byte) '"'); break;
                        case '\\': out.put((byte) '\\'); break;
                        case '/': out.put((byte) '/'); break;
                        case 'b': out.put((byte) 8); break;
                        case 'f': out.put((byte) 12); break;
                        case 'n': out.put((byte) '\n'); break;
                        case 'r': out.put((byte) '\r'); break;
                        case 't': out.put((byte) '\t'); break;
                        case 'u': {
                            int cp = hex4();
                            if (cp >= 0xD800 && cp <= 0xDBFF && pos + 1 < a.length
                                    && a[pos] == '\\' && a[pos + 1] == 'u') {
                                pos += 2;
                                int lo = hex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF)
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                else { utf8(out, cp); cp = lo; }
                            }
                            utf8(out, cp);
                            break;
                        }
                        default:
                            throw err("invalid escape");
                    }
                    continue;
                }
                out.put((byte) c);
                pos++;
            }
            byte[] r = new byte[out.len];
            System.arraycopy(out.a, 0, r, 0, out.len);
            return r;
        }

        int hex4() {
            int v = 0;
            for (int k = 0; k < 4; k++) {
                if (pos >= a.length) throw eof();
                int c = a[pos++] & 0xff;
                int dv;
                if (c >= '0' && c <= '9') dv = c - '0';
                else if (c >= 'a' && c <= 'f') dv = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') dv = c - 'A' + 10;
                else throw err("invalid escape");
                v = v * 16 + dv;
            }
            return v;
        }

        RJson number() {
            int start = pos;
            if (peek() == '-') pos++;
            boolean isFloat = false;
            while (pos < a.length) {
                int c = a[pos] & 0xff;
                if (c >= '0' && c <= '9') { pos++; continue; }
                if (c == '.' || c == 'e' || c == 'E') { isFloat = true; pos++; continue; }
                if ((c == '+' || c == '-') && pos > start
                        && (a[pos - 1] == 'e' || a[pos - 1] == 'E')) { pos++; continue; }
                break;
            }
            String txt = new String(a, start, pos - start, java.nio.charset.StandardCharsets.ISO_8859_1);
            if (!isFloat) {
                try { return RJson.integer(Long.parseLong(txt)); } catch (NumberFormatException ignored) { }
                if (txt.charAt(0) != '-') {
                    try { return RJson.uinteger(Long.parseUnsignedLong(txt)); }
                    catch (NumberFormatException ignored) { }
                }
            }
            double v;
            try { v = Double.parseDouble(txt); }
            catch (NumberFormatException e) { throw err("invalid number"); }
            if (Double.isInfinite(v)) throw err("number out of range");
            return RJson.dbl(v);
        }
    }

    static void utf8(Buf out, int cp) {
        if (cp < 0x80) out.put((byte) cp);
        else if (cp < 0x800) {
            out.put((byte) (0xC0 | (cp >> 6))).put((byte) (0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.put((byte) (0xE0 | (cp >> 12))).put((byte) (0x80 | ((cp >> 6) & 0x3F)))
               .put((byte) (0x80 | (cp & 0x3F)));
        } else {
            out.put((byte) (0xF0 | (cp >> 18))).put((byte) (0x80 | ((cp >> 12) & 0x3F)))
               .put((byte) (0x80 | ((cp >> 6) & 0x3F))).put((byte) (0x80 | (cp & 0x3F)));
        }
    }

    // ==================================================================
    // serializer
    // ==================================================================

    /** JSON.GET formatting options (default: everything empty = compact). */
    public static final class Fmt {
        public byte[] indent = Util.EMPTY, newline = Util.EMPTY, space = Util.EMPTY;
    }

    public static final Fmt COMPACT = new Fmt();

    public byte[] toBytes() { return toBytes(COMPACT); }

    public byte[] toBytes(Fmt f) {
        Buf b = new Buf(64);
        write(b, f, 0);
        byte[] r = new byte[b.len];
        System.arraycopy(b.a, 0, r, 0, b.len);
        return r;
    }

    public String toJsonString() { return Util.s(toBytes()); }

    private void write(Buf o, Fmt f, int depth) {
        switch (t) {
            case NULL: o.put("null"); return;
            case BOOL: o.put(b ? "true" : "false"); return;
            case INT: o.put(Long.toString(i)); return;
            case UINT: o.put(Long.toUnsignedString(i)); return;
            case DOUBLE: o.put(fmtDouble(d)); return;
            case STRING: writeString(o, s); return;
            case ARRAY: {
                if (arr.isEmpty()) { o.put("[]"); return; }
                o.put((byte) '[');
                boolean first = true;
                for (RJson e : arr) {
                    if (!first) o.put((byte) ',');
                    first = false;
                    o.put(f.newline);
                    for (int k = 0; k <= depth; k++) o.put(f.indent);
                    e.write(o, f, depth + 1);
                }
                o.put(f.newline);
                for (int k = 0; k < depth; k++) o.put(f.indent);
                o.put((byte) ']');
                return;
            }
            default: {
                if (obj.isEmpty()) { o.put("{}"); return; }
                o.put((byte) '{');
                boolean first = true;
                for (Map.Entry<Key, RJson> e : obj.entrySet()) {
                    if (!first) o.put((byte) ',');
                    first = false;
                    o.put(f.newline);
                    for (int k = 0; k <= depth; k++) o.put(f.indent);
                    writeString(o, e.getKey().b);
                    o.put((byte) ':');
                    o.put(f.space);
                    e.getValue().write(o, f, depth + 1);
                }
                o.put(f.newline);
                for (int k = 0; k < depth; k++) o.put(f.indent);
                o.put((byte) '}');
            }
        }
    }

    private static final char[] HEX = "0123456789abcdef".toCharArray();

    static void writeString(Buf o, byte[] v) {
        o.put((byte) '"');
        for (byte value : v) {
            int c = value & 0xff;
            switch (c) {
                case '"': o.put("\\\""); break;
                case '\\': o.put("\\\\"); break;
                case '\n': o.put("\\n"); break;
                case '\r': o.put("\\r"); break;
                case '\t': o.put("\\t"); break;
                case 8: o.put("\\b"); break;
                case 12: o.put("\\f"); break;
                default:
                    if (c < 0x20) {
                        o.put("\\u00").put((byte) HEX[(c >> 4) & 0xf]).put((byte) HEX[c & 0xf]);
                    } else {
                        o.put((byte) c);
                    }
            }
        }
        o.put((byte) '"');
    }

    /**
     * serde_json prints f64 through ryu: the shortest round-tripping digits,
     * laid out plainly while the decimal point sits in (-4 .. 16] and in
     * "<d>.<ddd>e<exp>" form otherwise. An f64 always keeps a fractional part
     * ("1.0"), which is what tells it apart from an i64 on the wire.
     */
    public static String fmtDouble(double v) {
        if (Double.isNaN(v) || Double.isInfinite(v)) return "null";
        if (v == 0) return (1.0 / v < 0) ? "-0.0" : "0.0";
        boolean neg = v < 0;
        double av = Math.abs(v);
        String[] dk = shortestDigits(av);
        String digits = dk[0];
        int kk = Integer.parseInt(dk[1]);      /* digits before the decimal point */
        StringBuilder sb = new StringBuilder();
        if (neg) sb.append('-');
        int n = digits.length();
        if (kk > 0 && kk <= 16) {
            if (kk >= n) {
                sb.append(digits);
                for (int k = n; k < kk; k++) sb.append('0');
                sb.append(".0");
            } else {
                sb.append(digits, 0, kk).append('.').append(digits, kk, n);
            }
        } else if (kk <= 0 && kk > -5) {
            sb.append("0.");
            for (int k = 0; k < -kk; k++) sb.append('0');
            sb.append(digits);
        } else {
            sb.append(digits.charAt(0));
            if (n > 1) sb.append('.').append(digits, 1, n);
            sb.append('e').append(kk - 1);
        }
        return sb.toString();
    }

    /**
     * The shortest decimal digits of a positive finite double together with the
     * position of the decimal point, i.e. value == 0.&lt;digits&gt; * 10^kk.
     */
    static String[] shortestDigits(double av) {
        String repr = Double.toString(av);      /* always d.dddd[Eexp] */
        int e = repr.indexOf('E');
        int exp10 = 0;
        String mant = repr;
        if (e >= 0) { exp10 = Integer.parseInt(repr.substring(e + 1)); mant = repr.substring(0, e); }
        int dot = mant.indexOf('.');
        String digits = mant.substring(0, dot) + mant.substring(dot + 1);
        int kk = dot + exp10;
        int lead = 0;
        while (lead < digits.length() - 1 && digits.charAt(lead) == '0') { lead++; kk--; }
        digits = digits.substring(lead);
        int end = digits.length();
        while (end > 1 && digits.charAt(end - 1) == '0') end--;
        digits = digits.substring(0, end);
        if (digits.equals("0")) kk = 1;
        /* Java 17's Double.toString can emit one digit more than necessary; drop
         * it while the shorter form still round-trips. */
        while (digits.length() > 1) {
            String shorter = roundTo(digits, digits.length() - 1);
            int kk2 = kk;
            if (shorter.length() > digits.length() - 1) { shorter = shorter.substring(0, 1); kk2 = kk + 1; }
            double back;
            try { back = Double.parseDouble("0." + shorter + "e" + kk2); }
            catch (NumberFormatException ex) { break; }
            if (back != av) break;
            digits = shorter;
            kk = kk2;
            int t2 = digits.length();
            while (t2 > 1 && digits.charAt(t2 - 1) == '0') t2--;
            digits = digits.substring(0, t2);
        }
        return new String[]{digits, Integer.toString(kk)};
    }

    /** Round the digit string to `n` digits, half-up; may carry into an extra digit. */
    private static String roundTo(String digits, int n) {
        char[] d = digits.substring(0, n).toCharArray();
        boolean up = digits.charAt(n) >= '5';
        int k = n - 1;
        while (up && k >= 0) {
            if (d[k] == '9') { d[k] = '0'; k--; }
            else { d[k]++; up = false; }
        }
        String r = new String(d);
        if (up) r = "1" + r;
        return r;
    }

    // ==================================================================
    // JSONPath
    // ==================================================================

    /**
     * serde_json's map is an IndexMap, so removing a member swaps the last one
     * into the hole rather than shifting the rest down. JSON.DEL and a merge
     * patch that deletes a key both show that order afterwards.
     */
    public static boolean swapRemove(LinkedHashMap<Key, RJson> m, Key k) {
        if (!m.containsKey(k)) return false;
        List<Key> keys = new ArrayList<>(m.keySet());
        int idx = keys.indexOf(k);
        int last = keys.size() - 1;
        if (idx == last) { m.remove(k); return true; }
        Key lastKey = keys.get(last);
        RJson lastVal = m.get(lastKey);
        LinkedHashMap<Key, RJson> out = new LinkedHashMap<>();
        for (int i = 0; i < last; i++) {
            if (i == idx) out.put(lastKey, lastVal);
            else out.put(keys.get(i), m.get(keys.get(i)));
        }
        m.clear();
        m.putAll(out);
        return true;
    }

    /** One matched node together with the place it lives, so writes can update it. */
    public static final class Loc {
        public RJson parent;        // null for the document root
        public int idx = -1;        // array position when parent is an array
        public Key key;             // object member name when parent is an object
        public RJson val;

        Loc(RJson parent, int idx, Key key, RJson val) {
            this.parent = parent; this.idx = idx; this.key = key; this.val = val;
        }

        public void set(RJson v) {
            if (parent == null) return;
            if (parent.t == ARRAY) parent.arr.set(idx, v);
            else parent.obj.put(key, v);
            val = v;
        }
    }

    public static final class PathError extends RuntimeException {
        public PathError(String m) { super(m); }
    }

    /** A compiled path plus whether it arrived as a legacy (non-"$") path. */
    public static final class Path {
        public final String text;       // always the "$..." spelling
        public final boolean legacy;
        final List<Object[]> steps;

        Path(String text, boolean legacy, List<Object[]> steps) {
            this.text = text; this.legacy = legacy; this.steps = steps;
        }

        public boolean isRoot() { return steps.isEmpty(); }
        /** true when every step addresses exactly one place (so it can create). */
        public boolean isStatic() {
            for (Object[] st : steps) {
                int op = (Integer) st[0];
                if (op != S_KEY && op != S_IDX) return false;
            }
            return true;
        }
        public int len() { return steps.size(); }
        Object[] step(int i) { return steps.get(i); }
    }

    /* step opcodes */
    static final int S_KEY = 0, S_WILD = 1, S_IDX = 2, S_SLICE = 3,
                     S_DESC = 4, S_UNION = 5, S_FILTER = 6, S_DESC_WILD = 7;

    public static Path compile(byte[] raw) {
        String p = Util.s(raw);
        boolean legacy = !p.startsWith("$");
        String norm = p;
        if (legacy) {
            if (p.isEmpty() || p.equals(".")) norm = "$";
            else if (p.startsWith(".") || p.startsWith("[")) norm = "$" + p;
            else norm = "$." + p;
        }
        List<Object[]> steps = new ArrayList<>();
        PP pp = new PP(norm);
        pp.parse(steps);
        return new Path(norm, legacy, steps);
    }

    private static final class PP {
        final String p;
        int i;

        PP(String p) { this.p = p; }

        PathError err(String expected) {
            /* the module reports this one through RedisModule_ReplyWithError with no
             * error code of its own, so there is no "ERR " prefix */
            return new PathError("Error occurred on position " + i + ", \""
                    + p.substring(0, Math.min(i, p.length())) + " <<<<----\", expected one of the following: "
                    + expected + ".");
        }

        void parse(List<Object[]> out) {
            if (!p.startsWith("$")) throw err("'$'");
            i = 1;
            while (i < p.length()) {
                char c = p.charAt(i);
                if (c == '.') {
                    i++;
                    if (i < p.length() && p.charAt(i) == '.') {
                        i++;
                        if (i < p.length() && p.charAt(i) == '*') { i++; out.add(new Object[]{S_DESC_WILD}); }
                        else if (i < p.length() && p.charAt(i) == '[') descBracket(out);
                        else {
                            String n = name();
                            if (n == null) throw err("<string>, '*'");
                            out.add(new Object[]{S_DESC, n});
                        }
                    } else if (i < p.length() && p.charAt(i) == '*') {
                        i++;
                        out.add(new Object[]{S_WILD});
                    } else {
                        String n = name();
                        if (n == null) throw err("<string>, '*'");
                        out.add(new Object[]{S_KEY, n});
                    }
                } else if (c == '[') {
                    bracket(out);
                } else {
                    throw err("'.', '['");
                }
            }
        }

        void descBracket(List<Object[]> out) {
            int save = out.size();
            bracket(out);
            if (out.size() == save + 1 && ((Integer) out.get(save)[0]) == S_KEY) {
                out.set(save, new Object[]{S_DESC, out.get(save)[1]});
            } else {
                out.add(save, new Object[]{S_DESC_WILD});
            }
        }

        String name() {
            int st = i;
            while (i < p.length()) {
                char c = p.charAt(i);
                if (c == '.' || c == '[' || c == ']') break;
                i++;
            }
            if (i == st) return null;
            return p.substring(st, i);
        }

        void bracket(List<Object[]> out) {
            i++;                                  /* '[' */
            skipWs();
            if (i >= p.length()) throw err("<string>, '*', <number>");
            char c = p.charAt(i);
            if (c == '*') {
                i++;
                expect(']');
                out.add(new Object[]{S_WILD});
                return;
            }
            if (c == '\'' || c == '"') {
                List<String> names = new ArrayList<>();
                while (true) {
                    skipWs();
                    char q = p.charAt(i);
                    if (q != '\'' && q != '"') throw err("<string>");
                    i++;
                    StringBuilder sb = new StringBuilder();
                    while (i < p.length() && p.charAt(i) != q) {
                        if (p.charAt(i) == '\\' && i + 1 < p.length()) i++;
                        sb.append(p.charAt(i++));
                    }
                    if (i >= p.length()) throw err("<string>");
                    i++;
                    names.add(sb.toString());
                    skipWs();
                    if (i < p.length() && p.charAt(i) == ',') { i++; continue; }
                    break;
                }
                expect(']');
                if (names.size() == 1) out.add(new Object[]{S_KEY, names.get(0)});
                else out.add(new Object[]{S_UNION, names.toArray(new String[0])});
                return;
            }
            if (c == '?') {
                i++;
                boolean paren = i < p.length() && p.charAt(i) == '(';
                if (paren) i++;
                int depth = paren ? 1 : 0;
                int st = i;
                while (i < p.length()) {
                    char q = p.charAt(i);
                    if (q == '(') depth++;
                    else if (q == ')') { depth--; if (depth == 0) break; }
                    else if (q == ']' && depth == 0) break;
                    i++;
                }
                String expr = p.substring(st, i);
                if (paren) { if (i >= p.length()) throw err("')'"); i++; }
                expect(']');
                out.add(new Object[]{S_FILTER, Filter.compile(expr)});
                return;
            }
            int st = i;
            while (i < p.length() && p.charAt(i) != ']') i++;
            if (i >= p.length()) throw err("']'");
            String body = p.substring(st, i);
            i++;                                  /* ']' */
            if (body.indexOf(':') >= 0) {
                String[] parts = body.split(":", -1);
                Long a = parseOpt(parts.length > 0 ? parts[0] : "");
                Long b = parseOpt(parts.length > 1 ? parts[1] : "");
                Long sp = parseOpt(parts.length > 2 ? parts[2] : "");
                out.add(new Object[]{S_SLICE, a, b, sp});
                return;
            }
            if (body.indexOf(',') >= 0) {
                String[] parts = body.split(",", -1);
                long[] idx = new long[parts.length];
                for (int k = 0; k < parts.length; k++) idx[k] = num(parts[k].trim());
                out.add(new Object[]{S_UNION, idx});
                return;
            }
            out.add(new Object[]{S_IDX, num(body.trim())});
        }

        Long parseOpt(String v) {
            v = v.trim();
            if (v.isEmpty()) return null;
            return num(v);
        }

        long num(String v) {
            try { return Long.parseLong(v.trim()); }
            catch (NumberFormatException e) { throw err("<number>"); }
        }

        void expect(char c) {
            if (i >= p.length() || p.charAt(i) != c) throw err("'" + c + "'");
            i++;
        }

        void skipWs() { while (i < p.length() && p.charAt(i) == ' ') i++; }
    }

    /** A `[?(...)]` predicate. */
    static final class Filter {
        String lhsPath;
        String op;
        RJson rhs;
        List<Filter> and, or;

        static Filter compile(String expr) {
            expr = expr.trim();
            int d = 0;
            for (int k = 0; k + 1 < expr.length(); k++) {
                char c = expr.charAt(k);
                if (c == '(') d++;
                else if (c == ')') d--;
                else if (d == 0 && c == '|' && expr.charAt(k + 1) == '|') {
                    Filter f = new Filter();
                    f.or = new ArrayList<>();
                    f.or.add(compile(expr.substring(0, k)));
                    f.or.add(compile(expr.substring(k + 2)));
                    return f;
                }
            }
            d = 0;
            for (int k = 0; k + 1 < expr.length(); k++) {
                char c = expr.charAt(k);
                if (c == '(') d++;
                else if (c == ')') d--;
                else if (d == 0 && c == '&' && expr.charAt(k + 1) == '&') {
                    Filter f = new Filter();
                    f.and = new ArrayList<>();
                    f.and.add(compile(expr.substring(0, k)));
                    f.and.add(compile(expr.substring(k + 2)));
                    return f;
                }
            }
            if (expr.startsWith("(") && expr.endsWith(")")) return compile(expr.substring(1, expr.length() - 1));
            Filter f = new Filter();
            String[] ops = {"==", "!=", "<=", ">=", "<", ">"};
            for (String o : ops) {
                int k = indexOutside(expr, o);
                if (k > 0) {
                    f.lhsPath = expr.substring(0, k).trim();
                    f.op = o;
                    f.rhs = literal(expr.substring(k + o.length()).trim());
                    return f;
                }
            }
            f.lhsPath = expr.trim();
            f.op = "";
            return f;
        }

        private static int indexOutside(String s, String op) {
            boolean inStr = false;
            char q = 0;
            for (int k = 0; k + op.length() <= s.length(); k++) {
                char c = s.charAt(k);
                if (inStr) { if (c == q) inStr = false; continue; }
                if (c == '\'' || c == '"') { inStr = true; q = c; continue; }
                if (s.startsWith(op, k)) {
                    if (op.equals("<") && s.startsWith("<=", k)) return -1;
                    if (op.equals(">") && s.startsWith(">=", k)) return -1;
                    return k;
                }
            }
            return -1;
        }

        private static RJson literal(String v) {
            if (v.isEmpty()) return RJson.nul();
            char c = v.charAt(0);
            if (c == '\'' || c == '"') {
                String inner = v.length() >= 2 ? v.substring(1, v.length() - 1) : "";
                return RJson.str(Util.b(inner));
            }
            if (v.equals("true")) return RJson.bool(true);
            if (v.equals("false")) return RJson.bool(false);
            if (v.equals("null")) return RJson.nul();
            try { return parse(Util.b(v)); } catch (RuntimeException e) { return RJson.str(Util.b(v)); }
        }

        boolean test(RJson node) {
            if (or != null) {
                for (Filter f : or) if (f.test(node)) return true;
                return false;
            }
            if (and != null) {
                for (Filter f : and) if (!f.test(node)) return false;
                return true;
            }
            RJson lhs = resolve(node, lhsPath);
            if (op.isEmpty()) return lhs != null;
            if (lhs == null) return false;
            return compare(lhs, rhs, op);
        }

        private static RJson resolve(RJson node, String path) {
            if (path.isEmpty()) return node;
            if (path.startsWith("@")) path = path.substring(1);
            RJson cur = node;
            int k = 0;
            while (k < path.length() && cur != null) {
                char c = path.charAt(k);
                if (c == '.') { k++; continue; }
                if (c == '[') {
                    int e = path.indexOf(']', k);
                    if (e < 0) return null;
                    String body = path.substring(k + 1, e).trim();
                    k = e + 1;
                    if (body.length() >= 2 && (body.charAt(0) == '\'' || body.charAt(0) == '"'))
                        body = body.substring(1, body.length() - 1);
                    cur = child(cur, body);
                    continue;
                }
                int st = k;
                while (k < path.length() && path.charAt(k) != '.' && path.charAt(k) != '[') k++;
                cur = child(cur, path.substring(st, k));
            }
            return cur;
        }

        private static RJson child(RJson cur, String name) {
            if (cur == null) return null;
            if (cur.t == OBJECT) return cur.obj.get(new Key(Util.b(name)));
            if (cur.t == ARRAY) {
                try {
                    int ix = Integer.parseInt(name);
                    if (ix < 0) ix += cur.arr.size();
                    if (ix < 0 || ix >= cur.arr.size()) return null;
                    return cur.arr.get(ix);
                } catch (NumberFormatException e) { return null; }
            }
            return null;
        }

        static boolean compare(RJson a, RJson b, String op) {
            int cmp;
            if (a.isNumber() && b.isNumber()) cmp = Double.compare(a.num(), b.num());
            else if (a.t == STRING && b.t == STRING) cmp = Util.s(a.s).compareTo(Util.s(b.s));
            else if (a.t == BOOL && b.t == BOOL) cmp = Boolean.compare(a.b, b.b);
            else if (a.t == NULL && b.t == NULL) cmp = 0;
            else return op.equals("!=");
            switch (op) {
                case "==": return cmp == 0;
                case "!=": return cmp != 0;
                case "<": return cmp < 0;
                case "<=": return cmp <= 0;
                case ">": return cmp > 0;
                default: return cmp >= 0;
            }
        }
    }

    /** Evaluate a compiled path against this document. */
    public List<Loc> select(Path path) {
        List<Loc> cur = new ArrayList<>();
        cur.add(new Loc(null, -1, null, this));
        for (Object[] st : path.steps) {
            List<Loc> next = new ArrayList<>();
            int op = (Integer) st[0];
            for (Loc l : cur) step(l, op, st, next);
            cur = next;
            if (cur.isEmpty()) break;
        }
        return cur;
    }

    private static void step(Loc l, int op, Object[] st, List<Loc> out) {
        RJson v = l.val;
        switch (op) {
            case S_KEY: {
                if (v.t != OBJECT) return;
                Key k = new Key(Util.b((String) st[1]));
                RJson c = v.obj.get(k);
                if (c != null) out.add(new Loc(v, -1, k, c));
                return;
            }
            case S_WILD: {
                if (v.t == OBJECT) {
                    for (Map.Entry<Key, RJson> e : v.obj.entrySet())
                        out.add(new Loc(v, -1, e.getKey(), e.getValue()));
                } else if (v.t == ARRAY) {
                    for (int k = 0; k < v.arr.size(); k++) out.add(new Loc(v, k, null, v.arr.get(k)));
                }
                return;
            }
            case S_IDX: {
                if (v.t != ARRAY) return;
                long ix = (Long) st[1];
                if (ix < 0) ix += v.arr.size();
                if (ix < 0 || ix >= v.arr.size()) return;
                out.add(new Loc(v, (int) ix, null, v.arr.get((int) ix)));
                return;
            }
            case S_SLICE: {
                if (v.t != ARRAY) return;
                int n = v.arr.size();
                long stepv = st[3] == null ? 1 : (Long) st[3];
                if (stepv == 0) return;
                if (stepv > 0) {
                    long start = st[1] == null ? 0 : (Long) st[1];
                    long end = st[2] == null ? n : (Long) st[2];
                    if (start < 0) start += n;
                    if (end < 0) end += n;
                    if (start < 0) start = 0;
                    if (end > n) end = n;
                    for (long k = start; k < end; k += stepv) out.add(new Loc(v, (int) k, null, v.arr.get((int) k)));
                } else {
                    long start = st[1] == null ? n - 1 : (Long) st[1];
                    long end = st[2] == null ? -1 : (Long) st[2];
                    if (st[1] != null && start < 0) start += n;
                    if (st[2] != null && end < 0) end += n;
                    if (start > n - 1) start = n - 1;
                    for (long k = start; k > end; k += stepv) {
                        if (k < 0 || k >= n) break;
                        out.add(new Loc(v, (int) k, null, v.arr.get((int) k)));
                    }
                }
                return;
            }
            case S_UNION: {
                if (st[1] instanceof long[]) {
                    if (v.t != ARRAY) return;
                    for (long ix : (long[]) st[1]) {
                        long j = ix < 0 ? ix + v.arr.size() : ix;
                        if (j >= 0 && j < v.arr.size()) out.add(new Loc(v, (int) j, null, v.arr.get((int) j)));
                    }
                } else {
                    if (v.t != OBJECT) return;
                    for (String nm : (String[]) st[1]) {
                        Key k = new Key(Util.b(nm));
                        RJson c = v.obj.get(k);
                        if (c != null) out.add(new Loc(v, -1, k, c));
                    }
                }
                return;
            }
            case S_FILTER: {
                Filter f = (Filter) st[1];
                if (v.t == ARRAY) {
                    for (int k = 0; k < v.arr.size(); k++)
                        if (f.test(v.arr.get(k))) out.add(new Loc(v, k, null, v.arr.get(k)));
                } else if (v.t == OBJECT) {
                    for (Map.Entry<Key, RJson> e : v.obj.entrySet())
                        if (f.test(e.getValue())) out.add(new Loc(v, -1, e.getKey(), e.getValue()));
                }
                return;
            }
            case S_DESC: {
                descend(v, new Key(Util.b((String) st[1])), out);
                return;
            }
            case S_DESC_WILD: {
                descendAll(v, out);
                return;
            }
            default:
        }
    }

    private static void descend(RJson v, Key k, List<Loc> out) {
        if (v.t == OBJECT) {
            RJson hit = v.obj.get(k);
            if (hit != null) out.add(new Loc(v, -1, k, hit));
            for (Map.Entry<Key, RJson> e : v.obj.entrySet()) descend(e.getValue(), k, out);
        } else if (v.t == ARRAY) {
            for (RJson e : v.arr) descend(e, k, out);
        }
    }

    private static void descendAll(RJson v, List<Loc> out) {
        if (v.t == OBJECT) {
            for (Map.Entry<Key, RJson> e : v.obj.entrySet()) out.add(new Loc(v, -1, e.getKey(), e.getValue()));
            for (Map.Entry<Key, RJson> e : v.obj.entrySet()) descendAll(e.getValue(), out);
        } else if (v.t == ARRAY) {
            for (int j = 0; j < v.arr.size(); j++) out.add(new Loc(v, j, null, v.arr.get(j)));
            for (RJson e : v.arr) descendAll(e, out);
        }
    }
}
