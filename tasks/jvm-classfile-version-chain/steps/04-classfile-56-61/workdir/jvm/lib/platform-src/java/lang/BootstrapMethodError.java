package java.lang;
public class BootstrapMethodError extends LinkageError {
    public BootstrapMethodError() { super(); }
    public BootstrapMethodError(String s) { super(s); }
    public BootstrapMethodError(String s, Throwable c) { super(s, c); }
    public BootstrapMethodError(Throwable c) { super(c); }
}
