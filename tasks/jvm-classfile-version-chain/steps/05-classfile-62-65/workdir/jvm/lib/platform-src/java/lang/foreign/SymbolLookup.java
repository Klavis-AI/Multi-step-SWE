package java.lang.foreign;
import java.util.Optional;
public interface SymbolLookup {
    Optional<MemorySegment> find(String name);
}
