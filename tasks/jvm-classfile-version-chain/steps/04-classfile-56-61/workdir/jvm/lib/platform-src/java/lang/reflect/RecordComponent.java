package java.lang.reflect;
public final class RecordComponent {
    private String name;
    private Class<?> type;
    private Class<?> declaringRecord;
    private RecordComponent() {}
    public String getName() { return name; }
    public Class<?> getType() { return type; }
    public Class<?> getDeclaringRecord() { return declaringRecord; }
    public String toString() { return type.getName() + " " + name; }
}
