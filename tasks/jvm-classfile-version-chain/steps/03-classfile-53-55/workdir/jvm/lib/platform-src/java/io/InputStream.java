package java.io;
public abstract class InputStream implements Closeable {
    public abstract int read() throws IOException;
    public int read(byte[] b) throws IOException { return read(b, 0, b.length); }
    public int read(byte[] b, int off, int len) throws IOException {
        int n = 0;
        while (n < len) {
            int c = read();
            if (c < 0) return n == 0 ? -1 : n;
            b[off + n++] = (byte) c;
        }
        return n;
    }
    public void close() throws IOException {}
}
