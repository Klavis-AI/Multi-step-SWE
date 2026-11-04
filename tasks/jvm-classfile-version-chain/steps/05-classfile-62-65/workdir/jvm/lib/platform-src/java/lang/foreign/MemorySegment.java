package java.lang.foreign;

/** A region of memory with an address and a size. */
public interface MemorySegment {
    MemorySegment NULL = new SegmentImpl(0, 0);

    long address();
    long byteSize();
    byte get(ValueLayout.OfByte layout, long offset);
    void set(ValueLayout.OfByte layout, long offset, byte value);
    int get(ValueLayout.OfInt layout, long offset);
    void set(ValueLayout.OfInt layout, long offset, int value);
    long get(ValueLayout.OfLong layout, long offset);
    void set(ValueLayout.OfLong layout, long offset, long value);
}
