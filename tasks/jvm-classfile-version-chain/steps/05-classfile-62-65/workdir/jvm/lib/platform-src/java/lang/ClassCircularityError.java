package java.lang;
public class ClassCircularityError extends LinkageError {
    public ClassCircularityError() { super(); }
    public ClassCircularityError(String s) { super(s); }
}
