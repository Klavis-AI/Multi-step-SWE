package arqgate;

import java.io.File;
import java.io.IOException;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

import javax.lang.model.element.AnnotationMirror;
import javax.lang.model.element.AnnotationValue;
import javax.lang.model.element.Element;
import javax.lang.model.element.ElementKind;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.element.Modifier;
import javax.lang.model.element.TypeElement;
import javax.lang.model.type.DeclaredType;
import javax.lang.model.type.TypeKind;
import javax.lang.model.type.TypeMirror;
import javax.lang.model.util.Elements;
import javax.lang.model.util.Types;

import com.sun.source.tree.AnnotationTree;
import com.sun.source.tree.BlockTree;
import com.sun.source.tree.ClassTree;
import com.sun.source.tree.CompilationUnitTree;
import com.sun.source.tree.EnhancedForLoopTree;
import com.sun.source.tree.ExpressionStatementTree;
import com.sun.source.tree.DoWhileLoopTree;
import com.sun.source.tree.ExpressionTree;
import com.sun.source.tree.ForLoopTree;
import com.sun.source.tree.IdentifierTree;
import com.sun.source.tree.IfTree;
import com.sun.source.tree.LambdaExpressionTree;
import com.sun.source.tree.MethodTree;
import com.sun.source.tree.MemberReferenceTree;
import com.sun.source.tree.MemberSelectTree;
import com.sun.source.tree.MethodInvocationTree;
import com.sun.source.tree.NewArrayTree;
import com.sun.source.tree.NewClassTree;
import com.sun.source.tree.ParameterizedTypeTree;
import com.sun.source.tree.ReturnTree;
import com.sun.source.tree.StatementTree;
import com.sun.source.tree.TryTree;
import com.sun.source.tree.Tree;
import com.sun.source.tree.VariableTree;
import com.sun.source.tree.WhileLoopTree;
import com.sun.source.util.TreeScanner;
import com.sun.source.util.JavacTask;
import com.sun.source.util.Plugin;
import com.sun.source.util.SourcePositions;
import com.sun.source.util.TaskEvent;
import com.sun.source.util.TaskListener;
import com.sun.source.util.TreePath;
import com.sun.source.util.TreePathScanner;
import com.sun.source.util.Trees;

/**
 * The source half of the migrated-ness gates: a javac plugin that reports what
 * only a typed AST can see.
 *
 * <pre>
 *   javac -processorpath arqgate.jar \
 *         "-Xplugin:ArqSourceScan &lt;out.txt&gt; [&lt;source-root&gt;]" \
 *         -cp ... -d ... @sources.txt
 * </pre>
 *
 * <p>Three things live here rather than in {@link Scan}, each because the class
 * file does not carry the answer:
 *
 * <ul>
 * <li><b>Anonymous classes, split by whether the supertype is a functional
 *     interface.</b> {@code Scan} can list every {@code Outer$N} and say what it
 *     extends and implements, but deciding whether {@code Runnable} is a
 *     functional interface means resolving it and counting its abstract
 *     methods. That needs the element model, which needs a compile.</li>
 * <li><b>{@code @SuppressWarnings} sites and their values.</b> The annotation is
 *     {@code RetentionPolicy.SOURCE}. It is <i>not in the class file at all</i>,
 *     so no bytecode tool of any kind can count it -- and rule 11 requires
 *     suppressed findings to be counted, because a gate that reads only the
 *     warnings javac printed can be satisfied by silencing javac.</li>
 * <li><b>{@code native} method declarations, by source position.</b> {@code Scan}
 *     reports these from the class file; having both is a cross-check between
 *     two independent instruments rather than a duplicate.</li>
 * </ul>
 *
 * <p>Like {@code Scan}, <b>it measures and never decides.</b> No thresholds and
 * no allow list: it reports a functional-interface anonymous class the same way
 * whether the step in question wants zero of them or does not care.
 *
 * <p>Output is the same flat, sorted, fixed-string-greppable shape:
 *
 * <pre>
 *   files=&lt;n&gt;                       compilation units analysed
 *   anonymous.size=&lt;n&gt;              anonymous class creation sites
 *   anonymousfunctional.size=&lt;n&gt;    ...whose supertype is a functional interface
 *   anonymousunresolved.size=&lt;n&gt;     ...whose supertype would not resolve; must be 0
 *   suppress.sites=&lt;n&gt;              &#64;SuppressWarnings annotations
 *   suppress.pairs=&lt;n&gt;              (site, value) pairs; a site may carry several
 *   suppress.value.&lt;name&gt;=&lt;n&gt;
 *   native.size=&lt;n&gt;
 *   anonymous[i]=&lt;file&gt;:&lt;line&gt; &lt;supertype&gt; functional=yes|no abstract=&lt;n&gt;
 *   suppress[i]=&lt;file&gt;:&lt;line&gt; &lt;value&gt; on=&lt;kind&gt;
 *   native[i]=&lt;file&gt;:&lt;line&gt; &lt;owner&gt;.&lt;method&gt;
 *
 *   newgeneric.explicit=&lt;n&gt;         new X&lt;A&gt;(..) spelling its type arguments
 *   newgeneric.diamond=&lt;n&gt;          new X&lt;&gt;(..)
 *   newgeneric.raw=&lt;n&gt;              new X(..) where X declares type parameters
 *   newgeneric.explicitanon=&lt;n&gt;     ...of the explicit ones, those with a class
 *                                   body, where &lt;&gt; is Java 9 and not Java 7
 *   try.size=&lt;n&gt;                    try statements
 *   try.resources=&lt;n&gt;               ...declaring a resource list
 *   try.resourcevars=&lt;n&gt;            resources declared across them
 *   close.manual=&lt;n&gt;                explicit no-argument .close() invocations
 *   iofile.new=&lt;n&gt;                  new java.io.File / File{Input,Output}Stream
 *                                   / File{Reader,Writer}
 *   iofile.decl=&lt;n&gt;                 variables declared at one of those types
 *   niofile.use=&lt;n&gt;                 new-expressions and variables at a
 *                                   java.nio.file type
 *   niofile.ref=&lt;n&gt;                 member selects whose qualifier IS a
 *                                   java.nio.file type: Paths.get, Files.exists
 *   niofile.staticref=&lt;n&gt;           the same calls written through a static
 *                                   import, where there is no qualifier to read
 *   newgeneric[i]=&lt;file&gt;:&lt;line&gt; &lt;type&gt; args=&lt;n&gt; anon=yes|no
 *   close[i]=&lt;file&gt;:&lt;line&gt; &lt;receiver type&gt;
 *   iofile[i]=&lt;file&gt;:&lt;line&gt; &lt;type&gt; as=new|decl
 *
 *   anonymousfunctional.convertible=&lt;n&gt;  ...that have a lambda form at all
 *   lambda.size=&lt;n&gt; / .files=&lt;n&gt;    lambda expressions, and their spread
 *   methodref.size=&lt;n&gt;              Foo::bar, this::bar, Foo::new
 *   foreach.size=&lt;n&gt;                enhanced-for statements
 *   foreach.collect=&lt;n&gt;             ...whose body is only [if (c)] dst.add(e)
 *                                   and throws nothing a lambda cannot
 *   foreach.unresolved=&lt;n&gt;          calls in those bodies the classpath could
 *                                   not resolve, so that a count which moved
 *                                   for that reason is visible as such
 *   stream.producers=&lt;n&gt;            calls RETURNING a java.util.stream type
 *   stream.ops=&lt;n&gt;                  calls ON one
 *   stream.files=&lt;n&gt;                distinct files holding a producer, so that
 *                                   "which file gained a pipeline" can be asked
 *                                   of the report rather than inferred from a
 *                                   total
 *   stream.opfiles=&lt;n&gt;              distinct files holding a STAGE. A producer
 *                                   whose result nobody uses is a statement,
 *                                   not a pipeline, and the two counts are what
 *                                   tell them apart per file
 *   stream.opmethods=&lt;n&gt;            distinct &lt;file&gt;#&lt;method&gt; holding a STAGE.
 *                                   A file is too coarse to ask whether the
 *                                   pipeline arrived WHERE the loop was; this
 *                                   is the same question one scope down
 *   foreach.collectmethods=&lt;n&gt;      distinct &lt;file&gt;#&lt;method&gt; holding a
 *                                   collecting enhanced-for, which is what
 *                                   that arrival is measured against
 *   loop.size=&lt;n&gt;                   every loop statement in the tree -- for,
 *                                   enhanced-for, while, do. A gate that asks
 *                                   whether 24 loops BECAME something else has
 *                                   to be able to see that they did not merely
 *                                   change shape
 *   iface.abstract / .default / .static=&lt;n&gt;
 *   emptyoverride.size=&lt;n&gt;          no-op bodies in abstract classes that
 *                                   implement an interface (the adapter shape)
 *   anonfn[i]=&lt;file&gt;:&lt;line&gt; &lt;supertype&gt; convertible=yes|no why=members|self|nomethod|-
 *   lambda[i]=&lt;file&gt;:&lt;line&gt; &lt;target type&gt;
 *   methodref[i]=&lt;file&gt;:&lt;line&gt; &lt;mode&gt; &lt;name&gt;
 *   foreach[i]=&lt;file&gt;:&lt;line&gt; collect=yes|no in=&lt;method&gt;
 *   loop[i]=&lt;file&gt;:&lt;line&gt; for|foreach|while|do
 *   stream[i]=&lt;file&gt;:&lt;line&gt; ENTER &lt;method&gt; in=&lt;method&gt;  (producers only)
 *   streamop[i]=&lt;file&gt;:&lt;line&gt; ON &lt;method&gt; in=&lt;method&gt;   (stages only)
 *
 *   The trailing {@code in=} on those three is the OUTERMOST enclosing method
 *   by simple name, or {@code -} outside a method. See enclosingMethod().
 *   default[i]=&lt;file&gt;:&lt;line&gt; &lt;interface&gt;.&lt;method&gt;
 *   emptyoverride[i]=&lt;file&gt;:&lt;line&gt; &lt;class&gt;.&lt;method&gt;
 *
 *   -- the Java 8 LIBRARY facts (step 05) --
 *   retnull.size / .methods / .files=&lt;n&gt;
 *                                   {@code return null;} statements, the
 *                                   distinct &lt;file&gt;#&lt;method&gt; holding them, and
 *                                   the distinct files
 *   retnull.api=&lt;n&gt;                 how many of those sit in a method
 *                                   {@code apisig.sh} would print. A
 *                                   cross-check, not a gate input: the gate
 *                                   reads a curated list frozen against
 *                                   baseline/api.txt, because whether a
 *                                   signature is published is that file's
 *                                   question and not this plugin's
 *   optret.size / .files=&lt;n&gt;        methods DECLARED to return
 *                                   {@code java.util.Optional}
 *   optop.size / .methods=&lt;n&gt;       calls on or to {@code Optional}
 *   optop.get=&lt;n&gt;                   of those, instance {@code get()} --
 *                                   counted apart because a pipeline that
 *                                   ends in {@code .get()} has moved the
 *                                   null, not removed it
 *   accum.size / .methods=&lt;n&gt;       {@code src.forEach(sink::add)} accumulate
 *                                   sites, which is the shape step 04 left
 *                                   behind on purpose
 *   collect.size / .methods=&lt;n&gt;     {@code stream.collect(..)} calls
 *   collect.tolist / .toset / .tocollection / .other=&lt;n&gt;
 *                                   which {@code Collectors} factory each
 *                                   names. The split is the point: one of this
 *                                   tree's sinks is a {@code LinkedHashSet}
 *                                   whose iteration order is recorded
 *                                   behaviour, so {@code toSet()} there is a
 *                                   wrong answer that a bare count calls right
 *   objects.size / .methods=&lt;n&gt;     {@code java.util.Objects} calls
 *   jtime.size / .files / .methods=&lt;n&gt;
 *                                   {@code java.time} calls
 *   millis.size / .methods=&lt;n&gt;      {@code System.currentTimeMillis},
 *                                   {@code System.nanoTime} and the legacy
 *                                   calendar types -- the departure half of
 *                                   the same question
 *   retnull[i]=&lt;file&gt;:&lt;line&gt; scope=decl|anon|lambda|- mods=&lt;m&gt; ret=&lt;type&gt; api=yes|no in=&lt;method&gt;
 *   optret[i]=&lt;file&gt;:&lt;line&gt; ret=&lt;type&gt; name=&lt;method&gt; in=&lt;method&gt;
 *   optop[i]=&lt;file&gt;:&lt;line&gt; make|use &lt;name&gt; in=&lt;method&gt;
 *   accum[i]=&lt;file&gt;:&lt;line&gt; sink=&lt;expr&gt; op=add|put|addAll sinktype=&lt;type&gt; in=&lt;method&gt;
 *   collect[i]=&lt;file&gt;:&lt;line&gt; to=&lt;factory&gt; in=&lt;method&gt;
 *   objects[i]=&lt;file&gt;:&lt;line&gt; &lt;name&gt; in=&lt;method&gt;
 *   jtime[i]=&lt;file&gt;:&lt;line&gt; &lt;owner&gt;.&lt;member&gt; as=call in=&lt;method&gt;
 *   millis[i]=&lt;file&gt;:&lt;line&gt; &lt;owner&gt;.&lt;member&gt; in=&lt;method&gt;
 * </pre>
 *
 * <p>Every {@code in=} above is LAST on its line on purpose: {@code bucketOf()}
 * finds the method by searching for {@code " in="} and taking the rest of the
 * line, so a field added after it would be read as part of the method name and
 * would quietly split one bucket into two.
 *
 * <p>The library facts resolve through javac's ELEMENTS, never through the name
 * a call is spelled with. {@code Collectors.toList()} reaches this tree as a
 * bare {@code toList()} under a static import, as {@code Collectors.toList()},
 * and as the fully qualified form; those are one finding. A local helper named
 * {@code collect} or a project class named {@code Optional} is not a finding at
 * all. That is countStream's rule applied to four more types, and it is applied
 * because countStream is where this plugin learned the lesson.
 *
 * <p>The Java 8 block is additive: no existing key changed name or meaning when
 * it was added, because step 03's frozen baselines are fixed strings compared
 * with {@code grep -qxF} and a renamed key would be a silent zero rather than a
 * failure. The library block above is additive in the same sense and for the
 * same reason -- steps 03 and 04 both read this plugin's output and neither
 * knows these keys exist. {@code anonfn[i]} is a second list over the same sites {@code
 * anonymous[i]} already reports, for exactly that reason -- widening the
 * existing line would have been the smaller diff and the larger risk.
 *
 * <p>The Java 7 facts are here for the same reason as the three above: the
 * class file cannot answer them. Generic type arguments are erased, so
 * {@code new ArrayList&lt;String&gt;()} and {@code new ArrayList&lt;&gt;()}
 * compile to the same bytecode; and a {@code try}-with-resources lowers to a
 * {@code try}/{@code finally} with a synthetic suppression dance that is not
 * reliably distinguishable from a hand-written one.
 *
 * <p>{@code newgeneric.explicitanon} is split out because the two are not the
 * same finding. The diamond is forbidden on an anonymous class body until Java
 * <b>9</b>, so on a Java 7 tree those sites are the only correct way to write
 * themselves, and a gate that lumped them in with the rest would be asking for
 * something that does not compile.
 *
 * <p>Counts are <b>sites</b>, not files and not blocks (rule 11). Two anonymous
 * {@code Runnable}s in one method are two findings;
 * {@code @SuppressWarnings({"unchecked", "rawtypes"})} is one site and two
 * pairs, and is reported both ways because "how many places were silenced" and
 * "how many findings were silenced" are different questions with different
 * right answers.
 *
 * <p>The reported path is made relative to the source root given as the
 * plugin's second argument, when that is a prefix of it, so a report does not
 * depend on where the tree happened to be checked out.
 */
