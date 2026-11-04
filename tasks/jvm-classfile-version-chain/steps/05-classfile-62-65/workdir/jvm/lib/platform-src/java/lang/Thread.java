package java.lang;
public class Thread implements Runnable {
    public static final int MIN_PRIORITY = 1;
    public static final int NORM_PRIORITY = 5;
    public static final int MAX_PRIORITY = 10;

    private volatile String name;
    private int priority = NORM_PRIORITY;
    private boolean daemon;
    private Runnable target;
    private long eetop;          // VM thread handle
    private long tid;
    private boolean started;
    private boolean virtual;
    private long contptr;
    private volatile boolean interrupted;
    private static int threadSeq;
    private static long tidSeq = 1;

    public Thread() { this(null, null); }
    public Thread(Runnable target) { this(target, null); }
    public Thread(String name) { this(null, name); }
    public Thread(Runnable target, String name) {
        this.target = target;
        synchronized (Thread.class) {
            this.name = (name != null) ? name : ("Thread-" + threadSeq++);
            this.tid = tidSeq++;
        }
        Thread parent = currentThread();
        if (parent != null) { this.priority = parent.priority; this.daemon = parent.daemon; }
    }
    Thread(Runnable target, String name, boolean virtual) { this(target, name); this.virtual = virtual; }

    public static native Thread currentThread();
    public static native void yield();
    public static void sleep(long millis) throws InterruptedException {
        if (millis < 0) throw new IllegalArgumentException("timeout value is negative");
        if (inVirtualThread0() && parkCurrent0(millis)) return;
        sleep0(millis);
    }
    private static native void sleep0(long millis) throws InterruptedException;
    public static void sleep(long millis, int nanos) throws InterruptedException { sleep(millis); }
    public static void onSpinWait() {}

    private native void start0();
    public synchronized void start() {
        if (started) throw new IllegalThreadStateException();
        started = true;
        if (virtual) { startVirtual0(); return; }
        start0();
    }
    public void run() { if (target != null) target.run(); }
    public final String getName() { return name; }
    public final void setName(String name) { this.name = name; }
    public final int getPriority() { return priority; }
    public final void setPriority(int newPriority) {
        if (newPriority > MAX_PRIORITY || newPriority < MIN_PRIORITY) throw new IllegalArgumentException();
        priority = newPriority;
    }
    public final boolean isDaemon() { return daemon; }
    public final void setDaemon(boolean on) {
        if (started) throw new IllegalThreadStateException();
        daemon = on;
    }
    public long getId() { return tid; }
    public final long threadId() { return tid; }
    public final boolean isVirtual() { return virtual; }
    private native void startVirtual0();
    private static native boolean parkCurrent0(long millis);
    private static native boolean inVirtualThread0();

    /** Thread.Builder for virtual threads (JEP 444). */
    public static Builder.OfVirtual ofVirtual() { return new VirtualBuilder(); }
    public static Builder.OfPlatform ofPlatform() { return new PlatformBuilder(); }

    public static Thread startVirtualThread(Runnable task) {
        Thread t = new Thread(task, null, true);
        t.start();
        return t;
    }

    public interface Builder {
        Builder name(String name);
        Thread unstarted(Runnable task);
        Thread start(Runnable task);
        interface OfVirtual extends Builder {
            OfVirtual name(String name);
            Thread unstarted(Runnable task);
            Thread start(Runnable task);
        }
        interface OfPlatform extends Builder {
            OfPlatform name(String name);
            Thread unstarted(Runnable task);
            Thread start(Runnable task);
        }
    }

    static final class VirtualBuilder implements Builder.OfVirtual {
        private String name;
        public Builder.OfVirtual name(String n) { name = n; return this; }
        public Thread unstarted(Runnable task) { return new Thread(task, name, true); }
        public Thread start(Runnable task) { Thread t = unstarted(task); t.start(); return t; }
    }
    static final class PlatformBuilder implements Builder.OfPlatform {
        private String name;
        public Builder.OfPlatform name(String n) { name = n; return this; }
        public Thread unstarted(Runnable task) { return new Thread(task, name); }
        public Thread start(Runnable task) { Thread t = unstarted(task); t.start(); return t; }
    }
    public native boolean isAlive();
    public native void interrupt();
    public native boolean isInterrupted();
    public static native boolean interrupted();
    public final synchronized void join(long millis) throws InterruptedException {
        long base = System.currentTimeMillis();
        if (millis == 0) { while (isAlive()) wait(0); }
        else {
            while (isAlive()) {
                long delay = millis - (System.currentTimeMillis() - base);
                if (delay <= 0) break;
                wait(delay);
            }
        }
    }
    public final void join() throws InterruptedException { join(0); }
    public String toString() { return "Thread[" + getName() + "," + getPriority() + ",main]"; }
    public StackTraceElement[] getStackTrace() { return new StackTraceElement[0]; }
}
