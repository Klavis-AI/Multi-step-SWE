package lua;

import java.util.List;

/** Expression nodes. Each knows how to evaluate itself against a frame. */
abstract class Expr {
    int line;
    abstract Object eval(Frame f);
    Object[] evalMulti(Frame f) { return new Object[]{eval(f)}; }
    boolean multi() { return false; }
    String kind() { return null; }
    String kindName() { return null; }
    /** The name this expression has when its value must live in a register. */
    String stackKind() { return kind(); }
    String stackName() { return kindName(); }
}

/** Statement nodes. exec returns a control signal. */
abstract class Stat {
    static final int NORMAL = 0, BREAK = 1, RETURN = 2, GOTO = 3;
    int line;
    abstract int exec(Frame f);
}

/** Statements plus the frame slots their locals occupy and the labels they define. */
final class Blk {
    final Stat[] stats;
    final int from, to;
    java.util.HashMap<String, Integer> labels;
    int[] tbc;                       // slots of this block's to-be-closed variables

    Blk(Stat[] stats, int from, int to) {
        this.stats = stats;
        this.from = from;
        this.to = to;
        for (int i = 0; i < stats.length; i++) {
            if (stats[i] instanceof LabelStat) {
                if (labels == null) labels = new java.util.HashMap<>(4);
                labels.put(((LabelStat) stats[i]).name, i);
            }
        }
    }

    int run(Frame f) {
        if (tbc != null) return runClosing(f);
        if (to == from) return runOpen(f);
        try {
            return runOpen(f);
        } finally {
            for (int i = from; i < to; i++) f.slots[i] = null;
        }
    }

    /** Runs the block and closes its to-be-closed variables, in reverse order. */
    private int runClosing(Frame f) {
        int r = Stat.NORMAL;
        LuaError err = null;
        Coroutine owner = Interp.current;
        int mark = owner.tbc.size();
        owner.tbcFrames.add(f);
        owner.tbcBlocks.add(this);
        try {
            r = runOpen(f);
        } catch (LuaError e) {
            err = e;
        }
        owner.tbcFrames.remove(owner.tbcFrames.size() - 1);
        owner.tbcBlocks.remove(owner.tbcBlocks.size() - 1);
        while (owner.tbc.size() > mark) owner.tbc.remove(owner.tbc.size() - 1);
        for (int i = tbc.length - 1; i >= 0; i--) {
            Object v = f.slots[tbc[i]];
            if (v instanceof Upvalue) v = ((Upvalue) v).v;
            if (v == null || v == Boolean.FALSE) continue;
            Object h = Interp.metamethod(v, "__close");
            if (h == null) continue;
            try {
                Interp.callFromC("close", h, new Object[]{v, err == null ? null : err.value});
            } catch (LuaError e2) {
                err = e2;
            }
        }
        for (int i = from; i < to; i++) f.slots[i] = null;
        if (err != null) throw err;
        return r;
    }

    /** Runs the body without releasing the slots; `repeat` needs them for its condition. */
    int runOpen(Frame f) {
        int i = 0;
        while (i < stats.length) {
            if (Gc.due()) Gc.step();
            int r = stats[i].exec(f);
            if (r == Stat.NORMAL) { i++; continue; }
            if (r == Stat.GOTO && labels != null) {
                Integer t = labels.get(f.gotoLabel);
                if (t != null) { f.gotoLabel = null; i = t; continue; }
            }
            return r;
        }
        return Stat.NORMAL;
    }

    void release(Frame f) {
        for (int i = from; i < to; i++) f.slots[i] = null;
    }
}

final class LabelStat extends Stat {
    final String name;
    LabelStat(String name) { this.name = name; }
    @Override int exec(Frame f) { return NORMAL; }
}

final class GotoStat extends Stat {
    final String name;
    GotoStat(String name) { this.name = name; }
    @Override int exec(Frame f) {
        f.gotoLabel = name;
        return GOTO;
    }
}

final class VarInfo {
    static final int NONE = 0, CONST = 1, CLOSE = 2;
    final String name;
    int slot;
    boolean boxed;
    int attrib = NONE;
    VarInfo(String name, int slot) { this.name = name; this.slot = slot; }
}

// ------------------------------------------------------------------ expressions