public class SourceScan implements Plugin, TaskListener
{
   /** The name the {@code -Xplugin:} argument uses. */
   public static final String NAME = "ArqSourceScan";

   private Trees trees;
   private Elements elements;
   private Types types;
   private File out;
   private String root;

   private final List<String> anonymous = new ArrayList<String>();
   private final List<String> suppress = new ArrayList<String>();
   private final List<String> natives = new ArrayList<String>();
   private final List<String> newGeneric = new ArrayList<String>();
   private final List<String> closes = new ArrayList<String>();
   private final List<String> ioFiles = new ArrayList<String>();
   private final List<String> nioSites = new ArrayList<String>();
   private final List<String> diamonds = new ArrayList<String>();
   private final TreeMap<String, Integer> suppressValues = new TreeMap<String, Integer>();
   private final Set<String> seen = new HashSet<String>();
   private int files;
   private int anonymousFunctional;
   private int anonymousUnresolved;
   private int suppressSites;
   private int suppressPairs;
   private int genericExplicit;
   private int genericExplicitAnon;
   private int genericDiamond;
   private int genericRaw;
   private int tryTotal;
   private int tryResources;
   private int tryResourceVars;
   private int nioUse;
   private int nioRef;
   private int nioStatic;

   // --- Java 8, added for step 04 -------------------------------------------
   private final List<String> anonFn = new ArrayList<String>();
   private final List<String> lambdas = new ArrayList<String>();
   private final List<String> methodRefs = new ArrayList<String>();
   private final List<String> foreachSites = new ArrayList<String>();
   // Producers only, and as a list rather than a count for one reason: step 04's
   // stream gate has to ask which of fourteen NAMED files gained a pipeline, and
   // a total cannot answer that. A tree that deleted the fourteen collecting
   // loops and wrote fourteen unrelated pipelines in a file of its own moves the
   // total exactly as far as one that migrated them in place.
   private final List<String> streamSites = new ArrayList<String>();
   // Stages, as a list for the same reason producers are one. A bare
   // `xs.stream();` is a producer with no stage: it enters the stream world and
   // leaves it again without doing anything, which is what a file-level
   // "does this file hold a pipeline?" question mistakes for a pipeline. Asking
   // both lists for the file is asking whether anything was piped.
   private final List<String> streamOpSites = new ArrayList<String>();
   // Every loop statement, of all four spellings. The collecting loops of C-3
   // are supposed to BECOME pipelines, and `foreach.collect` falling to zero
   // says only that the enhanced-for shape is gone -- a `while (it.hasNext())`
   // or an indexed `for` over the same collection moves it exactly as far. The
   // total is what notices that a loop was reshaped rather than removed.
   private final List<String> loopSites = new ArrayList<String>();
   private final List<String> defaults = new ArrayList<String>();
   private final List<String> emptyOverrides = new ArrayList<String>();
   private final List<String> sams = new ArrayList<String>();
   private int anonFnConvertible;
   private int anonFnForward;
   private int lambdaExpr;
   private int lambdaMethodRefable;
   private int ifaceSam;
   private int ifaceSamAnnotated;
   private int foreachCollect;
   private int foreachUnresolved;
   private int ifaceDefault;
   private int ifaceStatic;
   private int ifaceAbstract;

   // --- the Java 8 LIBRARY facts, added for step 05 -------------------------
   //
   // Additive, exactly as the Java 8 language block above was: no key here
   // renames or re-means an existing one, because steps 03 and 04 compare
   // frozen baselines with `grep -qxF` and a renamed key is a silent zero.
   private final List<String> retNulls = new ArrayList<String>();
   private final List<String> optRets = new ArrayList<String>();
   private final List<String> optOps = new ArrayList<String>();
   private final List<String> accumSites = new ArrayList<String>();
   private final List<String> collectSites = new ArrayList<String>();
   private final List<String> objectsCalls = new ArrayList<String>();
   private final List<String> jtimeSites = new ArrayList<String>();
   private final List<String> millisSites = new ArrayList<String>();
   private int retNullApi;
   private int optOpGet;
   private int optOpIsPresent;
   private int collectToList;
   private int collectToSet;
   private int collectToCollection;
   private int collectOther;

   // --- the Java 9/10 facts, added for step 06 -------------------------------
   //
   // Additive again, on the same rule the step-05 block was added under: every
   // key below is new, and not one key above changes name or meaning, because
   // steps 03, 04 and 05 all compare frozen baselines with `grep -qxF`.
   private final List<String> newInstanceSites = new ArrayList<String>();
   private final List<String> boxedCtorSites = new ArrayList<String>();
   private final List<String> factorySites = new ArrayList<String>();
   private final List<String> legacyCollSites = new ArrayList<String>();
   private final List<String> varSites = new ArrayList<String>();
   private final List<String> varCandidates = new ArrayList<String>();
   private final TreeMap<String, Integer> boxedByType = new TreeMap<String, Integer>();
   private final TreeMap<String, Integer> declSpans = new TreeMap<String, Integer>();
   private int newInstanceLegacy;
   private int newInstanceCtor;
   private int factoryOf;
   private int factoryCopyOf;
   private int legacyCollView;
   private int legacyCollViewField;
   private int legacyCollBuild;
   private final List<String> accessibleSites = new ArrayList<String>();
   private final List<String> getPackageSites = new ArrayList<String>();
   private int accIsAccessible;
   private int accCanAccess;
   private int accSetAccessible;

   public String getName()
   {
      return NAME;
   }

   public void init(JavacTask task, String... args)
   {
      if (args.length < 1)
      {
         throw new IllegalArgumentException(
            "usage: -Xplugin:" + NAME + " <out.txt> [<source-root>]");
      }
      this.out = new File(args[0]);
      this.root = args.length > 1 ? new File(args[1]).getAbsolutePath() : null;
      this.trees = Trees.instance(task);
      this.elements = task.getElements();
      this.types = task.getTypes();
      task.addTaskListener(this);
   }

   public void started(TaskEvent e)
   {
   }

   /**
    * ANALYZE, not PARSE: the parse tree has all the shapes and none of the
    * types, and every question asked here is a question about a resolved type.
    *
    * <p>One event per top-level <em>class</em>, not per file, and the same
    * {@code CompilationUnitTree} comes back on each of them. Both obvious
    * readings are wrong, and each is wrong in its own direction:
    *
    * <ul>
    * <li>Walking the whole unit on every event counts a two-class file's
    *     findings twice -- a gate stricter than it says it is.</li>
    * <li>Walking the whole unit on the <em>first</em> event only resolves
    *     nothing in the later classes, which have not been attributed yet:
    *     every supertype in them comes back null and every anonymous class is
    *     recorded as non-functional. A gate quietly blind to most of the file.
    *     (Measured: {@code Pair.java}'s second {@code new Runnable(){}} scanned
    *     as {@code ? functional=no abstract=-1}.)</li>
    * </ul>
    *
    * <p>So: walk the subtree of the class the event is about, once per class.
    * A unit with no top-level class of its own -- {@code package-info.java} --
    * falls back to a single whole-unit walk, which is safe because there is
    * nothing in it to attribute late.
    */
   public void finished(TaskEvent e)
   {
      if (e.getKind() != TaskEvent.Kind.ANALYZE)
      {
         return;
      }
      CompilationUnitTree unit = e.getCompilationUnit();
      if (unit == null)
      {
         return;
      }
      String uri = unit.getSourceFile().toUri().toString();
      TypeElement type = e.getTypeElement();
      TreePath path = type == null ? null : trees.getPath(type);
      if (path == null)
      {
         if (!seen.add(uri))
         {
            return;
         }
         files++;
         new Walk(unit).scan(new TreePath(unit), null);
      }
      else
      {
         if (!seen.add(uri + "#" + type.getQualifiedName()))
         {
            return;
         }
         if (seen.add(uri))
         {
            files++;
         }
         new Walk(unit).scan(path, null);
      }
      write();
   }

   // ------------------------------------------------------------------ walk

   private final class Walk extends TreePathScanner<Void, Void>
   {
      private final CompilationUnitTree unit;
      private final SourcePositions positions;
      private CharSequence sourceText;
      private boolean sourceRead;

      Walk(CompilationUnitTree unit)
      {
         this.unit = unit;
         this.positions = trees.getSourcePositions();
      }

      /**
       * A class body on a {@code new} expression is what makes it anonymous. An
       * enum constant with a body parses as a {@code NewClassTree} with a body
       * too, and is deliberately included: it is an anonymous subclass by
       * another name, and it is a real source construct a step can be asked to
       * remove -- unlike the synthetic {@code Outer$1} that {@link Scan} has to
       * filter out of the bytecode view.
       */
      @Override
      public Void visitNewClass(NewClassTree node, Void p)
      {
         if (node.getClassBody() != null)
         {
            TypeMirror superType = superTypeOf(getCurrentPath());
            int abstracts = abstractMethods(superType);
            boolean functional = abstracts == 1;
            if (superType == null)
            {
               // Nothing to report about, and worse, indistinguishable in the
               // findings from an anonymous subclass of a class. A run with a
               // non-zero count here has not measured what it claims to; the
               // gate scripts require it to be 0 rather than trusting a total.
               anonymousUnresolved++;
            }
            anonymous.add(where(node) + " " + name(superType)
               + " functional=" + (functional ? "yes" : "no")
               + " abstract=" + abstracts);
            if (functional)
            {
               anonymousFunctional++;
               String why = notLambdable(node);
               boolean fwd = why == null && forwards(soleMethod(node));
               if (why == null)
               {
                  anonFnConvertible++;
                  if (fwd)
                  {
                     anonFnForward++;
                  }
               }
               anonFn.add(where(node) + " " + name(superType)
                  + " convertible=" + (why == null ? "yes" : "no")
                  + " why=" + (why == null ? "-" : why)
                  + " forward=" + (fwd ? "yes" : "no"));
            }
         }
         classifyTypeArguments(node);
         String created = name(trees.getTypeMirror(getCurrentPath()));
         if (IO_FILE_TYPES.contains(created))
         {
            ioFiles.add(where(node) + " " + created + " as=new");
         }
         else if (isNioFile(created))
         {
            nioUse++;
            nioSites.add(where(node) + " " + created + " as=new");
         }
         if (BOXED_TYPES.contains(created))
         {
            boxedCtorSites.add(where(node) + " " + created
               + " in=" + enclosingMethod());
            Integer prev = boxedByType.get(created);
            boxedByType.put(created, prev == null ? 1 : prev + 1);
         }
         return super.visitNewClass(node, p);
      }

