package lua;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Recursive-descent parser producing directly executable trees. */
final class Parser {

    // ------------------------------------------------------- runtime helpers

    static Object[] evalArgs(Expr[] es, Frame f) {
        int n = es.length;
        if (n == 0) return Interp.EMPTY;
        Expr last = es[n - 1];
        if (!last.multi()) {
            Object[] r = new Object[n];
            f.pushTemp(r);
            try {
                for (int i = 0; i < n; i++) r[i] = es[i].eval(f);
            } finally {
                f.popTemp();
            }
            return r;
        }
        Object[] head = new Object[n - 1];
        for (int i = 0; i < n - 1; i++) head[i] = es[i].eval(f);
        f.pushTemp(head);
        Object[] tail;
        try { tail = last.evalMulti(f); } finally { f.popTemp(); }
        Object[] r = new Object[head.length + tail.length];
        System.arraycopy(head, 0, r, 0, head.length);
        System.arraycopy(tail, 0, r, head.length, tail.length);
        return r;
    }

    static Object[] evalList(Expr[] es, int want, Frame f) {
        Object[] vs = evalArgs(es, f);
        if (vs.length == want) return vs;
        Object[] r = new Object[want];
        System.arraycopy(vs, 0, r, 0, Math.min(want, vs.length));
        return r;
    }

    // ----------------------------------------------------------- parser state

    static final class FuncState {
        FuncState parent;
        Proto p = new Proto();
        List<VarInfo> actives = new ArrayList<>();
        List<Integer> blockStarts = new ArrayList<>();
        List<UpvalDesc> upvals = new ArrayList<>();
        List<VarInfo> allVars = new ArrayList<>();
        int nslots;
        int declared;               // locals registered in this function so far
        int loopDepth;
        boolean isVararg;
        VarInfo argVar;
        // 5.1 keys the constant table with the number itself, so -0.0 and 0.0
        // collide and the first literal of the pair wins for both.  5.2 keys
        // zeroes by their raw bytes instead (luaK_numberK).
        Map<Double, Double> numk = new HashMap<>();
    }

    /** Set while the operand of a '-' that may still fold is being read. */
    private boolean unmPending;

    /** The number constant this function will really hold for `d`. */
    private Double numConst(double d) {
        Double prev = fs.numk.putIfAbsent(d + 0.0, d);
        return prev != null ? prev : d;
    }

    private static final int MAXCCALLS = 200;
    private static final int MAXVARS = 200;
    private static final int MAXUPVALUES = 60;

    private final Lexer lx;
    private FuncState fs;
    private int level;

    private void enterLevel() {
        if (++level > MAXCCALLS) {
            throw Interp.syntaxError(lx.chunkname + ":" + lx.line + ": chunk has too many syntax levels");
        }
    }

    private void leaveLevel() { level--; }

    private Parser(Lexer lx) { this.lx = lx; }

    static LuaClosure load(byte[] chunk, String rawname, String shortSrc, LuaTable env) {
        Lexer lx = new Lexer(chunk, rawname);
        Parser ps = new Parser(lx);
        Proto p = ps.mainFunc(rawname, shortSrc);
        return new LuaClosure(p, new Upvalue[0], env);
    }

    private Proto mainFunc(String rawname, String shortSrc) {
        fs = new FuncState();
        fs.p.source = rawname;
        fs.p.shortSrc = shortSrc;
        fs.p.isVararg = true;
        fs.isVararg = true;
        fs.p.numParams = 0;
        fs.p.lineDefined = 0;
        enterBlock();
        Stat[] body = block();
        if (lx.type != Lexer.TK_EOF) throw lx.error("'<eof>' expected", null);
        leaveBlock();
        return finish(body);
    }

    private Proto finish(Stat[] body) {
        Proto p = fs.p;
        p.body = body;
        p.numSlots = Math.max(fs.nslots, 1);
        p.boxed = new boolean[p.numSlots];
        p.slotNames = new String[p.numSlots];
        for (VarInfo v : fs.allVars) {
            p.boxed[v.slot] = v.boxed;
            p.slotNames[v.slot] = v.name;
        }
        p.upvals = fs.upvals.toArray(new UpvalDesc[0]);
        return p;
    }

    // --------------------------------------------------------------- scoping

    private void enterBlock() { fs.blockStarts.add(fs.actives.size()); }

