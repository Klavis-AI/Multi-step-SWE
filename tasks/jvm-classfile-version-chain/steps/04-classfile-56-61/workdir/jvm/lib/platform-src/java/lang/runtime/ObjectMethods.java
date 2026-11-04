package java.lang.runtime;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.TypeDescriptor;
public final class ObjectMethods {
    private ObjectMethods() {}
    public static native Object bootstrap(MethodHandles.Lookup lookup, String methodName, TypeDescriptor type,
                                          Class<?> recordClass, String names, MethodHandle... getters) throws Throwable;
}
