package java.lang.invoke;
public final class StringConcatFactory {
    private StringConcatFactory() {}
    public static native CallSite makeConcat(MethodHandles.Lookup lookup, String name, MethodType concatType) throws StringConcatException;
    public static native CallSite makeConcatWithConstants(MethodHandles.Lookup lookup, String name, MethodType concatType,
                                                          String recipe, Object... constants) throws StringConcatException;
}