    private void leaveBlock() {
        int n = fs.blockStarts.remove(fs.blockStarts.size() - 1);
        while (fs.actives.size() > n) fs.actives.remove(fs.actives.size() - 1);
        fs.declared = n;                      // removevars
    }

    private VarInfo newLocal(String name) {
        // new_localvar counts variables as they are registered, before the
        // declaration that creates them becomes active.
        if (++fs.declared > MAXVARS) throw limitError(MAXVARS, "local variables");
        VarInfo v = new VarInfo(name, fs.nslots++);
        fs.allVars.add(v);
        return v;
    }

    /** errorlimit() of lparser.c. */
    private LuaError limitError(int limit, String what) {
        String msg = (fs.p.lineDefined == 0)
                ? "main function has more than " + limit + " " + what
                : "function at line " + fs.p.lineDefined + " has more than " + limit + " " + what;
        return Interp.syntaxError(lx.chunkname + ":" + lx.line + ": " + msg);
    }

    private void activate(VarInfo v) { fs.actives.add(v); }

    private static VarInfo findLocal(FuncState f, String name) {
        for (int i = f.actives.size() - 1; i >= 0; i--) {
            if (f.actives.get(i).name.equals(name)) return f.actives.get(i);
        }
        return null;
    }

    private int checkUpvals(FuncState f) {
        if (f.upvals.size() + 1 > MAXUPVALUES) {
            FuncState saved = fs;
            fs = f;
            LuaError e = limitError(MAXUPVALUES, "upvalues");
            fs = saved;
            throw e;
        }
        return 0;
    }

    private int findUpval(FuncState f, String name) {
        for (int i = 0; i < f.upvals.size(); i++) {
            if (f.upvals.get(i).name.equals(name)) return i;
        }
        if (f.parent == null) return -1;
        VarInfo v = findLocal(f.parent, name);
        if (v != null) {
            checkUpvals(f);
            v.boxed = true;
            f.upvals.add(new UpvalDesc(name, true, v.slot));
            return f.upvals.size() - 1;
        }
        int idx = findUpval(f.parent, name);
        if (idx < 0) return -1;
        checkUpvals(f);
        f.upvals.add(new UpvalDesc(name, false, idx));
        return f.upvals.size() - 1;
    }

    private Expr singleVar(String name, int line) {
        VarInfo v = findLocal(fs, name);
        Expr e;
        if (v != null) e = new LocalExpr(v);
        else {
            int u = findUpval(fs, name);
            e = (u >= 0) ? new UpvalExpr(u, name) : new GlobalExpr(name);
        }
        e.line = line;
        return e;
    }

    // -------------------------------------------------------------- helpers

    private boolean isOp(String s) { return lx.type == Lexer.TK_OP && lx.text.equals(s); }

    private boolean isKw(String s) { return lx.type == Lexer.TK_KEYWORD && lx.text.equals(s); }

    private boolean testOp(String s) { if (isOp(s)) { lx.next(); return true; } return false; }

    private boolean testKw(String s) { if (isKw(s)) { lx.next(); return true; } return false; }

    private void checkOp(String s) {
        if (!testOp(s)) throw lx.error("'" + s + "' expected", null);
    }

    private void checkKw(String s) {
        if (!testKw(s)) throw lx.error("'" + s + "' expected", null);
    }

    private void checkMatch(String what, String who, int line) {
        if (lx.type == Lexer.TK_KEYWORD && lx.text.equals(what)) { lx.next(); return; }
        if (lx.type == Lexer.TK_OP && lx.text.equals(what)) { lx.next(); return; }
        if (line == lx.line) throw lx.error("'" + what + "' expected", null);
        throw lx.error("'" + what + "' expected (to close '" + who + "' at line " + line + ")", null);
    }

    private String checkName() {
        if (lx.type != Lexer.TK_NAME) throw lx.error("'<name>' expected", null);
        String s = lx.text;
        lx.next();
        return s;
    }

    private boolean blockFollow() {
        if (lx.type == Lexer.TK_EOF) return true;
        if (lx.type != Lexer.TK_KEYWORD) return false;
        switch (lx.text) {
            case "else": case "elseif": case "end": case "until": return true;
            default: return false;
        }
    }

    // ---------------------------------------------------------------- blocks

