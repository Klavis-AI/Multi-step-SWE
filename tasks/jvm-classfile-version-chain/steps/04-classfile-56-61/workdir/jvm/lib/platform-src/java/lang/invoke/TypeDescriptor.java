package java.lang.invoke;
public interface TypeDescriptor {
    String descriptorString();
    public interface OfField<F extends TypeDescriptor.OfField<F>> extends TypeDescriptor {}
    public interface OfMethod<F extends TypeDescriptor.OfField<F>, M extends TypeDescriptor.OfMethod<F, M>> extends TypeDescriptor {}
}