final class ConstExpr extends Expr {
    final Object v;
    ConstExpr(Object v) { this.v = v; }
    @Override Object eval(Frame f) { return v; }
    // A constant is named only where the operation needs it in a register:
    // as the target of a call or of an indexing, not as an RK operand.
    @Override String stackKind() { return (v instanceof LuaString) ? "constant" : null; }
    @Override String stackName() { return (v instanceof LuaString) ? v.toString() : null; }
}

final class VarargExpr extends Expr {
    @Override Object eval(Frame f) { return f.varargs.length > 0 ? f.varargs[0] : null; }
    @Override Object[] evalMulti(Frame f) { return f.varargs; }
    @Override boolean multi() { return true; }
}

final class LocalExpr extends Expr {
    final VarInfo v;
    LocalExpr(VarInfo v) { this.v = v; }
    @Override Object eval(Frame f) {
        Object o = f.slots[v.slot];
        return v.boxed ? ((Upvalue) o).v : o;
    }
    @Override String kind() { return "local"; }
    @Override String kindName() { return v.name; }
}

final class UpvalExpr extends Expr {
    final int idx;
    final String name;
    UpvalExpr(int idx, String name) { this.idx = idx; this.name = name; }
    @Override Object eval(Frame f) { return f.cl.upvals[idx].v; }
    @Override String kind() { return "upvalue"; }
    @Override String kindName() { return name; }
}

/** A free name: `x` is `_ENV.x`, and diagnostics still call it a global. */
final class GlobalExpr extends Expr {
    final Expr env;
    final LuaString name;
    final String sname;
    GlobalExpr(Expr env, String n) { this.env = env; this.sname = n; this.name = LuaString.of(n); }
    @Override Object eval(Frame f) {
        Object t = env.eval(f);
        f.line = line;
        return Interp.index(t, name, env.kind(), env.kindName());
    }
    @Override String kind() { return "global"; }
    @Override String kindName() { return sname; }
}

final class IndexExpr extends Expr {
    final Expr obj, key;
    final String fieldName;                  // constant string key, for messages
    IndexExpr(Expr obj, Expr key) {
        this.obj = obj; this.key = key;
        this.fieldName = (key instanceof ConstExpr && ((ConstExpr) key).v instanceof LuaString)
                ? ((ConstExpr) key).v.toString() : "?";
    }
    @Override Object eval(Frame f) {
        Object o = obj.eval(f);
        Object k = key.eval(f);
        f.line = line;
        return Interp.index(o, k, obj.stackKind(), obj.stackName());
    }
    @Override String kind() { return "field"; }
    @Override String kindName() { return fieldName; }
}

final class CallExpr extends Expr {
    final Expr fn;
    final Expr[] args;
    CallExpr(Expr fn, Expr[] args) { this.fn = fn; this.args = args; }
    @Override Object eval(Frame f) { return Interp.first(evalMulti(f)); }
    @Override boolean multi() { return true; }
    @Override Object[] evalMulti(Frame f) {
        Object func = fn.eval(f);
        Object[] a = Parser.evalArgs(args, f);
        f.line = line;
        return Interp.callDirect(func, a, fn.stackKind(), fn.stackName());
    }
}

final class MethodCallExpr extends Expr {
    final Expr obj;
    final LuaString name;
    final String sname;
    final Expr[] args;
    MethodCallExpr(Expr obj, String name, Expr[] args) {
        this.obj = obj; this.sname = name; this.name = LuaString.of(name); this.args = args;
    }
    @Override Object eval(Frame f) { return Interp.first(evalMulti(f)); }
    @Override boolean multi() { return true; }
    @Override Object[] evalMulti(Frame f) {
        Object self = obj.eval(f);
        f.line = line;
        Object func = Interp.index(self, name, obj.stackKind(), obj.stackName());
        Object[] a = Parser.evalArgs(args, f);
        Object[] a2 = new Object[a.length + 1];
        a2[0] = self;
        System.arraycopy(a, 0, a2, 1, a.length);
        f.line = line;
        return Interp.callDirect(func, a2, "method", sname);
    }
}

final class ParenExpr extends Expr {
    final Expr e;
    ParenExpr(Expr e) { this.e = e; }
    @Override Object eval(Frame f) { return e.eval(f); }
    @Override String kind() { return e.kind(); }
    @Override String kindName() { return e.kindName(); }
}