    private Stat[] block() {
        enterLevel();
        try {
            return block0();
        } finally {
            leaveLevel();
        }
    }

    private Stat[] block0() {
        List<Stat> out = new ArrayList<>();
        while (!blockFollow()) {
            if (isKw("return") || isKw("break")) {
                out.add(lastStat());
                testOp(";");
                break;
            }
            Stat s = statement();
            if (s != null) out.add(s);
            testOp(";");
        }
        return out.toArray(new Stat[0]);
    }

    private Stat lastStat() {
        int line = lx.tline;
        Stat s;
        if (testKw("break")) {
            if (fs.loopDepth == 0) throw lx.error("no loop to break", null);
            s = new BreakStat();
        } else {
            lx.next();                         // 'return'
            Expr[] es = (blockFollow() || isOp(";")) ? new Expr[0] : explist();
            s = new ReturnStat(es);
        }
        s.line = line;
        return s;
    }

    private Blk scopedBlock() {
        enterBlock();
        int from = fs.nslots;
        Stat[] b = block();
        leaveBlock();
        return new Blk(b, from, fs.nslots);
    }

    // ------------------------------------------------------------ statements

    private Stat statement() {
        int line = lx.tline;
        if (lx.type == Lexer.TK_KEYWORD) {
            switch (lx.text) {
                case "if": return ifStat(line);
                case "while": {
                    lx.next();
                    Expr c = expr();
                    checkKw("do");
                    fs.loopDepth++;
                    Blk b = scopedBlock();
                    fs.loopDepth--;
                    checkMatch("end", "while", line);
                    Stat s = new WhileStat(c, b); s.line = line; return s;
                }
                case "do": {
                    lx.next();
                    Blk b = scopedBlock();
                    checkMatch("end", "do", line);
                    Stat s = new DoStat(b); s.line = line; return s;
                }
                case "for": return forStat(line);
                case "repeat": {
                    lx.next();
                    enterBlock();
                    int from = fs.nslots;
                    fs.loopDepth++;
                    Stat[] b = block();
                    fs.loopDepth--;
                    checkMatch("until", "repeat", line);
                    Expr c = expr();
                    int to = fs.nslots;
                    leaveBlock();
                    Stat s = new RepeatStat(new Blk(b, from, to), c); s.line = line; return s;
                }
                case "function": return funcStat(line);
                case "local": {
                    lx.next();
                    if (testKw("function")) return localFunc(line);
                    return localStat(line);
                }
                case "return": case "break":
                    return lastStat();
            }
        }
        return exprStat(line);
    }

    private Stat ifStat(int line) {
        List<Expr> conds = new ArrayList<>();
        List<Blk> blocks = new ArrayList<>();
        lx.next();
        conds.add(expr());
        checkKw("then");
        blocks.add(scopedBlock());
        while (isKw("elseif")) {
            lx.next();
            conds.add(expr());
            checkKw("then");
            blocks.add(scopedBlock());
        }
        Blk elseB = null;
        if (testKw("else")) elseB = scopedBlock();
        checkMatch("end", "if", line);
        Stat s = new IfStat(conds.toArray(new Expr[0]), blocks.toArray(new Blk[0]), elseB);
        s.line = line;
        return s;
    }

    private Stat forStat(int line) {
        lx.next();
        String n1 = checkName();
        if (isOp("=")) {
            lx.next();
            Expr a = expr();
            checkOp(",");
            Expr b = expr();
            Expr c = testOp(",") ? expr() : null;
            checkKw("do");
            enterBlock();
            VarInfo v = newLocal(n1);
            activate(v);
            int from = fs.nslots;
            fs.loopDepth++;
            Stat[] body = block();
            fs.loopDepth--;
            int to = fs.nslots;
            leaveBlock();
            checkMatch("end", "for", line);
            Stat s = new NumForStat(v, a, b, c, new Blk(body, from, to));
            s.line = line;
            return s;
        }
        if (isOp(",") || isKw("in")) {
            List<String> names = new ArrayList<>();
            names.add(n1);
            while (testOp(",")) { names.add(checkName()); checkVarLimit(names.size()); }
            checkKw("in");
            int eline = lx.tline;
            Expr[] es = explist();
            checkKw("do");
            enterBlock();
            VarInfo[] vars = new VarInfo[names.size()];
            for (int i = 0; i < names.size(); i++) { vars[i] = newLocal(names.get(i)); activate(vars[i]); }
            int from = fs.nslots;
            fs.loopDepth++;
            Stat[] body = block();
            fs.loopDepth--;
            int to = fs.nslots;
            leaveBlock();
            checkMatch("end", "for", line);
            Stat s = new GenForStat(vars, es, new Blk(body, from, to));
            s.line = eline;
            return s;
        }
        throw lx.error("'=' or 'in' expected", null);
    }

