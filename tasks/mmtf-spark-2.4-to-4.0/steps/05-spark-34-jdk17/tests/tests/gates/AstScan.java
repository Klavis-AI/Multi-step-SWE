// Emit the source-level facts a migrated-ness gate needs, from a real parse.
//
// Rule 11 wants gates that read artifacts or ASTs rather than grepping text,
// and that count suppressed findings as findings. Those two requirements pull
// in opposite directions -- a resolved API reference is best read out of
// bytecode, but @SuppressWarnings has SOURCE retention and is not in the class
// file at all -- so the gates use two instruments and this is the source-side
// one. It reports what the *parser* saw: an annotation inside a comment is not
// an annotation, and the word "SuppressWarnings" inside a string literal is a
// string literal.
//
// The parser is the JDK's own, through com.sun.source. Nothing is downloaded
// and nothing is pinned, which matters because this has to run on the JDK of
// whichever rung is being scored -- 8 for the early steps and 21 for the late
// ones -- and a third-party parser would be a second opinion about what Java
// is. Parse only, never analyze: attribution would need the whole Spark
// classpath and would make a source-level scan fail for reasons that have
// nothing to do with source. Resolved call targets come from the bytecode scan.
//
//   usage: AstScan <source-root> [<source-root> ...]
//
// Output is one TSV record per fact, sorted, with these kinds:
//
//   file        <path>\t<package>
//   import      <path>\t<line>\t<imported name>\t<static|type>
//   type        <path>\t<line>\t<binary-ish name>\t<kind>
//   annotation  <path>\t<line>\t<annotation name>\t<argument text>
//   suppress    <path>\t<line>\t<warning name>\t<annotated element>
//   newclass    <path>\t<line>\t<type name>\t<argument count>
//   call        <path>\t<line>\t<method name>\t<receiver text>
//   read        <path>\t<line>\t<format>\t<yes|no>\t<option k=v list>
//   strlit      <path>\t<line>\t<length>\t-
//   comment     <path>\t<line>\t<marker>\t<comment text>
//   unit        <path>\t<main methods>\t<sessions built>\t<sessions in main>
//   deadsession <path>\t<line>\t<discarded|unused-local>
//   error       <path>\t<line>\t<diagnostic code>\t<message>
//
// `unit` is the one kind that is a per-file summary rather than a per-site
// record, and it exists because a flat table of sites cannot answer a negative
// question about a file. "Every class with a main builds its own session" is
// such a question: no `call` row says a file lacks one, and counting `call`
// rows over the tree answers a different question -- one a single class with
// thirty-six unused methods can answer as well as thirty-six programs can.
// Both counts on one row let an `absent` rule join them, which is the only
// shape of rule that extra rows cannot help.
//
// A session built, for that count, is a `getOrCreate()` whose receiver chain
// starts at `SparkSession.builder()`. The chain and not the `builder()` call
// on its own, because `SparkSession.builder()` returns a builder and nothing
// else: a file can call it and still never bring a session into being, and the
// question the `unit` row exists to answer is about programs, not about
// method names.
//
// `deadsession` is the other half of that. A session built and then thrown
// away is a `getOrCreate()` that satisfies the count and runs no program: a
// bare expression statement discards the value outright, and a local nothing
// in the unit ever names again discards it one line later. Both are dead code
// by construction -- no tree that does this step's work contains either -- so
// they are reported as sites, for a prohibition to read, rather than being
// quietly excluded from the count.
//
// The receiver text on a `call` is syntax, not a type. It is there so a gate
// can say "this file still calls something named registerTempTable on
// something spelled sqlContext" while the bytecode scan says what it resolved
// to; a gate that needs the truth uses the second one.
//
// It is also the one field that is not stable across JDKs. Over the 230-file
// baseline, JDK 8 and JDK 21 agree on all 14299 facts except one `call` whose
// receiver is an anonymous class: JDK 8's tree printer writes "@Override()"
// where JDK 21 writes "@Override". Every other kind -- suppress, annotation,
// type, import, comment -- is byte-identical on both, which is what lets a
// suppression census taken at one rung be compared with one taken at another.
// A gate that diffs across rungs uses those kinds; rehearse-gates.sh asserts
// the property rather than leaving it to luck.
import com.sun.source.tree.*;
import com.sun.source.util.JavacTask;
import com.sun.source.util.SourcePositions;
import com.sun.source.util.TreePath;
import com.sun.source.util.TreePathScanner;
import com.sun.source.util.Trees;

