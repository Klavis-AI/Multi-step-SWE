package java.lang;
public class Exception extends Throwable {
    public Exception() { super(); }
    public Exception(String s) { super(s); }
    public Exception(String s, Throwable c) { super(s, c); }
    public Exception(Throwable c) { super(c); }
}
