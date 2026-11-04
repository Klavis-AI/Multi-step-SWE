package java.lang.foreign;

final class SegmentImpl implements MemorySegment {
    long address;
    long size;
    SegmentImpl(long address, long size) { this.address = address; this.size = size; }
    public long address() { return address; }
    public long byteSize() { return size; }
    public String toString() { return "MemorySegment{address=0x" + Long.toHexString(address) + ",byteSize=" + size + "}"; }
    private void check(long off, long len) {
        if (off < 0 || len < 0 || off + len > size)
            throw new IndexOutOfBoundsException("Out of bound access on segment " + this + "; new offset = " + off + "; new length = " + len);
        if (address == 0) throw new NullPointerException();
    }
    public byte get(ValueLayout.OfByte l, long off) { check(off, 1); return getByte0(address + off); }
    public void set(ValueLayout.OfByte l, long off, byte v) { check(off, 1); setByte0(address + off, v); }
    public int get(ValueLayout.OfInt l, long off) { check(off, 4); return getInt0(address + off); }
    public void set(ValueLayout.OfInt l, long off, int v) { check(off, 4); setInt0(address + off, v); }
    public long get(ValueLayout.OfLong l, long off) { check(off, 8); return getLong0(address + off); }
    public void set(ValueLayout.OfLong l, long off, long v) { check(off, 8); setLong0(address + off, v); }
    private static native byte getByte0(long addr);
    private static native void setByte0(long addr, byte v);
    private static native int getInt0(long addr);
    private static native void setInt0(long addr, int v);
    private static native long getLong0(long addr);
    private static native void setLong0(long addr, long v);
}
