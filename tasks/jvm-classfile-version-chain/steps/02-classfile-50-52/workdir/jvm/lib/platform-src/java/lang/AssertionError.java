package java.lang;
public class AssertionError extends Error {
    public AssertionError() { super(); }
    public AssertionError(String s) { super(s); }
    public AssertionError(String s, Throwable c) { super(s, c); }
    public AssertionError(Throwable c) { super(c); }
}
