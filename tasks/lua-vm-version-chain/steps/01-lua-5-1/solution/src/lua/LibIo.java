package lua;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;

/** The io and os libraries of Lua 5.1 (the portable subset the manual specifies). */
final class LibIo {

    static final class LFile {
        RandomAccessFile raf;
        OutputStream out;              // for the standard streams
        java.io.InputStream in;
        boolean closed;
        boolean std;
        boolean canRead = true, canWrite = true;
        int pushback = -1;
        String name;
        // setvbuf: "no" writes straight through, "full" and "line" hold bytes in
        // this buffer until it is large enough, a newline arrives, or the file
        // is flushed or closed.
        String vbuf = "no";
        int vbufSize = 0;
        java.io.ByteArrayOutputStream pend;
        LFile(String name) { this.name = name; }
    }

    static java.io.OutputStream stdout = new java.io.BufferedOutputStream(new java.io.FileOutputStream(java.io.FileDescriptor.out), 1 << 16);
    static java.io.OutputStream stderrS = new java.io.FileOutputStream(java.io.FileDescriptor.err);

    static Userdata stdinF, stdoutF, stderrF;
    static Userdata defIn, defOut;
    static LuaTable fileMeta;

    /** print and the standalone interpreter write to the process stdout, as C's stdout does. */
    static void writeStdout(byte[] b) {
        try {
            stdout.write(b);
        } catch (IOException e) {
            throw Interp.libError(String.valueOf(e.getMessage()));
        }
    }

    static void flushAll() {
        try { stdout.flush(); } catch (IOException e) { /* ignored */ }
    }

    private static void writeTo(Userdata u, byte[] b) throws IOException {
        LFile f = (LFile) u.data;
        if (f.out != null) f.out.write(b);
        else if (f.raf != null) f.raf.write(b);
    }

    /** getiofile: using a closed default stream is reported by stream name. */
    static Userdata stdFile(Userdata u, String which) {
        if (u == null || ((LFile) u.data).closed) {
            throw Interp.libError("standard " + which + " file is closed");
        }
        return u;
    }

    static Userdata mkFile(LFile f) {
        Userdata u = new Userdata(f);
        u.metatable = fileMeta;
        return u;
    }

    static LFile check(Object[] a, int i) {
        Object v = Lib.arg(a, i);
        if (v instanceof Userdata && ((Userdata) v).data instanceof LFile) {
            LFile f = (LFile) ((Userdata) v).data;
            if (f.closed) throw Interp.libError("attempt to use a closed file");
            return f;
        }
        throw Lib.typeError(a, i, "FILE*");
    }

