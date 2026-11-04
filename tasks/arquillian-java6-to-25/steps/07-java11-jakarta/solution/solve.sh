#!/usr/bin/env bash
# Golden solution, step 07 -- 07-java11-jakarta.
#
# Four transforms over the tree step 06 left behind, and one of them is the
# whole rung. Nothing here compiles anything; the build that proves the edits
# is the one the step is graded by.
#
#   the namespace   javax.annotation, javax.ejb, javax.interceptor and
#                   javax.transaction become jakarta.*, and the `javax.`
#                   packages that are JDK rather than EE do not move
#   the level       the build goes to --release 11, and every other place in
#                   the build that states the level in Java-10 terms -- the
#                   JavaVersion pair, the TargetJvmVersion attribute, the
#                   comment -- moves with it, wherever those live
#   the arrays      8 `toArray(new T[n])` sites become `toArray(T[]::new)`
#   the client      the servlet protocol's HttpURLConnection retry loop
#                   becomes java.net.http.HttpClient
#
# WHY A PROGRAM AND NOT A LIST, which is the opposite of step 06's answer.
# Step 06 shipped 120 exact-text edits because no rule generated them: `var` is
# legal or not one declaration at a time. This rung is two rules and three
# exclusions, and the exclusions are the part worth reading. `javax.servlet`
# is EE and would move, and does not, because the only servlet container on
# this tree is Jetty 6.1.9 and it cannot load a jakarta.servlet.http.Http\
# Servlet -- the tree would compile and then fail on a dead port.
# `javax.enterprise` does not move because Jakarta CDI 3.0 needs Weld 4 and
# this tree's own CDI test drives Weld 1.1 internals that Weld 4 deleted.
# `javax.inject` does not move because it occurs three times, twice inside the
# module pinned to CDI 1.0, and moving the third would leave one package half
# on each namespace. A list of substitutions would show the five that moved
# and hide the three decisions that are the actual content of a Jakarta
# migration, so the tables ship as tables.
#
# WHAT THE HTTP REWRITE CHANGES, AND THE ONE LINE THAT IS NOT DECORATION.
# HttpURLConnection follows redirects by default. HttpClient.newHttpClient()
# does NOT -- its default is Redirect.NEVER -- so the like-for-like one-liner
# silently stops following them, and on this tree that is a live behaviour
# change with a recorded fixture behind it. `followRedirects(NORMAL)` is
# therefore load-bearing and is not to be dropped as noise. The body handler
# is `ofByteArray` and not `ofInputStream` for a second reason of the same
# kind: the 404 branch never reads the body, and a stream handler would leak
# the connection on exactly that branch. `getResponseMessage()` has no
# HttpClient equivalent at all -- HttpResponse carries the status code and the
# headers and the reason phrase is deliberately absent from HTTP/2 -- so the
# exception message it fed is rebuilt from the status code, and that string
# change is expected and is carried by this step's own recording era.
#
# ONE TRAP THAT COST A GATE AND IS WORTH NAMING. The obvious spelling of the
# array conversion at `toArray(new Class<?>[0])` is `toArray(Class[]::new)`,
# on the instinct that a generic array constructor reference is not legal. It
# is legal -- `Class<?>[]::new` compiles -- and stripping the type argument to
# avoid it leaves a RAW TYPE, which an inherited gate holds at zero. The type
# arguments stay.
#
# The block below is embedded verbatim from authoring/solution-src/apply11.py
# by authoring/mk-solve07.py, which reads it back out of this file afterwards
# and diffs it against the source. Edit the source and re-run the generator;
# do not edit the block here.
#
# Run inside the agent container, from anywhere.
set -euo pipefail

TREE=${TREE:-/app/arquillian}
cd "${TREE}"

