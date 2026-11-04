package java.lang.invoke;
/** VM-constructed implementation of MethodHandle. All fields are written by the VM. */
final class DirectMethodHandle extends MethodHandle {
    private long vmtarget;
    private MethodType mtype;
    private int kind;
    private Object bound;
    private Object extra;
    private DirectMethodHandle() {}
}
