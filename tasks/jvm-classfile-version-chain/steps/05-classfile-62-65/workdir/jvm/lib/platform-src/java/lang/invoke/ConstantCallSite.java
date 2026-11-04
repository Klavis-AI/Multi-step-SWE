package java.lang.invoke;
public class ConstantCallSite extends CallSite {
    public ConstantCallSite(MethodHandle target) { super(target); }
    public final void setTarget(MethodHandle ignore) { throw new UnsupportedOperationException("ConstantCallSite"); }
    public final MethodHandle dynamicInvoker() { return getTarget(); }
}
