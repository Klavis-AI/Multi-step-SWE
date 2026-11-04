package java.lang.foreign;
import java.lang.invoke.MethodHandle;
import java.util.Optional;

final class LinkerImpl implements Linker, SymbolLookup {
    static final LinkerImpl INSTANCE = new LinkerImpl();
    private LinkerImpl() {}
    public SymbolLookup defaultLookup() { return this; }
    public Optional<MemorySegment> find(String name) {
        long addr = find0(name);
        return addr == 0 ? Optional.<MemorySegment>empty() : Optional.of((MemorySegment) new SegmentImpl(addr, Long.MAX_VALUE));
    }
    public native MethodHandle downcallHandle(MemorySegment symbol, FunctionDescriptor function, Linker.Option... options);
    private static native long find0(String name);
}
