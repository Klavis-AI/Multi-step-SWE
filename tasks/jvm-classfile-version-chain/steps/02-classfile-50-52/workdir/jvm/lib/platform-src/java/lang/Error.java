package java.lang;
public class Error extends Throwable {
    public Error() { super(); }
    public Error(String s) { super(s); }
    public Error(String s, Throwable c) { super(s, c); }
    public Error(Throwable c) { super(c); }
}