      /**
       * Which of the three ways a generic {@code new} can be written this one
       * uses. The question is about the <i>source</i>, so it is answered from
       * the tree: a {@code ParameterizedTypeTree} with arguments is explicit,
       * the same tree with none is the diamond, and a bare identifier naming a
       * type that declares type parameters is raw. A {@code new} of a
       * non-generic type is none of the three and is not reported.
       */
      private void classifyTypeArguments(NewClassTree node)
      {
         Tree id = node.getIdentifier();
         boolean anon = node.getClassBody() != null;
         if (id instanceof ParameterizedTypeTree)
         {
            int args = ((ParameterizedTypeTree) id).getTypeArguments().size();
            if (args == 0)
            {
               genericDiamond++;
               diamonds.add(where(node) + " "
                  + name(trees.getTypeMirror(getCurrentPath()))
                  + " anon=" + (anon ? "yes" : "no"));
            }
            else
            {
               genericExplicit++;
               if (anon)
               {
                  genericExplicitAnon++;
               }
               newGeneric.add(where(node) + " "
                  + name(trees.getTypeMirror(getCurrentPath()))
                  + " args=" + args + " anon=" + (anon ? "yes" : "no"));
            }
            return;
         }
         // A raw use: `new ArrayList()` where ArrayList declares <E>. Reported
         // so that "no explicit type arguments left" cannot be reached by
         // deleting them rather than by replacing them with <>.
         TypeMirror t = trees.getTypeMirror(getCurrentPath());
         if (t != null && t.getKind() == TypeKind.DECLARED)
         {
            Element el = ((DeclaredType) t).asElement();
            if (el instanceof TypeElement
               && !((TypeElement) el).getTypeParameters().isEmpty())
            {
               genericRaw++;
               newGeneric.add(where(node) + " "
                  + ((TypeElement) el).getQualifiedName()
                  + " args=raw anon=" + (anon ? "yes" : "no"));
            }
         }
      }

      /**
       * Whether an anonymous class over a functional interface could be written
       * as a lambda or a method reference <em>without changing what it does</em>,
       * and if not, the one word that says why.
       *
       * <p>Three things stop a conversion, and all three are structural, so they
       * are decidable here rather than guessed:
       *
       * <ul>
       * <li><b>{@code members}</b> -- the body declares anything besides the one
       *     override: a field, an initializer, a nested type, a helper method. A
       *     lambda body has no declarations of its own, so such a class has no
       *     lambda form at all.</li>
       * <li><b>{@code self}</b> -- the body uses bare {@code this} or
       *     {@code super}. Inside an anonymous class those name the anonymous
       *     instance; inside a lambda they name the <em>enclosing</em> instance.
       *     Converting silently changes the referent, which is the one way this
       *     transform can alter behaviour. {@code Outer.this} is <b>not</b> a
       *     blocker: it names the enclosing instance in both forms.</li>
       * <li><b>{@code nomethod}</b> -- no override to turn into a body. Should be
       *     unreachable (such a class would not compile against a functional
       *     interface), and is reported rather than assumed away.</li>
       * </ul>
       *
       * <p>javac synthesises a constructor into an anonymous body by the time
       * ANALYZE runs, so {@code <init>} is skipped: counting it would make every
       * site in the tree read {@code members}.
       *
       * <p>Like everything else here this measures and does not decide -- a site
       * reported {@code convertible=no} is not a failure, it is a site the step's
       * arithmetic must not ask anyone to convert.
       */
      private String notLambdable(NewClassTree node)
      {
         ClassTree body = node.getClassBody();
         MethodTree only = null;
         for (Tree member : body.getMembers())
         {
            if (member instanceof MethodTree)
            {
               MethodTree m = (MethodTree) member;
               if ("<init>".contentEquals(m.getName()))
               {
                  continue;
               }
               if (only != null)
               {
                  return "members";
               }
               only = m;
               continue;
            }
            return "members";
         }
         if (only == null)
         {
            return "nomethod";
         }
         return usesSelf(only) ? "self" : null;
      }

      /**
       * The single override of a site {@link #notLambdable} accepted, or null.
       * It repeats that method's walk rather than sharing a field with it: this
       * scanner recurses through {@code visitNewClass}, and a field holding a
       * tree across a visit is a bug waiting for the first nested anonymous
       * class. The walk is four lines; the field would be a whole invariant.
       */
      private MethodTree soleMethod(NewClassTree node)
      {
         MethodTree only = null;
         for (Tree member : node.getClassBody().getMembers())
         {
            if (member instanceof MethodTree
               && !"<init>".contentEquals(((MethodTree) member).getName()))
            {
               only = (MethodTree) member;
            }
         }
         return only;
      }

      /**
       * Is this body nothing but a call that hands its own parameters straight
       * on, in order? That is the shape -- and the only shape -- that can be
       * spelled as a method reference rather than as a lambda:
       *
       * <pre>
       *   void execute()  { target.run();    }   ->  target::run
       *   Object run()    { return f(a, b);  }   ->  Owner::f     (a, b the params)
       *   T call()        { return new T();  }   ->  T::new
       * </pre>
       *
       * <p>A call that reorders, drops, wraps or adds an argument is not this
       * shape, and asking for a method reference there would be asking for a
       * behaviour change. Zero parameters is the common case in this codebase
       * and is allowed: the receiver carries the bind.
       *
       * <p>This is a DENOMINATOR and not a requirement. Step 04 sizes how many
       * method references the tree can honestly yield from it, so that the gate
       * is not scoring a number copied out of one correct solution's spelling --
       * the mistake NOTES 8.12.3 had to undo in step 03's C-3.
       */
      private boolean forwards(MethodTree method)
      {
         return method != null && method.getBody() != null
            && forwardsBody(method.getBody().getStatements(), method.getParameters());
      }

      /** The same question asked of a lambda, whose body may be an expression. */
      private boolean forwardsLambda(LambdaExpressionTree node)
      {
         Tree body = node.getBody();
         if (body instanceof BlockTree)
         {
            return forwardsBody(((BlockTree) body).getStatements(), node.getParameters());
         }
         return body instanceof ExpressionTree
            && forwardsCall((ExpressionTree) body, node.getParameters());
      }

      private boolean forwardsBody(List<? extends StatementTree> body,
         List<? extends VariableTree> params)
      {
         if (body.size() != 1)
         {
            return false;
         }
         StatementTree only = body.get(0);
         if (only instanceof ReturnTree)
         {
            return forwardsCall(((ReturnTree) only).getExpression(), params);
         }
         if (only instanceof ExpressionStatementTree)
         {
            return forwardsCall(((ExpressionStatementTree) only).getExpression(), params);
         }
         return false;
      }

      private boolean forwardsCall(ExpressionTree call,
         List<? extends VariableTree> params)
      {
         List<? extends ExpressionTree> args;
         if (call instanceof MethodInvocationTree)
         {
            args = ((MethodInvocationTree) call).getArguments();
         }
         else if (call instanceof NewClassTree)
         {
            NewClassTree n = (NewClassTree) call;
            if (n.getClassBody() != null)
            {
               // `new Foo() { .. }` is not a constructor reference.
               return false;
            }
            args = n.getArguments();
         }
         else
         {
            return false;
         }
         if (args.size() != params.size())
         {
            return false;
         }
         for (int i = 0; i < args.size(); i++)
         {
            ExpressionTree a = args.get(i);
            if (!(a instanceof IdentifierTree)
               || !((IdentifierTree) a).getName().contentEquals(params.get(i).getName()))
            {
               return false;
            }
         }
         return true;
      }

      /**
       * Does this method body name the instance it is declared on, as bare
       * {@code this} or {@code super}? Nested class bodies are not descended
       * into: their {@code this} is their own, and an anonymous class nested
       * inside a lambda keeps its own {@code this} unchanged.
       */
      private boolean usesSelf(MethodTree method)
      {
         final boolean[] hit = new boolean[1];
         new TreeScanner<Void, Void>()
         {
            @Override
            public Void visitIdentifier(IdentifierTree id, Void ignored)
            {
               String n = id.getName().toString();
               if ("this".equals(n) || "super".equals(n))
               {
                  hit[0] = true;
               }
               return null;
            }

            @Override
            public Void visitClass(ClassTree nested, Void ignored)
            {
               return null;
            }

            @Override
            public Void visitLambdaExpression(LambdaExpressionTree l, Void ignored)
            {
               // A lambda already inside the body shares the anonymous class's
               // `this`, so its uses count. Scanned, unlike a nested class.
               return super.visitLambdaExpression(l, ignored);
            }
         }.scan(method, null);
         return hit[0];
      }

      /**
       * A lambda, which is one half of what step 04 is about arriving at. Its
       * body shape is reported alongside it, for two different readers:
       * {@code body=expr} vs {@code body=block} says whether the conversion went
       * further than deleting the ceremony, and {@code methodrefable=yes} marks
       * a lambda that is still one step short of the method reference it could
       * be. Neither is a failure on its own.
       */
      @Override
      public Void visitLambdaExpression(LambdaExpressionTree node, Void p)
      {
         boolean expr = !(node.getBody() instanceof BlockTree);
         boolean refable = forwardsLambda(node);
         if (expr)
         {
            lambdaExpr++;
         }
         if (refable)
         {
            lambdaMethodRefable++;
         }
         lambdas.add(where(node) + " "
            + name(trees.getTypeMirror(getCurrentPath()))
            + " body=" + (expr ? "expr" : "block")
            + " methodrefable=" + (refable ? "yes" : "no"));
         return super.visitLambdaExpression(node, p);
      }

      /**
       * Every interface this tree declares, with the number of abstract methods
       * it declares itself and whether it carries {@code @FunctionalInterface}.
       *
       * <p>The count is javac's own rule for a functional interface, taken over
       * the interface's RESOLVED members rather than over the methods it happens
       * to spell out: exactly one abstract method that is not a public method of
       * {@code Object}. {@code abstractMethods} is the same helper the anonymous
       * -class half of this scanner uses on a supertype, so the two halves
       * cannot disagree about what a SAM is.
       *
       * <p>It used to count only the members declared in the interface itself,
       * and that was a defect rather than a conservative choice. It under-counts
       * in one direction -- {@code interface Handler extends Runnable {}}
       * declares nothing and is functional all the same -- and OVER-counts in
       * the other, which is the direction a denominator must not fail in:
       * {@code interface InstanceProducer<T> extends Instance<T> { void set(T v); }}
       * declares one abstract method and inherits a second, so it cannot carry
       * the annotation at all. javac says so in as many words -- "Unexpected
       * {@code @FunctionalInterface} annotation" -- and it said so about two of
       * this tree's 26 apparent SAMs. A gate sized by a population that included
       * them would ask for two annotations that do not compile.
       *
       * <p>Resolution can still fail: an interface whose super-interface is not
       * on the class path reports -1 and is not counted, which is the safe
       * direction and is recorded in the per-site detail rather than folded
       * silently into a zero.
       */
      @Override
      public Void visitClass(ClassTree node, Void p)
      {
         if (node.getKind() == Tree.Kind.INTERFACE)
         {
            Element self = trees.getElement(getCurrentPath());
            int abstracts = self instanceof TypeElement
               ? abstractMethods(((TypeElement) self).asType()) : -1;
            boolean annotated = hasFunctionalInterface(getCurrentPath());
            if (abstracts == 1)
            {
               ifaceSam++;
               if (annotated)
               {
                  ifaceSamAnnotated++;
               }
            }
            sams.add(where(node) + " " + node.getSimpleName()
               + " abstract=" + abstracts
               + " annotated=" + (annotated ? "yes" : "no"));
         }
         return super.visitClass(node, p);
      }

      /**
       * Does the type at this path carry {@code @FunctionalInterface}? Asked of
       * the resolved element rather than of the source annotations, so that an
       * import alias or a fully-qualified spelling reads the same.
       *
       * <p>Worth knowing when reading the number it feeds: javac ENFORCES this
       * annotation. A tree that compiles cannot carry it on a non-SAM interface,
       * so {@code iface.samannotated} can never exceed {@code iface.sam} in a
       * tree whose {@code source.ok} is yes -- the gate is measuring a claim the
       * compiler has already checked.
       */
      private boolean hasFunctionalInterface(TreePath path)
      {
         Element el = trees.getElement(path);
         if (el == null)
         {
            return false;
         }
         for (AnnotationMirror a : el.getAnnotationMirrors())
         {
            if ("java.lang.FunctionalInterface"
               .equals(a.getAnnotationType().toString()))
            {
               return true;
            }
         }
         return false;
      }

      /** The other half: {@code Foo::bar}, {@code this::bar}, {@code Foo::new}. */
      @Override
      public Void visitMemberReference(MemberReferenceTree node, Void p)
      {
         methodRefs.add(where(node) + " " + node.getMode() + " " + node.getName());
         countOptionalRef(node);
         return super.visitMemberReference(node, p);
      }

      /**
       * A method reference to a {@code java.util.Optional} method is that call,
       * made once per element: {@code .flatMap(Optional::stream)} is the Java 9
       * "Optional flattened into a stream", and {@code .map(Optional::get)} is a
       * {@code get()}. Recorded exactly as {@link #countOptional} records the
       * call, because a scanner that saw only the call spelling never credited
       * the canonical {@code Optional::stream} (wave R 2026-10-20, g02 A-2).
       *
       * <p>make|use is decided by the referenced METHOD and not by how the
       * qualifier is spelled: {@code Optional::stream} names the type and still
       * invokes an instance method on every element it is applied to, while
       * {@code Optional::ofNullable} is a static factory.
       */
      private void countOptionalRef(MemberReferenceTree node)
      {
         if (node.getMode() != MemberReferenceTree.ReferenceMode.INVOKE)
         {
            return;
         }
         Element el = trees.getElement(getCurrentPath());
         if (!(el instanceof ExecutableElement)
            || !(el.getEnclosingElement() instanceof TypeElement)
            || !"java.util.Optional".equals(((TypeElement) el.getEnclosingElement())
                  .getQualifiedName().toString()))
         {
            return;
         }
         boolean statically = el.getModifiers().contains(Modifier.STATIC);
         String called = node.getName().toString();
         if (!statically && "get".equals(called))
         {
            optOpGet++;
         }
         if (!statically && "isPresent".equals(called))
         {
            optOpIsPresent++;
         }
         optOps.add(where(node) + " " + (statically ? "make" : "use") + " " + called
            + " in=" + enclosingMethod());
      }