# ==========================================================================
# 1. The transforms.
# ==========================================================================
# Captured rather than streamed, because the census it prints is the
# assertion. apply11.py refuses a tree that does not hold every anchor it was
# computed against, and it re-walks the whole tree afterwards to prove no EE
# `javax.` reference survived -- but a solve.sh that let those lines scroll
# past unread would still be trusting that the program ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it the commonest failure -- run against a tree that is not the one step 06
# produces -- kills the script on the line below with apply11.py's whole
# explanation still sitting unprinted inside ${ap}.
ap=$(python3 - "${TREE}" 2>&1 <<'PY_APPLY11'
#!/usr/bin/env python3
"""Step 07 golden: Jakarta EE 9 namespace flip + --release 11, applied to a tree.

Two rules, and the whole step is in the gap between them:
  * the Jakarta EE packages move javax.* -> jakarta.*
  * the JDK packages that merely start with `javax.` do NOT, because they never
    became jakarta.* and never will.  A blanket s/javax\\./jakarta./ breaks them.

Trap 7: every edit asserts it landed, by byte, and the script exits non-zero if
any expected substitution found no site.
"""
import os, re, sys

# `servlet` is deliberately NOT here.  It is Jakarta EE, and it would move, but the
# only servlet container on this tree is Jetty 6.1.9, which implements javax.servlet
# 2.5 and cannot load a jakarta.servlet.http.HttpServlet.  Moving it compiles and then
# fails ProtocolTestCase at runtime on a dead localhost:8181.  See NOTES 12.6.
# `enterprise` is excluded for the same class of reason as `servlet`: see NOTES 12.7.
# Jakarta CDI 3.0 needs Weld 4+, and testenrichers/cdi's own test drives Weld 1.1
# internals (org.jboss.weld.context.api.helpers) that Weld 4 deleted.
# `inject` is deliberately NOT here, and it is the third package dropped for a
# measured reason rather than a guessed one.  javax.inject.Inject occurs THREE
# times on this tree: twice inside testenrichers/cdi, which is pinned to
# cdi-api 1.0 and cannot move, and once in OSGiTestEnricher, which could.
# Moving the one would leave the package MIXED -- jakarta.inject.Inject and
# javax.inject.Inject both live on the same tree -- and the behaviour probe
# binds its own fixture annotations to the tree's namespace ONE PACKAGE AT A
# TIME.  A package that is both cannot be bound to either, and the probe refuses
# rather than recording against a coin flip.  javax.inject moves on the rung
# that retires Weld 1.1, with the rest of the CDI module.  See NOTES 12.10.
EE  = ("annotation", "ejb", "interceptor", "transaction")
JDK = ("management", "naming", "xml", "security", "sql", "net", "crypto",
       "imageio", "sound", "swing", "print", "script", "tools", "lang")

# javax.annotation.processing is JDK, not Common Annotations.
KEEP_EXACT = ("javax.annotation.processing",)

# The two EE API coordinates, rewritten wherever any *.gradle names them.
#
# EVERY OCCURRENCE, NOT THE FIRST, and again the two trees this has to run on
# are what taught that. The tree a blind run produced declares each API once,
# as `provided`; the tree the shipped solve chain produces declares each one
# TWICE, `compileOnly` for main and `testImplementation` for the tests. Moving
# only the first leaves main compiling against jakarta.annotation.Resource and
# the tests compiling against javax.annotation.Resource -- and the test sources
# are among the ones whose imports this script has just moved, so the tests
# would not compile at all. A half-migrated classpath is a worse state than an
# unmigrated one, because it fails in the module furthest from the edit.
DEPS = [
  ("'javax.annotation:jsr250-api:1.0'",
   "'jakarta.annotation:jakarta.annotation-api:2.0.0'"),
  ("'javax.ejb:ejb-api:3.0'",
   "'jakarta.ejb:jakarta.ejb-api:4.0.0'"),
]

# The compile level, and every place in the build that describes it.
#
# FOUND BY CONTENT AND NOT BY PATH, and that is a measured decision rather than
# a taste. `options.release = 10` lives in gradle/java-module.gradle on the tree
# this rung is handed and in the root build.gradle on the tree the shipped
# solution chain produces -- step 06's own answer and the step-06 blind run's
# answer put it in different files, and both are correct Gradle. Anchoring on
# the path would make this script work on one of them and crash on the other,
# which is not a property the reference answer to a migration step may have.
#
# So: one pass over every *.gradle file in the tree, the substitutions below
# applied wherever they occur, and the invariants asserted tree-wide afterwards.
# `options.release = 10` must occur EXACTLY ONCE across the whole tree before
# the pass -- more than one and the tree has two compile levels, none and the
# anchor has drifted -- and the four LEVEL_GONE spellings must occur zero times
# after it.
#
# The JavaVersion pair is not redundant with options.release: `--release` wins
# for javac, but sourceCompatibility is what the Gradle model reports and what
# the published POM's maven.compiler properties are derived from, so leaving it
# at 1_10 publishes an artifact that claims to be Java 10 and is not. The
# TargetJvmVersion attribute is the same fact told to dependency resolution.
# The comment moves with them because it states the cap in words.
LEVEL_SUBS = [
  ("  sourceCompatibility = JavaVersion.VERSION_1_10\n"
   "  targetCompatibility = JavaVersion.VERSION_1_10\n",
   "  sourceCompatibility = JavaVersion.VERSION_11\n"
   "  targetCompatibility = JavaVersion.VERSION_11\n"),
  ("  // Java 10 as well as the bytecode, so a call to something the JDK 11 we\n"
   "  // compile with added is a compile error here rather than a NoSuchMethodError\n"
   "  // on a Java 10 runtime.\n",
   "  // Java 11 as well as the bytecode, so a call to something a later JDK added\n"
   "  // is a compile error here rather than a NoSuchMethodError on a Java 11\n"
   "  // runtime.\n"),
  ("TargetJvmVersion.TARGET_JVM_VERSION_ATTRIBUTE, 10)",
   "TargetJvmVersion.TARGET_JVM_VERSION_ATTRIBUTE, 11)"),
  ("options.release = 10",
   "options.release = 11"),
]

# Spellings that may not survive anywhere under *.gradle once the pass is done.
LEVEL_GONE = ("VERSION_1_10", "options.release = 10",
              "TARGET_JVM_VERSION_ATTRIBUTE, 10)", "Java 10")

def flip(text):
    """Rewrite only the EE packages. Returns (text, n_moved, n_kept)."""
    moved = kept = 0
    def sub(m):
        nonlocal moved, kept
        whole, pkg = m.group(0), m.group(1)
        if whole.startswith(KEEP_EXACT):
            kept += 1; return whole
        if pkg in EE:
            moved += 1; return "jakarta." + whole[len("javax."):]
        kept += 1; return whole
    text = re.sub(r"javax\.([a-z]+)(?:\.[a-zA-Z_$][a-zA-Z0-9_$]*)*", sub, text)
    return text, moved, kept

# ---------------------------------------------------------------- Java 11 ---

def toarray(root):
    """Collection.toArray(IntFunction) replaces the sized/zero-length array idiom."""
    pat = re.compile(r"\.toArray\(new ([A-Za-z_][\w.]*(?:<[^>]*>)?)\[0\]\)")
    n = 0
    for dirpath, _, names in os.walk(root):
        for nm in names:
            if not nm.endswith(".java"):
                continue
            fp = os.path.join(dirpath, nm)
            s = open(fp, encoding="utf-8").read()
            if ".toArray(new " not in s:
                continue
            # Keep the type arguments: stripping them to Class[] would introduce a raw
            # type, and step 06 ships a gate that holds raw types at zero (trap 9).
            out, k = pat.subn(lambda m: ".toArray(%s[]::new)" % m.group(1), s)
            if k:
                open(fp, "w", encoding="utf-8").write(out)
                n += k
    print(f"  toArray(IntFunction): {n} site(s)")
    return n

# ---------------------------------------------------------------- HttpClient
#
# The one substantive Java 11 LIBRARY migration this tree has a population for.
# `java.net.http.HttpClient` (JEP 321, standard in 11) replaces the
# HttpURLConnection retry loop in the servlet protocol's method executor.
#
# BodyHandlers.ofByteArray and not ofInputStream, deliberately: ofInputStream
# hands back a stream the caller must close on EVERY branch or the connection
# leaks, and this loop has a 404 branch that does not read the body at all.  The
# byte-array handler drains and releases the connection itself, so the 404 path
# needs no finally and the 200 path keeps the ObjectInputStream it already had.
#
# followRedirects(NORMAL) is NOT decoration.  HttpURLConnection follows redirects
# by default and HttpClient.newHttpClient() does NOT -- its default is
# Redirect.NEVER.  The naive one-liner therefore SILENTLY STOPS FOLLOWING
# REDIRECTS, and on this tree that is a live behaviour change: the recorded
# fixture `servlet.exec-redirect` goes from `invoke=ok result.status=PASSED` to
# an IllegalStateException.  Found by running the probe against the first golden
# rather than by reading the javadoc.  Family A is the gate that charges it.
#
# `getResponseMessage()` has NO HttpClient equivalent -- HttpResponse carries
# the status code and the headers, and the reason phrase is deliberately absent
# (HTTP/2 has none).  It appeared in one exception message; the code stays.
# That is a real judgement an agent has to make rather than a lookup, and the
# checklist names the property as "the reason phrase is gone from the message",
# not as any particular wording.  See NOTES 12.9.
HTTP_FILE = "protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/ServletMethodExecutor.java"

# The file's java.* import run, rewritten AS A SET rather than as a block.
#
# The block form was what this script shipped first, and it was wrong for a
# reason worth writing down: step 06's answer to this same file is not unique.
# The tree the shipped solve chain produces collapsed
#     try (InputStream response = c.getInputStream();
#          var ois = new ObjectInputStream(response))
# into the one-resource form, and so carries no `java.io.InputStream` import;
# the tree a blind run produced kept two resources and does. Both are correct
# Java 10 and both are correct answers to step 06. A verbatim eight-line anchor
# matches exactly one of them.
#
# What IS the same about both is the set: whatever else the file imports, the
# HttpURLConnection era brings HttpURLConnection/URL/URLConnection and possibly
# InputStream, and the java.net.http era wants ByteArrayInputStream and the
# three HttpClient types. So: drop what the old era needed, add what the new
# one needs, keep the four that outlive the change, re-sort. Sorting is what
# makes the output a function of the set and not of the input's order.
HTTP_DROP = ("java.io.InputStream", "java.net.HttpURLConnection",
             "java.net.URL", "java.net.URLConnection")
HTTP_ADD  = ("java.io.ByteArrayInputStream", "java.net.http.HttpClient",
             "java.net.http.HttpRequest", "java.net.http.HttpResponse")
# Present before and after, or the file is not the one this transform is for.
HTTP_KEEP = ("java.io.ObjectInputStream", "java.net.URI",
             "java.time.Duration", "java.time.Instant")

def http_imports(s):
    """Rewrite the contiguous `import java.*;` run. Returns (text, None) or
    (None, why)."""
    lines = s.split("\n")
    idx = [k for k, ln in enumerate(lines) if ln.startswith("import java.")]
    if not idx:
        return None, "no java.* import run"
    lo, hi = idx[0], idx[-1]
    if any(not lines[k].startswith("import java.") for k in range(lo, hi + 1)):
        return None, "the java.* imports are not contiguous"
    have = {ln[len("import "):-1] for ln in lines[lo:hi + 1]}
    missing = [k for k in HTTP_KEEP if k not in have]
    if missing:
        return None, "missing expected import(s): " + ", ".join(missing)
    if not (have & set(HTTP_DROP)):
        return None, "no legacy java.net import to drop"
    want = sorted((have - set(HTTP_DROP)) | set(HTTP_ADD))
    return "\n".join(lines[:lo] + ["import %s;" % q for q in want]
                      + lines[hi + 1:]), None

def httpclient(root):
    fp = os.path.join(root, HTTP_FILE)
    s = open(fp, encoding="utf-8").read()
    s, why = http_imports(s)
    if s is None:
        print("  MISS HttpClient: %s" % why); return 0

    # The body between `private TestResult execute` and the closing brace of the
    # class.  Anchored on the two literal lines that bracket it rather than on a
    # brace count, so a partial match cannot silently rewrite half a method.
    start = s.index("   private TestResult execute(String url) throws Exception")
    # The class's own closing brace, which on this file is the LAST byte: it has no
    # trailing newline, so rindex("}\n") finds the METHOD's brace and leaves a
    # stray one behind.  Measured, on a build that failed with "class, interface,
    # or enum expected".  Anchor on the newline before it instead.
    end = s.rindex("\n}") + 1
    body = """   private TestResult execute(String url) throws Exception 
   {
      HttpClient client = HttpClient.newBuilder()
            .followRedirects(HttpClient.Redirect.NORMAL)
            .build();
      HttpRequest request = HttpRequest.newBuilder(URI.create(url)).GET().build();
      Instant timeoutTime = Instant.now().plus(Duration.ofMillis(1000));
      boolean interrupted = false;
      while (timeoutTime.isAfter(Instant.now()))
      {
         HttpResponse<byte[]> response = client.send(request, HttpResponse.BodyHandlers.ofByteArray());
         if (response.statusCode() == 200)
         {
            Object o;
            try (var ois = new ObjectInputStream(new ByteArrayInputStream(response.body())))
            {
               o = ois.readObject();
            }

            if (!(o instanceof TestResult))
            {
               throw new IllegalStateException("Error reading test results - expected a TestResult but got " + o);
            }
            return (TestResult) o;
         }
         else if (response.statusCode() != 404)
         {
            throw new IllegalStateException(
                  "Error launching test at " + url + ". " +
                  "Got " + response.statusCode());
         }
         try
         {
            Thread.sleep(200);
         }
         catch (InterruptedException e)
         {
            interrupted = true;
         }
      }
      if (interrupted)
      {
         Thread.currentThread().interrupt();
      }
      throw new IllegalStateException("Error launching test at " + url + ". Kept on getting 404s.");
   }
"""
    s = s[:start] + body + s[end:]
    open(fp, "w", encoding="utf-8").write(s)
    # Trap 7: assert on bytes, in both directions.
    t = open(fp, encoding="utf-8").read()
    if t.count("{") != t.count("}"):
        print(f"  MISS HttpClient: braces unbalanced ({t.count('{')} vs {t.count('}')})"); return 0
    for dead in ("HttpURLConnection", "URLConnection", "openConnection", "getResponseMessage",
                 "getResponseCode", "setUseCaches", "disconnect"):
        if dead in t:
            print(f"  MISS HttpClient: {dead} survives"); return 0
    n = sum(t.count(x) for x in ("java.net.http.HttpClient", "java.net.http.HttpRequest",
                                 "java.net.http.HttpResponse"))
    print(f"  HttpClient: 1 file, {n} java.net.http imports, HttpURLConnection gone")
    return 1

def main(root):
    moved = kept = files = 0
    for dirpath, _, names in os.walk(root):
        for n in names:
            if not n.endswith(".java"):
                continue
            p = os.path.join(dirpath, n)
            with open(p, encoding="utf-8") as fh:
                src = fh.read()
            # The whole testenrichers/cdi module stays on javax: it is pinned to
            # cdi-api 1.0, which supplies its javax.inject transitively.  See 12.7.
            if "javax." not in src or "testenrichers/cdi/" in p.replace(os.sep, "/"):
                continue
            out, mv, kp = flip(src)
            moved += mv; kept += kp
            if out != src:
                files += 1
                with open(p, "w", encoding="utf-8") as fh:
                    fh.write(out)
    print(f"  sources: {files} files, {moved} references moved, {kept} left on javax")

    bad = 0
    gradles = []
    for dirpath, _, names in os.walk(root):
        for n in names:
            if n.endswith(".gradle"):
                gradles.append(os.path.join(dirpath, n))
    gradles.sort()
    dep_lines = dep_files = 0
    for p in gradles:
        with open(p, encoding="utf-8") as fh:
            t = fh.read()
        out = t
        for old, new in DEPS:
            dep_lines += out.count(old)
            out = out.replace(old, new)
        if out != t:
            dep_files += 1
            with open(p, "w", encoding="utf-8") as fh:
                fh.write(out)
    # Trap 7, in both directions: each coordinate had to be there to move, and
    # none of the old spelling may survive in any build script afterwards.
    for old, _ in DEPS:
        left = [os.path.relpath(p, root) for p in gradles
                if old in open(p, encoding="utf-8").read()]
        if left:
            print("  MISS deps: %s survives in %s" % (old, ", ".join(left)))
            bad += 1
    if dep_lines < len(DEPS):
        print("  MISS deps: only %d coordinate reference(s) found, wanted at "
              "least %d" % (dep_lines, len(DEPS)))
        bad += 1
    if not bad:
        print("  deps: %d coordinate reference(s) in %d build script(s) rewritten"
              % (dep_lines, dep_files))

    # The compile level, tree-wide.  See LEVEL_SUBS above for why by content
    # and not by path.  Re-read rather than reusing the dep pass's text, which
    # is stale by exactly the edits that pass just made.
    texts = {p: open(p, encoding="utf-8").read() for p in gradles}
    bad0 = bad
    n_rel = sum(t.count("options.release = 10") for t in texts.values())
    if n_rel != 1:
        print("  MISS build level: 'options.release = 10' occurs %d time(s) "
              "across %d .gradle file(s), wanted 1" % (n_rel, len(gradles)))
        bad += 1
    else:
        hits = 0
        for p in gradles:
            t = texts[p]
            for old, new in LEVEL_SUBS:
                if old in t:
                    hits += t.count(old)
                    t = t.replace(old, new)
            if t != texts[p]:
                open(p, "w", encoding="utf-8").write(t)
                texts[p] = t
        left = [(os.path.relpath(p, root), g)
                for p in gradles for g in LEVEL_GONE if g in texts[p]]
        for rel, g in left:
            print("  MISS build level: %r survived in %s" % (g, rel))
            bad += 1
        n11 = sum(t.count("options.release = 11") for t in texts.values())
        if n11 != 1:
            print("  MISS build level: 'options.release = 11' occurs %d time(s), wanted 1" % n11)
            bad += 1
        if bad == bad0:
            print("  build level: %d statement(s) moved to Java 11 across %d .gradle file(s)"
                  % (hits, len(gradles)))

    if toarray(root) != 8:
        print("  MISS toArray: expected 8 sites"); bad += 1

    if httpclient(root) != 1:
        bad += 1

    # Trap 7: no EE javax reference may survive, and the JDK ones must all be there.
    left = 0
    for dirpath, _, names in os.walk(root):
        for n in names:
            if n.endswith(".java"):
                fp = os.path.join(dirpath, n).replace(os.sep, "/")
                if "testenrichers/cdi/" in fp:
                    continue
                s = open(fp, encoding="utf-8").read()
                for pkg in EE:
                    left += len(re.findall(r"javax\.%s\b" % pkg, s))
    print(f"  residual EE javax references: {left}")
    if left or bad:
        print("APPLY11 FAILED"); return 1
    print("APPLY11 OK"); return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
PY_APPLY11
) || true
printf '%s\n' "${ap}"
for want in \
      '  toArray(IntFunction): 8 site(s)' \
      '  HttpClient: 1 file, 3 java.net.http imports, HttpURLConnection gone' \
      '  residual EE javax references: 0' \
      'APPLY11 OK'; do
   if ! printf '%s\n' "${ap}" | grep -qxF "${want}"; then
      echo "solve07: apply11.py did not report: ${want}" >&2
      exit 1
   fi
