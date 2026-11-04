// Read the built artifact as bytecode and report what it actually references.
//
//   java -cp asm.jar:. ArtifactScan <out.jsonl> <root> [<root> ...]
//
// A root is a directory (walked for *.class, *.jar, *.war, *.ear) or an
// archive. Archives are opened recursively, so a war's WEB-INF/lib is in scope
// and a Spring Boot jar's BOOT-INF/lib is too.
//
// For every class this records the class-file version, the supertypes, every
// type name reachable from the constant pool, the descriptors, the signatures,
// the annotations and the bytecode, plus every string constant that looks like
// a dotted identifier -- so a dependency reached by reflection is visible as
// well as one reached by a symbolic reference.
//
// Output is JSON Lines so a scan of a fat jar stays streamable:
//   {"kind":"header",...}  once
//   {"kind":"entry",...}   one per non-class archive entry or plain file
//   {"kind":"class",...}   one per class file
//
// This file measures a property -- "no class in the artifact refers to type X"
// -- not a tool.
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
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
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.jar.Attributes;
import java.util.jar.Manifest;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import org.objectweb.asm.AnnotationVisitor;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.FieldVisitor;
import org.objectweb.asm.Handle;
import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.Type;
import org.objectweb.asm.TypePath;
import org.objectweb.asm.signature.SignatureReader;
import org.objectweb.asm.signature.SignatureVisitor;

public final class ArtifactScan {

  static final int API = Opcodes.ASM9;

  // A string constant only counts as a possible reflective reference when it
  // is shaped like a fully qualified name. Anything looser turns every log
  // message into a finding.
  static final Pattern DOTTED =
      Pattern.compile("^[a-zA-Z_$][a-zA-Z0-9_$]*(\\.[a-zA-Z_$][a-zA-Z0-9_$]*)+$");

  final PrintWriter out;

  ArtifactScan(PrintWriter out) {
    this.out = out;
  }

  public static void main(String[] args) throws Exception {
    if (args.length < 2) {
      System.err.println("usage: ArtifactScan <out.jsonl> <root> [<root> ...]");
      System.exit(2);
    }
    Writer w = new OutputStreamWriter(
        Files.newOutputStream(Paths.get(args[0])), StandardCharsets.UTF_8);
    PrintWriter pw = new PrintWriter(w, false);
    ArtifactScan scan = new ArtifactScan(pw);

    List<String> roots = new ArrayList<String>();
    for (int i = 1; i < args.length; i++) {
      roots.add(args[i]);
    }
    scan.header(roots);
    for (String r : roots) {
      scan.root(Paths.get(r));
    }
    pw.flush();
    pw.close();
  }

  void header(List<String> roots) {
    StringBuilder sb = new StringBuilder();
    sb.append("{\"kind\":\"header\",\"roots\":[");
    for (int i = 0; i < roots.size(); i++) {
      if (i > 0) sb.append(',');
      json(sb, roots.get(i));
    }
    sb.append("]}");
    out.println(sb);
  }

  // ---------------------------------------------------------------- walking

  void root(final Path p) throws IOException {
    if (Files.isDirectory(p)) {
      Files.walkFileTree(p, new SimpleFileVisitor<Path>() {
        @Override
        public FileVisitResult visitFile(Path f, BasicFileAttributes a)
            throws IOException {
          member(p.relativize(f).toString(), Files.readAllBytes(f));
          return FileVisitResult.CONTINUE;
        }
      });
    } else if (Files.isRegularFile(p)) {
      member(p.getFileName().toString(), Files.readAllBytes(p));
    } else {
      System.err.println("ArtifactScan: no such root: " + p);
      System.exit(3);
    }
  }

  /** One file, wherever it came from: a class, an archive, or neither. */
  void member(String origin, byte[] bytes) throws IOException {
    String lower = origin.toLowerCase(Locale.ROOT);
    if (lower.endsWith(".class")) {
      klass(origin, bytes);
    } else if (lower.endsWith(".jar") || lower.endsWith(".war")
        || lower.endsWith(".ear") || lower.endsWith(".zip")) {
      entry(origin, bytes.length);
      archive(origin, bytes);
    } else {
      entry(origin, bytes.length);
      if (lower.endsWith("meta-inf/manifest.mf")) {
        readManifest(origin, bytes);
      }
    }
  }