final class FuncExpr extends Expr {
    final Proto p;
    FuncExpr(Proto p) { this.p = p; }
    @Override Object eval(Frame f) {
        Upvalue[] ups = new Upvalue[p.upvals.length];
        for (int i = 0; i < ups.length; i++) {
            UpvalDesc d = p.upvals[i];
            ups[i] = d.fromParent ? (Upvalue) f.slots[d.index] : f.cl.upvals[d.index];
        }
        return new LuaClosure(p, ups);
    }
}

final class BinExpr extends Expr {
    final String op, event;
    final Expr l, r;
    BinExpr(String op, Expr l, Expr r) {
        this.op = op; this.l = l; this.r = r;
        switch (op) {
            case "+": event = "__add"; break;
            case "-": event = "__sub"; break;
            case "*": event = "__mul"; break;
            case "/": event = "__div"; break;
            case "%": event = "__mod"; break;
            case "^": event = "__pow"; break;
            case "//": event = "__idiv"; break;
            case "&": event = "__band"; break;
            case "|": event = "__bor"; break;
            case "~": event = "__bxor"; break;
            case "<<": event = "__shl"; break;
            case ">>": event = "__shr"; break;
            default: event = null;
        }
    }
    @Override Object eval(Frame f) {
        Object a = l.eval(f);
        Object b;
        if (a instanceof LuaTable || a instanceof LuaClosure) {
            f.pushTemp(a);
            try { b = r.eval(f); } finally { f.popTemp(); }
        } else {
            b = r.eval(f);
        }
        f.line = line;
        switch (op) {
            case "..": return Interp.concat(a, b, l, r);
            case "==": return Interp.equals(a, b);
            case "~=": return !Interp.equals(a, b);
            case "<": return Interp.lessThan(a, b);
            case "<=": return Interp.lessEq(a, b);
            case ">": return Interp.lessThan(b, a);
            case ">=": return Interp.lessEq(b, a);
            default:
                if (LuaNum.isNum(a) && LuaNum.isNum(b) && event.charAt(2) != 'b'
                        && !event.equals("__shl") && !event.equals("__shr")) {
                    return Interp.rawArith(op, a, b);
                }
                return Interp.arith(op, event, a, b, l, r);
        }
    }
}

final class AndExpr extends Expr {
    final Expr l, r;
    AndExpr(Expr l, Expr r) { this.l = l; this.r = r; }
    @Override Object eval(Frame f) {
        Object a = l.eval(f);
        return Interp.truthy(a) ? r.eval(f) : a;
    }
}

final class OrExpr extends Expr {
    final Expr l, r;
    OrExpr(Expr l, Expr r) { this.l = l; this.r = r; }
    @Override Object eval(Frame f) {
        Object a = l.eval(f);
        return Interp.truthy(a) ? a : r.eval(f);
    }
}

final class NotExpr extends Expr {
    final Expr e;
    NotExpr(Expr e) { this.e = e; }
    @Override Object eval(Frame f) { return !Interp.truthy(e.eval(f)); }
}

final class BnotExpr extends Expr {
    final Expr e;
    BnotExpr(Expr e) { this.e = e; }
    @Override Object eval(Frame f) {
        Object a = e.eval(f);
        f.line = line;
        return Interp.bnot(a, e);
    }
}

final class UnmExpr extends Expr {
    final Expr e;
    UnmExpr(Expr e) { this.e = e; }
    @Override Object eval(Frame f) {
        Object a = e.eval(f);
        f.line = line;
        if (a instanceof Long) return -(Long) a;
        if (a instanceof Double) return -(Double) a;
        return Interp.unm(a, e);
    }
}

final class LenExpr extends Expr {
    final Expr e;
    LenExpr(Expr e) { this.e = e; }
    @Override Object eval(Frame f) {
        Object a = e.eval(f);
        f.line = line;
        return Interp.len(a, e);
    }
}

