package lua;

import java.util.ArrayList;
import java.util.List;

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

    static final class Lbl {
        final String name;
        final int line;
        int nactvar;
        final List<String> actives;
        Lbl(String name, int line, int nactvar, List<String> actives) {
            this.name = name; this.line = line; this.nactvar = nactvar;
            this.actives = actives;
        }
    }

    static final class Gt {
        final String name;
        final int line;
        final int nactvar;
        Gt(String name, int line, int nactvar) {
            this.name = name; this.line = line; this.nactvar = nactvar;
        }
    }

    /** One lexical block, for the visibility rules of labels and gotos. */
    static final class BlockCnt {
        BlockCnt prev;
        final List<Lbl> labels = new ArrayList<>();
        final List<Gt> gotos = new ArrayList<>();
        int nactvar;
        boolean isLoop;
    }

    static final class FuncState {
        FuncState parent;
        BlockCnt bl;
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
        final List<Integer> pendingTbc = new ArrayList<>();
    }

    private static final int MAXCCALLS = 200;
    private static final int MAXVARS = 200;
    private static final int MAXUPVALUES = 255;

    private final Lexer lx;
    private FuncState fs;
    private int level;

    private void enterLevel() {
        if (++level > MAXCCALLS) {
            throw limitError(MAXCCALLS, "C levels");
        }
    }

    private void leaveLevel() { level--; }

    private Parser(Lexer lx) { this.lx = lx; }

    static LuaClosure load(byte[] chunk, String rawname, String shortSrc, Object env) {
        Lexer lx = new Lexer(chunk, rawname);
        Parser ps = new Parser(lx);
        Proto p = ps.mainFunc(rawname, shortSrc);
        return new LuaClosure(p, new Upvalue[]{new Upvalue(env)});
    }

    private Proto mainFunc(String rawname, String shortSrc) {
        fs = new FuncState();
        fs.p.source = rawname;
        fs.p.shortSrc = shortSrc;
        fs.p.isVararg = true;
        fs.isVararg = true;
        fs.p.numParams = 0;
        fs.p.lineDefined = 0;
        fs.upvals.add(new UpvalDesc("_ENV", false, 0));
        enterBlock(false);
        int from = fs.nslots;
        int tbcMark = fs.pendingTbc.size();
        Stat[] body = block();
        if (lx.type != Lexer.TK_EOF) throw lx.error("<eof> expected", null);
        leaveBlock(body);
        return finish(finishBlk(body, from, tbcMark));
    }

    private Proto finish(Blk body) {
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

    private void enterBlock(boolean isLoop) {
        fs.blockStarts.add(fs.actives.size());
        BlockCnt b = new BlockCnt();
        b.prev = fs.bl;
        b.nactvar = fs.actives.size();
        b.isLoop = isLoop;
        fs.bl = b;
    }

    /** Closes a block: resolve its gotos, then drop its locals. */
    private void leaveBlock(Stat[] stats) { leaveBlock(stats, true); }

    private void leaveBlock(Stat[] stats, boolean unused) {
        BlockCnt bl = fs.bl;
        fs.bl = bl.prev;
        for (Gt g : bl.gotos) {
            if (g.name.equals("break") && bl.isLoop) continue;
            // leaving the block also leaves its locals: clamp the goto's scope depth,
            // then try to close it against a label visible in the enclosing block
            Gt moved = new Gt(g.name, g.line, Math.min(g.nactvar, bl.nactvar));
            if (fs.bl != null) {
                addGoto(moved);
            } else if (g.name.equals("break")) {
                throw Interp.syntaxError(lx.chunkname + ":" + lx.line
                        + ": break outside loop at line " + g.line);
            } else {
                throw Interp.syntaxError(lx.chunkname + ":" + lx.line + ": no visible label '"
                        + g.name + "' for <goto> at line " + g.line);
            }
        }
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

    /** errorlimit() of lparser.c, reported through luaX_syntaxerror. */
    private LuaError limitError(int limit, String what) {
        String where = (fs.p.lineDefined == 0)
                ? "main function" : "function at line " + fs.p.lineDefined;
        return lx.error("too many " + what + " (limit is " + limit + ") in " + where, null);
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
            UpvalDesc d = new UpvalDesc(name, true, v.slot);
            d.readonly = (v.attrib != VarInfo.NONE);
            f.upvals.add(d);
            return f.upvals.size() - 1;
        }
        int idx = findUpval(f.parent, name);
        if (idx < 0) return -1;
        checkUpvals(f);
        UpvalDesc d = new UpvalDesc(name, false, idx);
        d.readonly = f.parent.upvals.get(idx).readonly;
        f.upvals.add(d);
        return f.upvals.size() - 1;
    }

    private Expr singleVar(String name, int line) {
        Expr e = resolve(name, line);
        if (e != null) { e.line = line; return e; }
        Expr env = resolve("_ENV", line);          // a free name is a field of _ENV
        env.line = line;
        e = new GlobalExpr(env, name);
        e.line = line;
        return e;
    }

    private Expr resolve(String name, int line) {
        VarInfo v = findLocal(fs, name);
        if (v != null) return new LocalExpr(v);
        int u = findUpval(fs, name);
        return (u >= 0) ? new UpvalExpr(u, name) : null;
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
        if (lx.type != Lexer.TK_NAME) throw lx.error("<name> expected", null);
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
            if (isKw("return")) {
                out.add(returnStat());
                break;
            }
            if (isOp("::") || isOp(";")) {
                labelRun(out);
                continue;
            }
            Stat s = statement();
            if (s != null) out.add(s);
        }
        return out.toArray(new Stat[0]);
    }

    private Stat returnStat() {
        int line = lx.tline;
        lx.next();
        Expr[] es = (blockFollow() || isOp(";")) ? new Expr[0] : explist();
        testOp(";");
        Stat s = new ReturnStat(es);
        s.line = line;
        return s;
    }

    private Blk scopedBlock() { return scopedBlock(false); }

    private Blk scopedBlock(boolean isLoop) {
        enterBlock(isLoop);
        int from = fs.nslots;
        int tbcMark = fs.pendingTbc.size();
        Stat[] b = block();
        leaveBlock(b);
        return finishBlk(b, from, tbcMark);
    }

    /** Attaches to a block the to-be-closed variables declared directly in it. */
    private Blk finishBlk(Stat[] stats, int from, int tbcMark) {
        Blk blk = new Blk(stats, from, fs.nslots);
        if (fs.pendingTbc.size() > tbcMark) {
            int[] slots = new int[fs.pendingTbc.size() - tbcMark];
            for (int i = 0; i < slots.length; i++) slots[i] = fs.pendingTbc.get(tbcMark + i);
            blk.tbc = slots;
            while (fs.pendingTbc.size() > tbcMark) fs.pendingTbc.remove(fs.pendingTbc.size() - 1);
        }
        return blk;
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
                    Blk b = scopedBlock(true);
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
                    enterBlock(true);
                    int from = fs.nslots;
                    int tbcMark = fs.pendingTbc.size();
                    fs.loopDepth++;
                    Stat[] b = block();
                    fs.loopDepth--;
                    checkMatch("until", "repeat", line);
                    Expr c = expr();
                    leaveBlock(b, false);      // the until-condition still sees the locals
                    Stat s = new RepeatStat(finishBlk(b, from, tbcMark), c); s.line = line; return s;
                }
                case "function": return funcStat(line);
                case "local": {
                    lx.next();
                    if (testKw("function")) return localFunc(line);
                    return localStat(line);
                }
                case "return":
                    return returnStat();
                case "break": {
                    lx.next();
                    addGoto(new Gt("break", line, fs.actives.size()));
                    Stat s = new BreakStat();
                    s.line = line;
                    return s;
                }
                case "goto": {
                    lx.next();
                    String n = checkName();
                    addGoto(new Gt(n, line, fs.actives.size()));
                    Stat s = new GotoStat(n);
                    s.line = line;
                    return s;
                }
            }
        }
        return exprStat(line);
    }

    /**
     * labelstat: a run of labels and empty statements. A label with nothing but
     * no-op statements before the end of the block is outside the scope of the
     * block's locals, so a goto to it does not enter that scope. `until` does not
     * count as the end of a block here.
     */
    private void labelRun(List<Stat> out) {
        List<Lbl> run = new ArrayList<>();
        for (;;) {
            if (isOp(";")) { lx.next(); continue; }
            if (!isOp("::")) break;
            int line = lx.tline;
            lx.next();
            String n = checkName();
            checkOp("::");
            // From 5.4 a label may not shadow one that is still visible, so
            // every enclosing block in this function is checked, not just this one.
            for (BlockCnt b = fs.bl; b != null; b = b.prev) {
                for (Lbl l : b.labels) {
                    if (l.name.equals(n)) {
                        throw Interp.syntaxError(lx.chunkname + ":" + lx.line + ": label '" + n
                                + "' already defined on line " + l.line);
                    }
                }
            }
            Lbl l = new Lbl(n, line, fs.actives.size(), activeNames());
            fs.bl.labels.add(l);
            run.add(l);
            Stat s = new LabelStat(n);
            s.line = line;
            out.add(s);
        }
        boolean last = lx.type == Lexer.TK_EOF
                || (lx.type == Lexer.TK_KEYWORD
                    && (lx.text.equals("end") || lx.text.equals("else") || lx.text.equals("elseif")));
        for (Lbl l : run) {
            if (last) l.nactvar = fs.bl.nactvar;
            closeGotos(l);
        }
    }

    /** findlabel: a goto whose label is already visible in this block closes at once. */
    private void addGoto(Gt g) {
        for (Lbl l : fs.bl.labels) {
            if (!l.name.equals(g.name)) continue;
            if (g.nactvar < l.nactvar) {
                throw Interp.syntaxError(lx.chunkname + ":" + lx.line + ": <goto " + g.name
                        + "> at line " + g.line + " jumps into the scope of local '"
                        + l.actives.get(g.nactvar) + "'");
            }
            return;
        }
        fs.bl.gotos.add(g);
    }

    /** findgotos: close every pending goto in this block that names this label. */
    private void closeGotos(Lbl l) {
        for (int i = fs.bl.gotos.size() - 1; i >= 0; i--) {
            Gt g = fs.bl.gotos.get(i);
            if (!g.name.equals(l.name)) continue;
            if (g.nactvar < l.nactvar) {
                throw Interp.syntaxError(lx.chunkname + ":" + lx.line + ": <goto " + g.name
                        + "> at line " + g.line + " jumps into the scope of local '"
                        + l.actives.get(g.nactvar) + "'");
            }
            fs.bl.gotos.remove(i);
        }
    }

    private List<String> activeNames() {
        List<String> r = new ArrayList<>(fs.actives.size());
        for (VarInfo v : fs.actives) r.add(v.name);
        return r;
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
            enterBlock(true);
            VarInfo v = newLocal(n1);
            activate(v);
            int from = fs.nslots;
            int tbcMark = fs.pendingTbc.size();
            fs.loopDepth++;
            Stat[] body = block();
            fs.loopDepth--;
            leaveBlock(body);
            checkMatch("end", "for", line);
            Stat s = new NumForStat(v, a, b, c, finishBlk(body, from, tbcMark));
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
            enterBlock(true);
            VarInfo[] vars = new VarInfo[names.size()];
            for (int i = 0; i < names.size(); i++) { vars[i] = newLocal(names.get(i)); activate(vars[i]); }
            int from = fs.nslots;
            int tbcMark = fs.pendingTbc.size();
            fs.loopDepth++;
            Stat[] body = block();
            fs.loopDepth--;
            leaveBlock(body);
            checkMatch("end", "for", line);
            Stat s = new GenForStat(vars, es, finishBlk(body, from, tbcMark));
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
        List<Integer> attribs = new ArrayList<>();
        int closes = 0;
        for (;;) {
            names.add(checkName());
            checkVarLimit(names.size());
            attribs.add(getAttrib());
            if (attribs.get(attribs.size() - 1) == VarInfo.CLOSE) {
                closes++;
                if (closes > 1) {
                    throw Interp.syntaxError(lx.chunkname + ":" + lx.line
                            + ": multiple to-be-closed variables in local list");
                }
            }
            if (!testOp(",")) break;
        }
        Expr[] es = testOp("=") ? explist() : new Expr[0];
        VarInfo[] vars = new VarInfo[names.size()];
        for (int i = 0; i < names.size(); i++) {
            vars[i] = newLocal(names.get(i));
            vars[i].attrib = attribs.get(i);
        }
        for (VarInfo v : vars) {
            activate(v);
            if (v.attrib == VarInfo.CLOSE) fs.pendingTbc.add(v.slot);
        }
        Stat s = new LocalStat(vars, es);
        s.line = line;
        return s;
    }

    /** getlocalattribute: `<const>` or `<close>`, both new in 5.4. */
    private int getAttrib() {
        if (!testOp("<")) return VarInfo.NONE;
        String a = checkName();
        checkOp(">");
        if (a.equals("const")) return VarInfo.CONST;
        if (a.equals("close")) return VarInfo.CLOSE;
        throw Interp.syntaxError(lx.chunkname + ":" + lx.line + ": unknown attribute '" + a + "'");
    }

    private Stat exprStat(int line) {
        Expr e = suffixedExp();
        if (isOp("=") || isOp(",")) {
            List<Target> ts = new ArrayList<>();
            ts.add(toTarget(e, line));
            while (testOp(",")) {
                ts.add(toTarget(suffixedExp(), line));
                // assignment() counts its variables against the same budget as
                // the parser's own recursion.
                if (ts.size() + level > MAXCCALLS) throw limitError(MAXCCALLS, "C levels");
            }
            checkOp("=");
            Expr[] es = explist();
            Stat s = new AssignStat(ts.toArray(new Target[0]), es);
            s.line = line;
            return s;
        }
        if (!(e instanceof CallExpr || e instanceof MethodCallExpr)) throw lx.error("syntax error", null);
        Stat s = new CallStat(e);
        s.line = line;
        return s;
    }

    private Target toTarget(Expr e, int line) {
        if (e instanceof LocalExpr) {
            VarInfo v = ((LocalExpr) e).v;
            if (v.attrib != VarInfo.NONE) {
                throw Interp.syntaxError(lx.chunkname + ":" + lx.line
                        + ": attempt to assign to const variable '" + v.name + "'");
            }
            return new LocalTarget(v);
        }
        if (e instanceof UpvalExpr) {
            UpvalExpr u = (UpvalExpr) e;
            if (fs.upvals.get(u.idx).readonly) {
                throw Interp.syntaxError(lx.chunkname + ":" + lx.line
                        + ": attempt to assign to const variable '" + u.name + "'");
            }
            return new UpvalTarget(u.idx);
        }
        if (e instanceof GlobalExpr) {
            GlobalExpr g = (GlobalExpr) e;
            return new GlobalTarget(g.env, g.sname, line);
        }
        if (e instanceof IndexExpr) {
            IndexExpr ie = (IndexExpr) e;
            return new IndexTarget(ie.obj, ie.key, ie.line);
        }
        throw lx.error("syntax error", null);
    }

    // ---------------------------------------------------------- expressions

    /** MAXREGS: an expression list longer than this cannot be held in registers. */
    private static final int MAXREGS = 254;

    private Expr[] explist() {
        List<Expr> es = new ArrayList<>();
        es.add(expr());
        while (testOp(",")) {
            // luaK_checkstack: the previous value is moved to its own register
            // before the next one is read, so the check happens here.
            if (es.size() >= MAXREGS) throw lx.error("function or expression needs too many registers", null);
            es.add(expr());
        }
        return es.toArray(new Expr[0]);
    }

    private static int[] binPri(String op) {
        switch (op) {
            case "|": return new int[]{4, 4};
            case "~": return new int[]{5, 5};
            case "&": return new int[]{6, 6};
            case "<<": case ">>": return new int[]{7, 7};
            case "..": return new int[]{9, 8};
            case "+": case "-": return new int[]{10, 10};
            case "*": case "/": case "//": case "%": return new int[]{11, 11};
            case "^": return new int[]{14, 13};
            case "==": case "~=": case "<": case ">": case "<=": case ">=": return new int[]{3, 3};
            default: return null;
        }
    }

    private static final int UNARY_PRIORITY = 12;

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
        if (isKw("not")) { lx.next(); e = new NotExpr(subexpr(UNARY_PRIORITY)); e.line = line; }
        else if (isOp("-")) { lx.next(); Expr s = subexpr(UNARY_PRIORITY);
            Object cv = (s instanceof ConstExpr) ? ((ConstExpr) s).v : null;
            if (cv instanceof Double) e = new ConstExpr(-(Double) cv);
            else if (cv instanceof Long) e = new ConstExpr(-(Long) cv);
            else e = new UnmExpr(s);
            e.line = line; }
        else if (isOp("~")) { lx.next(); e = new BnotExpr(subexpr(UNARY_PRIORITY)); e.line = line; }
        else if (isOp("#")) { lx.next(); e = new LenExpr(subexpr(UNARY_PRIORITY)); e.line = line; }
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
        switch (lx.type) {
            case Lexer.TK_NUMBER: e = new ConstExpr(lx.num); lx.next(); break;
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
        // From 5.4 a call is reported at the line where its arguments begin,
                // not at the start of the whole suffixed expression.
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
                int cline = lx.tline;
                Expr[] args = callArgs();
                MethodCallExpr mc = new MethodCallExpr(e, m, args);
                mc.line = cline;
                e = mc;
            } else if (isOp("(") || isOp("{") || lx.type == Lexer.TK_STRING) {
                int cline = lx.tline;
                Expr[] args = callArgs();
                CallExpr ce = new CallExpr(e, args);
                ce.line = cline;
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
        enterBlock(false);
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
        int bfrom = fs.nslots;
        int btbc = fs.pendingTbc.size();
        Stat[] body = block();
        fs.p.lastLineDefined = lx.tline;
        checkMatch("end", "function", line);
        leaveBlock(body);
        Proto p = finish(finishBlk(body, bfrom, btbc));
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
