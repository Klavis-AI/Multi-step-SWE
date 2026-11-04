package java.lang.foreign;

/** Implementations of the value layouts. The virtual machine reads `kind`. */
final class LayoutImpl {
    private LayoutImpl() {}
    static class Base { char kind; long size; }
    static final class ByteLayout implements ValueLayout.OfByte {
        char kind = 'B';
        public char kind() { return kind; }
        public long byteSize() { return 1; }
        public String toString() { return "b1"; }
    }
    static final class BooleanLayout implements ValueLayout.OfBoolean {
        char kind = 'Z';
        public char kind() { return kind; }
        public long byteSize() { return 1; }
        public String toString() { return "z1"; }
    }
    static final class CharLayout implements ValueLayout.OfChar {
        char kind = 'C';
        public char kind() { return kind; }
        public long byteSize() { return 2; }
        public String toString() { return "c2"; }
    }
    static final class ShortLayout implements ValueLayout.OfShort {
        char kind = 'S';
        public char kind() { return kind; }
        public long byteSize() { return 2; }
        public String toString() { return "s2"; }
    }
    static final class IntLayout implements ValueLayout.OfInt {
        char kind = 'I';
        public char kind() { return kind; }
        public long byteSize() { return 4; }
        public String toString() { return "i4"; }
    }
    static final class FloatLayout implements ValueLayout.OfFloat {
        char kind = 'F';
        public char kind() { return kind; }
        public long byteSize() { return 4; }
        public String toString() { return "f4"; }
    }
    static final class LongLayout implements ValueLayout.OfLong {
        char kind = 'J';
        public char kind() { return kind; }
        public long byteSize() { return 8; }
        public String toString() { return "j8"; }
    }
    static final class DoubleLayout implements ValueLayout.OfDouble {
        char kind = 'D';
        public char kind() { return kind; }
        public long byteSize() { return 8; }
        public String toString() { return "d8"; }
    }
    static final class AddrLayout implements AddressLayout {
        char kind = 'P';
        public char kind() { return kind; }
        public long byteSize() { return 8; }
        public String toString() { return "a8"; }
    }
}
