// Parse every Java source file in the repository and report its AST facts.
//
//   java -cp javaparser.jar:. AstScan <out.jsonl> <root> [<root> ...]
//
// Roots are walked for *.java with no exclusions: the scope is the whole
// repository, vendored source included. A file that fails to parse is reported
// as a parse-error record rather than dropped, so a tree cannot lower a count
// by becoming unparsable.
//
// Output is JSON Lines:
//   {"kind":"file",...}          one per source file (package, imports, package annos)
//   {"kind":"parse-error",...}   one per source file that did not parse
//   {"kind":"type",...}          one per declared type, nested types included
//   {"kind":"method",...}        one per declared method or constructor
//   {"kind":"field",...}         one per declared field VARIABLE
//   {"kind":"suppress",...}      one per suppression SITE (not per file)
//   {"kind":"anno",...}          one per annotation that carries arguments
//   {"kind":"node",...}          one per counted construct occurrence
//   {"kind":"typeuse",...}       one per written type name
//   {"kind":"summary",...}       once
import com.github.javaparser.JavaParser;
import com.github.javaparser.ParseResult;
import com.github.javaparser.ParserConfiguration;
import com.github.javaparser.Position;
import com.github.javaparser.ast.CompilationUnit;
import com.github.javaparser.ast.Node;
import com.github.javaparser.ast.PackageDeclaration;
import com.github.javaparser.ast.NodeList;
import com.github.javaparser.ast.body.AnnotationDeclaration;
import com.github.javaparser.ast.body.BodyDeclaration;
import com.github.javaparser.ast.body.ClassOrInterfaceDeclaration;
import com.github.javaparser.ast.body.ConstructorDeclaration;
import com.github.javaparser.ast.body.FieldDeclaration;
import com.github.javaparser.ast.body.VariableDeclarator;
import com.github.javaparser.ast.body.EnumConstantDeclaration;
import com.github.javaparser.ast.body.EnumDeclaration;
import com.github.javaparser.ast.body.MethodDeclaration;
import com.github.javaparser.ast.body.Parameter;
import com.github.javaparser.ast.body.RecordDeclaration;
import com.github.javaparser.ast.body.TypeDeclaration;
import com.github.javaparser.ast.expr.AnnotationExpr;
import com.github.javaparser.ast.expr.ArrayInitializerExpr;
import com.github.javaparser.ast.expr.CastExpr;
import com.github.javaparser.ast.expr.Expression;
import com.github.javaparser.ast.expr.InstanceOfExpr;
import com.github.javaparser.ast.expr.LambdaExpr;
import com.github.javaparser.ast.expr.NullLiteralExpr;
import com.github.javaparser.ast.expr.MemberValuePair;
import com.github.javaparser.ast.expr.MethodCallExpr;
import com.github.javaparser.ast.expr.MethodReferenceExpr;
import com.github.javaparser.ast.expr.NormalAnnotationExpr;
import com.github.javaparser.ast.expr.ObjectCreationExpr;
import com.github.javaparser.ast.expr.SingleMemberAnnotationExpr;
import com.github.javaparser.ast.expr.StringLiteralExpr;
import com.github.javaparser.ast.stmt.CatchClause;
import com.github.javaparser.ast.stmt.ReturnStmt;
import com.github.javaparser.ast.stmt.SynchronizedStmt;
import com.github.javaparser.ast.stmt.TryStmt;
import com.github.javaparser.ast.type.ClassOrInterfaceType;
import com.github.javaparser.ast.type.Type;
import com.github.javaparser.ast.type.UnionType;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

public final class AstScan {

  final PrintWriter out;
  final JavaParser parser;
  int files;
  int errors;

  AstScan(PrintWriter out) {
    ParserConfiguration cfg = new ParserConfiguration()
        .setLanguageLevel(ParserConfiguration.LanguageLevel.JAVA_21)
        .setAttributeComments(true);
    this.parser = new JavaParser(cfg);
    this.out = out;
  }

