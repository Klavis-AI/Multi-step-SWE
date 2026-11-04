package java.lang.foreign;
import java.lang.invoke.MethodHandle;

/** The foreign function interface of JEP 454. */
public interface Linker {
    static Linker nativeLinker() { return LinkerImpl.INSTANCE; }
    SymbolLookup defaultLookup();
    MethodHandle downcallHandle(MemorySegment symbol, FunctionDescriptor function, Option... options);

    /** Linker options; none are supported here. */
    interface Option {}
}
