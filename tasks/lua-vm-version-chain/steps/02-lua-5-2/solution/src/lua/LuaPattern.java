package lua;

/** The Lua pattern matcher, working on bytes, following lstrlib.c. */
final class LuaPattern {
    static final int MAXCAPTURES = 32;
    static final int CAP_UNFINISHED = -1, CAP_POSITION = -2;
    private static final int MAXCCALLS = 200;

    final byte[] s;
    final byte[] p;
    int level;
    final int[] capStart = new int[MAXCAPTURES];
    final int[] capLen = new int[MAXCAPTURES];
    int matchdepth;

    LuaPattern(byte[] s, byte[] p) { this.s = s; this.p = p; }

    private static int u(byte b) { return b & 0xff; }

    static LuaError err(String m) { return Interp.libError(m); }

    private boolean classMatch(int c, int cl) {
        boolean res;
        switch (Character.toLowerCase((char) cl)) {
            case 'a': res = isalpha(c); break;
            case 'c': res = iscntrl(c); break;
            case 'd': res = c >= '0' && c <= '9'; break;
            case 'g': res = c > 32 && c < 127; break;
            case 'l': res = c >= 'a' && c <= 'z'; break;
            case 'p': res = ispunct(c); break;
            case 's': res = c == ' ' || (c >= 9 && c <= 13); break;
            case 'u': res = c >= 'A' && c <= 'Z'; break;
            case 'w': res = isalpha(c) || (c >= '0' && c <= '9'); break;
            case 'x': res = NumFmt.isHex((char) c); break;
            case 'z': res = c == 0; break;
            default: return cl == c;
        }
        if (Character.isUpperCase((char) cl)) res = !res;
        return res;
    }

    private static boolean isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

    private static boolean iscntrl(int c) { return c < 32 || c == 127; }