    private Stat funcStat(int line) {
        lx.next();
        int nline = lx.tline;
        String base = checkName();
        Expr e = singleVar(base, nline);
        StringBuilder fname = new StringBuilder(base);
        Target target;
        boolean method = false;
        Expr lastObj = e;
        Expr lastKey = null;
        while (isOp(".")) {
            lx.next();
            String k = checkName();
            fname.append('.').append(k);
            if (lastKey != null) { IndexExpr ie = new IndexExpr(lastObj, lastKey); ie.line = nline; lastObj = ie; }
            lastKey = new ConstExpr(LuaString.of(k));
        }
        if (isOp(":")) {
            lx.next();
            String k = checkName();
            fname.append(':').append(k);
            if (lastKey != null) { IndexExpr ie = new IndexExpr(lastObj, lastKey); ie.line = nline; lastObj = ie; }
            lastKey = new ConstExpr(LuaString.of(k));
            method = true;
        }
        // The assignment target is built before the body, so that refusing a
        // read-only name reports the line of 'function', not of its 'end'.
        if (lastKey == null) target = toTarget(e, nline);
        else target = new IndexTarget(lastObj, lastKey, nline);
        Expr body = funcBody(method, fname.toString(), line);
        Stat s = new AssignStat(new Target[]{target}, new Expr[]{body});
        s.line = line;
        return s;
    }

    private Stat localFunc(int line) {
        String name = checkName();
        VarInfo v = newLocal(name);
        activate(v);                         // visible inside its own body
        Expr body = funcBody(false, name, line);
        Stat s = new LocalFuncStat(v, body);
        s.line = line;
        return s;
    }

    private Stat localStat(int line) {
        List<String> names = new ArrayList<>();
        names.add(checkName());
        checkVarLimit(names.size());
        while (testOp(",")) { names.add(checkName()); checkVarLimit(names.size()); }
        Expr[] es = testOp("=") ? explist() : new Expr[0];
        VarInfo[] vars = new VarInfo[names.size()];
        for (int i = 0; i < names.size(); i++) vars[i] = newLocal(names.get(i));
        for (VarInfo v : vars) activate(v);
        Stat s = new LocalStat(vars, es);
        s.line = line;
        return s;
    }

    private Stat exprStat(int line) {
        Expr e = suffixedExp();
        if (isOp("=") || isOp(",")) {
            List<Target> ts = new ArrayList<>();
            ts.add(toTarget(e, line));
            while (testOp(",")) ts.add(toTarget(suffixedExp(), line));
            checkOp("=");
            Expr[] es = explist();
            Stat s = new AssignStat(ts.toArray(new Target[0]), es);
            s.line = line;
            return s;
        }
        if (!(e instanceof CallExpr || e instanceof MethodCallExpr)) {
            checkOp("=");
            throw lx.error("syntax error", null);
        }
        Stat s = new CallStat(e);
        s.line = line;
        return s;
    }

    private Target toTarget(Expr e, int line) {
        if (e instanceof LocalExpr) return new LocalTarget(((LocalExpr) e).v);
        if (e instanceof UpvalExpr) return new UpvalTarget(((UpvalExpr) e).idx);
        if (e instanceof GlobalExpr) return new GlobalTarget(((GlobalExpr) e).sname);
        if (e instanceof IndexExpr) {
            IndexExpr ie = (IndexExpr) e;
            return new IndexTarget(ie.obj, ie.key, ie.line);
        }
        throw lx.error("syntax error", null);
    }

    // ---------------------------------------------------------- expressions

    private Expr[] explist() {
        List<Expr> es = new ArrayList<>();
        es.add(expr());
        while (testOp(",")) es.add(expr());
        return es.toArray(new Expr[0]);
    }