  public static void main(String[] args) throws Exception {
    if (args.length < 2) {
      System.err.println("usage: AstScan <out.jsonl> <root> [<root> ...]");
      System.exit(2);
    }
    Writer w = new OutputStreamWriter(
        Files.newOutputStream(Paths.get(args[0])), StandardCharsets.UTF_8);
    PrintWriter pw = new PrintWriter(w, false);
    AstScan scan = new AstScan(pw);
    for (int i = 1; i < args.length; i++) {
      scan.root(Paths.get(args[i]).toAbsolutePath().normalize());
    }
    scan.emit("{\"kind\":\"summary\",\"files\":" + scan.files
        + ",\"errors\":" + scan.errors + "}");
    pw.flush();
    pw.close();
    // A tree whose sources do not parse cannot be measured, and a gate that
    // reports zero findings over an unparsable tree is worse than no gate.
    if (scan.errors > 0) {
      System.err.println("AstScan: " + scan.errors + " file(s) did not parse");
    }
  }

  void root(final Path p) throws IOException {
    if (!Files.isDirectory(p)) {
      return;
    }
    final Path base = p;
    Files.walkFileTree(p, new SimpleFileVisitor<Path>() {
      @Override
      public FileVisitResult visitFile(Path f, BasicFileAttributes a) {
        if (f.getFileName().toString().endsWith(".java")) {
          file(base, f);
        }
        return FileVisitResult.CONTINUE;
      }
    });
  }

  void file(Path base, Path f) {
    String rel = base.relativize(f).toString();
    files++;
    ParseResult<CompilationUnit> r;
    try {
      r = parser.parse(f);
    } catch (IOException e) {
      errors++;
      emit("{\"kind\":\"parse-error\",\"p\":" + j(rel) + ",\"msg\":"
          + j(e.toString()) + "}");
      return;
    }
    if (!r.isSuccessful() || !r.getResult().isPresent()) {
      errors++;
      emit("{\"kind\":\"parse-error\",\"p\":" + j(rel) + ",\"msg\":"
          + j(String.valueOf(r.getProblems())) + "}");
      return;
    }
    CompilationUnit cu = r.getResult().get();

    StringBuilder sb = new StringBuilder();
    sb.append("{\"kind\":\"file\",\"p\":").append(j(rel));
    sb.append(",\"pkg\":").append(j(cu.getPackageDeclaration()
        .map(d -> d.getNameAsString()).orElse("")));
    sb.append(",\"imports\":[");
    boolean first = true;
    for (var im : cu.getImports()) {
      if (!first) {
        sb.append(',');
      }
      first = false;
      sb.append(j(im.getNameAsString() + (im.isAsterisk() ? ".*" : "")
          + (im.isStatic() ? " static" : "")));
    }
    sb.append("]");
    // Annotations on the package declaration itself. Added for the rung that
    // asks whether a package states its own nullness: a package annotation is
    // written in package-info.java, which declares no type and so produces no
    // `type` record, and `@NullMarked` carries no arguments and so produces no
    // `anno` record either. Without this the only witness was the source text.
    sb.append(",\"pkgannos\":[");
    first = true;
    if (cu.getPackageDeclaration().isPresent()) {
      PackageDeclaration pd = cu.getPackageDeclaration().get();
      for (AnnotationExpr a : pd.getAnnotations()) {
        if (!first) {
          sb.append(',');
        }
        first = false;
        sb.append(j(a.getNameAsString()));
      }
    }
    sb.append("]}");
    emit(sb.toString());

    for (TypeDeclaration<?> td : cu.findAll(TypeDeclaration.class)) {
      type(rel, td);
    }
    for (MethodDeclaration md : cu.findAll(MethodDeclaration.class)) {
      method(rel, md.getNameAsString(), md, md.getParameters(),
          md.getModifiers().toString(), md.getType().asString());
    }
    for (ConstructorDeclaration cd : cu.findAll(ConstructorDeclaration.class)) {
      method(rel, "<init>", cd, cd.getParameters(),
          cd.getModifiers().toString(), "");
    }
    for (FieldDeclaration fd : cu.findAll(FieldDeclaration.class)) {
      field(rel, fd);
    }
    for (AnnotationExpr an : cu.findAll(AnnotationExpr.class)) {
      suppression(rel, an);
      annotation(rel, an);
    }
    constructs(rel, cu);
    for (ClassOrInterfaceType t : cu.findAll(ClassOrInterfaceType.class)) {
      // `targs` says only whether type arguments were written. Whether that
      // makes the use a raw type depends on the declaration, which needs
      // symbol resolution; javac's own -Xlint:rawtypes is the authority there.
      emit("{\"kind\":\"typeuse\",\"p\":" + j(rel) + ",\"line\":" + line(t)
          + ",\"n\":" + j(t.getNameWithScope())
          + ",\"targs\":" + (t.getTypeArguments().isPresent() ? "true" : "false")
          + "}");
    }
  }

