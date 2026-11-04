package java.lang;
public class VirtualMachineError extends Error {
    public VirtualMachineError() { super(); }
    public VirtualMachineError(String s) { super(s); }
    public VirtualMachineError(String s, Throwable c) { super(s, c); }
    public VirtualMachineError(Throwable c) { super(c); }
}