      /**
       * An enhanced {@code for}, and whether its body is the filter/collect
       * shape a stream pipeline replaces one-for-one:
       *
       * <pre>
       *   for (T t : src) { if (c) { dst.add(e); } }      -- or without the if
       * </pre>
       *
       * <p>Nothing wider is counted. A loop that mutates two collections, or
       * carries an index, or returns early, has no mechanical stream form, and a
       * gate that counted it would be asking for a rewrite whose correctness
       * nobody can check from a count.
       *
       * <p>{@code add} must take exactly one argument. The arity is part of the
       * shape and not a convenience: {@code store.add(key, value)} is a
       * map-style put that happens to be spelled {@code add}, and a loop around
       * one is not the filter/collect shape this row of the restatement table
       * asks for. Counting it would put a site in the denominator that no
       * stream form improves -- any solution, whether it reaches for
       * {@code forEach}, {@code map} or {@code Collectors}, would leave it
       * alone -- so the ceiling would sit below full credit for every candidate
       * including a correct one.
       *
       * <p>Neither the guard nor the added expression may call anything that
       * declares a checked exception, and for the same reason. See
       * {@link #throwsChecked}.
       */
      @Override
      public Void visitEnhancedForLoop(EnhancedForLoopTree node, Void p)
      {
         boolean collect = isCollectShape(node.getStatement());
         if (collect)
         {
            foreachCollect++;
         }
         foreachSites.add(where(node) + " collect=" + (collect ? "yes" : "no")
            + " in=" + enclosingMethod());
         loopSites.add(where(node) + " foreach");
         return super.visitEnhancedForLoop(node, p);
      }

      /**
       * The other three loop spellings, counted for one purpose: so that C-3's
       * departure half can tell a loop that was REPLACED from a loop that was
       * merely respelled.
       *
       * <p>Nothing here looks at the body. The shape question is
       * {@code foreach.collect}'s and it is asked of the enhanced-for only,
       * because that is the one spelling the instruction names. This census is
       * deliberately shape-blind and complete: a candidate who turns
       * {@code for (T t : src) dst.add(t)} into {@code while (it.hasNext())}, or
       * into an indexed {@code for}, or into a {@code do} over an index, has
       * written a different loop and not a pipeline, and only a count that
       * includes all four spellings sees that the tree still holds the same
       * number of loops.
       *
       * <p>{@code loop.size} is a total and not a set, which is the honest
       * shape for it: the question is "did 24 loops go away", and the sites are
       * emitted alongside so that a total which moved for some other reason can
       * be read rather than guessed at.
       */
      @Override
      public Void visitForLoop(ForLoopTree node, Void p)
      {
         loopSites.add(where(node) + " for");
         return super.visitForLoop(node, p);
      }

      @Override
      public Void visitWhileLoop(WhileLoopTree node, Void p)
      {
         loopSites.add(where(node) + " while");
         return super.visitWhileLoop(node, p);
      }

      @Override
      public Void visitDoWhileLoop(DoWhileLoopTree node, Void p)
      {
         loopSites.add(where(node) + " do");
         return super.visitDoWhileLoop(node, p);
      }

      private boolean isCollectShape(StatementTree body)
      {
         StatementTree s = unwrap(body);
         ExpressionTree guard = null;
         if (s instanceof IfTree)
         {
            IfTree i = (IfTree) s;
            if (i.getElseStatement() != null)
            {
               return false;
            }
            guard = i.getCondition();
            s = unwrap(i.getThenStatement());
         }
         if (!(s instanceof ExpressionStatementTree))
         {
            return false;
         }
         ExpressionTree e = ((ExpressionStatementTree) s).getExpression();
         if (!(e instanceof MethodInvocationTree))
         {
            return false;
         }
         MethodInvocationTree call = (MethodInvocationTree) e;
         if (call.getArguments().size() != 1)
         {
            return false;
         }
         ExpressionTree target = call.getMethodSelect();
         if (!(target instanceof MemberSelectTree)
            || !"add".contentEquals(((MemberSelectTree) target).getIdentifier()))
         {
            return false;
         }
         return !throwsChecked(guard) && !throwsChecked(call.getArguments().get(0));
      }

      /**
       * Does this expression call anything that declares a checked exception?
       *
       * <p>A {@code for} body may throw whatever the enclosing method declares.
       * A lambda body may throw only what its own functional interface
       * declares, and the ones a pipeline is built from -- {@code
       * Predicate.test}, {@code Function.apply}, {@code Consumer.accept} --
       * declare nothing. So a loop whose guard or added expression calls a
       * method with a checked exception in its {@code throws} clause has no
       * stream form that means the same thing: the direct translation does not
       * compile, and every form that does compile moves where the exception
       * surfaces.
       *
       * <p>{@code ManagerImpl.createContexts} is the case in this tree. It
       * declares {@code throws Exception} and its loop body is one {@code
       * created.add(Reflections.createInstance(contextClass))}. As a loop that
       * is legal; as {@code .map(c -> Reflections.createInstance(c))} it is
       * "unreported exception Exception; must be caught or declared to be
       * thrown". Counting it would hold the ceiling below full credit for a
       * correct answer, which is the same defect as counting a two-argument
       * {@code add} and fails in the same direction.
       *
       * <p>A call that does not resolve is counted as throwing nothing, so that
       * a gap in the classpath cannot quietly shrink the denominator. The sites
       * where that happens are counted in {@code foreach.unresolved} rather
       * than passed over, because a number that moved for that reason should
       * not look like a number that moved for a real one.
       */
      private boolean throwsChecked(ExpressionTree e)
      {
         if (e == null)
         {
            return false;
         }
         final boolean[] hit = new boolean[1];
         final TreePath base = getCurrentPath();
         new TreeScanner<Void, Void>()
         {
            @Override
            public Void visitMethodInvocation(MethodInvocationTree t, Void v)
            {
               check(t);
               return super.visitMethodInvocation(t, v);
            }

            @Override
            public Void visitNewClass(NewClassTree t, Void v)
            {
               check(t);
               return super.visitNewClass(t, v);
            }

            private void check(Tree t)
            {
               TreePath path = TreePath.getPath(base, t);
               Element el = path == null ? null : trees.getElement(path);
               if (!(el instanceof ExecutableElement))
               {
                  foreachUnresolved++;
                  return;
               }
               for (TypeMirror thrown : ((ExecutableElement) el).getThrownTypes())
               {
                  if (isChecked(thrown))
                  {
                     hit[0] = true;
                  }
               }
            }
         }.scan(e, null);
         return hit[0];
      }

      /** Anything that is not a RuntimeException and not an Error. */
      private boolean isChecked(TypeMirror t)
      {
         return !isSubtypeOf(t, "java.lang.RuntimeException")
            && !isSubtypeOf(t, "java.lang.Error");
      }

      private boolean isSubtypeOf(TypeMirror t, String name)
      {
         TypeElement el = elements.getTypeElement(name);
         return el != null
            && types.isSubtype(types.erasure(t), types.erasure(el.asType()));
      }

      /** A one-statement block is that statement; anything else is itself. */
      private StatementTree unwrap(StatementTree s)
      {
         if (s instanceof BlockTree && ((BlockTree) s).getStatements().size() == 1)
         {
            return ((BlockTree) s).getStatements().get(0);
         }
         return s;
      }

      /**
       * A {@code try}, and whether it manages its own resources. {@code
       * getResources()} is empty for every {@code try} written before Java 7
       * and non-empty for exactly the construct this step is about.
       */
      @Override
      public Void visitTry(TryTree node, Void p)
      {
         tryTotal++;
         int n = node.getResources().size();
         if (n > 0)
         {
            tryResources++;
            tryResourceVars += n;
         }
         return super.visitTry(node, p);
      }

      /**
       * A hand-written {@code close()}. Counted by name and arity rather than
       * by resolving {@code java.io.Closeable}, because the receiver of a
       * close in dead-ish cleanup code is routinely a type the classpath does
       * not have -- and an unresolvable receiver would silently drop the
       * finding, which is the direction a gate must not fail in. The receiver
       * type is reported when it resolves, for the reader rather than for the
       * count.
       */
      @Override
      public Void visitMethodInvocation(MethodInvocationTree node, Void p)
      {
         if (node.getArguments().isEmpty()
            && node.getMethodSelect() instanceof MemberSelectTree)
         {
            MemberSelectTree sel = (MemberSelectTree) node.getMethodSelect();
            if ("close".contentEquals(sel.getIdentifier()))
            {
               TypeMirror recv = trees.getTypeMirror(
                  new TreePath(getCurrentPath(), sel.getExpression()));
               closes.add(where(node) + " " + name(recv));
            }
         }
         countStaticNio(node);
         countStream(node);
         countOptional(node);
         countAccum(node);
         countCollect(node);
         countLibrary(node);
         return super.visitMethodInvocation(node, p);
      }

      /**
       * Stream pipelines, counted by TYPE rather than by method name. A call
       * whose result is a {@code java.util.stream} type opens a pipeline
       * ({@code list.stream()}, {@code Arrays.stream(a)},
       * {@code Stream.of(..)}); a call whose RECEIVER is one is a stage in it
       * ({@code .filter}, {@code .map}, {@code .collect}).
       *
       * <p>Names would be the obvious way and are the wrong one: this codebase
       * has its own {@code map(..)} and {@code filter(..)} methods that have
       * nothing to do with streams, and a name-matching counter would report a
       * pipeline in a tree that has none. The type is not guessable and not
       * spoofable by naming a helper {@code collect}.
       *
       * <p>The two are exclusive, and the first version of this method did not
       * make them so: {@code .filter(..)} returns a stream as well as taking
       * one, so it counted as a producer AND a stage, and a three-call pipeline
       * reported two pipelines. The self-test's known answer said 1 and the
       * plugin said 2 (NOTES 9.1). A producer is a call that ENTERS the stream
       * world -- its result is a stream and its receiver is not a stream VALUE.
       * "Value" carries the rest of the rule: in {@code IntStream.range(0, 3)}
       * the receiver is the TYPE {@code IntStream}, which is how a static
       * factory is spelled, and that is an entry rather than a stage.
       */
      private void countStream(MethodInvocationTree node)
      {
         TypeMirror recv = null;
         if (node.getMethodSelect() instanceof MemberSelectTree)
         {
            MemberSelectTree sel = (MemberSelectTree) node.getMethodSelect();
            TreePath path = new TreePath(getCurrentPath(), sel.getExpression());
            Element el = trees.getElement(path);
            boolean isType = el instanceof TypeElement;
            recv = isType ? null : trees.getTypeMirror(path);
         }
         boolean onStream = recv != null && isStream(name(recv));
         if (onStream)
         {
            streamOpSites.add(where(node) + " ON " + calledName(node)
               + " in=" + enclosingMethod());
         }
         else if (isStream(name(trees.getTypeMirror(getCurrentPath()))))
         {
            streamSites.add(where(node) + " ENTER " + calledName(node)
               + " in=" + enclosingMethod());
         }
      }

      /**
       * The simple name of an invoked method, for the producer record. Both
       * spellings occur here: {@code src.stream()} is a member select and a
       * statically imported {@code of(..)} is a bare identifier.
       */
      private String calledName(MethodInvocationTree node)
      {
         if (node.getMethodSelect() instanceof MemberSelectTree)
         {
            return ((MemberSelectTree) node.getMethodSelect()).getIdentifier().toString();
         }
         if (node.getMethodSelect() instanceof IdentifierTree)
         {
            return ((IdentifierTree) node.getMethodSelect()).getName().toString();
         }
         return "?";
      }

      /**
       * The same NIO.2 call that {@link #visitMemberSelect} counts, written
       * through a static import. {@code import static java.nio.file.Files.exists;}
       * turns {@code Files.exists(p)} into {@code exists(p)}: the member select
       * is gone, the qualifier with it, and a gate that only reads qualifiers
       * sees a tree with no {@code java.nio.file} in it at all.
       *
       * <p>That is a false NEGATIVE and it was found by review rather than by
       * measurement -- the golden solution writes every call qualified, so this
       * counter is 0 on it and the frozen target is unchanged. It exists so that
       * a migration which is correct and spelled differently is not scored as a
       * migration that did not happen. C-3's arrival half reads ref + staticref.
       *
       * <p>The method symbol's ENCLOSING element is what is tested, not the
       * identifier's name: {@code exists} is a name a dozen classes use, and the
       * owner is the only thing that says which one this is. An unqualified call
       * to a method of the enclosing class lands here too and its owner is that
       * class, so it does not count.
       */
      private void countStaticNio(MethodInvocationTree node)
      {
         if (!(node.getMethodSelect() instanceof IdentifierTree))
         {
            return;
         }
         Element m = trees.getElement(
            new TreePath(getCurrentPath(), node.getMethodSelect()));
         if (m == null || m.getKind() != ElementKind.METHOD)
         {
            return;
         }
         Element owner = m.getEnclosingElement();
         if (!(owner instanceof TypeElement))
         {
            return;
         }
         String n = ((TypeElement) owner).getQualifiedName().toString();
         if (isNioFile(n))
         {
            nioStatic++;
            nioSites.add(where(node) + " " + n + "." + m.getSimpleName()
               + " as=staticimport");
         }
      }

