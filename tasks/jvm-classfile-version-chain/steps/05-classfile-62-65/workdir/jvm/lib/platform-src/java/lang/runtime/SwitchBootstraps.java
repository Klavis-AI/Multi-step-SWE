package java.lang.runtime;
import java.lang.invoke.CallSite;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
public final class SwitchBootstraps {
    private SwitchBootstraps() {}
    public static native CallSite typeSwitch(MethodHandles.Lookup lookup, String invocationName, MethodType invocationType, Object... labels);
    public static native CallSite enumSwitch(MethodHandles.Lookup lookup, String invocationName, MethodType invocationType, Object... labels);
}
