package java.lang;
public class Throwable implements java.io.Serializable {
    private String detailMessage;
    private Throwable cause = this;
    private StackTraceElement[] stackTrace;
    private Object backtrace;
    private int btcount;

    public Throwable() { fillInStackTrace(); }
    public Throwable(String message) { fillInStackTrace(); detailMessage = message; }
    public Throwable(String message, Throwable cause) { fillInStackTrace(); detailMessage = message; this.cause = cause; }
    public Throwable(Throwable cause) {
        fillInStackTrace();
        this.cause = cause;
        detailMessage = (cause == null ? null : cause.toString());
    }

    public String getMessage() { return detailMessage; }
    public String getLocalizedMessage() { return getMessage(); }
    public Throwable getCause() { return (cause == this ? null : cause); }
    public synchronized Throwable initCause(Throwable cause) {
        if (this.cause != this) throw new IllegalStateException("Can't overwrite cause");
        if (cause == this) throw new IllegalArgumentException("Self-causation not permitted");
        this.cause = cause;
        return this;
    }
    public String toString() {
        String s = getClass().getName();
        String message = getLocalizedMessage();
        return (message != null) ? (s + ": " + message) : s;
    }
    public synchronized native Throwable fillInStackTrace();
    private native StackTraceElement[] getStackTrace0();
    public StackTraceElement[] getStackTrace() {
        if (stackTrace == null) stackTrace = getStackTrace0();
        return stackTrace;
    }
    public void setStackTrace(StackTraceElement[] st) { stackTrace = st; }
    public void printStackTrace() { printStackTrace(System.err); }
    public void printStackTrace(java.io.PrintStream s) {
        synchronized (s) {
            s.println(this);
            StackTraceElement[] trace = getStackTrace();
            for (int i = 0; i < trace.length; i++) s.println((char) 9 + "at " + trace[i]);
            Throwable c = getCause();
            if (c != null) c.printEnclosedStackTrace(s);
        }
    }
    private void printEnclosedStackTrace(java.io.PrintStream s) {
        s.println("Caused by: " + this);
        StackTraceElement[] trace = getStackTrace();
        for (int i = 0; i < trace.length; i++) s.println((char) 9 + "at " + trace[i]);
        Throwable c = getCause();
        if (c != null) c.printEnclosedStackTrace(s);
    }
    public final synchronized void addSuppressed(Throwable exception) {}
    public final synchronized Throwable[] getSuppressed() { return new Throwable[0]; }
}