      /**
       * A reference to a {@code java.nio.file} type through its name, which is
       * what the static half of NIO.2 looks like: {@code Files.newOutputStream},
       * {@code Paths.get}. Counted separately from declarations because a
       * migration can be substantial without declaring a single {@code Path} --
       * replacing {@code new File(s).isDirectory()} with
       * {@code Files.isDirectory(Paths.get(s))} declares nothing and is the
       * change the step is about.
       *
       * <p>The qualifier is resolved to an element and required to be a type.
       * That is what keeps {@code import java.nio.file.Files} from counting:
       * there the qualifier is the package, not the class. Fully-qualified uses
       * still count once, at the innermost member select whose qualifier
       * resolves to the type.
       */
      @Override
      public Void visitMemberSelect(MemberSelectTree node, Void p)
      {
         Element e = trees.getElement(
            new TreePath(getCurrentPath(), node.getExpression()));
         if (e != null
            && (e.getKind() == ElementKind.CLASS || e.getKind() == ElementKind.INTERFACE
                || e.getKind() == ElementKind.ENUM))
         {
            String n = ((TypeElement) e).getQualifiedName().toString();
            if (isNioFile(n))
            {
               nioRef++;
               nioSites.add(where(node) + " " + n + "." + node.getIdentifier());
            }
         }
         return super.visitMemberSelect(node, p);
      }

      /**
       * A variable's declared type, bucketed. Declarations are what a NIO.2
       * migration actually moves: a method that took a {@code File} and now
       * takes a {@code Path} has changed even though neither side allocates.
       */
      @Override
      public Void visitVariable(VariableTree node, Void p)
      {
         TypeMirror t = trees.getTypeMirror(
            new TreePath(getCurrentPath(), node.getType()));
         String n = name(t);
         if (IO_FILE_TYPES.contains(n))
         {
            ioFiles.add(where(node) + " " + n + " as=decl");
         }
         else if (isNioFile(n))
         {
            nioUse++;
            nioSites.add(where(node) + " " + n + " as=decl");
         }
         countVar(node, t, n);
         return super.visitVariable(node, p);
      }

      /**
       * {@code var}, in both directions: the declarations that already use it,
       * and the declarations that could.
       *
       * <p>There is no {@code var} node in {@code com.sun.source} -- the type
       * is inferred during attribution and the tree afterwards holds the
       * inferred type, not the word. Two public-API signals survive that, and
       * this asks for both because each has a failure mode the other covers:
       * javac synthesises the replacement type tree at {@code NOPOS}, so a
       * start position below zero is an inferred declaration; and where a
       * position does survive, the source text at the type tree's extent is
       * literally {@code var}. Nothing here reaches into javac internals, which
       * is the property that lets this same class keep running on every JDK in
       * the ladder.
       *
       * <p>Only {@code LOCAL_VARIABLE} and {@code RESOURCE_VARIABLE}. A lambda
       * parameter with no written type is also a tree javac synthesises at
       * {@code NOPOS}, and it is an {@code ElementKind.PARAMETER}: excluding
       * parameters by kind is what keeps {@code (a, b) -> a + b} out of the
       * {@code var} count, and it is worth saying out loud because the
       * alternative reading -- "a type tree with no position is a var" -- is
       * true and useless.
       */
      private void countVar(VariableTree node, TypeMirror t, String n)
      {
         Element el = trees.getElement(getCurrentPath());
         ElementKind k = el == null ? null : el.getKind();
         if (k != ElementKind.LOCAL_VARIABLE && k != ElementKind.RESOURCE_VARIABLE)
         {
            return;
         }
         if (isInferred(node))
         {
            varSites.add(where(node) + " inferred=" + n + " in=" + enclosingMethod());
            return;
         }
         String span = spanKey(node);
         if (span != null)
         {
            Integer prev = declSpans.get(span);
            declSpans.put(span, prev == null ? 1 : prev + 1);
         }
         ExpressionTree init = node.getInitializer();
         if (!convertibleToVar(node, t, init))
         {
            return;
         }
         TypeMirror it = trees.getTypeMirror(new TreePath(getCurrentPath(), init));
         varCandidates.add((span == null ? "-" : span) + "\0"
            + where(node) + " type=" + n + " init=" + name(it)
            + " ctor=" + (init instanceof NewClassTree ? "yes" : "no")
            + " in=" + enclosingMethod());
      }

      /** Is this declaration's type tree the inferred stand-in for {@code var}? */
      private boolean isInferred(VariableTree node)
      {
         Tree type = node.getType();
         if (type == null)
         {
            return true;
         }
         long start = positions.getStartPosition(unit, type);
         if (start < 0)
         {
            return true;
         }
         long end = positions.getEndPosition(unit, type);
         CharSequence src = source();
         if (src == null || end <= start || end > src.length())
         {
            return false;
         }
         return "var".contentEquals(src.subSequence((int) start, (int) end));
      }

      /**
       * The type tree's start offset, which two declarators of one statement
       * share: {@code int a = 1, b = 2;} is two {@code VariableTree}s over one
       * written type, and {@code var} cannot express it. Counting the sharers
       * is left to {@link SourceScan#write()} because the second declarator has
       * not been walked yet when the first one asks.
       */
      private String spanKey(VariableTree node)
      {
         Tree type = node.getType();
         if (type == null)
         {
            return null;
         }
         long start = positions.getStartPosition(unit, type);
         return start < 0 ? null : file() + "@" + start;
      }

      /**
       * Could this declaration be written with {@code var} without changing
       * what it means? Inference takes the INITIALISER's type, so the only
       * declarations that survive the rewrite untouched are the ones where the
       * two types are already the same -- {@code types.isSameType}, not
       * {@link SourceScan#name}, because {@code name} erases type arguments and
       * would call {@code List<String> x = new ArrayList<Object>()} a match.
       *
       * <p>The exclusions are the shapes that do not have a usable initialiser
       * type at all: {@code null} (the null type is not denotable), a lambda or
       * method reference (poly expressions -- they have no type until a target
       * supplies one), an array initialiser without {@code new} ({@code int[] a
       * = {1, 2}} is syntax, not an expression), and an anonymous class body
       * (legal with {@code var}, but it infers the anonymous type rather than
       * the named supertype, which is a different declaration).
       */
      private boolean convertibleToVar(VariableTree node, TypeMirror t, ExpressionTree init)
      {
         if (init == null || t == null)
         {
            return false;
         }
         Tree.Kind ik = init.getKind();
         if (ik == Tree.Kind.NULL_LITERAL
            || init instanceof LambdaExpressionTree
            || init instanceof MemberReferenceTree)
         {
            return false;
         }
         if (init instanceof NewArrayTree && ((NewArrayTree) init).getType() == null)
         {
            return false;
         }
         if (init instanceof NewClassTree && ((NewClassTree) init).getClassBody() != null)
         {
            return false;
         }
         TypeMirror it = trees.getTypeMirror(new TreePath(getCurrentPath(), init));
         if (it == null)
         {
            return false;
         }
         TypeKind ikind = it.getKind();
         if (ikind != TypeKind.DECLARED && ikind != TypeKind.ARRAY && !ikind.isPrimitive())
         {
            return false;
         }
         try
         {
            return types.isSameType(t, it);
         }
         catch (RuntimeException e)
         {
            return false;
         }
      }

      /** The unit's text, read once, {@code null} if it cannot be read. */
      private CharSequence source()
      {
         if (!sourceRead)
         {
            sourceRead = true;
            try
            {
               sourceText = unit.getSourceFile().getCharContent(true);
            }
            catch (IOException e)
            {
               sourceText = null;
            }
            catch (RuntimeException e)
            {
               sourceText = null;
            }
         }
         return sourceText;
      }

      @Override
      public Void visitMethod(MethodTree node, Void p)
      {
         if (node.getModifiers().getFlags().contains(Modifier.NATIVE))
         {
            Element el = trees.getElement(getCurrentPath());
            Element owner = el == null ? null : el.getEnclosingElement();
            natives.add(where(node) + " " + (owner == null ? "?" : owner.toString())
               + "." + node.getName());
         }
         countOptRet(node);
         classifyMember(node);
         return super.visitMethod(node, p);
      }

      /**
       * Where a method sits in the shape step 04 moves: an interface's own
       * methods split three ways, and the empty overrides that make an abstract
       * class an "adapter".
       *
       * <pre>
       *   iface.abstract   an interface method with no body
       *   iface.default    an interface method WITH a body, not static
       *   iface.static     a static interface method
       *   emptyoverride[]  a no-op method body in an abstract class that
       *                    implements an interface -- the adapter pattern
       * </pre>
       *
       * <p>{@code default} and {@code static} interface methods are Java 8 and
       * do not exist in a Java 7 tree, so both counters are 0 at the step's
       * baseline by construction rather than by luck: the arrival side of this
       * gate cannot be satisfied by anything that was already there.
       *
       * <p>An empty body is {@code { }} with no statements. A body that returns
       * a constant is <em>not</em> counted: "return null" is a decision the
       * subclass may be relying on, and moving it to a {@code default} method is
       * a behaviour question rather than a mechanical one.
       */
      private void classifyMember(MethodTree node)
      {
         if ("<init>".contentEquals(node.getName()))
         {
            return;
         }
         ClassTree owner = enclosingClass();
         if (owner == null)
         {
            return;
         }
         boolean isInterface = owner.getKind() == Tree.Kind.INTERFACE;
         boolean isStatic = node.getModifiers().getFlags().contains(Modifier.STATIC);
         if (isInterface)
         {
            if (isStatic)
            {
               ifaceStatic++;
            }
            else if (node.getBody() != null)
            {
               ifaceDefault++;
               defaults.add(where(node) + " " + owner.getSimpleName()
                  + "." + node.getName());
            }
            else
            {
               ifaceAbstract++;
            }
            return;
         }
         if (owner.getKind() != Tree.Kind.CLASS
            || !owner.getModifiers().getFlags().contains(Modifier.ABSTRACT)
            || owner.getImplementsClause().isEmpty())
         {
            return;
         }
         BlockTree body = node.getBody();
         if (body != null && body.getStatements().isEmpty() && !isStatic)
         {
            emptyOverrides.add(where(node) + " " + owner.getSimpleName()
               + "." + node.getName());
         }
      }

      /**
       * The class, interface or enum this path is lexically inside. A
       * {@code TreePath} iterates leaf-first, so the first {@code ClassTree} on
       * the way up is the declaring type.
       */
      private ClassTree enclosingClass()
      {
         for (Tree t : getCurrentPath())
         {
            if (t instanceof ClassTree)
            {
               return (ClassTree) t;
            }
         }
         return null;
      }

      @Override
      public Void visitAnnotation(AnnotationTree node, Void p)
      {
         if (isSuppressWarnings(node))
         {
            String on = enclosingKind();
            List<String> values = suppressedNames(getCurrentPath());
            suppressSites++;
            if (values.isEmpty())
            {
               // @SuppressWarnings({}) silences nothing and is still a site.
               values = Collections.singletonList("(none)");
            }
            for (String v : values)
            {
               suppress.add(where(node) + " " + v + " on=" + on);
               suppressPairs++;
               Integer n = suppressValues.get(v);
               suppressValues.put(v, n == null ? 1 : n + 1);
            }
         }
         return super.visitAnnotation(node, p);
      }

      /**
       * A method DECLARED to return {@code java.util.Optional} -- the arrival
       * half of the C-1 gate, and the only one of these facts read off a
       * declaration rather than off a call.
       *
       * <p>Resolved through the method's element, so
       * {@code java.util.Optional<X>}, an imported {@code Optional<X>} and a
       * type variable that happens to be spelled {@code Optional} are told
       * apart; {@code name()} erases, so the type argument does not have to be
       * guessed at. The declared return TYPE is still printed as written,
       * because that is what a reader of a failing gate needs to see.
       */
      private void countOptRet(MethodTree node)
      {
         Element el = trees.getElement(getCurrentPath());
         if (!(el instanceof ExecutableElement))
         {
            return;
         }
         if (!"java.util.Optional".equals(
            name(((ExecutableElement) el).getReturnType())))
         {
            return;
         }
         optRets.add(where(node)
            + " ret=" + (node.getReturnType() == null
                           ? "-" : node.getReturnType().toString())
            + " name=" + node.getName()
            + " in=" + enclosingMethod());
      }

      // ------------------------------------------- the Java 8 library block
      //
      // Five findings, and every one of them is resolved through the ELEMENT
      // the call binds to rather than through the name it is spelled with.
      // That is not fastidiousness: this codebase has its own `map(..)`,
      // `filter(..)` and `get(..)`, `Collectors.toList()` can arrive through a
      // static import as a bare `toList()`, and a tree can declare a class
      // called `Optional`. A name-matching counter answers all three wrongly,
      // and the same mistake already cost this plugin a wrong stream count
      // once (see countStream).