  void type(String rel, TypeDeclaration<?> td) {
    String kind = td instanceof RecordDeclaration ? "record"
        : td instanceof EnumDeclaration ? "enum"
        : td instanceof AnnotationDeclaration ? "annotation"
        : (td instanceof ClassOrInterfaceDeclaration
            && ((ClassOrInterfaceDeclaration) td).isInterface())
            ? "interface" : "class";
    StringBuilder sb = new StringBuilder();
    sb.append("{\"kind\":\"type\",\"p\":").append(j(rel));
    sb.append(",\"n\":").append(j(td.getFullyQualifiedName().orElse(
        td.getNameAsString())));
    sb.append(",\"kindOf\":").append(j(kind));
    sb.append(",\"public\":").append(td.isPublic() ? "true" : "false");
    sb.append(",\"extends\":[");
    boolean first = true;
    if (td instanceof ClassOrInterfaceDeclaration) {
      for (ClassOrInterfaceType t
          : ((ClassOrInterfaceDeclaration) td).getExtendedTypes()) {
        if (!first) {
          sb.append(',');
        }
        first = false;
        sb.append(j(t.getNameWithScope()));
      }
    }
    sb.append("],\"implements\":[");
    first = true;
    List<ClassOrInterfaceType> impl = new ArrayList<>();
    if (td instanceof ClassOrInterfaceDeclaration) {
      impl.addAll(((ClassOrInterfaceDeclaration) td).getImplementedTypes());
    } else if (td instanceof EnumDeclaration) {
      impl.addAll(((EnumDeclaration) td).getImplementedTypes());
    } else if (td instanceof RecordDeclaration) {
      impl.addAll(((RecordDeclaration) td).getImplementedTypes());
    }
    for (ClassOrInterfaceType t : impl) {
      if (!first) {
        sb.append(',');
      }
      first = false;
      sb.append(j(t.getNameWithScope()));
    }
    sb.append("],\"annos\":[");
    first = true;
    for (AnnotationExpr a : td.getAnnotations()) {
      if (!first) {
        sb.append(',');
      }
      first = false;
      sb.append(j(a.getNameAsString()));
    }
    sb.append("],\"line\":").append(line(td)).append('}');
    emit(sb.toString());
  }

  /**
   * One record per declared method or constructor.
   *
   * <p>`ret` is the return type exactly as it was written -- simple name,
   * qualified name or {@code var}-free type expression, whatever the source
   * says -- and is empty for a constructor. It is here because a gate that
   * asks what a factory method *produces* has nowhere else to look: `sig` is
   * the name and the parameter list, and the produced type is the whole point
   * of a `@Bean`. Added after the fact and purely additive: every existing
   * field keeps its name and meaning.
   *
   * <p>`nulls` is how many times this method's own body returns the null
   * literal. "Its own" is load-bearing: a `return null;` inside a lambda or
   * inside an anonymous class's method belongs to that lambda or that method,
   * not to the one lexically containing it, so each return is attributed to
   * its NEAREST enclosing executable and counted there once. A gate asking
   * "does this method ever hand back null" has nowhere else to look -- the
   * return type is written the same either way -- and the source is the only
   * place the question can be asked at all, since the bytecode of a method
   * that returns null is not distinguishable from one that does not without a
   * dataflow pass. Also purely additive.
   */
  void method(String rel, String name, BodyDeclaration<?> d,
      Iterable<Parameter> params, String mods, String ret) {
    StringBuilder sig = new StringBuilder(name).append('(');
    boolean first = true;
    for (Parameter p : params) {
      if (!first) {
        sig.append(',');
      }
      first = false;
      Type t = p.getType();
      sig.append(t.asString()).append(p.isVarArgs() ? "..." : "");
    }
    sig.append(')');
    StringBuilder sb = new StringBuilder();
    sb.append("{\"kind\":\"method\",\"p\":").append(j(rel));
    sb.append(",\"owner\":").append(j(owner(d)));
    sb.append(",\"sig\":").append(j(sig.toString()));
    sb.append(",\"mods\":").append(j(mods.trim()));
    sb.append(",\"ret\":").append(j(ret));
    sb.append(",\"nulls\":").append(ownNullReturns(d));
    sb.append(",\"annos\":[");
    first = true;
    for (AnnotationExpr a : d.getAnnotations()) {
      if (!first) {
        sb.append(',');
      }
      first = false;
      sb.append(j(a.getNameAsString()));
    }
    sb.append("],\"line\":").append(line(d)).append('}');
    emit(sb.toString());
  }

