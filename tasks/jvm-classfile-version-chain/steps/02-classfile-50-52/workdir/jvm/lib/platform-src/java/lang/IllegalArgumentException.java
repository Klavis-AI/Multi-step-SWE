package java.lang;
public class IllegalArgumentException extends RuntimeException {
    public IllegalArgumentException() { super(); }
    public IllegalArgumentException(String s) { super(s); }
    public IllegalArgumentException(String s, Throwable c) { super(s, c); }
    public IllegalArgumentException(Throwable c) { super(c); }
}