final class TableExpr extends Expr {
    final Expr[] arrayItems;
    final Expr[] keys, vals;
    TableExpr(Expr[] arrayItems, Expr[] keys, Expr[] vals) {
        this.arrayItems = arrayItems; this.keys = keys; this.vals = vals;
    }
    @Override Object eval(Frame f) {
        LuaTable t = new LuaTable(arrayItems.length, keys.length);
        f.pushTemp(t);
        try {
            int n = arrayItems.length;
            for (int i = 0; i < n; i++) {
                Expr e = arrayItems[i];
                if (i == n - 1 && e.multi()) {
                    Object[] vs = e.evalMulti(f);
                    for (int j = 0; j < vs.length; j++) t.rawseti(i + 1 + j, vs[j]);
                } else {
                    t.rawseti(i + 1, e.eval(f));
                }
            }
            for (int i = 0; i < keys.length; i++) {
                Object k = keys[i].eval(f);
                Object v = vals[i].eval(f);
                f.line = line;
                if (k == null) throw Interp.runtimeError("table index is nil");
                if (k instanceof Double && ((Double) k).isNaN()) throw Interp.runtimeError("table index is NaN");
                t.rawset(k, v);
            }
        } finally {
            f.popTemp();
        }
        return t;
    }
}

// ------------------------------------------------------------------ statements

interface Target {
    void assign(Frame f, Object v);
}

final class LocalTarget implements Target {
    final VarInfo v;
    LocalTarget(VarInfo v) { this.v = v; }
    public void assign(Frame f, Object val) {
        if (v.boxed) ((Upvalue) f.slots[v.slot]).v = val;
        else f.slots[v.slot] = val;
    }
}

final class UpvalTarget implements Target {
    final int idx;
    UpvalTarget(int idx) { this.idx = idx; }
    public void assign(Frame f, Object val) { f.cl.upvals[idx].v = val; }
}

final class GlobalTarget implements Target {
    final Expr env;
    final LuaString name;
    final int line;
    GlobalTarget(Expr env, String n, int line) {
        this.env = env; this.name = LuaString.of(n); this.line = line;
    }
    public void assign(Frame f, Object val) {
        Object t = env.eval(f);
        f.line = line;
        Interp.setindex(t, name, val, env.kind(), env.kindName());
    }
}

final class IndexTarget implements Target {
    final Expr obj, key;
    final int line;
    IndexTarget(Expr obj, Expr key, int line) { this.obj = obj; this.key = key; this.line = line; }
    public void assign(Frame f, Object val) {
        Object o = obj.eval(f);
        Object k = key.eval(f);
        f.line = line;
        Interp.setindex(o, k, val, obj.kind(), obj.kindName());
    }
}

final class LocalStat extends Stat {
    final VarInfo[] vars;
    final Expr[] exprs;
    LocalStat(VarInfo[] vars, Expr[] exprs) { this.vars = vars; this.exprs = exprs; }
    @Override int exec(Frame f) {
        f.line = line;
        Object[] vs = Parser.evalList(exprs, vars.length, f);
        for (int i = 0; i < vars.length; i++) {
            VarInfo v = vars[i];
            if (v.attrib == VarInfo.CLOSE) checkClosable(vs[i], v.name);
            if (v.boxed) f.slots[v.slot] = new Upvalue(vs[i]);
            else f.slots[v.slot] = vs[i];
        }
        return NORMAL;
    }

    /** A to-be-closed value must be false, nil, or have a __close metamethod. */
    static void checkClosable(Object v, String name) {
        if (v == null || v == Boolean.FALSE) return;
        if (Interp.metamethod(v, "__close") != null) return;
        throw Interp.runtimeError("variable '" + name + "' got a non-closable value");
    }
}

final class AssignStat extends Stat {
    final Target[] targets;
    final Expr[] exprs;
    AssignStat(Target[] targets, Expr[] exprs) { this.targets = targets; this.exprs = exprs; }
    @Override int exec(Frame f) {
        f.line = line;
        if (targets.length == 1 && exprs.length == 1 && !exprs[0].multi()) {
            targets[0].assign(f, exprs[0].eval(f));
            return NORMAL;
        }
        Object[] vs = Parser.evalList(exprs, targets.length, f);
        for (int i = 0; i < targets.length; i++) targets[i].assign(f, vs[i]);
        return NORMAL;
    }
}

final class CallStat extends Stat {
    final Expr call;
    CallStat(Expr call) { this.call = call; }
    @Override int exec(Frame f) {
        f.line = line;
        call.evalMulti(f);
        return NORMAL;
    }
}

final class DoStat extends Stat {
    final Blk body;
    DoStat(Blk body) { this.body = body; }
    @Override int exec(Frame f) { return body.run(f); }
}

