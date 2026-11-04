package java.lang.invoke;
public abstract class CallSite {
    MethodHandle target;
    CallSite(MethodHandle target) { this.target = target; }
    public MethodType type() { return target.type(); }
    public MethodHandle getTarget() { return target; }
    public abstract void setTarget(MethodHandle newTarget);
    public MethodHandle dynamicInvoker() { return target; }
}
