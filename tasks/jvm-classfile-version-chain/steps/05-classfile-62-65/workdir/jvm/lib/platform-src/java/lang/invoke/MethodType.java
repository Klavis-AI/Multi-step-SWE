package java.lang.invoke;
public final class MethodType implements java.io.Serializable, TypeDescriptor.OfMethod<Class<?>, MethodType> {
    private Class<?> rtype;
    private Class<?>[] ptypes;
    private MethodType() {}
    private MethodType(Class<?> rtype, Class<?>[] ptypes) { this.rtype = rtype; this.ptypes = ptypes; }

    public static MethodType methodType(Class<?> rtype, Class<?>[] ptypes) {
        return new MethodType(rtype, ptypes == null ? new Class<?>[0] : java.util.Arrays.copyOf(ptypes, ptypes.length));
    }
    public static MethodType methodType(Class<?> rtype) { return new MethodType(rtype, new Class<?>[0]); }
    public static MethodType methodType(Class<?> rtype, Class<?> p0) { return new MethodType(rtype, new Class<?>[] { p0 }); }
    public static MethodType methodType(Class<?> rtype, Class<?> p0, Class<?>... rest) {
        Class<?>[] p = new Class<?>[1 + rest.length];
        p[0] = p0;
        System.arraycopy(rest, 0, p, 1, rest.length);
        return new MethodType(rtype, p);
    }
    public static MethodType methodType(Class<?> rtype, java.util.List<Class<?>> ptypes) {
        Class<?>[] p = new Class<?>[ptypes.size()];
        for (int i = 0; i < p.length; i++) p[i] = ptypes.get(i);
        return new MethodType(rtype, p);
    }
    public static MethodType genericMethodType(int objectArgCount) {
        Class<?>[] p = new Class<?>[objectArgCount];
        java.util.Arrays.fill(p, Object.class);
        return new MethodType(Object.class, p);
    }
    public static native MethodType fromMethodDescriptorString(String descriptor, ClassLoader loader);

    public Class<?> returnType() { return rtype; }
    public int parameterCount() { return ptypes.length; }
    public Class<?> parameterType(int num) { return ptypes[num]; }
    public Class<?>[] parameterArray() { return java.util.Arrays.copyOf(ptypes, ptypes.length); }
    public java.util.List<Class<?>> parameterList() { return java.util.Arrays.asList(parameterArray()); }
    public MethodType changeReturnType(Class<?> nrtype) { return new MethodType(nrtype, ptypes); }
    public MethodType changeParameterType(int num, Class<?> nptype) {
        Class<?>[] p = parameterArray();
        p[num] = nptype;
        return new MethodType(rtype, p);
    }
    public MethodType dropParameterTypes(int start, int end) {
        Class<?>[] p = new Class<?>[ptypes.length - (end - start)];
        int k = 0;
        for (int i = 0; i < ptypes.length; i++) if (i < start || i >= end) p[k++] = ptypes[i];
        return new MethodType(rtype, p);
    }
    public MethodType insertParameterTypes(int num, Class<?>... ptypesToInsert) {
        Class<?>[] p = new Class<?>[ptypes.length + ptypesToInsert.length];
        System.arraycopy(ptypes, 0, p, 0, num);
        System.arraycopy(ptypesToInsert, 0, p, num, ptypesToInsert.length);
        System.arraycopy(ptypes, num, p, num + ptypesToInsert.length, ptypes.length - num);
        return new MethodType(rtype, p);
    }
    public String descriptorString() { return toMethodDescriptorString(); }
    public native String toMethodDescriptorString();
    public boolean equals(Object x) {
        if (!(x instanceof MethodType)) return false;
        MethodType t = (MethodType) x;
        if (t.rtype != rtype || t.ptypes.length != ptypes.length) return false;
        for (int i = 0; i < ptypes.length; i++) if (t.ptypes[i] != ptypes[i]) return false;
        return true;
    }
    public int hashCode() {
        int h = 31 + rtype.hashCode();
        for (int i = 0; i < ptypes.length; i++) h = 31 * h + ptypes[i].hashCode();
        return h;
    }
    public String toString() {
        StringBuilder sb = new StringBuilder("(");
        for (int i = 0; i < ptypes.length; i++) {
            if (i > 0) sb.append(',');
            sb.append(ptypes[i].getSimpleName());
        }
        sb.append(')').append(rtype.getSimpleName());
        return sb.toString();
    }
}