final class WhileStat extends Stat {
    final Expr cond;
    final Blk body;
    WhileStat(Expr cond, Blk body) { this.cond = cond; this.body = body; }
    @Override int exec(Frame f) {
        while (true) {
            f.line = line;
            if (!Interp.truthy(cond.eval(f))) return NORMAL;
            int r = body.run(f);
            if (r == BREAK) return NORMAL;
            if (r != NORMAL) return r;
        }
    }
}

final class RepeatStat extends Stat {
    final Expr cond;
    final Blk body;
    RepeatStat(Blk body, Expr cond) { this.body = body; this.cond = cond; }
    @Override int exec(Frame f) {
        while (true) {
            int r = body.runOpen(f);
            if (r == BREAK) { body.release(f); return NORMAL; }
            if (r != NORMAL) { body.release(f); return r; }
            boolean done = Interp.truthy(cond.eval(f));
            body.release(f);
            if (done) return NORMAL;
        }
    }
}

final class IfStat extends Stat {
    final Expr[] conds;
    final Blk[] blocks;
    final Blk elseBlock;
    IfStat(Expr[] conds, Blk[] blocks, Blk elseBlock) {
        this.conds = conds; this.blocks = blocks; this.elseBlock = elseBlock;
    }
    @Override int exec(Frame f) {
        f.line = line;
        for (int i = 0; i < conds.length; i++) {
            if (Interp.truthy(conds[i].eval(f))) return blocks[i].run(f);
        }
        if (elseBlock != null) return elseBlock.run(f);
        return NORMAL;
    }
}

final class NumForStat extends Stat {
    final VarInfo v;
    final Expr start, stop, step;
    final Blk body;
    NumForStat(VarInfo v, Expr start, Expr stop, Expr step, Blk body) {
        this.v = v; this.start = start; this.stop = stop; this.step = step; this.body = body;
    }
    @Override int exec(Frame f) {
        f.line = line;
        Object iv = start.eval(f), lv = stop.eval(f);
        Object sv = step == null ? (Object) 1L : step.eval(f);
        Object i0 = Interp.toNumber(iv);
        if (i0 == null) throw forError(iv, "initial value");
        Object l0 = Interp.toNumber(lv);
        if (l0 == null) throw forError(lv, "limit");
        Object s0 = Interp.toNumber(sv);
        if (s0 == null) throw forError(sv, "step");
        if (i0 instanceof Long && s0 instanceof Long) {
            long i = (Long) i0, st = (Long) s0;
            if (st == 0) throw Interp.runtimeError("'for' step is zero");
            // A float limit is clipped to the integer range in the loop's direction.
            Long lim = forLimit(l0, st);
            if (lim == null) { f.slots[v.slot] = null; return NORMAL; }
            long limit = lim;
            if (st > 0 ? i > limit : i < limit) { f.slots[v.slot] = null; return NORMAL; }
            long count = (st == 0) ? Long.MAX_VALUE
                    : (st > 0) ? Long.divideUnsigned(limit - i, st)
                               : Long.divideUnsigned(i - limit, -st);
            for (;;) {
                if (v.boxed) f.slots[v.slot] = new Upvalue(i);
                else f.slots[v.slot] = i;
                int r = body.run(f);
                if (r == BREAK) break;
                if (r != NORMAL) { f.slots[v.slot] = null; return r; }
                if (count-- == 0) break;
                i += st;
            }
            f.slots[v.slot] = null;
            return NORMAL;
        }
        double i = LuaNum.dbl(i0), limit = LuaNum.dbl(l0), st = LuaNum.dbl(s0);
        if (st == 0) throw Interp.runtimeError("'for' step is zero");
        while (st > 0 ? i <= limit : i >= limit) {
            if (v.boxed) f.slots[v.slot] = new Upvalue(i);
            else f.slots[v.slot] = i;
            int r = body.run(f);
            if (r == BREAK) break;
            if (r != NORMAL) { f.slots[v.slot] = null; return r; }
            i += st;
        }
        f.slots[v.slot] = null;
        return NORMAL;
    }

    /** luaG_forerror. */
    private static LuaError forError(Object v, String what) {
        return Interp.runtimeError("bad 'for' " + what + " (number expected, got "
                + Interp.objTypeName(v) + ")");
    }