done
# The dep count is not a constant: one tree declares each EE API once and the
# other declares it twice.  What is constant is that it reported a rewrite at
# all, with both numbers non-zero.
if ! printf '%s\n' "${ap}" | grep -qE \
     '^  deps: [1-9][0-9]* coordinate reference\(s\) in [1-9][0-9]* build script\(s\) rewritten$'; then
   echo "solve07: apply11.py did not report a dependency rewrite" >&2
   exit 1
fi

# ==========================================================================
# 2. The compile level, which this step moves for the second rung running.
# ==========================================================================
# Re-read off the tree rather than inferred from apply11.py having printed its
# census, because this is the one property of the build the rest of the task is
# scored on, and because a Gradle build has several separate places to say what
# level it is. `options.release` is the one javac obeys; `sourceCompatibility`
# is the one the Gradle model reports and the published POM derives from; the
# TargetJvmVersion attribute is the one dependency resolution obeys; the
# comment is the one a human reads. All of them, or the build is telling two
# different stories.
#
# ASKED OF EVERY *.gradle FILE AND NOT OF ONE PATH. Which file holds the
# compile level is a layout choice, not a fact about the migration: it sits in
# gradle/java-module.gradle on one tree this script is expected to run on and
# in the root build.gradle on another, and an assertion anchored on a path
# passes on one of them by luck and crashes on the other. The invariant that is
# actually true of both is the tree-wide one: exactly one statement of the
# level, at 11, and no spelling of 10 left anywhere.
mapfile -t gradles < <(find . -name '*.gradle' -type f | sort)
if [ "${#gradles[@]}" -lt 1 ]; then
   echo "solve07: no .gradle files under $(pwd)" >&2
   exit 1
