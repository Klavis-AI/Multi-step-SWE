package java.lang;

/**
 * The root of the class hierarchy (Java SE API: java.lang.Object).
 *
 * The five {@code native} methods below are the VM's responsibility.
 */
public class Object {
    public Object() {}

    /** Returns the runtime class of this object. */
    public final native Class<?> getClass();

    /** Identity hash code unless overridden; stable for the object's lifetime. */
    public native int hashCode();

    public boolean equals(Object obj) { return this == obj; }

    /** Field-for-field copy; throws CloneNotSupportedException unless Cloneable. */
    protected native Object clone() throws CloneNotSupportedException;

    public String toString() {
        return getClass().getName() + "@" + Integer.toHexString(hashCode());
    }

    /** Wakes one thread waiting on this object's monitor. IllegalMonitorStateException if not owned. */
    public final native void notify();

    /** Wakes all threads waiting on this object's monitor. */
    public final native void notifyAll();

    /** Waits until notified or timeout (ms; 0 = forever). Releases the monitor. */
    public final native void wait(long timeoutMillis) throws InterruptedException;

    public final void wait() throws InterruptedException { wait(0L); }

    public final void wait(long timeoutMillis, int nanos) throws InterruptedException {
        if (timeoutMillis < 0) throw new IllegalArgumentException("timeoutMillis value is negative");
        if (nanos < 0 || nanos > 999999) throw new IllegalArgumentException("nanosecond timeout value out of range");
        if (nanos > 0 && timeoutMillis < Long.MAX_VALUE) timeoutMillis++;
        wait(timeoutMillis);
    }

    protected void finalize() throws Throwable {}
}
