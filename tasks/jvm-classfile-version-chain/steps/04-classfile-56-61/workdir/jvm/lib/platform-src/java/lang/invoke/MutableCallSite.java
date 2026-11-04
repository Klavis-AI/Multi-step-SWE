package java.lang.invoke;
public class MutableCallSite extends CallSite {
    public MutableCallSite(MethodHandle target) { super(target); }
    public MutableCallSite(MethodType type) { super(null); }
    public void setTarget(MethodHandle newTarget) { this.target = newTarget; }
}