      /**
       * {@code return null;} -- the departure half of step 05's Optional gate.
       *
       * <p>Recorded with the modifiers and declared return type of the method
       * the statement would move, and with {@code api=yes|no} saying whether
       * that method is one {@code apisig.sh} dumps. The second field is the
       * one that matters: {@code javap -protected} reports public and
       * protected members of EVERY class in the jar, package-private and
       * nested ones included, and drops only anonymous and local classes. So
       * "not in the published API" is not "the enclosing type is not public" --
       * `final class Reflections`'s two public statics are two of B-6's 1147
       * lines. A method with {@code api=yes} cannot change its return type
       * without B-6 charging for it AND without the scorer's own probe, which
       * is compiled against the candidate's jars, failing to compile at all.
       *
       * <p>{@code scope=} says what kind of place the return sits in:
       * {@code decl} a named method declaration outside any anonymous or local
       * class, {@code anon} one inside such a class (an override, whose
       * signature is not the candidate's to move), {@code lambda} a lambda
       * body (whose return type is the target type's, not a declaration), and
       * {@code -} no enclosing method at all.
       */
      @Override
      public Void visitReturn(ReturnTree node, Void p)
      {
         ExpressionTree e = node.getExpression();
         if (e != null && e.getKind() == Tree.Kind.NULL_LITERAL)
         {
            MethodTree m = null;
            boolean lambda = false;
            boolean nested = false;
            boolean seen = false;
            for (TreePath q = getCurrentPath(); q != null; q = q.getParentPath())
            {
               Tree t = q.getLeaf();
               if (!seen && t instanceof LambdaExpressionTree)
               {
                  lambda = true;
                  break;
               }
               if (!seen && t instanceof MethodTree)
               {
                  m = (MethodTree) t;
                  seen = true;
                  continue;
               }
               if (seen && t instanceof MethodTree)
               {
                  nested = true;
               }
               if (seen && t instanceof NewClassTree
                  && ((NewClassTree) t).getClassBody() != null)
               {
                  nested = true;
               }
            }
            String scope = lambda ? "lambda" : m == null ? "-" : nested ? "anon" : "decl";
            boolean api = "decl".equals(scope) && isApiMember(m);
            if (api)
            {
               retNullApi++;
            }
            retNulls.add(where(node)
               + " scope=" + scope
               + " mods=" + (m == null ? "-" : modsOf(m))
               + " ret=" + (m == null || m.getReturnType() == null
                              ? "-" : m.getReturnType().toString())
               + " api=" + (api ? "yes" : "no")
               + " in=" + enclosingMethod());
         }
         return super.visitReturn(node, p);
      }

      /**
       * Whether {@code apisig.sh} would print this method. Public or protected,
       * or any member of an interface or annotation type (implicitly public) --
       * and not inside an anonymous or local class, which that script drops.
       * The enclosing type's own visibility is deliberately NOT consulted, for
       * the reason the visitReturn javadoc gives.
       */
      private boolean isApiMember(MethodTree m)
      {
         ClassTree owner = enclosingClass();
         boolean iface = owner != null
            && (owner.getKind() == Tree.Kind.INTERFACE
               || owner.getKind() == Tree.Kind.ANNOTATION_TYPE);
         Set<Modifier> f = m.getModifiers().getFlags();
         return iface || f.contains(Modifier.PUBLIC) || f.contains(Modifier.PROTECTED);
      }

      private String modsOf(MethodTree m)
      {
         Set<Modifier> f = m.getModifiers().getFlags();
         if (f.contains(Modifier.PUBLIC))
         {
            return "public";
         }
         if (f.contains(Modifier.PROTECTED))
         {
            return "protected";
         }
         if (f.contains(Modifier.PRIVATE))
         {
            return "private";
         }
         return "package";
      }

      /**
       * The type that declares the method a call binds to, fully qualified, or
       * null when the call does not resolve to one. This is the single place
       * the library block asks "whose method is this?", and it answers with
       * javac's own binding rather than with the receiver's spelling -- so a
       * static import, a fully qualified call and a plain one are the same
       * finding, and a local helper with a famous name is not.
       */
      private String calleeOwner(MethodInvocationTree node)
      {
         TreePath sel = new TreePath(getCurrentPath(), node.getMethodSelect());
         Element el = trees.getElement(sel);
         if (!(el instanceof ExecutableElement))
         {
            return null;
         }
         Element owner = el.getEnclosingElement();
         return owner instanceof TypeElement
            ? ((TypeElement) owner).getQualifiedName().toString() : null;
      }

      /** {@code java.util.Optional} calls, split into arrival and consumption. */
      private void countOptional(MethodInvocationTree node)
      {
         if (!(node.getMethodSelect() instanceof MemberSelectTree))
         {
            return;
         }
         MemberSelectTree sel = (MemberSelectTree) node.getMethodSelect();
         TreePath rp = new TreePath(getCurrentPath(), sel.getExpression());
         Element rel = trees.getElement(rp);
         boolean statically = rel instanceof TypeElement;
         String owner = statically
            ? ((TypeElement) rel).getQualifiedName().toString()
            : name(trees.getTypeMirror(rp));
         if (!"java.util.Optional".equals(owner))
         {
            return;
         }
         String called = sel.getIdentifier().toString();
         if (!statically && "get".equals(called))
         {
            optOpGet++;
         }
         // Java 9 gave `Optional` the three operations that make an
         // `isPresent()` branch unnecessary -- `or`, `ifPresentOrElse`,
         // `stream` -- so the count of surviving `isPresent()` calls is a
         // step-06 departure figure in its own right, alongside step 05's
         // `optop.get`.
         if (!statically && "isPresent".equals(called))
         {
            optOpIsPresent++;
         }
         optOps.add(where(node) + " " + (statically ? "make" : "use") + " " + called
            + " in=" + enclosingMethod());
      }

      /**
       * {@code src.forEach(sink::add)} -- the accumulate shape step 04 left in
       * place on purpose and step 05 converts. The sink's DECLARED type is
       * recorded because that is what decides which collector is correct, and
       * one of this tree's sinks is a {@code LinkedHashSet} whose iteration
       * order is recorded behaviour.
       */
      private void countAccum(MethodInvocationTree node)
      {
         if (!"forEach".equals(calledName(node)) || node.getArguments().size() != 1)
         {
            return;
         }
         ExpressionTree arg = node.getArguments().get(0);
         if (!(arg instanceof MemberReferenceTree))
         {
            return;
         }
         MemberReferenceTree mr = (MemberReferenceTree) arg;
         String op = mr.getName().toString();
         if (!"add".equals(op) && !"put".equals(op) && !"addAll".equals(op))
         {
            return;
         }
         TypeMirror st = trees.getTypeMirror(
            new TreePath(new TreePath(getCurrentPath(), mr), mr.getQualifierExpression()));
         accumSites.add(where(node)
            + " sink=" + mr.getQualifierExpression()
            + " op=" + op
            + " sinktype=" + name(st)
            + " in=" + enclosingMethod());
      }

      /**
       * {@code stream.collect(..)}, with the {@code Collectors} factory the
       * argument names. The receiver is required to BE a stream, by type, for
       * the reason countStream gives; the factory is read off the element the
       * argument's call binds to, so {@code toList()} through a static import
       * and {@code java.util.stream.Collectors.toList()} are one finding.
       */
      private void countCollect(MethodInvocationTree node)
      {
         if (!"collect".equals(calledName(node))
            || !(node.getMethodSelect() instanceof MemberSelectTree))
         {
            return;
         }
         MemberSelectTree sel = (MemberSelectTree) node.getMethodSelect();
         TreePath rp = new TreePath(getCurrentPath(), sel.getExpression());
         if (trees.getElement(rp) instanceof TypeElement)
         {
            return;
         }
         if (!isStream(name(trees.getTypeMirror(rp))))
         {
            return;
         }
         String to = collectorKind(node);
         if ("toList".equals(to))
         {
            collectToList++;
         }
         else if ("toSet".equals(to))
         {
            collectToSet++;
         }
         else if ("toCollection".equals(to))
         {
            collectToCollection++;
         }
         else
         {
            collectOther++;
         }
         collectSites.add(where(node) + " to=" + to + " in=" + enclosingMethod());
      }

      /** Which {@code java.util.stream.Collectors} factory an argument names. */
      private String collectorKind(MethodInvocationTree node)
      {
         if (node.getArguments().size() != 1)
         {
            return "other";
         }
         ExpressionTree arg = node.getArguments().get(0);
         if (!(arg instanceof MethodInvocationTree))
         {
            return "other";
         }
         MethodInvocationTree call = (MethodInvocationTree) arg;
         TreePath sel = new TreePath(new TreePath(getCurrentPath(), call),
            call.getMethodSelect());
         Element el = trees.getElement(sel);
         if (!(el instanceof ExecutableElement))
         {
            return "other";
         }
         Element owner = el.getEnclosingElement();
         if (!(owner instanceof TypeElement)
            || !"java.util.stream.Collectors".contentEquals(
                  ((TypeElement) owner).getQualifiedName()))
         {
            return "other";
         }
         return el.getSimpleName().toString();
      }

      /**
       * {@code java.util.Objects}, {@code java.time} and the legacy clock, all
       * three off the same resolved owner. {@code millis} is the departure
       * half of the time gate and {@code jtime} its arrival half.
       */
      private void countLibrary(MethodInvocationTree node)
      {
         String owner = calleeOwner(node);
         if (owner == null)
         {
            return;
         }
         String called = calledName(node);
         if ("java.util.Objects".equals(owner))
         {
            objectsCalls.add(where(node) + " " + called + " in=" + enclosingMethod());
         }
         else if (owner.startsWith("java.time."))
         {
            jtimeSites.add(where(node) + " " + owner + "." + called
               + " as=call in=" + enclosingMethod());
         }
         else if ("java.lang.System".equals(owner)
            && ("currentTimeMillis".equals(called) || "nanoTime".equals(called)))
         {
            millisSites.add(where(node) + " System." + called
               + " in=" + enclosingMethod());
         }
         else if (LEGACY_TIME_TYPES.contains(owner))
         {
            millisSites.add(where(node) + " " + owner + "." + called
               + " in=" + enclosingMethod());
         }
         countJava9Library(owner, called, node);
      }

      /**
       * The three call-shaped Java 9/10 facts: reflective instantiation, the
       * collection factories, and the pre-factory idioms they replace.
       *
       * <p>{@code Collections.unmodifiableList(field)} and
       * {@code Collections.emptyList()} are both "legacy collection" calls and
       * they are not the same kind of thing at all -- the first is a live view
       * over something the owner goes on mutating, the second builds a value.
       * Converting the second is free; converting the first changes what
       * callers see, and on this tree one of them re-orders a recorded
       * transcript from run to run (NOTES 11.5). So the record carries the
       * distinction rather than the total: {@code kind=view} when the call
       * wraps, {@code kind=build} when it constructs, and {@code arg=field}
       * when the wrapped expression resolves to a field, which is the shape
       * that makes a view load-bearing.
       */
      private void countJava9Library(String owner, String called, MethodInvocationTree node)
      {
         if ("java.lang.Class".equals(owner) && "newInstance".equals(called))
         {
            newInstanceLegacy++;
            newInstanceSites.add(where(node) + " form=legacy in=" + enclosingMethod());
         }
         else if ("java.lang.reflect.Constructor".equals(owner)
            && "newInstance".equals(called))
         {
            newInstanceCtor++;
            newInstanceSites.add(where(node) + " form=ctor in=" + enclosingMethod());
         }
         else if (FACTORY_OWNERS.contains(owner)
            && ("of".equals(called) || "copyOf".equals(called)
               || "ofEntries".equals(called) || "entry".equals(called)))
         {
            if ("copyOf".equals(called))
            {
               factoryCopyOf++;
            }
            else
            {
               factoryOf++;
            }
            factorySites.add(where(node) + " " + owner + "." + called
               + " args=" + node.getArguments().size() + " in=" + enclosingMethod());
         }
         else if ("java.util.Collections".equals(owner) && LEGACY_COLL_CALLS.contains(called))
         {
            boolean view = called.startsWith("unmodifiable");
            String arg = argShape(node);
            if (view)
            {
               legacyCollView++;
               if ("field".equals(arg))
               {
                  legacyCollViewField++;
               }
            }
            else
            {
               legacyCollBuild++;
            }
            legacyCollSites.add(where(node) + " call=" + called
               + " kind=" + (view ? "view" : "build") + " arg=" + arg
               + " in=" + enclosingMethod());
         }
         else if ("java.util.Arrays".equals(owner) && "asList".equals(called))
         {
            legacyCollBuild++;
            legacyCollSites.add(where(node) + " call=asList kind=build arg="
               + argShape(node) + " in=" + enclosingMethod());
         }
         else if (ACCESSIBLE_OWNERS.contains(owner)
            && ("isAccessible".equals(called) || "canAccess".equals(called)
               || "setAccessible".equals(called)))
         {
            if ("isAccessible".equals(called))
            {
               accIsAccessible++;
            }
            else if ("canAccess".equals(called))
            {
               accCanAccess++;
            }
            else
            {
               accSetAccessible++;
            }
            accessibleSites.add(where(node) + " call=" + called
               + " recv=" + simple(owner) + " in=" + enclosingMethod());
         }
         else if ("java.lang.Package".equals(owner) && "getPackage".equals(called)
            && node.getArguments().size() == 1)
         {
            getPackageSites.add(where(node) + " call=getPackage in="
               + enclosingMethod());
         }
      }

      /** The last dot-separated segment of a qualified name. */
      private String simple(String qualified)
      {
         int dot = qualified.lastIndexOf('.');
         return dot < 0 ? qualified : qualified.substring(dot + 1);
      }

      /**
       * What the first argument of a wrapping call resolves to: {@code field}
       * when it is a field of the enclosing class (the case that makes a
       * {@code Collections.unmodifiable*} call a live window), {@code local}
       * for a local or a parameter, {@code call} for anything computed, and
       * {@code none} for a no-argument factory like {@code emptyList()}.
       */
      private String argShape(MethodInvocationTree node)
      {
         if (node.getArguments().isEmpty())
         {
            return "none";
         }
         ExpressionTree arg = node.getArguments().get(0);
         if (!(arg instanceof IdentifierTree) && !(arg instanceof MemberSelectTree))
         {
            return "call";
         }
         // `Foo.class` is a MemberSelectTree and javac resolves it to a synthetic
         // element of kind FIELD, so the plain element test below reads a class
         // literal as a field -- and every one of this tree's
         // `Arrays.asList(A.class, B.class, ...)` profile lists came back
         // `arg=field`, which is the one value that is supposed to mean "a live
         // window onto somebody's mutable state". Measured, not guessed at.
         if (arg instanceof MemberSelectTree
            && ((MemberSelectTree) arg).getIdentifier().contentEquals("class"))
         {
            return "literal";
         }
         Element el = trees.getElement(new TreePath(getCurrentPath(), arg));
         if (el == null)
         {
            return "call";
         }
         ElementKind k = el.getKind();
         if (k == ElementKind.FIELD)
         {
            return "field";
         }
         if (k == ElementKind.LOCAL_VARIABLE || k == ElementKind.PARAMETER
            || k == ElementKind.RESOURCE_VARIABLE || k == ElementKind.EXCEPTION_PARAMETER)
         {
            return "local";
         }
         return "call";
      }

