package lua;

import java.util.ArrayList;
import java.util.List;

/** Small runtime types that have no better home. */
final class Core {}

/** A Lua error carrying an arbitrary Lua value. */
class LuaError extends RuntimeException {
    final Object value;
    String traceback;
    boolean handled;

    LuaError(Object value) {
        super(value instanceof LuaString ? value.toString() : "(error object)", null, false, false);
        this.value = value;
    }
}

/** Thrown to unwind a coroutine thread that is being discarded. */
class KillThread extends Error {
    KillThread() { super(null, null, false, false); }
}

/** A boxed local variable shared with closures. */
final class Upvalue {
    Object v;
    Upvalue() {}
    Upvalue(Object v) { this.v = v; }
}

/** Full userdata. Lua 5.1 has no __gc on tables, so finalisers live here. */
class Userdata {
    LuaTable metatable;
    Object data;                 // implementation payload (a file handle, ...)
    Userdata(Object data) { this.data = data; }
}

abstract class LuaFunction {
    abstract Object[] call(Object[] args);
    String name() { return "?"; }
}

/** A library function implemented in Java. */
abstract class Builtin extends LuaFunction {
    String name;
    /** true when this builtin may call back into Lua in a context that allows yields. */
    boolean yieldable;
    Builtin(String name) { this.name = name; }
    @Override String name() { return name; }
    @Override public String toString() { return "builtin " + name; }
}

/** Compiled function prototype produced by the parser. */
final class Proto {
    String source;              // raw chunk name, as debug.getinfo reports it
    String shortSrc;            // luaO_chunkid form, used in messages
    int lineDefined;
    int lastLineDefined;
    int numParams;
    boolean isVararg;
    int numSlots;               // frame size
    boolean[] boxed;            // slot needs an Upvalue cell
    Stat[] body;
    UpvalDesc[] upvals;
    String[] slotNames;         // for debug.getlocal / error messages
    String name;                // best-effort name for tracebacks
    int argSlot = -1;           // LUA_COMPAT_VARARG: slot of the implicit 'arg'
    boolean needsArg;
}

final class UpvalDesc {
    final String name;
    final boolean fromParent;   // capture parent's slot, else parent's upvalue
    final int index;
    UpvalDesc(String name, boolean fromParent, int index) {
        this.name = name; this.fromParent = fromParent; this.index = index;
    }
}

final class LuaClosure extends LuaFunction {
    final Proto p;
    final Upvalue[] upvals;
    LuaTable env;               // 5.1 function environment

    LuaClosure(Proto p, Upvalue[] upvals, LuaTable env) {
        this.p = p; this.upvals = upvals; this.env = env;
        Gc.alloc(32 + 8 * upvals.length);
    }

    @Override Object[] call(Object[] args) { return Interp.callClosure(this, args); }

    @Override String name() { return p.name == null ? "?" : p.name; }
}

/** One activation record: a Lua call when cl is set, otherwise a library function. */
final class Frame {
    LuaClosure cl;
    String cname;               // library function name, when cl == null
    String callName;            // how the callee was named at the call site
    Object[] slots;
    Object[] varargs = Interp.EMPTY;
    Object[] ret;
    boolean hasTail;            // a `return f(...)` statement is pending
    Object tailFunc;
    Object[] tailArgs;
    String tailName;
    String tailKind;
    int tailcalls;              // frames this one replaced
    int line;
    Frame prev;
    /** Extra GC roots: values in flight that live only in Java locals. */
    ArrayList<Object> temps;

    Frame(LuaClosure cl) {
        this.cl = cl;
        this.slots = new Object[cl.p.numSlots];
    }

    Frame(String cname) {
        this.cname = cname;
        this.slots = Interp.EMPTY;
    }

    boolean isC() { return cl == null; }

    void pushTemp(Object o) {
        if (temps == null) temps = new ArrayList<>(4);
        temps.add(o);
    }

    void popTemp() { temps.remove(temps.size() - 1); }
}