  /**
   * `return null;` statements whose nearest enclosing executable is {@code d}.
   *
   * <p>The walk stops at the first method, constructor or lambda above the
   * return, which is what makes the count belong to one declaration only. A
   * `return null` in an anonymous `Callable` inside a factory method is that
   * `call()` method's, and `call()` gets its own record.
   */
  static int ownNullReturns(BodyDeclaration<?> d) {
    int n = 0;
    for (ReturnStmt r : d.findAll(ReturnStmt.class)) {
      Optional<Expression> e = r.getExpression();
      if (!e.isPresent() || !(e.get() instanceof NullLiteralExpr)) {
        continue;
      }
      if (nearestExecutable(r) == d) {
        n++;
      }
    }
    return n;
  }

  static Node nearestExecutable(Node n) {
    Optional<Node> p = n.getParentNode();
    while (p.isPresent()) {
      Node q = p.get();
      if (q instanceof MethodDeclaration
          || q instanceof ConstructorDeclaration
          || q instanceof LambdaExpr) {
        return q;
      }
      p = q.getParentNode();
    }
    return null;
  }

  /**
   * One record per declared variable of a field declaration.
   *
   * <p>Per variable and not per declaration, because `int a, b;` declares two
   * fields and a gate counting fields has to see two. Every variable of one
   * declaration repeats that declaration's modifiers, type and annotations,
   * which is what the language says they mean.
   */
  void field(String rel, FieldDeclaration fd) {
    for (VariableDeclarator v : fd.getVariables()) {
      StringBuilder sb = new StringBuilder();
      sb.append("{\"kind\":\"field\",\"p\":").append(j(rel));
      sb.append(",\"owner\":").append(j(owner(fd)));
      sb.append(",\"n\":").append(j(v.getNameAsString()));
      sb.append(",\"type\":").append(j(v.getType().asString()));
      sb.append(",\"mods\":").append(j(fd.getModifiers().toString().trim()));
      sb.append(",\"annos\":[");
      boolean first = true;
      for (AnnotationExpr a : fd.getAnnotations()) {
        if (!first) {
          sb.append(',');
        }
        first = false;
        sb.append(j(a.getNameAsString()));
      }
      sb.append("],\"line\":").append(line(v)).append('}');
      emit(sb.toString());
    }
  }

  static String owner(Node n) {
    Optional<Node> p = n.getParentNode();
    while (p.isPresent()) {
      Node q = p.get();
      if (q instanceof TypeDeclaration) {
        return ((TypeDeclaration<?>) q).getFullyQualifiedName()
            .orElse(((TypeDeclaration<?>) q).getNameAsString());
      }
      p = q.getParentNode();
    }
    return "";
  }