fi
n=$(grep -h -o -F 'options.release = 11' "${gradles[@]}" 2>/dev/null | wc -l)
if [ "${n}" != 1 ]; then
   echo "solve07: 'options.release = 11' occurs ${n} time(s) across ${#gradles[@]} .gradle file(s), wanted 1" >&2
   exit 1
fi
for stale in 'VERSION_1_10' 'options.release = 10' 'TARGET_JVM_VERSION_ATTRIBUTE, 10)' 'Java 10'; do
   hits=$(grep -l -F "${stale}" "${gradles[@]}" 2>/dev/null || true)
   if [ -n "${hits}" ]; then
      echo "solve07: '${stale}' survives in: ${hits}" >&2
      exit 1
   fi
done
# Wherever the JavaVersion pair exists at all, it names 11.  The tree that
# keeps its level in the root build.gradle has no such lines, and passes this
# vacuously -- which is correct, because there is nothing there to disagree.
bad=$(grep -h -E '^[[:space:]]*(source|target)Compatibility[[:space:]]*=' "${gradles[@]}" 2>/dev/null       | grep -v -F 'JavaVersion.VERSION_11' || true)
if [ -n "${bad}" ]; then
   echo "solve07: a compatibility line does not name VERSION_11:" >&2
   echo "${bad}" >&2
   exit 1
