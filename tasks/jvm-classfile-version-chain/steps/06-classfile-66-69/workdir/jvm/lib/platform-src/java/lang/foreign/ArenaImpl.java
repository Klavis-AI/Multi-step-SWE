package java.lang.foreign;

final class ArenaImpl implements Arena {
    static final ArenaImpl GLOBAL = new ArenaImpl(false);
    private final boolean closeable;
    private long[] blocks = new long[8];
    private int n;
    private boolean closed;
    ArenaImpl(boolean closeable) { this.closeable = closeable; }
    public MemorySegment allocate(long byteSize) {
        if (closed) throw new IllegalStateException("Already closed");
        if (byteSize < 0) throw new IllegalArgumentException("Invalid size: " + byteSize);
        long addr = alloc0(byteSize);
        if (addr == 0) throw new OutOfMemoryError("native allocation failed");
        if (n == blocks.length) blocks = java.util.Arrays.copyOf(blocks, n * 2);
        blocks[n++] = addr;
        return new SegmentImpl(addr, byteSize);
    }
    public MemorySegment allocate(long byteSize, long byteAlignment) { return allocate(byteSize); }
    public void close() {
        if (!closeable) throw new UnsupportedOperationException("Not closeable");
        if (closed) throw new IllegalStateException("Already closed");
        closed = true;
        for (int i = 0; i < n; i++) free0(blocks[i]);
        n = 0;
    }
    private static native long alloc0(long size);
    private static native void free0(long addr);
}
