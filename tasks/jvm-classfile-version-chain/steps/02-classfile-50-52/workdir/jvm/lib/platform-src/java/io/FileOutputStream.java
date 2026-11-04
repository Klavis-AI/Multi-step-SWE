package java.io;
public class FileOutputStream extends OutputStream {
    private final int fd;
    public FileOutputStream(int fd) { this.fd = fd; }
    private static native void writeBytes(int fd, byte[] b, int off, int len) throws IOException;
    public void write(int b) throws IOException { write(new byte[] { (byte) b }, 0, 1); }
    public void write(byte[] b, int off, int len) throws IOException {
        if (off < 0 || len < 0 || off + len > b.length) throw new IndexOutOfBoundsException();
        writeBytes(fd, b, off, len);
    }
}
