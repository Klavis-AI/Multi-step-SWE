package java.lang;
public class Runtime {
    private static final Runtime current = new Runtime();
    private Runtime() {}
    public static Runtime getRuntime() { return current; }
    public native void gc();
    public native long freeMemory();
    public native long totalMemory();
    public native long maxMemory();
    public native int availableProcessors();
    public native void halt(int status);
    public void exit(int status) { halt(status); }
}