    static void open(LuaTable g) {
        fileMeta = new LuaTable();
        LuaTable methods = new LuaTable();
        fileMeta.rawset("__index", methods);
        fileMeta.rawset("__name", LuaString.of("FILE*"));
        Lib.reg(fileMeta, "__gc", a -> {
            Object v = Lib.arg(a, 0);
            if (!(v instanceof Userdata) || !(((Userdata) v).data instanceof LFile)) {
                throw Lib.argError(0, "FILE* expected, got " + Lib.typeAt(a, 0));
            }
            LFile f = (LFile) ((Userdata) v).data;
            if (!f.std && !f.closed) closeFile((Userdata) v);
            return Lib.NONE;
        });
        Lib.reg(fileMeta, "__tostring", a -> {
            LFile f = (LFile) ((Userdata) a[0]).data;
            return Lib.ret(LuaString.of(f.closed ? "file (closed)"
                    : "file (0x" + Long.toHexString(Interp.addressOf(a[0])) + ")"));
        });

        LuaTable io = new LuaTable();
        g.rawset("io", io);

        LFile si = new LFile("stdin"); si.std = true; si.in = System.in;
        LFile so = new LFile("stdout"); so.std = true; so.out = stdout;
        LFile se = new LFile("stderr"); se.std = true; se.out = stderrS;
        stdinF = mkFile(si); stdoutF = mkFile(so); stderrF = mkFile(se);
        io.rawset("stdin", stdinF);
        io.rawset("stdout", stdoutF);
        io.rawset("stderr", stderrF);
        defIn = stdinF; defOut = stdoutF;

        Lib.reg(io, "type", a -> {
            Object v = Lib.checkAny(a, 0);
            if (v instanceof Userdata && ((Userdata) v).data instanceof LFile) {
                LFile f = (LFile) ((Userdata) v).data;
                return Lib.ret(LuaString.of(f.closed ? "closed file" : "file"));
            }
            return Lib.ret((Object) null);
        });

        Lib.reg(io, "open", a -> {
            String name = Lib.checkString(a, 0).toString();
            String mode = Lib.optString(a, 1, "r").toString();
            if (!validMode(mode)) throw Lib.argError(1, "invalid mode");
            return openFile(name, mode);
        });

        Lib.reg(io, "close", a -> {
            if (a.length == 0 || a[0] == null) return closeFile(defOut);
            check(a, 0);
            return closeFile((Userdata) a[0]);
        });

        Lib.reg(io, "output", a -> {
            if (a.length > 0 && a[0] != null) {
                if (a[0] instanceof LuaString) {
                    Object[] r = openFile(a[0].toString(), "w");
                    if (r[0] == null) throw new LuaError(r[1]);
                    defOut = (Userdata) r[0];
                } else defOut = (Userdata) a[0];
            }
            return Lib.ret(defOut);
        });

        Lib.reg(io, "input", a -> {
            if (a.length > 0 && a[0] != null) {
                if (a[0] instanceof LuaString) {
                    Object[] r = openFile(a[0].toString(), "r");
                    if (r[0] == null) throw new LuaError(r[1]);
                    defIn = (Userdata) r[0];
                } else defIn = (Userdata) a[0];
            }
            return Lib.ret(defIn);
        });

        Lib.reg(io, "write", a -> writeTo(stdFile(defOut, "output"), a, 0));

        Lib.reg(io, "read", a -> {
            Object[] a2 = new Object[a.length + 1];
            a2[0] = stdFile(defIn, "input");
            System.arraycopy(a, 0, a2, 1, a.length);
            return read(a2, 1);
        });

        Lib.reg(io, "flush", a -> {
            try {
                if (defOut == stdoutF) stdout.flush();
                else drain((LFile) defOut.data);
            } catch (IOException e) {
                return Lib.ret(null, LuaString.of(String.valueOf(e.getMessage())), 28L);
            }
            return Lib.ret(defOut);
        });

        Lib.reg(io, "popen", a -> { throw Interp.libError("'popen' not supported"); });

        Lib.reg(io, "tmpfile", a -> {
            try {
                java.io.File tf = java.io.File.createTempFile("lua", null);
                tf.deleteOnExit();
                LFile lf = new LFile(tf.getAbsolutePath());
                lf.raf = new RandomAccessFile(tf, "rw");
                return Lib.ret(mkFile(lf));
            } catch (IOException e) {
                return Lib.ret(null, LuaString.of(String.valueOf(e.getMessage())));
            }
        });

        Lib.reg(io, "lines", a -> {
            if (a.length == 0 || a[0] == null) return lines(stdFile(defIn, "input"), false);
            Object[] r = openFile(Lib.checkString(a, 0).toString(), "r");
            if (r[0] == null) throw new LuaError(r[1]);
            return lines((Userdata) r[0], true);
        });

        Lib.reg(methods, "write", a -> { check(a, 0); return writeTo((Userdata) a[0], a, 1); });
        Lib.reg(methods, "read", a -> read(a, 1));
        Lib.reg(methods, "close", a -> { check(a, 0); return closeFile((Userdata) a[0]); });
        Lib.reg(methods, "lines", a -> lines((Userdata) a[0], false));
        Lib.reg(methods, "flush", a -> {
            LFile f = check(a, 0);
            try {
                drain(f);
                if (f.out != null) f.out.flush();
            } catch (IOException e) {
                return Lib.ret(null, LuaString.of(String.valueOf(e.getMessage())), 28L);
            }
            return Lib.ret(a[0]);
        });
        Lib.reg(methods, "setvbuf", a -> {
            LFile f = check(a, 0);
            String mode = Lib.checkString(a, 1).toString();
            int size = (int) Lib.optInt(a, 2, 1024);
            if (!mode.equals("no") && !mode.equals("full") && !mode.equals("line")) {
                throw Lib.argError(1, "invalid option '" + mode + "'");
            }
            try { drain(f); } catch (IOException e) { /* ignored */ }
            f.vbuf = mode;
            f.vbufSize = size;
            f.pend = (mode.equals("no") || f.raf == null) ? null : new java.io.ByteArrayOutputStream();
            return Lib.ret(Boolean.TRUE);
        });
        Lib.reg(methods, "seek", a -> {
            LFile f = check(a, 0);
            String whence = Lib.optString(a, 1, "cur").toString();
            long off = Lib.optInt(a, 2, 0);
            try {
                if (f.raf == null) return Lib.ret(null, LuaString.of("cannot seek"));
                drain(f);
                long base;
                switch (whence) {
                    case "set": base = 0; break;
                    case "cur": base = f.raf.getFilePointer(); break;
                    case "end": base = f.raf.length(); break;
                    default: throw Lib.argError(1, "invalid option '" + whence + "'");
                }
                f.raf.seek(base + off);
                return Lib.ret((double) f.raf.getFilePointer());
            } catch (IOException e) {
                return Lib.ret(null, LuaString.of(e.getMessage()));
            }
        });

        // ------------------------------------------------------------------ os
        LuaTable os = new LuaTable();
        g.rawset("os", os);
        Lib.reg(os, "time", a -> {
            if (a.length == 0 || a[0] == null) return Lib.ret((double) (System.currentTimeMillis() / 1000));
            LuaTable t = Lib.checkTable(a, 0);
            java.util.Calendar c = java.util.Calendar.getInstance();
            c.clear();
            c.set(field(t, "year", 1970), field(t, "month", 1) - 1, field(t, "day", 1),
                    field(t, "hour", 12), field(t, "min", 0), field(t, "sec", 0));
            return Lib.ret((double) (c.getTimeInMillis() / 1000));
        });
        Lib.reg(os, "clock", a -> Lib.ret(System.nanoTime() / 1e9));
        Lib.reg(os, "difftime", a -> Lib.ret(Lib.checkNumber(a, 0) - Lib.optNumber(a, 1, 0)));
        Lib.reg(os, "date", a -> LibOsDate.date(a));
        Lib.reg(os, "getenv", a -> {
            String v = System.getenv(Lib.checkString(a, 0).toString());
            return Lib.ret(v == null ? null : LuaString.of(v));
        });
        Lib.reg(os, "remove", a -> {
            java.io.File f = new java.io.File(Lib.checkString(a, 0).toString());
            if (f.delete()) return Lib.ret(Boolean.TRUE);
            return Lib.ret(null, LuaString.of(f + ": No such file or directory"), 2.0);
        });
        Lib.reg(os, "rename", a -> {
            java.io.File f = new java.io.File(Lib.checkString(a, 0).toString());
            java.io.File t = new java.io.File(Lib.checkString(a, 1).toString());
            if (f.renameTo(t)) return Lib.ret(Boolean.TRUE);
            return Lib.ret(null, LuaString.of(f + ": No such file or directory"), 2.0);
        });
        Lib.reg(os, "tmpname", a -> {
            try {
                java.io.File f = java.io.File.createTempFile("lua", null);
                f.deleteOnExit();
                return Lib.ret(LuaString.of(f.getAbsolutePath()));
            } catch (IOException e) {
                throw Interp.libError("unable to generate a unique filename");
            }
        });
        Lib.reg(os, "setlocale", a -> {
            LuaString l = Lib.optString(a, 0, null);
            if (l == null || l.toString().isEmpty() || l.toString().equals("C")) return Lib.ret(LuaString.of("C"));
            return Lib.ret((Object) null);
        });
        Lib.reg(os, "exit", a -> {
            flushAll();
            Object v = Lib.arg(a, 0);
            int code = 0;
            if (v instanceof Double) code = (int) (double) (Double) v;
            else if (v == Boolean.FALSE) code = 1;
            System.exit(code);
            return Lib.NONE;
        });
        Lib.reg(os, "execute", a -> Lib.ret(0.0));
    }

