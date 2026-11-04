package java.io;
public class PrintStream extends OutputStream implements Appendable {
    private final OutputStream out;
    private final boolean autoFlush;
    private byte[] buf = new byte[8192];
    private int count;
    private boolean error;

    public PrintStream(OutputStream out) { this(out, false); }
    public PrintStream(OutputStream out, boolean autoFlush) { this.out = out; this.autoFlush = autoFlush; }

    public void write(int b) { writeRaw(new byte[] { (byte) b }, 0, 1); if (b == 10 && autoFlush) flush(); }
    public void write(byte[] b, int off, int len) { writeRaw(b, off, len); if (autoFlush) flush(); }
    private synchronized void writeRaw(byte[] b, int off, int len) {
        if (count + len > buf.length) flush();
        if (len > buf.length) {
            try { out.write(b, off, len); } catch (IOException e) { error = true; }
            return;
        }
        System.arraycopy(b, off, buf, count, len);
        count += len;
    }
    public synchronized void flush() {
        if (count > 0) {
            try { out.write(buf, 0, count); } catch (IOException e) { error = true; }
            count = 0;
        }
        try { out.flush(); } catch (IOException e) { error = true; }
    }
    public void close() { flush(); }
    public boolean checkError() { flush(); return error; }

    private void writeString(String s) {
        byte[] b = java.lang.StringHelper.encode(s.toCharArray(), System.streamEncodingIsUTF8());
        writeRaw(b, 0, b.length);
    }

    public synchronized void print(boolean b) { writeString(String.valueOf(b)); }
    public synchronized void print(char c) { writeString(String.valueOf(c)); }
    public synchronized void print(int i) { writeString(String.valueOf(i)); }
    public synchronized void print(long l) { writeString(String.valueOf(l)); }
    public synchronized void print(float f) { writeString(String.valueOf(f)); }
    public synchronized void print(double d) { writeString(String.valueOf(d)); }
    public synchronized void print(char[] s) { writeString(String.valueOf(s)); }
    public synchronized void print(String s) { writeString(s == null ? "null" : s); }
    public synchronized void print(Object obj) { writeString(String.valueOf(obj)); }

    public synchronized void println() { newLine(); }
    public synchronized void println(boolean x) { print(x); newLine(); }
    public synchronized void println(char x) { print(x); newLine(); }
    public synchronized void println(int x) { print(x); newLine(); }
    public synchronized void println(long x) { print(x); newLine(); }
    public synchronized void println(float x) { print(x); newLine(); }
    public synchronized void println(double x) { print(x); newLine(); }
    public synchronized void println(char[] x) { print(x); newLine(); }
    public synchronized void println(String x) { print(x); newLine(); }
    public synchronized void println(Object x) { print(String.valueOf(x)); newLine(); }
    private void newLine() { writeString(System.lineSeparator()); if (autoFlush) flush(); }

    public PrintStream append(CharSequence csq) { print(csq == null ? "null" : csq.toString()); return this; }
    public PrintStream append(CharSequence csq, int start, int end) { return append(csq.subSequence(start, end)); }
    public PrintStream append(char c) { print(c); return this; }
}