    /** forlimit: converts the limit to an integer, clipped towards the loop. */
    private static Long forLimit(Object l, long step) {
        if (l instanceof Long) return (Long) l;
        double d = (Double) l;
        double f = (step > 0) ? Math.floor(d) : Math.ceil(d);
        if (Double.isNaN(f)) return null;
        if (f >= LuaNum.TWO63) return (step > 0) ? Long.MAX_VALUE : null;
        if (f < -LuaNum.TWO63) return (step > 0) ? null : Long.MIN_VALUE;
        return (long) f;
    }
}

final class GenForStat extends Stat {
    final VarInfo[] vars;
    final Expr[] exprs;
    final Blk body;
    GenForStat(VarInfo[] vars, Expr[] exprs, Blk body) {
        this.vars = vars; this.exprs = exprs; this.body = body;
    }
    @Override int exec(Frame f) {
        f.line = line;
        Object[] init = Parser.evalList(exprs, 3, f);
        Object func = init[0], state = init[1], ctrl = init[2];
        f.pushTemp(state);
        try {
            while (true) {
                f.line = line;
                // From 5.4 the iterator is named "for iterator" when it is
                // not callable.
                Object[] rs = Interp.call(func, new Object[]{state, ctrl},
                        "for iterator", "for iterator");
                Object first = rs.length > 0 ? rs[0] : null;
                if (first == null) return NORMAL;
                ctrl = first;
                for (int i = 0; i < vars.length; i++) {
                    Object val = i < rs.length ? rs[i] : null;
                    VarInfo v = vars[i];
                    if (v.boxed) f.slots[v.slot] = new Upvalue(val);
                    else f.slots[v.slot] = val;
                }
                int r = body.run(f);
                if (r == BREAK) return NORMAL;
                if (r != NORMAL) return r;
            }
        } finally {
            for (VarInfo v : vars) f.slots[v.slot] = null;
            f.popTemp();
        }
    }
}

final class ReturnStat extends Stat {
    final Expr[] exprs;
    final CallExpr tailCall;
    final MethodCallExpr tailMethod;

    ReturnStat(Expr[] exprs) {
        this.exprs = exprs;
        Expr only = exprs.length == 1 ? exprs[0] : null;
        this.tailCall = (only instanceof CallExpr) ? (CallExpr) only : null;
        this.tailMethod = (only instanceof MethodCallExpr) ? (MethodCallExpr) only : null;
    }

    @Override int exec(Frame f) {
        f.line = line;
        if (tailCall != null) {
            Object func = tailCall.fn.eval(f);
            Object[] a = Parser.evalArgs(tailCall.args, f);
            f.line = tailCall.line;
            f.tailFunc = func;
            f.tailArgs = a;
            f.tailName = tailCall.fn.stackName();
            f.tailKind = tailCall.fn.stackKind();
            f.hasTail = true;
            return RETURN;
        }
        if (tailMethod != null) {
            Object self = tailMethod.obj.eval(f);
            f.line = tailMethod.line;
            Object func = Interp.index(self, tailMethod.name,
                    tailMethod.obj.stackKind(), tailMethod.obj.stackName());
            Object[] a = Parser.evalArgs(tailMethod.args, f);
            Object[] a2 = new Object[a.length + 1];
            a2[0] = self;
            System.arraycopy(a, 0, a2, 1, a.length);
            f.line = tailMethod.line;
            f.tailFunc = func;
            f.tailArgs = a2;
            f.tailName = tailMethod.sname;
            f.tailKind = "method";
            f.hasTail = true;
            return RETURN;
        }
        f.ret = Parser.evalArgs(exprs, f);
        return RETURN;
    }
}

/** `local function f` declares f before its body is compiled, so it can recurse. */
final class LocalFuncStat extends Stat {
    final VarInfo v;
    final Expr body;
    LocalFuncStat(VarInfo v, Expr body) { this.v = v; this.body = body; }
    @Override int exec(Frame f) {
        f.line = line;
        if (v.boxed) {
            Upvalue box = new Upvalue(null);
            f.slots[v.slot] = box;
            box.v = body.eval(f);
        } else {
            f.slots[v.slot] = body.eval(f);
        }
        return NORMAL;
    }
}

final class BreakStat extends Stat {
    @Override int exec(Frame f) { return BREAK; }
}