  void archive(String origin, byte[] bytes) throws IOException {
    ZipInputStream zin =
        new ZipInputStream(new java.io.ByteArrayInputStream(bytes));
    ZipEntry ze;
    while ((ze = zin.getNextEntry()) != null) {
      if (ze.isDirectory()) {
        continue;
      }
      member(origin + "!" + ze.getName(), readAll(zin));
    }
    zin.close();
  }

  /**
   * One record per manifest, tagged with where it came from.
   *
   * <p>Merging them would be a lie: a war bundles thirty dependency manifests
   * and the merged result claims whatever `Main-Class` happened to be read
   * first. A gate that wants the artifact's own manifest asks for the record
   * whose origin has no `!` in it.
   */
  void readManifest(String origin, byte[] bytes) {
    Map<String, String> attrs = new TreeMap<String, String>();
    try {
      Manifest mf = new Manifest(new java.io.ByteArrayInputStream(bytes));
      Attributes a = mf.getMainAttributes();
      for (Object k : a.keySet()) {
        attrs.put(k.toString(), a.getValue(k.toString()));
      }
    } catch (IOException e) {
      attrs.put("!unreadable", e.toString());
    }
    StringBuilder sb = new StringBuilder();
    sb.append("{\"kind\":\"manifest\",\"o\":");
    json(sb, origin);
    sb.append(",\"attrs\":{");
    boolean first = true;
    for (Map.Entry<String, String> e : attrs.entrySet()) {
      if (!first) {
        sb.append(',');
      }
      first = false;
      json(sb, e.getKey());
      sb.append(':');
      json(sb, e.getValue());
    }
    sb.append("}}");
    out.println(sb);
  }

  static byte[] readAll(InputStream in) throws IOException {
    ByteArrayOutputStream bos = new ByteArrayOutputStream();
    byte[] buf = new byte[16384];
    int n;
    while ((n = in.read(buf)) > 0) {
      bos.write(buf, 0, n);
    }
    return bos.toByteArray();
  }

  void entry(String origin, int size) {
    StringBuilder sb = new StringBuilder();
    sb.append("{\"kind\":\"entry\",\"o\":");
    json(sb, origin);
    sb.append(",\"size\":").append(size).append('}');
    out.println(sb);
  }

  // ----------------------------------------------------------------- classes

  void klass(String origin, byte[] bytes) {
    if (bytes.length < 8
        || (bytes[0] & 0xff) != 0xca || (bytes[1] & 0xff) != 0xfe) {
      entry(origin, bytes.length);      // a *.class that is not a class file
      return;
    }
    Collector c = new Collector();
    try {
      new ClassReader(bytes).accept(c, ClassReader.SKIP_FRAMES);
      constantPool(bytes, c);
    } catch (RuntimeException e) {
      System.err.println("ArtifactScan: unreadable class " + origin + ": " + e);
      entry(origin, bytes.length);
      return;
    }
    StringBuilder sb = new StringBuilder();
    sb.append("{\"kind\":\"class\",\"n\":");
    json(sb, c.name);
    sb.append(",\"v\":").append(c.major);
    sb.append(",\"o\":");
    json(sb, origin);
    sb.append(",\"s\":");
    json(sb, c.superName);
    sb.append(",\"i\":");
    arr(sb, c.interfaces);
    sb.append(",\"a\":");
    arr(sb, c.annotations);
    sb.append(",\"r\":");
    arr(sb, c.refs);
    sb.append(",\"l\":");
    arr(sb, c.lits);
    sb.append('}');
    out.println(sb);
  }

  /**
   * String constants, read straight out of the constant pool.
   *
   * <p>ASM reports the ones an instruction loads; a string that is only an
   * annotation element or an unreferenced leftover never reaches a visitor,
   * and a reflective lookup can hide in either.
   */
  void constantPool(byte[] b, Collector c) {
    ClassReader cr = new ClassReader(b);
    char[] buf = new char[cr.getMaxStringLength()];
    for (int i = 1; i < cr.getItemCount(); i++) {
      int off = cr.getItem(i);
      if (off <= 0) {
        continue;                       // the dead slot after a long or double
      }
      int tag = cr.readByte(off - 1);
      try {
        if (tag == 8) {                 // CONSTANT_String
          c.lit(cr.readUTF8(off, buf));
        } else if (tag == 7) {          // CONSTANT_Class
          c.internal(cr.readUTF8(off, buf));
        } else if (tag == 12) {         // CONSTANT_NameAndType
          c.desc(cr.readUTF8(off + 2, buf));
        } else if (tag == 16) {         // CONSTANT_MethodType
          c.desc(cr.readUTF8(off, buf));
        }
      } catch (RuntimeException e) {
        // a malformed entry is not a reason to lose the rest of the class
      }
    }
  }