fi
echo "solve07: the build now compiles at --release 11"

# ==========================================================================
# 3. The two dependency coordinates.
# ==========================================================================
# The namespace flip is not a source-only edit and this is where that shows.
# The API jars are named by coordinate in two module build scripts, and a tree
# whose sources import jakarta.annotation while its build still resolves
# jsr250-api does not compile. Asserted on arrival AND on departure, because a
# coordinate that was added beside the old one rather than in place of it
# leaves both on the compile classpath and the failure moves to whichever jar
# the resolver happens to order first.
#
# ASKED OF EVERY *.gradle, AND ON EVERY OCCURRENCE. Which module script holds
# the coordinate, and how many times, is not fixed: one tree declares each API
# once as `provided`, the other twice -- `compileOnly` for main and
# `testImplementation` for the tests. The per-file, first-occurrence form of
# this assertion passed on the first tree and hid a half-migrated classpath on
# the second, where main resolved jakarta and the tests still resolved javax.
for pair in \
      'jakarta.annotation:jakarta.annotation-api|javax.annotation:jsr250-api' \
      'jakarta.ejb:jakarta.ejb-api|javax.ejb:ejb-api'; do
   new=${pair%%|*}; old=${pair#*|}
   hits=$(grep -rl --include='*.gradle' -F "${old}" . || true)
   if [ -n "${hits}" ]; then
      echo "solve07: '${old}' survives in: ${hits}" >&2
      exit 1
   fi
   n=$(grep -rh --include='*.gradle' -o -F "${new}" . | wc -l)
   if [ "${n}" -lt 1 ]; then
      echo "solve07: '${new}' appears ${n} time(s), wanted at least 1" >&2
      exit 1
   fi
done
echo "solve07: both EE API coordinates now resolve from jakarta.*"

echo "solve: step 07 complete"