import javax.lang.model.element.Modifier;
import javax.tools.*;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.*;

public class AstScan {

    // Comment markers that suppress a finding in some tool's vocabulary. The
    // list is deliberately broader than the tools this repository uses: the
    // gate counts suppressions as findings, and a suppression style nobody has
    // adopted yet is exactly the one that would slip through.
    private static final String[] MARKERS = {
        "CHECKSTYLE:OFF", "CHECKSTYLE.OFF", "@SuppressWarnings", "NOPMD",
        "NOSONAR", "SuppressFBWarnings", "noinspection", "$NON-NLS",
        "spotbugs:disable", "errorprone:off", "@formatter:off",
    };

    private final List<String> out = new ArrayList<String>();

    private void rec(String kind, Object... f) {
        StringBuilder b = new StringBuilder(kind);
        for (Object o : f) {
            b.append('\t').append(o == null ? "-" : clean(o.toString()));
        }
        out.add(b.toString());
    }

    // One record is one line, so a tab or a newline inside a captured fragment
    // would silently invent a column. Bounded too: an argument list can be a
    // page long and no gate reads past the first hundred characters of one.
    private static String clean(String s) {
        String t = s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
        t = t.trim();
        return t.length() > 200 ? t.substring(0, 200) : t;
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 0) {
            System.err.println("usage: AstScan <source-root> [<source-root> ...]");
            System.exit(2);
        }
        List<Path> files = new ArrayList<Path>();
        for (String a : args) {
            Path root = Paths.get(a);
            if (!Files.isDirectory(root)) {
                System.err.println("AstScan: not a directory: " + a);
                System.exit(2);
            }
            collect(root, files);
        }
        Collections.sort(files);
        if (files.isEmpty()) {
            System.err.println("AstScan: no .java under " + Arrays.toString(args));
            System.exit(3);
        }
        AstScan scan = new AstScan();
        scan.run(files);
        Collections.sort(scan.out);
        StringBuilder b = new StringBuilder();
        for (String line : scan.out) {
            b.append(line).append('\n');
        }
        System.out.print(b);
        System.err.println("AstScan: " + files.size() + " file(s), "
                           + scan.out.size() + " fact(s)");
    }

    // Every .java under the root, including src/test and any directory a
    // candidate invented. "Scan the whole repo" is rule 11's phrase and it is
    // meant literally: a construct moved into a new module is still in the
    // tree. target/ is the one exclusion, because generated sources there are
    // the compiler's opinion and not the candidate's.
    private static void collect(Path root, final List<Path> into) throws IOException {
        Files.walkFileTree(root, new SimpleFileVisitor<Path>() {
            @Override
            public FileVisitResult preVisitDirectory(Path d, BasicFileAttributes a) {
                String n = d.getFileName() == null ? "" : d.getFileName().toString();
                if (n.equals("target") || n.equals(".git")) {
                    return FileVisitResult.SKIP_SUBTREE;
                }
                return FileVisitResult.CONTINUE;
            }

            @Override
            public FileVisitResult visitFile(Path p, BasicFileAttributes a) {
                if (p.toString().endsWith(".java")) {
                    into.add(p);
                }
                return FileVisitResult.CONTINUE;
            }
        });
    }

    private void run(List<Path> files) throws IOException {
        JavaCompiler jc = ToolProvider.getSystemJavaCompiler();
        if (jc == null) {
            System.err.println("AstScan: no system Java compiler; a JRE cannot parse");
            System.exit(2);
        }
        final DiagnosticCollector<JavaFileObject> diags =
                new DiagnosticCollector<JavaFileObject>();
        StandardJavaFileManager fm = jc.getStandardFileManager(diags, null,
                                                               StandardCharsets.UTF_8);
        Iterable<? extends JavaFileObject> objs =
                fm.getJavaFileObjectsFromFiles(toFiles(files));
        JavacTask task = (JavacTask) jc.getTask(null, fm, diags,
                                                Arrays.asList("-proc:none"), null, objs);
        Iterable<? extends CompilationUnitTree> trees = task.parse();
        Trees util = Trees.instance(task);
        for (CompilationUnitTree cu : trees) {
            Path path = Paths.get(cu.getSourceFile().toUri());
            String rel = path.toString();
            Walker w = new Walker(rel, cu, util);
            w.scan(cu, null);
            String pkg = cu.getPackageName() == null ? "-"
                                                     : cu.getPackageName().toString();
            rec("file", rel, pkg);
            rec("unit", rel, w.mains, w.sessions, w.mainSessions);
            w.deadSessions();
            comments(rel, path);
        }
        // A parse error is a finding, not a crash: a tree that does not parse
        // is a tree no gate can make a statement about, and saying so is more
        // useful than an empty report that looks like a clean one.
        for (Diagnostic<? extends JavaFileObject> d : diags.getDiagnostics()) {
            if (d.getKind() == Diagnostic.Kind.ERROR) {
                String where = d.getSource() == null ? "-"
                        : Paths.get(d.getSource().toUri()).toString();
                rec("error", where, d.getLineNumber(), d.getCode(),
                    d.getMessage(Locale.ROOT));
            }
        }
        fm.close();
    }

    private static List<java.io.File> toFiles(List<Path> ps) {
        List<java.io.File> fs = new ArrayList<java.io.File>();
        for (Path p : ps) {
            fs.add(p.toFile());
        }
        return fs;
    }

    // Suppression markers live in comments, and comments are not in the AST.
    // So they are read off the source text -- but only after the text has had
    // every string and character literal blanked out, using the exact spans the
    // parser reported. That is what keeps a URL from being a comment: the "//"
    // in "https://files.rcsb.org" is inside a literal the walker measured, and
    // after masking it is not there to be found. Scanning by line instead, as
    // the first version of this did, reported 77 comment lines in a tree that
    // has no suppression comments at all.
    //
    // With literals gone, finding comments is a two-state walk and needs no
    // lexer: outside a comment, "//" runs to the newline and "/*" runs to "*/".
    private final Map<String, List<int[]>> literalSpans =
            new HashMap<String, List<int[]>>();

    private void comments(String rel, Path path) throws IOException {
        byte[] raw = Files.readAllBytes(path);
        char[] src = new String(raw, StandardCharsets.UTF_8).toCharArray();
        List<int[]> spans = literalSpans.get(rel);
        if (spans != null) {
            for (int[] sp : spans) {
                for (int i = Math.max(0, sp[0]); i < Math.min(src.length, sp[1]); i++) {
                    if (src[i] != '\n') {
                        src[i] = ' ';
                    }
                }
            }
        }
        int line = 1;
        int i = 0;
        while (i < src.length) {
            if (src[i] == '\n') {
                line++;
                i++;
            } else if (src[i] == '/' && i + 1 < src.length && src[i + 1] == '/') {
                int e = i;
                while (e < src.length && src[e] != '\n') {
                    e++;
                }
                marker(rel, line, new String(src, i, e - i));
                i = e;
            } else if (src[i] == '/' && i + 1 < src.length && src[i + 1] == '*') {
                int e = i + 2;
                int startLine = line;
                while (e + 1 < src.length && !(src[e] == '*' && src[e + 1] == '/')) {
                    if (src[e] == '\n') {
                        line++;
                    }
                    e++;
                }
                marker(rel, startLine, new String(src, i, Math.min(e + 2, src.length) - i));
                i = Math.min(e + 2, src.length);
            } else {
                i++;
            }
        }
    }

    private void marker(String rel, long line, String text) {
        for (String m : MARKERS) {
            if (text.contains(m)) {
                rec("comment", rel, line, m, text);
            }
        }
    }

    private final class Walker extends TreePathScanner<Void, Void> {
        private final String rel;
        private final CompilationUnitTree cu;
        private final Trees util;
        private final SourcePositions pos;
        // Read once by scan()'s caller, after the walk, for the `unit` row.
        private int mains = 0;
        private int sessions = 0;
        // Sessions built inside one of this file's own `main` methods. The
        // third count exists because the second one answers "does this file
        // contain the idiom" and the requirement is "does this program run it":
        // a `getOrCreate()` in a method nothing calls satisfies the former and
        // starts no session at all.
        private int mainSessions = 0;
        // Depth of `main` methods currently being walked. An int rather than a
        // boolean because a file may declare more than one class and each may
        // declare a main, and a nested class's main is still a main.
        private int inMain = 0;
        // Every name the unit uses as an identifier, and every session local
        // it declares. The dead ones are the difference, and the difference
        // cannot be taken until the whole unit has been read -- a local is
        // used by a line that may come after the one that declares it.
        private final Set<String> used = new HashSet<String>();
        private final List<Object[]> declared = new ArrayList<Object[]>();

        Walker(String rel, CompilationUnitTree cu, Trees util) {
            this.rel = rel;
            this.cu = cu;
            this.util = util;
            this.pos = util.getSourcePositions();
        }

        private long line(Tree t) {
            long start = pos.getStartPosition(cu, t);
            return start < 0 ? -1 : cu.getLineMap().getLineNumber(start);
        }

        @Override
        public Void visitImport(ImportTree t, Void v) {
            rec("import", rel, line(t), t.getQualifiedIdentifier(),
                t.isStatic() ? "static" : "type");
            return super.visitImport(t, v);
        }

        @Override
        public Void visitClass(ClassTree t, Void v) {
            rec("type", rel, line(t), t.getSimpleName(), t.getKind());
            return super.visitClass(t, v);
        }

        @Override
        public Void visitAnnotation(AnnotationTree t, Void v) {
            String name = t.getAnnotationType().toString();
            rec("annotation", rel, line(t), name, t.getArguments());
            if (name.equals("SuppressWarnings")
                    || name.endsWith(".SuppressWarnings")
                    || name.contains("SuppressFBWarnings")) {
                // One record per suppressed warning name, not per annotation:
                // @SuppressWarnings({"unchecked", "serial"}) suppresses two
                // findings and a census that counts it once under-reports.
                for (String w : names(t)) {
                    rec("suppress", rel, line(t), w, element());
                }
            }
            return super.visitAnnotation(t, v);
        }

        // The enclosing declaration, so a reader can see what was suppressed
        // without opening the file.
        private String element() {
            com.sun.source.util.TreePath p = getCurrentPath();
            while (p != null) {
                Tree t = p.getLeaf();
                if (t instanceof MethodTree) {
                    return "method " + ((MethodTree) t).getName();
                }
                if (t instanceof VariableTree) {
                    return "field " + ((VariableTree) t).getName();
                }
                if (t instanceof ClassTree) {
                    return "type " + ((ClassTree) t).getSimpleName();
                }
                p = p.getParentPath();
            }
            return "-";
        }

        private List<String> names(AnnotationTree t) {
            List<String> ns = new ArrayList<String>();
            for (ExpressionTree arg : t.getArguments()) {
                collectLiterals(arg, ns);
            }
            if (ns.isEmpty()) {
                ns.add("-");
            }
            return ns;
        }

        private void collectLiterals(Tree t, List<String> into) {
            if (t instanceof LiteralTree) {
                Object val = ((LiteralTree) t).getValue();
                if (val instanceof String) {
                    into.add((String) val);
                }
            } else if (t instanceof AssignmentTree) {
                collectLiterals(((AssignmentTree) t).getExpression(), into);
            } else if (t instanceof NewArrayTree) {
                for (ExpressionTree e : ((NewArrayTree) t).getInitializers()) {
                    collectLiterals(e, into);
                }
            }
        }

        // A program, for the purposes of the `unit` row: `public static void
        // main(String[])`. The arity is checked and the return type is not --
        // a one-argument `public static main` that returns something is not a
        // JVM entry point, but it is also not a thing this codebase contains,
        // and the parser has no types to ask.
        @Override
        public Void visitMethod(MethodTree t, Void v) {
            boolean isMain = t.getName().contentEquals("main")
                    && t.getParameters().size() == 1
                    && t.getModifiers().getFlags().contains(Modifier.PUBLIC)
                    && t.getModifiers().getFlags().contains(Modifier.STATIC);
            if (isMain) {
                mains++;
                inMain++;
            }
            try {
                return super.visitMethod(t, v);
            } finally {
                if (isMain) {
                    inMain--;
                }
            }
        }

        @Override
        public Void visitNewClass(NewClassTree t, Void v) {
            rec("newclass", rel, line(t), t.getIdentifier(), t.getArguments().size());
            return super.visitNewClass(t, v);
        }

        // `SparkSession.builder()....getOrCreate()`, however long the chain
        // in the middle is. Walked rather than matched on the receiver text so
        // that a chain broken across lines reads the same as one on a single
        // line, and both spellings of the class are taken, because the
        // qualified one is a legitimate style and a rule that missed
        // `org.apache.spark.sql.SparkSession.builder().getOrCreate()` would
        // fail a tree that had done the work.
        private boolean isSessionBuild(Tree t) {
            if (!(t instanceof MethodInvocationTree)) {
                return false;
            }
            ExpressionTree sel = ((MethodInvocationTree) t).getMethodSelect();
            if (!(sel instanceof MemberSelectTree)) {
                return false;
            }
            MemberSelectTree m = (MemberSelectTree) sel;
            if (!m.getIdentifier().contentEquals("getOrCreate")) {
                return false;
            }
            ExpressionTree e = m.getExpression();
            while (e instanceof MethodInvocationTree) {
                ExpressionTree inner = ((MethodInvocationTree) e).getMethodSelect();
                if (!(inner instanceof MemberSelectTree)) {
                    return false;
                }
                MemberSelectTree step = (MemberSelectTree) inner;
                if (step.getIdentifier().contentEquals("builder")) {
                    String r = step.getExpression().toString();
                    return r.equals("SparkSession") || r.endsWith(".SparkSession");
                }
                e = step.getExpression();
            }
            return false;
        }

        // A session whose value is dropped on the floor. The statement form is
        // the whole finding: nothing reads what `getOrCreate()` returned, so
        // the only effect the line can have is on the count that is supposed to
        // be measuring programs.
        @Override
        public Void visitExpressionStatement(ExpressionStatementTree t, Void v) {
            if (isSessionBuild(t.getExpression())) {
                rec("deadsession", rel, line(t), "discarded");
            }
            return super.visitExpressionStatement(t, v);
        }

        // The other form, recorded here and judged after the walk.
        @Override
        public Void visitVariable(VariableTree t, Void v) {
            if (isSessionBuild(t.getInitializer())) {
                declared.add(new Object[] { t.getName().toString(), Long.valueOf(line(t)) });
            }
            return super.visitVariable(t, v);
        }

        // Every identifier in the unit, the declaration's own name excepted --
        // a VariableTree carries its name as a Name and not as an
        // IdentifierTree, so declaring `spark` does not count as using it,
        // while `spark.read()` and a bare `spark` both do.
        @Override
        public Void visitIdentifier(IdentifierTree t, Void v) {
            used.add(t.getName().toString());
            return super.visitIdentifier(t, v);
        }

        // Called after the walk, by the caller that reads `mains` and
        // `sessions`, for the same reason: a declaration is only dead once the
        // whole unit has been read.
        void deadSessions() {
            for (Object[] d : declared) {
                if (!used.contains((String) d[0])) {
                    rec("deadsession", rel, d[1], "unused-local");
                }
            }
        }

        // One record per read site, anchored on `read()` and not on the
        // terminal `load`/`csv`/`json` that ends the chain.
        //
        // Anchoring the other way round is the obvious way and it is wrong,
        // because it is the way that can emit nothing. A chain broken across
        // statements --
        //
        //     DataFrameReader r = spark.read().format("json");
        //     r = r.option("multiLine", "true");
        //     return r.load(path);
        //
        // -- has no `read()` anywhere in the receiver chain of `load`, so a
        // terminal-anchored scan produces no row at all, and an `absent` rule
        // over no rows PASSes for want of evidence. Anchored here the same
        // code still produces exactly one row, and the row says `no`: the
        // climb stops where the chain does, which is the honest reading of a
        // read whose schema this unit never states.
        //
        // The climb is over TreePaths rather than over receiver text for
        // isSessionBuild's reason -- a chain wrapped across four lines has to
        // read the same as one written on a single line.
        private void readSite(MethodInvocationTree start) {
            String format = null;
            String terminal = null;
            boolean schema = false;
            TreeMap<String, String> opts = new TreeMap<String, String>();
            TreePath p = getCurrentPath();
            Tree cur = start;
            while (p != null) {
                TreePath pp = p.getParentPath();
                if (pp == null || !(pp.getLeaf() instanceof MemberSelectTree)) {
                    break;
                }
                MemberSelectTree link = (MemberSelectTree) pp.getLeaf();
                // The chain continues only if we are the *receiver* of the
                // next select. `f(spark.read())` has a MethodInvocation parent
                // too, and it is an argument, not a link.
                if (link.getExpression() != cur) {
                    break;
                }
                TreePath gp = pp.getParentPath();
                if (gp == null || !(gp.getLeaf() instanceof MethodInvocationTree)) {
                    break;
                }
                MethodInvocationTree call = (MethodInvocationTree) gp.getLeaf();
                if (call.getMethodSelect() != link) {
                    break;
                }
                String name = link.getIdentifier().toString();
                List<? extends ExpressionTree> args = call.getArguments();
                if (name.equals("schema")) {
                    schema = true;
                } else if (name.equals("format")) {
                    format = literalArg(args, 0);
                } else if (name.equals("option")) {
                    opts.put(literalArg(args, 0), literalArg(args, 1));
                } else if (name.equals("options")) {
                    // A Map built elsewhere. The keys are real and unreadable
                    // from here, and saying so is not the same as saying none.
                    opts.put("?", "?");
                } else {
                    terminal = name;
                }
                cur = call;
                p = gp;
                if (terminal != null) {
                    break;
                }
            }
            // An explicit `.format("csv")` wins over the terminal's name
            // because that is what Spark does with it; a bare `.load()` with
            // no format reports `load`, which is a reading and not a default.
            String fmt = format != null ? format
                       : (terminal != null ? terminal : "-");
            StringBuilder o = new StringBuilder();
            for (Map.Entry<String, String> e : opts.entrySet()) {
                if (o.length() > 0) {
                    o.append(',');
                }
                o.append(e.getKey()).append('=').append(e.getValue());
            }
            rec("read", rel, line(start), fmt.toLowerCase(Locale.ROOT),
                schema ? "yes" : "no", o.length() == 0 ? "-" : o.toString());
        }

        // The i-th argument as its literal value, or `?` when it is not a
        // literal. `?` is a third answer and not a missing one: a key computed
        // at run time is a key this scan cannot name, and a rule that treated
        // it as absent would be green on the one site it can least vouch for.
        private String literalArg(List<? extends ExpressionTree> args, int i) {
            if (i >= args.size()) {
                return "?";
            }
            ExpressionTree a = args.get(i);
            if (!(a instanceof LiteralTree)) {
                return "?";
            }
            Object val = ((LiteralTree) a).getValue();
            if (val == null) {
                return "?";
            }
            // Commas and `=` would invent entries in a list this builds by
            // joining on exactly those two characters.
            return String.valueOf(val).replace(',', ' ').replace('=', ' ');
        }

        @Override
        public Void visitMethodInvocation(MethodInvocationTree t, Void v) {
            ExpressionTree sel = t.getMethodSelect();
            String name;
            String recv = "-";
            if (sel instanceof MemberSelectTree) {
                MemberSelectTree m = (MemberSelectTree) sel;
                name = m.getIdentifier().toString();
                recv = m.getExpression().toString();
            } else {
                name = sel.toString();
            }
            rec("call", rel, line(t), name, recv);
            if (name.equals("read") && t.getArguments().isEmpty()
                    && sel instanceof MemberSelectTree) {
                readSite(t);
            }
            if (isSessionBuild(t)) {
                sessions++;
                if (inMain > 0) {
                    mainSessions++;
                }
            }
            return super.visitMethodInvocation(t, v);
        }

        @Override
        public Void visitLiteral(LiteralTree t, Void v) {
            Object val = t.getValue();
            // Character literals are masked too: '/' is one character of a
            // comment opener and two of them in a row would invent one.
            if (val instanceof String || val instanceof Character) {
                long start = pos.getStartPosition(cu, t);
                long end = pos.getEndPosition(cu, t);
                if (val instanceof String) {
                    rec("strlit", rel, line(t), ((String) val).length(), "-");
                }
                if (start >= 0 && end > start) {
                    List<int[]> sp = literalSpans.get(rel);
                    if (sp == null) {
                        sp = new ArrayList<int[]>();
                        literalSpans.put(rel, sp);
                    }
                    sp.add(new int[] {(int) start, (int) end});
                }
            }
            return super.visitLiteral(t, v);
        }
    }
}
