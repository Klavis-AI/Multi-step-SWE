package java.lang;
public class RuntimeException extends Exception {
    public RuntimeException() { super(); }
    public RuntimeException(String s) { super(s); }
    public RuntimeException(String s, Throwable c) { super(s, c); }
    public RuntimeException(Throwable c) { super(c); }
}