    private static int field(LuaTable t, String k, int def) {
        Object v = t.rawget(k);
        if (v instanceof Double) return (int) (double) (Double) v;
        return def;
    }

    /** l_checkmode: one of r/w/a, an optional '+', then only 'b's. */
    static boolean validMode(String m) {
        int i = 0;
        if (i >= m.length() || "rwa".indexOf(m.charAt(i)) < 0) return false;
        i++;
        if (i < m.length() && m.charAt(i) == '+') i++;
        while (i < m.length()) {
            if (m.charAt(i) != 'b') return false;
            i++;
        }
        return true;
    }

    static Object[] openFile(String name, String mode) {
        try {
            boolean read = mode.startsWith("r");
            boolean append = mode.startsWith("a");
            java.io.File f = new java.io.File(name);
            if (read && !f.exists()) {
                return Lib.ret(null, LuaString.of(name + ": No such file or directory"), 2.0);
            }
            LFile lf = new LFile(name);
            lf.canRead = read || mode.indexOf('+') >= 0;
            lf.canWrite = !read || mode.indexOf('+') >= 0;
            lf.raf = new RandomAccessFile(f, read ? "r" : "rw");
            // A device such as /dev/null has no length to truncate.
            if (mode.startsWith("w")) {
                try { lf.raf.setLength(0); } catch (IOException e) { /* not a regular file */ }
            }
            if (append) lf.raf.seek(lf.raf.length());
            return Lib.ret(mkFile(lf));
        } catch (IOException e) {
            return Lib.ret(null, LuaString.of(name + ": " + e.getMessage()), 2.0);
        }
    }

