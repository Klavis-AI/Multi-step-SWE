package java.lang;
public class IllegalStateException extends RuntimeException {
    public IllegalStateException() { super(); }
    public IllegalStateException(String s) { super(s); }
    public IllegalStateException(String s, Throwable c) { super(s, c); }
    public IllegalStateException(Throwable c) { super(c); }
}
