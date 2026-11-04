package java.lang.foreign;

/** Controls the lifetime of native memory segments. */
public interface Arena extends AutoCloseable {
    static Arena ofConfined() { return new ArenaImpl(true); }
    static Arena global() { return ArenaImpl.GLOBAL; }
    MemorySegment allocate(long byteSize);
    MemorySegment allocate(long byteSize, long byteAlignment);
    void close();
}