    /** Writes out whatever setvbuf has been holding back. */
    static void drain(LFile f) throws IOException {
        if (f.pend != null && f.pend.size() > 0) {
            f.raf.write(f.pend.toByteArray());
            f.pend.reset();
        }
    }

    static Object[] closeFile(Userdata u) {
        LFile f = (LFile) u.data;
        try { drain(f); } catch (IOException e) { /* ignored */ }
        if (f.std) return Lib.ret(null, LuaString.of("cannot close standard file"));
        try {
            if (f.raf != null) f.raf.close();
        } catch (IOException e) { /* ignored */ }
        f.closed = true;
        return Lib.ret(Boolean.TRUE);
    }

    static Object[] writeTo(Userdata u, Object[] a, int first) {
        Object[] probe = {u};
        LFile f = check(probe, 0);
        try {
            for (int i = first; i < a.length; i++) {
                Object v = a[i];
                if (!(v instanceof LuaString || v instanceof Double)) {
                    throw Lib.argError(i - first, "string expected, got " + Interp.typeName(v));
                }
                if (!f.canWrite) return Lib.ret(null, LuaString.of("Bad file descriptor"), 9.0);
                LuaString s = Interp.toStr(v);
                if (f.pend != null) {
                    f.pend.write(s.b);
                    boolean nl = false;
                    if (f.vbuf.equals("line")) {
                        for (byte x : s.b) if (x == '\n') { nl = true; break; }
                    }
                    if (nl || f.pend.size() >= f.vbufSize) drain(f);
                } else if (f.out == stdout) stdout.write(s.b);
                else if (f.out != null) f.out.write(s.b);
                else f.raf.write(s.b);
            }
        } catch (IOException e) {
            return Lib.ret(null, LuaString.of(String.valueOf(e.getMessage())), 9.0);
        }
        return Lib.ret(Boolean.TRUE);        // 5.1 pushes a status, not the file
    }

    static Object[] lines(Userdata u, boolean autoclose) {
        return Lib.ret(new Lib.Fn("lines iterator", b -> {
            // io_readline checks the file itself, so a closed file gives its
            // own message rather than the one 'read' would give.
            if (((LFile) u.data).closed) throw Interp.libError("file is already closed");
            Object[] r = read(new Object[]{u, LuaString.of("*l")}, 1);
            if (r.length == 0 || r[0] == null) {
                if (autoclose) closeFile(u);
                return Lib.ret((Object) null);
            }
            return r;
        }));
    }