  /**
   * One record per suppression SITE.
   *
   * <p>Sites are counted, not files: consolidating twelve annotations onto
   * one class must not read as eleven fixes. The arguments are recorded so a
   * gate can ask about a specific warning category rather than the total.
   */
  void suppression(String rel, AnnotationExpr an) {
    String name = an.getNameAsString();
    String simple = name.contains(".")
        ? name.substring(name.lastIndexOf('.') + 1) : name;
    if (!simple.startsWith("Suppress") && !simple.equals("SafeVarargs")) {
      return;
    }
    List<String> args = new ArrayList<>();
    if (an instanceof SingleMemberAnnotationExpr) {
      collect(((SingleMemberAnnotationExpr) an).getMemberValue(), args);
    } else if (an instanceof NormalAnnotationExpr) {
      for (MemberValuePair pair : ((NormalAnnotationExpr) an).getPairs()) {
        collect(pair.getValue(), args);
      }
    }
    StringBuilder sb = new StringBuilder();
    sb.append("{\"kind\":\"suppress\",\"p\":").append(j(rel));
    sb.append(",\"line\":").append(line(an));
    sb.append(",\"anno\":").append(j(simple));
    sb.append(",\"owner\":").append(j(owner(an)));
    sb.append(",\"args\":[");
    boolean first = true;
    for (String a : args) {
      if (!first) {
        sb.append(',');
      }
      first = false;
      sb.append(j(a));
    }
    sb.append("]}");
    emit(sb.toString());
  }

  /**
   * One record per annotation that carries arguments.
   *
   * <p>The `annos` lists on type and method records carry names only, so an
   * annotation whose meaning lives in a member value -- `@Bean(initMethod =
   * "start")`, `@Scope("request")` -- reads there as a bare `@Bean` or
   * `@Scope`. This record carries the pairs as written. It is deliberately not
   * the `suppress` record: that one is a census of suppression sites and a
   * gate counts its arguments against a cap, which an unrelated annotation's
   * values must not be added to.
   *
   * <p>Values are recorded as written, not resolved: a constant reference
   * appears as its source text. A gate that wants a literal should ask for
   * one.
   */
  void annotation(String rel, AnnotationExpr an) {
    List<String> keys = new ArrayList<>();
    List<String> args = new ArrayList<>();
    if (an instanceof SingleMemberAnnotationExpr) {
      keys.add("value");
      collect(((SingleMemberAnnotationExpr) an).getMemberValue(), args);
    } else if (an instanceof NormalAnnotationExpr) {
      for (MemberValuePair pair : ((NormalAnnotationExpr) an).getPairs()) {
        keys.add(pair.getNameAsString());
        collect(pair.getValue(), args);
      }
    }
    if (keys.isEmpty()) {
      return;
    }
    String name = an.getNameAsString();
    String simple = name.contains(".")
        ? name.substring(name.lastIndexOf('.') + 1) : name;
    StringBuilder sb = new StringBuilder();
    sb.append("{\"kind\":\"anno\",\"p\":").append(j(rel));
    sb.append(",\"line\":").append(line(an));
    sb.append(",\"n\":").append(j(simple));
    sb.append(",\"owner\":").append(j(owner(an)));
    sb.append(",\"keys\":[");
    strings(sb, keys);
    sb.append("],\"args\":[");
    strings(sb, args);
    sb.append("]}");
    emit(sb.toString());
  }

  void strings(StringBuilder sb, List<String> vs) {
    boolean first = true;
    for (String v : vs) {
      if (!first) {
        sb.append(',');
      }
      first = false;
      sb.append(j(v));
    }
  }

  static void collect(Expression e, List<String> into) {
    if (e instanceof StringLiteralExpr) {
      into.add(((StringLiteralExpr) e).asString());
    } else if (e instanceof ArrayInitializerExpr) {
      for (Expression x : ((ArrayInitializerExpr) e).getValues()) {
        collect(x, into);
      }
    } else {
      into.add(e.toString());
    }
  }