    private static boolean ispunct(int c) {
        return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126);
    }

    private int classEnd(int pp) {
        int c = u(p[pp++]);
        if (c == '%') {
            if (pp >= p.length) throw err("malformed pattern (ends with '%')");
            return pp + 1;
        }
        if (c == '[') {
            if (pp < p.length && p[pp] == '^') pp++;
            do {
                if (pp >= p.length) throw err("malformed pattern (missing ']')");
                c = u(p[pp++]);
                if (c == '%' && pp < p.length) pp++;       // skip an escape such as %]
            } while (pp >= p.length || p[pp] != ']');
            return pp + 1;
        }
        return pp;
    }

    private boolean matchClass(int c, int pp, int ep) {
        switch (u(p[pp])) {
            case '.': return true;
            case '%': return classMatch(c, u(p[pp + 1]));
            case '[': return matchBracket(c, pp, ep - 1);
            default: return u(p[pp]) == c;
        }
    }

    private boolean matchBracket(int c, int pp, int ec) {
        boolean sig = true;
        pp++;
        if (p[pp] == '^') { sig = false; pp++; }
        while (pp < ec) {
            if (p[pp] == '%') {
                pp++;
                if (classMatch(c, u(p[pp]))) return sig;
                pp++;
            } else if (pp + 2 < ec && p[pp + 1] == '-') {
                if (u(p[pp]) <= c && c <= u(p[pp + 2])) return sig;
                pp += 3;
            } else {
                if (u(p[pp]) == c) return sig;
                pp++;
            }
        }
        return !sig;
    }

    /** Returns the end index of the match starting at ss, or -1. */
    int match(int ss, int pp) {
        if (matchdepth++ > MAXCCALLS) throw err("pattern too complex");
        try {
            while (true) {
                if (pp >= p.length) return ss;
                switch (u(p[pp])) {
                    case '(':
                        if (pp + 1 < p.length && p[pp + 1] == ')') return startCapture(ss, pp + 2, CAP_POSITION);
                        return startCapture(ss, pp + 1, CAP_UNFINISHED);
                    case ')':
                        return endCapture(ss, pp + 1);
                    case '$':
                        if (pp + 1 == p.length) return (ss == s.length) ? ss : -1;
                        break;
                    case '%':
                        if (pp + 1 < p.length) {
                            int nx = u(p[pp + 1]);
                            if (nx == 'b') return matchBalance(ss, pp + 2);
                            if (nx == 'f') {
                                pp += 2;
                                if (pp >= p.length || p[pp] != '[') throw err("missing '[' after '%f' in pattern");
                                int ep = classEnd(pp);
                                int prev = (ss == 0) ? 0 : u(s[ss - 1]);
                                int cur = (ss < s.length) ? u(s[ss]) : 0;
                                if (!matchBracket(prev, pp, ep - 1) && matchBracket(cur, pp, ep - 1)) {
                                    pp = ep;
                                    continue;
                                }
                                return -1;
                            }
                            if (nx >= '0' && nx <= '9') {
                                int r = matchCapture(ss, nx);
                                if (r < 0) return -1;
                                ss = r;
                                pp += 2;
                                continue;
                            }
                        }
                        break;
                }
                int ep = classEnd(pp);
                boolean m = ss < s.length && matchClass(u(s[ss]), pp, ep);
                if (ep < p.length) {
                    int epc = u(p[ep]);
                    if (epc == '?') {
                        if (m) {
                            int res = match(ss + 1, ep + 1);
                            if (res != -1) return res;
                        }
                        pp = ep + 1;
                        continue;
                    }
                    if (epc == '+') return m ? maxExpand(ss + 1, pp, ep) : -1;
                    if (epc == '*') return maxExpand(ss, pp, ep);
                    if (epc == '-') return minExpand(ss, pp, ep);
                }
                if (!m) return -1;
                ss++;
                pp = ep;
            }
        } finally {
            matchdepth--;
        }
    }

    private int startCapture(int ss, int pp, int what) {
        if (level >= MAXCAPTURES) throw err("too many captures");
        capStart[level] = ss;
        capLen[level] = what;
        level++;
        int r = match(ss, pp);
        if (r < 0) level--;
        return r;
    }

    private int endCapture(int ss, int pp) {
        int l = captureToClose();
        capLen[l] = ss - capStart[l];
        int r = match(ss, pp);
        if (r < 0) capLen[l] = CAP_UNFINISHED;
        return r;
    }

    private int captureToClose() {
        for (int i = level - 1; i >= 0; i--) if (capLen[i] == CAP_UNFINISHED) return i;
        throw err("invalid pattern capture");
    }

    private int matchBalance(int ss, int pp) {
        if (pp + 1 >= p.length) throw err("malformed pattern (missing arguments to '%b')");
        if (ss >= s.length || s[ss] != p[pp]) return -1;
        int b = u(p[pp]), e = u(p[pp + 1]);
        int cont = 1;
        int i = ss + 1;
        while (i < s.length) {
            int c = u(s[i]);
            if (c == e) { if (--cont == 0) return match(i + 1, pp + 2); }
            else if (c == b) cont++;
            i++;
        }
        return -1;
    }

    private int matchCapture(int ss, int ch) {
        int l = ch - '1';
        if (l < 0 || l >= level || capLen[l] == CAP_UNFINISHED) {
            throw err("invalid capture index %" + (l + 1));
        }
        int len = capLen[l];
        if (s.length - ss >= len) {
            for (int i = 0; i < len; i++) if (s[capStart[l] + i] != s[ss + i]) return -1;
            return ss + len;
        }
        return -1;
    }

    private int maxExpand(int ss, int pp, int ep) {
        int i = 0;
        while (ss + i < s.length && matchClass(u(s[ss + i]), pp, ep)) i++;
        while (i >= 0) {
            int r = match(ss + i, ep + 1);
            if (r != -1) return r;
            i--;
        }
        return -1;
    }

    private int minExpand(int ss, int pp, int ep) {
        for (;;) {
            int r = match(ss, ep + 1);
            if (r != -1) return r;
            if (ss < s.length && matchClass(u(s[ss]), pp, ep)) ss++;
            else return -1;
        }
    }

    Object oneCapture(int i, int ss, int e) {
        if (i >= level) {
            if (i == 0) return LuaString.of(s, ss, e - ss);
            throw err("invalid capture index");
        }
        int l = capLen[i];
        if (l == CAP_UNFINISHED) throw err("unfinished capture");
        if (l == CAP_POSITION) return (double) (capStart[i] + 1);
        return LuaString.of(s, capStart[i], l);
    }

    Object[] captures(int ss, int e) {
        int n = (level == 0) ? 1 : level;
        Object[] out = new Object[n];
        for (int i = 0; i < n; i++) out[i] = oneCapture(i, ss, e);
        return out;
    }

    void reset() { level = 0; matchdepth = 0; }
}