  static final class Collector extends ClassVisitor {
    String name = "";
    String superName = "";
    int major;
    final TreeSet<String> interfaces = new TreeSet<String>();
    final TreeSet<String> annotations = new TreeSet<String>();
    final TreeSet<String> refs = new TreeSet<String>();
    final TreeSet<String> lits = new TreeSet<String>();

    Collector() {
      super(API);
    }

    void lit(String s) {
      if (s != null && s.length() <= 512 && DOTTED.matcher(s).matches()) {
        lits.add(s);
      }
    }

    void internal(String in) {
      if (in == null || in.isEmpty()) {
        return;
      }
      if (in.charAt(0) == '[') {
        desc(in);
      } else {
        refs.add(in.replace('/', '.'));
      }
    }

    void desc(String d) {
      if (d == null || d.isEmpty()) {
        return;
      }
      try {
        if (d.charAt(0) == '(') {
          Type m = Type.getMethodType(d);
          type(m.getReturnType());
          for (Type a : m.getArgumentTypes()) {
            type(a);
          }
        } else {
          type(Type.getType(d));
        }
      } catch (RuntimeException e) {
        // not a descriptor: a NameAndType's name slot, for instance
      }
    }

    void type(Type t) {
      while (t.getSort() == Type.ARRAY) {
        t = t.getElementType();
      }
      if (t.getSort() == Type.OBJECT) {
        refs.add(t.getClassName());
      }
    }

    void sig(String s) {
      if (s == null) {
        return;
      }
      new SignatureReader(s).accept(new SignatureVisitor(API) {
        @Override
        public void visitClassType(String n) {
          internal(n);
        }
      });
    }

    void anno(String d) {
      desc(d);
      try {
        annotations.add(Type.getType(d).getClassName());
      } catch (RuntimeException e) {
        // an unparsable annotation descriptor is already recorded by desc()
      }
    }

    @Override
    public void visit(int version, int access, String n, String signature,
        String sup, String[] ifaces) {
      major = version & 0xffff;
      name = n == null ? "" : n.replace('/', '.');
      superName = sup == null ? "" : sup.replace('/', '.');
      if (ifaces != null) {
        for (String f : ifaces) {
          interfaces.add(f.replace('/', '.'));
          internal(f);
        }
      }
      internal(sup);
      sig(signature);
    }

    @Override
    public void visitOuterClass(String owner, String n, String d) {
      internal(owner);
      desc(d);
    }

    @Override
    public void visitNestHost(String host) {
      internal(host);
    }

    @Override
    public void visitNestMember(String m) {
      internal(m);
    }

    @Override
    public void visitPermittedSubclass(String s) {
      internal(s);
    }

    @Override
    public void visitInnerClass(String n, String outer, String inner, int a) {
      internal(n);
      internal(outer);
    }

    @Override
    public AnnotationVisitor visitAnnotation(String d, boolean visible) {
      anno(d);
      return new Anno(this);
    }

    @Override
    public AnnotationVisitor visitTypeAnnotation(int ref, TypePath p, String d,
        boolean visible) {
      anno(d);
      return new Anno(this);
    }

    @Override
    public FieldVisitor visitField(int access, String n, String d, String s,
        Object value) {
      desc(d);
      sig(s);
      if (value instanceof String) {
        lit((String) value);
      }
      return new Fld(this);
    }

    @Override
    public MethodVisitor visitMethod(int access, String n, String d, String s,
        String[] exceptions) {
      desc(d);
      sig(s);
      if (exceptions != null) {
        for (String e : exceptions) {
          internal(e);
        }
      }
      return new Mth(this);
    }
  }

  static final class Anno extends AnnotationVisitor {
    final Collector c;

    Anno(Collector c) {
      super(API);
      this.c = c;
    }

    @Override
    public void visit(String n, Object value) {
      if (value instanceof String) {
        c.lit((String) value);
      } else if (value instanceof Type) {
        c.type((Type) value);
      }
    }

    @Override
    public void visitEnum(String n, String d, String value) {
      c.desc(d);
    }

