package java.lang;

import java.io.PrintStream;
import java.io.FileOutputStream;
import java.util.Properties;

public final class System {
    private System() {}

    public static final java.io.InputStream in = null;
    public static final PrintStream out;
    public static final PrintStream err;

    private static Properties props;

    static {
        props = new Properties();
        initProperties(props);
        out = new PrintStream(new FileOutputStream(1), true);
        err = new PrintStream(new FileOutputStream(2), true);
    }

    /** The VM fills in the initial system properties (java.version, file.encoding, ... and -D values). */
    private static native void initProperties(Properties p);

    public static native void arraycopy(Object src, int srcPos, Object dest, int destPos, int length);
    public static native long currentTimeMillis();
    public static native long nanoTime();
    public static native int identityHashCode(Object x);

    public static String getProperty(String key) { return props.getProperty(key); }
    public static String getProperty(String key, String def) {
        String v = props.getProperty(key);
        return v == null ? def : v;
    }
    public static String setProperty(String key, String value) { return (String) props.setProperty(key, value); }
    public static Properties getProperties() { return props; }
    public static String lineSeparator() { return getProperty("line.separator", "\n"); }
    public static void exit(int status) { Runtime.getRuntime().exit(status); }
    public static void gc() { Runtime.getRuntime().gc(); }
    public static String getenv(String name) { return null; }

    /** True when the default charset is UTF-8. */
    static boolean stdoutEncodingIsUTF8() {
        String e = getProperty("file.encoding", "UTF-8");
        return e.equalsIgnoreCase("UTF-8") || e.equalsIgnoreCase("UTF8");
    }

    /** True when a standard stream encodes as UTF-8. */
    public static boolean streamEncodingIsUTF8() {
        String e = getProperty("stdout.encoding", getProperty("file.encoding", "UTF-8"));
        return e.equalsIgnoreCase("UTF-8") || e.equalsIgnoreCase("UTF8");
    }
}
