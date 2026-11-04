package java.lang.foreign;

/** The value layouts of the foreign function and memory API (JEP 454). */
public interface ValueLayout extends MemoryLayout {
    char kind();

    interface OfByte extends ValueLayout {}
    interface OfBoolean extends ValueLayout {}
    interface OfChar extends ValueLayout {}
    interface OfShort extends ValueLayout {}
    interface OfInt extends ValueLayout {}
    interface OfFloat extends ValueLayout {}
    interface OfLong extends ValueLayout {}
    interface OfDouble extends ValueLayout {}

    OfByte JAVA_BYTE = new LayoutImpl.ByteLayout();
    OfBoolean JAVA_BOOLEAN = new LayoutImpl.BooleanLayout();
    OfChar JAVA_CHAR = new LayoutImpl.CharLayout();
    OfShort JAVA_SHORT = new LayoutImpl.ShortLayout();
    OfInt JAVA_INT = new LayoutImpl.IntLayout();
    OfFloat JAVA_FLOAT = new LayoutImpl.FloatLayout();
    OfLong JAVA_LONG = new LayoutImpl.LongLayout();
    OfDouble JAVA_DOUBLE = new LayoutImpl.DoubleLayout();
    AddressLayout ADDRESS = new LayoutImpl.AddrLayout();
}