    private static int[] binPri(String op) {
        switch (op) {
            case "+": case "-": return new int[]{6, 6};
            case "*": case "/": case "%": return new int[]{7, 7};
            case "^": return new int[]{10, 9};
            case "..": return new int[]{5, 4};
            case "==": case "~=": case "<": case ">": case "<=": case ">=": return new int[]{3, 3};
            default: return null;
        }
    }

    private static final int UNARY_PRIORITY = 8;

    private Expr expr() { return subexpr(0); }

    private Expr subexpr(int limit) {
        enterLevel();
        try {
            return subexpr0(limit);
        } finally {
            leaveLevel();
        }
    }

    private Expr subexpr0(int limit) {
        Expr e;
        int line = lx.tline;
        if (isKw("not")) { unmPending = false; lx.next(); e = new NotExpr(subexpr(UNARY_PRIORITY)); e.line = line; }
        else if (isOp("-")) { lx.next(); unmPending = true; Expr s = subexpr(UNARY_PRIORITY);
            unmPending = false;
            if (s instanceof ConstExpr && ((ConstExpr) s).v instanceof Double) e = new ConstExpr(numConst(-(Double) ((ConstExpr) s).v));
            else e = new UnmExpr(s);
            e.line = line; }
        else if (isOp("#")) { unmPending = false; lx.next(); e = new LenExpr(subexpr(UNARY_PRIORITY)); e.line = line; }
        else e = simpleExp();

        for (;;) {
            String op = null;
            if (lx.type == Lexer.TK_OP) op = lx.text;
            else if (isKw("and")) op = "and";
            else if (isKw("or")) op = "or";
            if (op == null) break;
            int left, right;
            if (op.equals("and")) { left = 2; right = 2; }
            else if (op.equals("or")) { left = 1; right = 1; }
            else {
                int[] pr = binPri(op);
                if (pr == null) break;
                left = pr[0]; right = pr[1];
            }
            if (left <= limit) break;
            int oline = lx.tline;
            lx.next();
            Expr rhs = subexpr(right);
            Expr n;
            if (op.equals("and")) n = new AndExpr(e, rhs);
            else if (op.equals("or")) n = new OrExpr(e, rhs);
            else n = new BinExpr(op, e, rhs);
            n.line = oline;
            e = n;
        }
        return e;
    }

    private Expr simpleExp() {
        int line = lx.tline;
        Expr e;
        // A literal straight after '-' is folded before it reaches the constant
        // table, so only the folded value is entered there.
        boolean folding = unmPending;
        unmPending = false;
        switch (lx.type) {
            case Lexer.TK_NUMBER:
                e = new ConstExpr(folding ? Double.valueOf(lx.num) : numConst(lx.num));
                lx.next(); break;
            case Lexer.TK_STRING: e = new ConstExpr(lx.str); lx.next(); break;
            case Lexer.TK_KEYWORD:
                switch (lx.text) {
                    case "nil": e = new ConstExpr(null); lx.next(); break;
                    case "true": e = new ConstExpr(Boolean.TRUE); lx.next(); break;
                    case "false": e = new ConstExpr(Boolean.FALSE); lx.next(); break;
                    case "function": lx.next(); e = funcBody(false, null, line); break;
                    default: return suffixedExp();
                }
                break;
            case Lexer.TK_OP:
                if (lx.text.equals("...")) {
                    if (!fs.isVararg) throw lx.error("cannot use '...' outside a vararg function", null);
                    fs.p.needsArg = false;
                    lx.next();
                    e = new VarargExpr();
                    break;
                }
                if (lx.text.equals("{")) { e = tableCons(); break; }
                return suffixedExp();
            default:
                return suffixedExp();
        }
        e.line = line;
        return e;
    }

    private Expr primaryExp() {
        int line = lx.tline;
        if (isOp("(")) {
            lx.next();
            Expr e = expr();
            checkMatch(")", "(", line);
            if (!e.multi()) return e;       // keep the inner expression's own line
            Expr p = new ParenExpr(e);
            p.line = line;
            return p;
        }
        if (lx.type == Lexer.TK_NAME) {
            String n = lx.text;
            lx.next();
            return singleVar(n, line);
        }
        throw lx.error("unexpected symbol", null);
    }