      private boolean isSuppressWarnings(AnnotationTree node)
      {
         TreePath typePath = new TreePath(getCurrentPath(), node.getAnnotationType());
         TypeMirror t = trees.getTypeMirror(typePath);
         return "java.lang.SuppressWarnings".equals(name(t));
      }

      private String enclosingKind()
      {
         for (Tree t : getCurrentPath())
         {
            if (t instanceof MethodTree)
            {
               return "method";
            }
            if (t instanceof VariableTree)
            {
               return "variable";
            }
            if (t instanceof ClassTree)
            {
               return "type";
            }
         }
         return "other";
      }

      /**
       * The method a site sits in, rendered as a bare simple name for the
       * {@code in=} field of the foreach, stream and streamop records: the
       * OUTERMOST {@code MethodTree} on the current path.
       *
       * <p>Outermost and not innermost, and that is the whole point. A
       * collecting loop inside an anonymous {@code Runnable} has two enclosing
       * methods -- the anonymous body's {@code run()} and whatever method
       * declared the anonymous class -- and this step's own first requirement
       * turns that anonymous class into a lambda, which deletes {@code run()}
       * from the source. An innermost reading would therefore give one site two
       * identities, one before the step and one after, and C-3's arrival half
       * would be asking for a pipeline in a method that no longer exists.
       *
       * <p>Which is also why a method declared inside an anonymous body does
       * not become the bucket when it is the outermost one there is. A site in
       * an anonymous {@code Runnable} held by a FIELD reads {@code run} on the
       * outermost rule as stated, and {@code -} the moment that anonymous body
       * becomes a lambda: the same rename, one level up, where "outermost"
       * was supposed to have stopped it. So the walk discards everything it
       * has collected when it crosses an anonymous class body on the way to
       * the root, and only a method declared outside every anonymous body can
       * name a bucket. A local class inside a method is not crossed this way
       * and the method still names it, because no edit in this ladder dissolves
       * a local class.
       *
       * <p>The name alone, with no parameter types. Two overloads in one file
       * share a bucket, which merges two sites that would otherwise be
       * separate; merging can only ever make the arrival half easier to
       * satisfy, never impossible, and an erased-signature rendering would be
       * one more string that moves when a lambda replaces an anonymous body.
       * A constructor renders as {@code <init>}, which is a bucket name like
       * any other.
       *
       * <p>{@code -} when there is no such method: a field initialiser, a
       * static initialiser block, an annotation value, or any of those reached
       * through an anonymous body. Such a site still counts in every total; it
       * just carries a bucket that is the same before and after the migration
       * and that no frozen set of METHOD buckets can name, so it can never
       * satisfy an arrival half on its own.
       *
       * <p>{@link TreePath} iterates from the leaf towards the root, so the
       * last name this loop writes is the outermost one -- and the reset on an
       * anonymous class body lands in the right order for free: everything
       * already collected is inside the body, and anything assigned afterwards
       * is outside it.
       */
      private String enclosingMethod()
      {
         String outer = "-";
         for (Tree t : getCurrentPath())
         {
            if (t instanceof MethodTree)
            {
               outer = ((MethodTree) t).getName().toString();
            }
            else if (t instanceof NewClassTree
                     && ((NewClassTree) t).getClassBody() != null)
            {
               outer = "-";
            }
         }
         return outer.length() == 0 ? "-" : outer;
      }

      private String where(Tree node)
      {
         long pos = positions.getStartPosition(unit, node);
         long line = pos < 0 ? -1 : unit.getLineMap().getLineNumber(pos);
         return file() + ":" + line;
      }

      /**
       * Where a finding is, relative to the source root when it is under it.
       *
       * <p>{@code getPath()} is null for an opaque URI, and an opaque URI is
       * not hypothetical: a jar on the class path that also ships its sources
       * makes javac compile them implicitly, and the unit's URI is then
       * {@code jar:file:/...!/Foo.java}. Measured -- {@code mockito-all-1.8.3}
       * carries 469 {@code .java} files, and the first one javac reached took
       * the plugin down with a {@code NullPointerException} out of
       * {@code new File(null)}, in a run that still reported
       * {@code source.ok=yes}. The gate now sets {@code -sourcepath} so those
       * units never arrive; this is the second line of defence, and it renders
       * the URI rather than pretending it is a path.
       */
      private String file()
      {
         String raw = unit.getSourceFile().toUri().getPath();
         if (raw == null)
         {
            return unit.getSourceFile().toUri().toString();
         }
         String path = new File(raw).getAbsolutePath();
         if (root != null && path.startsWith(root))
         {
            path = path.substring(root.length());
            if (path.startsWith(File.separator))
            {
               path = path.substring(1);
            }
         }
         return path;
      }
   }

   // ------------------------------------------------------------- resolution

   /**
    * The type an anonymous class is a subtype of: its single interface if it
    * implements one, otherwise its superclass. javac models the anonymous class
    * itself as the expression's type, and whether the named type became a
    * superclass or an interface is not visible from the {@code new} expression.
    */
   private TypeMirror superTypeOf(TreePath path)
   {
      TypeMirror t = trees.getTypeMirror(path);
      if (t == null || t.getKind() != TypeKind.DECLARED)
      {
         return null;
      }
      Element el = ((DeclaredType) t).asElement();
      if (!(el instanceof TypeElement))
      {
         return null;
      }
      TypeElement te = (TypeElement) el;
      List<? extends TypeMirror> ifaces = te.getInterfaces();
      if (!ifaces.isEmpty())
      {
         return ifaces.get(0);
      }
      TypeMirror sup = te.getSuperclass();
      return sup == null || sup.getKind() == TypeKind.NONE ? null : sup;
   }

   /**
    * Abstract method count, which is what "functional interface" means in the
    * language: exactly one abstract method that is not a public method of
    * {@code Object}. The {@code @FunctionalInterface} annotation is <em>not</em>
    * what is checked -- it is optional, and nearly every interface a 2011
    * codebase implements anonymously predates it.
    *
    * @return the count, or -1 when the supertype is not an interface at all
    *         (an anonymous subclass of a class is never a lambda candidate)
    */
   private int abstractMethods(TypeMirror t)
   {
      if (t == null || t.getKind() != TypeKind.DECLARED)
      {
         return -1;
      }
      Element el = ((DeclaredType) t).asElement();
      if (!(el instanceof TypeElement) || el.getKind() != ElementKind.INTERFACE)
      {
         return -1;
      }
      int n = 0;
      for (Element m : elements.getAllMembers((TypeElement) el))
      {
         if (m.getKind() != ElementKind.METHOD)
         {
            continue;
         }
         ExecutableElement ee = (ExecutableElement) m;
         if (!ee.getModifiers().contains(Modifier.ABSTRACT))
         {
            continue;
         }
         if (redeclaresObjectMethod(ee))
         {
            continue;
         }
         n++;
      }
      return n;
   }

   /**
    * An interface may redeclare {@code equals}, {@code hashCode} or
    * {@code toString} as abstract; the language does not count those, because
    * every implementation inherits them from {@code Object} already.
    * {@code Comparator} is the classic case: two abstract methods declared, and
    * a functional interface all the same.
    */
   private boolean redeclaresObjectMethod(ExecutableElement m)
   {
      TypeElement object = elements.getTypeElement("java.lang.Object");
      Element owner = m.getEnclosingElement();
      if (object == null || !(owner instanceof TypeElement))
      {
         return false;
      }
      for (Element o : object.getEnclosedElements())
      {
         if (o.getKind() != ElementKind.METHOD)
         {
            continue;
         }
         ExecutableElement oe = (ExecutableElement) o;
         if (!oe.getModifiers().contains(Modifier.PUBLIC))
         {
            continue;
         }
         if (elements.overrides(m, oe, (TypeElement) owner))
         {
            return true;
         }
      }
      return false;
   }

   /**
    * The {@code java.io} types a NIO.2 migration replaces. Named rather than
    * derived: {@code java.io.File} is not a supertype of the four streams, and
    * "everything in java.io" would sweep in {@code InputStream} and
    * {@code Serializable}, which this step does not ask anyone to move.
    */
   private static final Set<String> IO_FILE_TYPES = new HashSet<String>(
      java.util.Arrays.asList(
         "java.io.File",
         "java.io.FileInputStream",
         "java.io.FileOutputStream",
         "java.io.FileReader",
         "java.io.FileWriter"));

   /**
    * The pre-Java-8 clock and calendar types, named for the same reason
    * IO_FILE_TYPES is: "everything in java.util that tells the time" is not a
    * package and not a supertype. {@code System.currentTimeMillis} and
    * {@code System.nanoTime} are handled separately because their owner is
    * {@code java.lang.System}, which is not a time type.
    */
   private static final Set<String> LEGACY_TIME_TYPES = new HashSet<String>(
      java.util.Arrays.asList(
         "java.util.Date",
         "java.util.Calendar",
         "java.util.GregorianCalendar",
         "java.text.DateFormat",
         "java.text.SimpleDateFormat"));

   /**
    * The eight boxed primitive wrappers, whose public constructors have been
    * deprecated since Java 9 (javac at --release 10 reports them under
    * -Xlint:deprecation and not under -Xlint:removal; the forRemoval flag
    * arrives in a later release). Named and not derived, for the same
    * reason IO_FILE_TYPES is: {@code java.lang.Void} and {@code java.lang.Number}
    * are in the same package and are not this.
    */
   private static final Set<String> BOXED_TYPES = new HashSet<String>(
      java.util.Arrays.asList(
         "java.lang.Boolean",
         "java.lang.Byte",
         "java.lang.Character",
         "java.lang.Double",
         "java.lang.Float",
         "java.lang.Integer",
         "java.lang.Long",
         "java.lang.Short"));

   /** The three interfaces that grew static factories in Java 9. */
   private static final Set<String> FACTORY_OWNERS = new HashSet<String>(
      java.util.Arrays.asList(
         "java.util.List",
         "java.util.Set",
         "java.util.Map"));

   /**
    * The {@code java.util.Collections} calls a factory rewrite reaches for.
    * {@code unmodifiable*} wraps; everything else here builds.
    */
   /**
    * The receiver types javac can resolve a {@code setAccessible} call to.
    * {@code isAccessible} and {@code canAccess} are declared only on
    * {@code AccessibleObject}, but {@code Field}, {@code Method} and
    * {@code Constructor} each override {@code setAccessible}, so the owner of
    * the resolved method varies with the static type of the receiver.
    */
   private static final Set<String> ACCESSIBLE_OWNERS = new HashSet<String>(
      java.util.Arrays.asList("java.lang.reflect.AccessibleObject", "java.lang.reflect.Field",
         "java.lang.reflect.Method", "java.lang.reflect.Constructor"));

   private static final Set<String> LEGACY_COLL_CALLS = new HashSet<String>(
      java.util.Arrays.asList(
         "unmodifiableList",
         "unmodifiableSet",
         "unmodifiableMap",
         "unmodifiableCollection",
         "unmodifiableSortedSet",
         "unmodifiableSortedMap",
         "emptyList",
         "emptySet",
         "emptyMap",
         "singletonList",
         "singleton",
         "singletonMap"));

   /** Any type in {@code java.nio.file}, including its subpackages. */
   /**
    * A {@code java.util.stream} type, by package. {@code Stream},
    * {@code IntStream}, {@code Collector} and the rest all live there and
    * nothing else does, so the package is the whole test.
    */
   private static boolean isStream(String qualified)
   {
      return qualified != null && qualified.startsWith("java.util.stream.");
   }

   private static boolean isNioFile(String qualified)
   {
      return qualified != null && qualified.startsWith("java.nio.file.");
   }

   private static String name(TypeMirror t)
   {
      if (t == null)
      {
         return "?";
      }
      if (t.getKind() != TypeKind.DECLARED)
      {
         return t.toString();
      }
      Element el = ((DeclaredType) t).asElement();
      return el instanceof TypeElement
         ? ((TypeElement) el).getQualifiedName().toString() : t.toString();
   }

   /**
    * The strings inside a {@code @SuppressWarnings}, flattened and sorted. One
    * value, an array of values and a named {@code value = {...}} all reduce to
    * the same list: the gate cares how many findings were silenced, not how the
    * annotation was written.
    *
    * <p>Read from the annotated element's {@link AnnotationMirror} rather than
    * from the annotation's argument trees, so a constant reference
    * ({@code @SuppressWarnings(Constants.UNCHECKED)}) resolves to its value
    * instead of to the name of the constant.
    */
   private List<String> suppressedNames(TreePath annotationPath)
   {
      List<String> out = new ArrayList<String>();
      TreePath declPath = annotationPath;
      while (declPath != null
         && !(declPath.getLeaf() instanceof ClassTree
            || declPath.getLeaf() instanceof MethodTree
            || declPath.getLeaf() instanceof VariableTree))
      {
         declPath = declPath.getParentPath();
      }
      Element annotated = declPath == null ? null : trees.getElement(declPath);
      if (annotated == null)
      {
         return out;
      }
      for (AnnotationMirror am : annotated.getAnnotationMirrors())
      {
         Element ae = am.getAnnotationType().asElement();
         if (!(ae instanceof TypeElement)
            || !"java.lang.SuppressWarnings".contentEquals(
                  ((TypeElement) ae).getQualifiedName()))
         {
            continue;
         }
         for (AnnotationValue av : am.getElementValues().values())
         {
            flatten(av, out);
         }
      }
      Collections.sort(out);
      return out;
   }

