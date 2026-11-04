package java.lang.foreign;
final class FunctionDescriptorImpl implements FunctionDescriptor {
    final MemoryLayout ret;
    final MemoryLayout[] args;
    FunctionDescriptorImpl(MemoryLayout ret, MemoryLayout[] args) { this.ret = ret; this.args = args; }
    public String toString() {
        StringBuilder sb = new StringBuilder("(");
        for (int i = 0; i < args.length; i++) sb.append(args[i]);
        return sb.append(')').append(ret == null ? "v" : ret.toString()).toString();
    }
}
