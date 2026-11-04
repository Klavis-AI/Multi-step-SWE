package java.lang.invoke;
/**
 * A typed, directly executable reference to a method, constructor or field.
 *
 * invokeExact and invoke are signature polymorphic (JVMS 2.9.3): the VM resolves
 * an invokevirtual of these names against any descriptor at the call site.
 */
public abstract class MethodHandle {
    MethodHandle() {}
    public final native Object invokeExact(Object... args) throws Throwable;
    public final native Object invoke(Object... args) throws Throwable;
    public native MethodType type();
    public native MethodHandle asType(MethodType newType);
    public native MethodHandle bindTo(Object x);
    public native MethodHandle asFixedArity();
    public native Object invokeWithArguments(Object... arguments) throws Throwable;
    public String toString() { return "MethodHandle" + type(); }
}
