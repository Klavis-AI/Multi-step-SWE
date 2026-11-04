package java.lang.foreign;
public interface FunctionDescriptor {
    static FunctionDescriptor of(MemoryLayout resLayout, MemoryLayout... argLayouts) {
        return new FunctionDescriptorImpl(resLayout, argLayouts);
    }
    static FunctionDescriptor ofVoid(MemoryLayout... argLayouts) {
        return new FunctionDescriptorImpl(null, argLayouts);
    }
}
