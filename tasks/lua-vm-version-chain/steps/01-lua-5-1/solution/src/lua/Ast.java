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
}

/** Statement nodes. exec returns a control signal. */
abstract class Stat {
    static final int NORMAL = 0, BREAK = 1, RETURN = 2;
    int line;
    abstract int exec(Frame f);
}

final class Block {
    static int run(Stat[] body, Frame f) {
        for (Stat s : body) {
            if (Gc.due()) Gc.step();
            int r = s.exec(f);
            if (r != Stat.NORMAL) return r;
        }
        return Stat.NORMAL;
    }
}

/**
 * A lexical block together with the frame slots its locals occupy. Leaving the
 * block releases them, so a value that has gone out of scope stops being a
 * garbage-collection root, exactly as a closed register does in PUC-Lua.
 */
final class Blk {
    final Stat[] stats;
    final int from, to;

    Blk(Stat[] stats, int from, int to) { this.stats = stats; this.from = from; this.to = to; }

    int run(Frame f) {
        if (to == from) return Block.run(stats, f);
        try {
            return Block.run(stats, f);
        } finally {
            for (int i = from; i < to; i++) f.slots[i] = null;
        }
    }

    /** Runs the body without releasing the slots; `repeat` needs them for its condition. */
    int runOpen(Frame f) { return Block.run(stats, f); }

    void release(Frame f) {
        for (int i = from; i < to; i++) f.slots[i] = null;
    }
}

final class VarInfo {
    final String name;
    int slot;
    boolean boxed;
    VarInfo(String name, int slot) { this.name = name; this.slot = slot; }
}

// ------------------------------------------------------------------ expressions

final class ConstExpr extends Expr {
    final Object v;
    ConstExpr(Object v) { this.v = v; }
    @Override Object eval(Frame f) { return v; }
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

final class GlobalExpr extends Expr {
    final LuaString name;
    final String sname;
    GlobalExpr(String n) { this.sname = n; this.name = LuaString.of(n); }
    @Override Object eval(Frame f) {
        f.line = line;
        return Interp.index(f.cl.env, name, null, null);
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
        return Interp.index(o, k, obj.kind(), obj.kindName());
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
        return Interp.callDirect(func, a, fn.kind(), fn.kindName());
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
        Object func = Interp.index(self, name, obj.kind(), obj.kindName());
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
        return new LuaClosure(p, ups, f.cl.env);
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
                if (a instanceof Double && b instanceof Double) {
                    return Interp.rawArith(op, (Double) a, (Double) b);
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

final class UnmExpr extends Expr {
    final Expr e;
    UnmExpr(Expr e) { this.e = e; }
    @Override Object eval(Frame f) {
        Object a = e.eval(f);
        f.line = line;
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
    final LuaString name;
    GlobalTarget(String n) { this.name = LuaString.of(n); }
    public void assign(Frame f, Object val) { Interp.setindex(f.cl.env, name, val, null, null); }
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
            if (v.boxed) f.slots[v.slot] = new Upvalue(vs[i]);
            else f.slots[v.slot] = vs[i];
        }
        return NORMAL;
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
            if (r == RETURN) return r;
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
            if (r == RETURN) { body.release(f); return r; }
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
        Double i0 = Interp.toNumber(start.eval(f));
        if (i0 == null) throw Interp.runtimeError("'for' initial value must be a number");
        Double l0 = Interp.toNumber(stop.eval(f));
        if (l0 == null) throw Interp.runtimeError("'for' limit must be a number");
        Double s0 = step == null ? Double.valueOf(1) : Interp.toNumber(step.eval(f));
        if (s0 == null) throw Interp.runtimeError("'for' step must be a number");
        double i = i0, limit = l0, st = s0;
        // 5.1 has no zero-step check: OP_FORLOOP simply asks "step > 0", so a
        // zero step is treated as a downward loop.
        while (st > 0 ? i <= limit : i >= limit) {
            if (v.boxed) f.slots[v.slot] = new Upvalue(i);
            else f.slots[v.slot] = i;
            int r = body.run(f);
            if (r == BREAK) break;
            if (r == RETURN) { f.slots[v.slot] = null; return r; }
            i += st;
        }
        f.slots[v.slot] = null;
        return NORMAL;
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
                Object[] rs = Interp.call(func, new Object[]{state, ctrl}, null, null);
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
                if (r == RETURN) return r;
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
            f.tailName = tailCall.fn.kindName();
            f.tailKind = tailCall.fn.kind();
            f.hasTail = true;
            return RETURN;
        }
        if (tailMethod != null) {
            Object self = tailMethod.obj.eval(f);
            f.line = tailMethod.line;
            Object func = Interp.index(self, tailMethod.name, tailMethod.obj.kind(), tailMethod.obj.kindName());
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