   @SuppressWarnings("unchecked")
   private static void flatten(AnnotationValue av, List<String> out)
   {
      Object v = av.getValue();
      if (v instanceof List)
      {
         for (Object o : (List<Object>) v)
         {
            if (o instanceof AnnotationValue)
            {
               flatten((AnnotationValue) o, out);
            }
         }
      }
      else if (v != null)
      {
         out.add(String.valueOf(v));
      }
   }

   // ----------------------------------------------------------------- report

   /**
    * Rewritten in full after every compilation unit rather than once at the
    * end. A javac run that dies half way still leaves a report of what it did
    * see, and {@code files=} says how much that was -- which is the difference
    * between "the tree is clean" and "the scan stopped".
    */
   private void write()
   {
      PrintStream ps = null;
      try
      {
         File parent = out.getAbsoluteFile().getParentFile();
         if (parent != null && !parent.isDirectory())
         {
            parent.mkdirs();
         }
         ps = new PrintStream(out, "UTF-8");
         ps.println("files=" + files);
         ps.println("anonymous.size=" + anonymous.size());
         ps.println("anonymousfunctional.size=" + anonymousFunctional);
         ps.println("anonymousunresolved.size=" + anonymousUnresolved);
         ps.println("suppress.sites=" + suppressSites);
         ps.println("suppress.pairs=" + suppressPairs);
         ps.println("native.size=" + natives.size());
         ps.println("newgeneric.explicit=" + genericExplicit);
         ps.println("newgeneric.explicitanon=" + genericExplicitAnon);
         ps.println("newgeneric.diamond=" + genericDiamond);
         // Per-file spread, for both halves. Totals say how much was converted;
         // these say how widely, and a migration that moved 90 sites inside one
         // file is a different thing from one that moved 90 across fifty.
         ps.println("newgeneric.explicitfiles=" + distinctFiles(newGeneric));
         ps.println("newgeneric.diamondfiles=" + distinctFiles(diamonds));
         ps.println("newgeneric.raw=" + genericRaw);
         ps.println("try.size=" + tryTotal);
         ps.println("try.resources=" + tryResources);
         ps.println("try.resourcevars=" + tryResourceVars);
         ps.println("close.manual=" + closes.size());
         ps.println("iofile.new=" + countIo("as=new"));
         ps.println("iofile.decl=" + countIo("as=decl"));
         ps.println("niofile.use=" + nioUse);
         ps.println("niofile.ref=" + nioRef);
         ps.println("niofile.staticref=" + nioStatic);
         ps.println("niofile.files=" + distinctFiles(nioSites));
         // --- Java 8 ---
         ps.println("anonymousfunctional.convertible=" + anonFnConvertible);
         ps.println("anonymousfunctional.forward=" + anonFnForward);
         ps.println("lambda.size=" + lambdas.size());
         ps.println("lambda.files=" + distinctFiles(lambdas));
         ps.println("lambda.expr=" + lambdaExpr);
         ps.println("lambda.methodrefable=" + lambdaMethodRefable);
         ps.println("methodref.size=" + methodRefs.size());
         ps.println("foreach.size=" + foreachSites.size());
         ps.println("foreach.collect=" + foreachCollect);
         ps.println("foreach.unresolved=" + foreachUnresolved);
         ps.println("foreach.collectmethods="
            + distinctMethods(matching(foreachSites, "collect=yes")));
         ps.println("stream.producers=" + streamSites.size());
         ps.println("stream.ops=" + streamOpSites.size());
         ps.println("stream.files=" + distinctFiles(streamSites));
         ps.println("stream.opfiles=" + distinctFiles(streamOpSites));
         ps.println("stream.opmethods=" + distinctMethods(streamOpSites));
         ps.println("loop.size=" + loopSites.size());
         ps.println("iface.abstract=" + ifaceAbstract);
         ps.println("iface.default=" + ifaceDefault);
         ps.println("iface.static=" + ifaceStatic);
         ps.println("iface.decl=" + sams.size());
         ps.println("iface.sam=" + ifaceSam);
         ps.println("iface.samannotated=" + ifaceSamAnnotated);
         ps.println("emptyoverride.size=" + emptyOverrides.size());
         ps.println("retnull.size=" + retNulls.size());
         ps.println("retnull.methods=" + distinctMethods(retNulls));
         ps.println("retnull.files=" + distinctFiles(retNulls));
         ps.println("retnull.api=" + retNullApi);
         ps.println("optret.size=" + optRets.size());
         ps.println("optret.files=" + distinctFiles(optRets));
         ps.println("optop.size=" + optOps.size());
         ps.println("optop.methods=" + distinctMethods(optOps));
         ps.println("optop.get=" + optOpGet);
         ps.println("optop.ispresent=" + optOpIsPresent);
         ps.println("accum.size=" + accumSites.size());
         ps.println("accum.methods=" + distinctMethods(accumSites));
         ps.println("collect.size=" + collectSites.size());
         ps.println("collect.methods=" + distinctMethods(collectSites));
         ps.println("collect.tolist=" + collectToList);
         ps.println("collect.toset=" + collectToSet);
         ps.println("collect.tocollection=" + collectToCollection);
         ps.println("collect.other=" + collectOther);
         ps.println("objects.size=" + objectsCalls.size());
         ps.println("objects.methods=" + distinctMethods(objectsCalls));
         ps.println("jtime.size=" + jtimeSites.size());
         ps.println("jtime.files=" + distinctFiles(jtimeSites));
         ps.println("jtime.methods=" + distinctMethods(jtimeSites));
         ps.println("millis.size=" + millisSites.size());
         ps.println("millis.methods=" + distinctMethods(millisSites));
         ps.println("newinstance.size=" + newInstanceSites.size());
         ps.println("newinstance.legacy=" + newInstanceLegacy);
         ps.println("newinstance.ctor=" + newInstanceCtor);
         ps.println("newinstance.methods=" + distinctMethods(newInstanceSites));
         ps.println("newinstance.files=" + distinctFiles(newInstanceSites));
         ps.println("boxedctor.size=" + boxedCtorSites.size());
         ps.println("boxedctor.methods=" + distinctMethods(boxedCtorSites));
         ps.println("boxedctor.files=" + distinctFiles(boxedCtorSites));
         ps.println("factory.size=" + factorySites.size());
         ps.println("factory.of=" + factoryOf);
         ps.println("factory.copyof=" + factoryCopyOf);
         ps.println("factory.methods=" + distinctMethods(factorySites));
         ps.println("factory.files=" + distinctFiles(factorySites));
         ps.println("legacycoll.size=" + legacyCollSites.size());
         ps.println("legacycoll.view=" + legacyCollView);
         ps.println("legacycoll.viewfield=" + legacyCollViewField);
         ps.println("legacycoll.build=" + legacyCollBuild);
         ps.println("legacycoll.methods=" + distinctMethods(legacyCollSites));
         ps.println("legacycoll.files=" + distinctFiles(legacyCollSites));
         ps.println("accessible.size=" + accessibleSites.size());
         ps.println("accessible.isaccessible=" + accIsAccessible);
         ps.println("accessible.canaccess=" + accCanAccess);
         ps.println("accessible.setaccessible=" + accSetAccessible);
         ps.println("accessible.methods=" + distinctMethods(accessibleSites));
         ps.println("accessible.files=" + distinctFiles(accessibleSites));
         ps.println("getpackage.size=" + getPackageSites.size());
         ps.println("getpackage.files=" + distinctFiles(getPackageSites));
         ps.println("var.size=" + varSites.size());
         ps.println("var.methods=" + distinctMethods(varSites));
         ps.println("var.files=" + distinctFiles(varSites));
         List<String> cands = new ArrayList<String>();
         int shared = 0;
         for (String raw : varCandidates)
         {
            int nul = raw.indexOf('\0');
            String span = raw.substring(0, nul);
            String rec = raw.substring(nul + 1);
            Integer n = declSpans.get(span);
            if (n != null && n.intValue() > 1)
            {
               // int a = 1, b = 2; -- one written type, two declarators, and
               // `var` cannot express it. Not a candidate, and counted so the
               // subtraction is visible rather than silent.
               shared++;
               continue;
            }
            cands.add(rec);
         }
         ps.println("var.candidates=" + cands.size());
         // The subset where the written type is redundant in the strictest
         // sense: `Foo x = new Foo(...)`, the same name twice on one line.
         ps.println("var.candidatenew=" + matching(cands, " ctor=yes ").size());
         ps.println("var.candidatemethods=" + distinctMethods(cands));
         ps.println("var.candidatefiles=" + distinctFiles(cands));
         ps.println("var.multideclarator=" + shared);
         for (Map.Entry<String, Integer> e : boxedByType.entrySet())
         {
            ps.println("boxedctor.type." + e.getKey() + "=" + e.getValue());
         }
         for (Map.Entry<String, Integer> e : suppressValues.entrySet())
         {
            ps.println("suppress.value." + e.getKey() + "=" + e.getValue());
         }
         emit(ps, "anonymous", anonymous);
         emit(ps, "suppress", suppress);
         emit(ps, "native", natives);
         emit(ps, "newgeneric", newGeneric);
         emit(ps, "close", closes);
         emit(ps, "iofile", ioFiles);
         emit(ps, "niofile", nioSites);
         emit(ps, "diamond", diamonds);
         emit(ps, "anonfn", anonFn);
         emit(ps, "lambda", lambdas);
         emit(ps, "methodref", methodRefs);
         emit(ps, "foreach", foreachSites);
         emit(ps, "loop", loopSites);
         emit(ps, "stream", streamSites);
         emit(ps, "streamop", streamOpSites);
         emit(ps, "default", defaults);
         emit(ps, "emptyoverride", emptyOverrides);
         emit(ps, "interfacedecl", sams);
         emit(ps, "retnull", retNulls);
         emit(ps, "optret", optRets);
         emit(ps, "optop", optOps);
         emit(ps, "accum", accumSites);
         emit(ps, "collect", collectSites);
         emit(ps, "objects", objectsCalls);
         emit(ps, "jtime", jtimeSites);
         emit(ps, "millis", millisSites);
         emit(ps, "newinstance", newInstanceSites);
         emit(ps, "boxedctor", boxedCtorSites);
         emit(ps, "factory", factorySites);
         emit(ps, "legacycoll", legacyCollSites);
         emit(ps, "accessible", accessibleSites);
         emit(ps, "getpackage", getPackageSites);
         emit(ps, "var", varSites);
         emit(ps, "varcand", cands);
      }
      catch (IOException e)
      {
         throw new IllegalStateException("cannot write " + out, e);
      }
      finally
      {
         if (ps != null)
         {
            ps.close();
         }
      }
   }

   /** How many of the {@code iofile} findings are of one kind. */
   /**
    * How many distinct files a finding list touches. A count of sites says how
    * much was done; a count of files says how widely, and the two come apart
    * exactly when a migration is concentrated in one place -- which is the
    * shape a gate on totals alone would accept.
    */
   /**
    * How many distinct {@code <file>#<method>} buckets a record list touches,
    * read back out of the records' own {@code in=} field rather than tracked
    * separately -- so the published count and the per-site detail cannot
    * disagree.
    *
    * <p>A record with no {@code in=} field contributes nothing. That is the
    * safe direction: the only way the field can be missing is a gate jar older
    * than this method, and a bucket count of 0 makes an arrival half read
    * "nothing arrived" rather than "everything did".
    */
   private static int distinctMethods(List<String> sites)
   {
      Set<String> m = new HashSet<String>();
      for (String s : sites)
      {
         String bucket = bucketOf(s);
         if (bucket != null)
         {
            m.add(bucket);
         }
      }
      return m.size();
   }

   /** {@code <file>#<method>} for one record, or null when it has no {@code in=}. */
   private static String bucketOf(String site)
   {
      int at = site.indexOf(" in=");
      if (at < 0)
      {
         return null;
      }
      String method = site.substring(at + 4).trim();
      int space = method.indexOf(' ');
      if (space >= 0)
      {
         method = method.substring(0, space);
      }
      int colon = site.indexOf(':');
      String file = colon < 0 ? site : site.substring(0, colon);
      return method.length() == 0 ? null : file + "#" + method;
   }

   /** The records whose text contains {@code needle}, in order. */
   private static List<String> matching(List<String> sites, String needle)
   {
      List<String> out = new ArrayList<String>();
      for (String s : sites)
      {
         if (s.contains(needle))
         {
            out.add(s);
         }
      }
      return out;
   }

   private static int distinctFiles(List<String> sites)
   {
      Set<String> f = new HashSet<String>();
      for (String s : sites)
      {
         int colon = s.indexOf(':');
         f.add(colon < 0 ? s : s.substring(0, colon));
      }
      return f.size();
   }

   private int countIo(String suffix)
   {
      int n = 0;
      for (String f : ioFiles)
      {
         if (f.endsWith(suffix))
         {
            n++;
         }
      }
      return n;
   }

   private static void emit(PrintStream ps, String key, List<String> values)
   {
      List<String> sorted = new ArrayList<String>(values);
      Collections.sort(sorted);
      for (int i = 0; i < sorted.size(); i++)
      {
         ps.println(key + "[" + i + "]=" + sorted.get(i));
      }
   }
}
