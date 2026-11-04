package java.lang;
public class ExceptionInInitializerError extends LinkageError {
    public ExceptionInInitializerError() { super(); }
    public ExceptionInInitializerError(String s) { super(s); }
    public ExceptionInInitializerError(String s, Throwable c) { super(s, c); }
    public ExceptionInInitializerError(Throwable c) { super(c); }
}
