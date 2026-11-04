package java.lang;
public class NoClassDefFoundError extends LinkageError {
    public NoClassDefFoundError() { super(); }
    public NoClassDefFoundError(String s) { super(s); }
    public NoClassDefFoundError(String s, Throwable c) { super(s, c); }
    public NoClassDefFoundError(Throwable c) { super(c); }
}