    private Expr suffixedExp() {
        int startLine = lx.tline;         // a call is reported at the start of the expression
        Expr e = primaryExp();
        for (;;) {
            int line = lx.tline;
            if (isOp(".")) {
                lx.next();
                String k = checkName();
                IndexExpr ie = new IndexExpr(e, new ConstExpr(LuaString.of(k)));
                ie.line = line;
                e = ie;
            } else if (isOp("[")) {
                lx.next();
                Expr k = expr();
                checkOp("]");
                IndexExpr ie = new IndexExpr(e, k);
                ie.line = line;
                e = ie;
            } else if (isOp(":")) {
                lx.next();
                String m = checkName();
                Expr[] args = callArgs();
                MethodCallExpr mc = new MethodCallExpr(e, m, args);
                mc.line = startLine;
                e = mc;
            } else if (isOp("(") || isOp("{") || lx.type == Lexer.TK_STRING) {
                if (isOp("(") && lx.tline != lx.lastline) {
                    throw Interp.syntaxError(lx.chunkname + ":" + lx.tline
                            + ": ambiguous syntax (function call x new statement)");
                }
                Expr[] args = callArgs();
                CallExpr ce = new CallExpr(e, args);
                ce.line = startLine;
                e = ce;
            } else {
                return e;
            }
        }
    }

    private Expr[] callArgs() {
        int line = lx.tline;
        if (lx.type == Lexer.TK_STRING) {
            Expr e = new ConstExpr(lx.str);
            e.line = line;
            lx.next();
            return new Expr[]{e};
        }
        if (isOp("{")) return new Expr[]{tableCons()};
        checkOp("(");
        if (testOp(")")) return new Expr[0];
        Expr[] es = explist();
        checkMatch(")", "(", line);
        return es;
    }

    private Expr tableCons() {
        int line = lx.tline;
        checkOp("{");
        List<Expr> arr = new ArrayList<>();
        List<Expr> keys = new ArrayList<>(), vals = new ArrayList<>();
        while (!isOp("}")) {
            if (lx.type == Lexer.TK_NAME && "=".equals(lx.peekText()) && lx.peek() == Lexer.TK_OP) {
                String k = lx.text;
                lx.next(); lx.next();
                keys.add(new ConstExpr(LuaString.of(k)));
                vals.add(expr());
            } else if (isOp("[")) {
                lx.next();
                Expr k = expr();
                checkOp("]");
                checkOp("=");
                keys.add(k);
                vals.add(expr());
            } else {
                arr.add(expr());
            }
            if (!testOp(",") && !testOp(";")) break;
        }
        checkMatch("}", "{", line);
        Expr e = new TableExpr(arr.toArray(new Expr[0]), keys.toArray(new Expr[0]), vals.toArray(new Expr[0]));
        e.line = line;
        return e;
    }

    private Expr funcBody(boolean method, String name, int line) {
        FuncState prev = fs;
        fs = new FuncState();
        fs.parent = prev;
        fs.p.source = prev.p.source;
        fs.p.shortSrc = prev.p.shortSrc;
        fs.p.lineDefined = line;
        fs.p.name = name;
        enterBlock();
        List<String> params = new ArrayList<>();
        if (method) params.add("self");
        checkOp("(");
        if (!isOp(")")) {
            for (;;) {
                if (isOp("...")) {
                    lx.next();
                    fs.isVararg = true;
                    fs.p.isVararg = true;
                    fs.p.needsArg = true;
                    break;
                }
                // parlist has its own message: a parameter may also be '...'.
                if (lx.type != Lexer.TK_NAME)
                    throw lx.error("<name> or '...' expected", null);
                params.add(checkName());
                checkVarLimit(params.size());
                if (!testOp(",")) break;
            }
        }
        checkOp(")");
        for (String pn : params) { VarInfo v = newLocal(pn); activate(v); }
        fs.p.numParams = params.size();
        if (fs.p.isVararg) {
            VarInfo av = newLocal("arg");
            activate(av);
            fs.argVar = av;
            fs.p.argSlot = av.slot;
        }
        Stat[] body = block();
        fs.p.lastLineDefined = lx.tline;
        checkMatch("end", "function", line);
        leaveBlock();
        Proto p = finish(body);
        fs = prev;
        Expr e = new FuncExpr(p);
        e.line = line;
        return e;
    }

    /** new_localvar's limit, checked as each name is read. */
    private void checkVarLimit(int pending) {
        if (fs.declared + pending > MAXVARS) throw limitError(MAXVARS, "local variables");
    }
}
