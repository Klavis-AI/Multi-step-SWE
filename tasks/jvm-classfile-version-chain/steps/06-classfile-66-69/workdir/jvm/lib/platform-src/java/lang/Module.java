package java.lang;
/** java.lang.Module. Instances are created only by the VM. */
public final class Module {
    private long vmmodule;
    private Module() {}
    private native String getName0();
    public String getName() { return getName0(); }
    public native boolean isNamed();
    public native boolean isExported(String pn);
    public native boolean isExported(String pn, Module other);
    public native boolean isOpen(String pn, Module other);
    public native boolean canRead(Module other);
    public native ClassLoader getClassLoader();
    public String toString() { return isNamed() ? "module " + getName() : "unnamed module"; }
}