    @Override
    public AnnotationVisitor visitAnnotation(String n, String d) {
      c.anno(d);
      return this;
    }

    @Override
    public AnnotationVisitor visitArray(String n) {
      return this;
    }
  }

  static final class Fld extends FieldVisitor {
    final Collector c;

    Fld(Collector c) {
      super(API);
      this.c = c;
    }

    @Override
    public AnnotationVisitor visitAnnotation(String d, boolean v) {
      c.anno(d);
      return new Anno(c);
    }

    @Override
    public AnnotationVisitor visitTypeAnnotation(int r, TypePath p, String d,
        boolean v) {
      c.anno(d);
      return new Anno(c);
    }
  }

  static final class Mth extends MethodVisitor {
    final Collector c;

    Mth(Collector c) {
      super(API);
      this.c = c;
    }

    @Override
    public AnnotationVisitor visitAnnotation(String d, boolean v) {
      c.anno(d);
      return new Anno(c);
    }

    @Override
    public AnnotationVisitor visitTypeAnnotation(int r, TypePath p, String d,
        boolean v) {
      c.anno(d);
      return new Anno(c);
    }

    @Override
    public AnnotationVisitor visitParameterAnnotation(int i, String d,
        boolean v) {
      c.anno(d);
      return new Anno(c);
    }

    @Override
    public AnnotationVisitor visitInsnAnnotation(int r, TypePath p, String d,
        boolean v) {
      c.anno(d);
      return new Anno(c);
    }

    @Override
    public AnnotationVisitor visitTryCatchAnnotation(int r, TypePath p,
        String d, boolean v) {
      c.anno(d);
      return new Anno(c);
    }

    @Override
    public AnnotationVisitor visitLocalVariableAnnotation(int r, TypePath p,
        Label[] s, Label[] e, int[] idx, String d, boolean v) {
      c.anno(d);
      return new Anno(c);
    }

    @Override
    public void visitTypeInsn(int op, String t) {
      c.internal(t);
    }

    @Override
    public void visitFieldInsn(int op, String owner, String n, String d) {
      c.internal(owner);
      c.desc(d);
    }

    @Override
    public void visitMethodInsn(int op, String owner, String n, String d,
        boolean itf) {
      c.internal(owner);
      c.desc(d);
    }

    @Override
    public void visitInvokeDynamicInsn(String n, String d, Handle bsm,
        Object... args) {
      c.desc(d);
      handle(bsm);
      if (args != null) {
        for (Object a : args) {
          constant(a);
        }
      }
    }

    @Override
    public void visitLdcInsn(Object value) {
      constant(value);
    }

    @Override
    public void visitMultiANewArrayInsn(String d, int dims) {
      c.desc(d);
    }

    @Override
    public void visitTryCatchBlock(Label s, Label e, Label h, String type) {
      c.internal(type);
    }

    @Override
    public void visitLocalVariable(String n, String d, String s, Label a,
        Label b, int i) {
      c.desc(d);
      c.sig(s);
    }

    void handle(Handle h) {
      if (h == null) {
        return;
      }
      c.internal(h.getOwner());
      c.desc(h.getDesc());
    }

    void constant(Object value) {
      if (value instanceof String) {
        c.lit((String) value);
      } else if (value instanceof Type) {
        Type t = (Type) value;
        if (t.getSort() == Type.METHOD) {
          c.desc(t.getDescriptor());
        } else {
          c.type(t);
        }
      } else if (value instanceof Handle) {
        handle((Handle) value);
      } else if (value instanceof org.objectweb.asm.ConstantDynamic) {
        org.objectweb.asm.ConstantDynamic cd =
            (org.objectweb.asm.ConstantDynamic) value;
        c.desc(cd.getDescriptor());
        handle(cd.getBootstrapMethod());
        for (int i = 0; i < cd.getBootstrapMethodArgumentCount(); i++) {
          constant(cd.getBootstrapMethodArgument(i));
        }
      }
    }
  }

  // -------------------------------------------------------------------- json

  static void arr(StringBuilder sb, Iterable<String> it) {
    sb.append('[');
    boolean first = true;
    for (String s : it) {
      if (!first) {
        sb.append(',');
      }
      first = false;
      json(sb, s);
    }
    sb.append(']');
  }

  static void json(StringBuilder sb, String s) {
    if (s == null) {
      sb.append("null");
      return;
    }
    sb.append('"');
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
    sb.append('"');
  }
}