    static Object[] read(Object[] a, int first) {
        LFile f = check(a, 0);
        if (a.length <= first) {
            Object l = readLine(f, false);
            return Lib.ret(l);
        }
        Object[] out = new Object[a.length - first];
        for (int i = first; i < a.length; i++) {
            Object fmtv = a[i];
            Object r;
            if (fmtv instanceof Double) {
                r = readCount(f, (int) (double) (Double) fmtv);
            } else {
                String fmt = Lib.checkString(a, i).toString();
                if (fmt.startsWith("*")) fmt = fmt.substring(1);
                switch (fmt.isEmpty() ? 'l' : fmt.charAt(0)) {
                    case 'l': r = readLine(f, false); break;
                    case 'n': r = readNumber(f); break;
                    case 'a': r = readAll(f); break;
                    default: throw Lib.argError(i, "invalid format");
                }
            }
            out[i - first] = r;
            if (r == null) break;
        }
        return out;
    }

    private static int readByte(LFile f) {
        if (f.pushback >= 0) { int c = f.pushback; f.pushback = -1; return c; }
        try {
            if (f.in != null) return f.in.read();
            return f.raf.read();
        } catch (IOException e) {
            return -1;
        }
    }

    private static void unread(LFile f, int c) {
        if (c >= 0) f.pushback = c;
    }

    private static Object readLine(LFile f, boolean keep) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        int c;
        boolean any = false;
        while ((c = readByte(f)) >= 0) {
            any = true;
            if (c == '\n') { if (keep) out.write(c); break; }
            out.write(c);
        }
        if (!any) return null;
        return LuaString.of(out.toByteArray());
    }

    private static Object readAll(LFile f) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        int c;
        while ((c = readByte(f)) >= 0) out.write(c);
        return LuaString.of(out.toByteArray());
    }

    private static Object readCount(LFile f, int n) {
        if (n == 0) {                                  // test_eof
            int c = readByte(f);
            if (c < 0) return null;
            unread(f, c);
            return LuaString.of("");
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (int i = 0; i < n; i++) {
            int c = readByte(f);
            if (c < 0) break;
            out.write(c);
        }
        if (out.size() == 0) return null;
        return LuaString.of(out.toByteArray());
    }

    /**
     * read_number: gather the longest prefix that could be a numeral, one
     * character class at a time, then convert it.  A bad prefix is consumed
     * either way, exactly as the reference reader does.
     */
    private static Object readNumber(LFile f) {
        StringBuilder sb = new StringBuilder();
        int[] c = new int[1];
        do { c[0] = readByte(f); } while (c[0] == ' ' || c[0] == '\t' || c[0] == '\n'
                || c[0] == '\r' || c[0] == '\f' || c[0] == 0x0b);
        int count = 0;
        boolean hex = false;
        test2(f, sb, c, "-+");
        if (test2(f, sb, c, "00")) {
            if (test2(f, sb, c, "xX")) hex = true;
            else count = 1;
        }
        count += readDigits(f, sb, c, hex);
        if (test2(f, sb, c, "..")) count += readDigits(f, sb, c, hex);
        if (count > 0 && test2(f, sb, c, hex ? "pP" : "eE")) {
            test2(f, sb, c, "-+");
            readDigits(f, sb, c, false);
        }
        unread(f, c[0]);
        return NumFmt.str2num(LuaString.of(sb.toString()));
    }

    /** L_MAXLENNUM: a numeral longer than this cannot be read at all. */
    private static final int MAXLENNUM = 200;

    /** test2: accept the current character when it is one of two. */
    private static boolean test2(LFile f, StringBuilder sb, int[] c, String set) {
        if (sb.length() >= MAXLENNUM) { sb.setLength(0); return false; }
        if (c[0] == set.charAt(0) || c[0] == set.charAt(1)) {
            sb.append((char) c[0]);
            c[0] = readByte(f);
            return true;
        }
        return false;
    }

    private static int readDigits(LFile f, StringBuilder sb, int[] c, boolean hex) {
        int n = 0;
        while (c[0] >= 0 && (hex ? NumFmt.isHex((char) c[0])
                                 : (c[0] >= '0' && c[0] <= '9'))) {
            if (sb.length() >= MAXLENNUM) { sb.setLength(0); return 0; }
            sb.append((char) c[0]);
            c[0] = readByte(f);
            n++;
        }
        return n;
    }
}
