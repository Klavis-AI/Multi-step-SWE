package java.lang.reflect;
public class Modifier {
    public static final int PUBLIC = 0x1, PRIVATE = 0x2, PROTECTED = 0x4, STATIC = 0x8;
    public static final int FINAL = 0x10, SYNCHRONIZED = 0x20, VOLATILE = 0x40, TRANSIENT = 0x80;
    public static final int NATIVE = 0x100, INTERFACE = 0x200, ABSTRACT = 0x400, STRICT = 0x800;
    public static boolean isPublic(int m) { return (m & PUBLIC) != 0; }
    public static boolean isPrivate(int m) { return (m & PRIVATE) != 0; }
    public static boolean isProtected(int m) { return (m & PROTECTED) != 0; }
    public static boolean isStatic(int m) { return (m & STATIC) != 0; }
    public static boolean isFinal(int m) { return (m & FINAL) != 0; }
    public static boolean isAbstract(int m) { return (m & ABSTRACT) != 0; }
    public static boolean isInterface(int m) { return (m & INTERFACE) != 0; }
}