  void constructs(String rel, CompilationUnit cu) {
    for (ObjectCreationExpr o : cu.findAll(ObjectCreationExpr.class)) {
      if (o.getAnonymousClassBody().isPresent()) {
        node(rel, o, "anonymous", o.getType().getNameWithScope());
      }
      if (o.getType().getNameAsString().equals("Thread")) {
        node(rel, o, "new-thread", o.getType().getNameWithScope());
      }
      // `new ArrayList<>()` against `new ArrayList<String>()`. Both halves are
      // counted, because the rung that introduces the diamond is measured by
      // the explicit form going away as much as by the short form appearing --
      // a tree that adds one diamond and keeps forty explicit arguments has
      // not done the work. A raw `new ArrayList()` has no type arguments at
      // all and is neither.
      Optional<NodeList<Type>> ta = o.getType().getTypeArguments();
      if (ta.isPresent()) {
        node(rel, o, ta.get().isEmpty() ? "diamond" : "explicit-type-args",
            o.getType().getNameWithScope());
      }
    }
    for (CatchClause c : cu.findAll(CatchClause.class)) {
      // `catch (A | B e)` against `catch (A e) ... catch (B e)`. Same reason:
      // the count of plain catch clauses is what has to fall.
      node(rel, c, c.getParameter().getType() instanceof UnionType
          ? "multi-catch" : "catch-plain",
          c.getParameter().getType().asString());
    }
    // An enum constant with a body is an anonymous subclass too, and it is not
    // an ObjectCreationExpr -- counting only the latter undercounts by one on
    // this tree.
    for (EnumConstantDeclaration e
        : cu.findAll(EnumConstantDeclaration.class)) {
      if (!e.getClassBody().isEmpty()) {
        node(rel, e, "enum-constant-body", e.getNameAsString());
      }
    }
    for (LambdaExpr l : cu.findAll(LambdaExpr.class)) {
      node(rel, l, "lambda", "");
    }
    // The other half of "an anonymous class becomes a lambda". `String::isEmpty`
    // retires exactly the construct `lambda` is counted for, so a gate that
    // counted only LambdaExpr would fail the more idiomatic of the two answers
    // the instruction offers.
    for (MethodReferenceExpr m : cu.findAll(MethodReferenceExpr.class)) {
      node(rel, m, "method-ref", m.getIdentifier());
    }
    for (SynchronizedStmt s : cu.findAll(SynchronizedStmt.class)) {
      node(rel, s, "synchronized-block", "");
    }
    for (MethodDeclaration m : cu.findAll(MethodDeclaration.class)) {
      if (m.isSynchronized()) {
        node(rel, m, "synchronized-method", m.getNameAsString());
      }
    }
    for (InstanceOfExpr i : cu.findAll(InstanceOfExpr.class)) {
      // A pattern binding is the modern form; the bare test followed by a cast
      // is the construct step 9 retires.
      node(rel, i, i.getPattern().isPresent()
          ? "instanceof-pattern" : "instanceof-plain",
          i.getType().asString());
    }
    for (CastExpr c : cu.findAll(CastExpr.class)) {
      node(rel, c, "cast", c.getType().asString());
    }
    for (TryStmt t : cu.findAll(TryStmt.class)) {
      node(rel, t, t.getResources().isEmpty()
          ? "try-plain" : "try-with-resources", "");
    }
    for (MethodCallExpr m : cu.findAll(MethodCallExpr.class)) {
      node(rel, m, "call", m.getNameAsString());
    }
  }

  void node(String rel, Node n, String type, String detail) {
    emit("{\"kind\":\"node\",\"p\":" + j(rel) + ",\"line\":" + line(n)
        + ",\"t\":" + j(type) + ",\"d\":" + j(detail) + "}");
  }

  static int line(Node n) {
    Optional<Position> p = n.getBegin();
    return p.isPresent() ? p.get().line : -1;
  }

  void emit(String s) {
    out.println(s);
  }

  static String j(String s) {
    if (s == null) {
      return "null";
    }
    StringBuilder sb = new StringBuilder("\"");
    for (int i = 0; i < s.length(); i++) {
      char ch = s.charAt(i);
      switch (ch) {
        case '"':
          sb.append("\\\"");
          break;
        case '\\':
          sb.append("\\\\");
          break;
        case '\n':
          sb.append("\\n");
          break;
        case '\r':
          sb.append("\\r");
          break;
        case '\t':
          sb.append("\\t");
          break;
        default:
          if (ch < 0x20 || ch > 0x7e) {
            sb.append(String.format("\\u%04x", (int) ch));
          } else {
            sb.append(ch);
          }
      }
    }
    return sb.append('"').toString();
  }
}
