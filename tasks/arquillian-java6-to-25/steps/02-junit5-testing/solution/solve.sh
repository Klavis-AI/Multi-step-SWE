#!/usr/bin/env bash
# Golden solution, step 02 -- 02-junit5-testing.
#
# Moves the project's own test suite from JUnit 4.8.1 / TestNG 5.14.6 /
# Mockito 1.8.3 to the JUnit Platform: Jupiter for the tests this project
# writes, and the vintage and TestNG engines for the handful of *fixture*
# methods that the product's own integration code drives reflectively and
# which therefore cannot leave their original framework.
#
# The published API does not move. arquillian-junit publishes
#    Arquillian extends org.junit.runners.BlockJUnit4ClassRunner
# and arquillian-testng publishes
#    Arquillian implements org.testng.IHookable
# so JUnit 4 and TestNG stay on those modules' compile classpaths as the
# frameworks Arquillian *integrates with*. What changes is the framework this
# project's own 54 test classes are written against.
#
# Run inside the agent container, from anywhere.
set -euo pipefail

TREE=${TREE:-/app/arquillian}
cd "${TREE}"

export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/temurin-8-jdk}
export PATH="${JAVA_HOME}/bin:${PATH}"

# Ceilings, all measured from the jars' own class-file major versions rather
# than from release notes: these are the newest releases of each library that
# still run on the Java 8 toolchain this step is built with. Jupiter 6 needs
# Java 17 and Mockito 5 needs Java 11, so neither is reachable yet; later
# steps raise the JDK and can raise these with it.
JUNIT4=4.13.2
JUPITER=5.14.4
PLATFORM=1.14.4
MOCKITO=4.11.0

# ==========================================================================
# 1. The test sources.
# ==========================================================================
python3 - <<'PY'
import os
import re

TREE = os.getcwd()

# The two files that are only *partly* this project's own test code. Handled
# separately, below.
HYBRIDS = {
    'junit/src/test/java/org/jboss/arquillian/junit/JUnitTestRunnerTestCase.java',
    'junit/src/test/java/org/jboss/arquillian/junit/JUnitIntegrationTestCase.java',
}

# ... and the one file that is *entirely* a fixture. JMXTestRunnerTestCase
# calls runTestMethodLocal("org.jboss.arquillian.jmx.DummyTestCase",
# "testMethod"), which goes through the Arquillian SPI to arquillian-junit's
# JUnit 4 runner; a Jupiter @Test there is simply not a runnable method as far
# as that runner is concerned. It stays on JUnit 4 untouched, and the vintage
# engine reports it -- which is also how it keeps its own place in the suite,
# since it is a recorded test in its own right.
FIXTURES = {
    'protocols/jmx/src/test/java/org/jboss/arquillian/jmx/DummyTestCase.java',
}

# The arquillian-testng module's own suite, which does not move in this step.
#
# This module's product code is compiled against TestNG 5.14.6 and publishes
#    Arquillian implements org.testng.IHookable
# The JUnit Platform's TestNG engine requires TestNG 7, and TestNG 7 renamed
# the two internals this module's main code uses (org.testng.internal.
# AnnotationTypeEnum is gone, and ITestNGMethod's reflective handle moved
# behind getConstructorOrMethod()). Bumping the library is therefore a
# third-party dependency migration, not a test-framework migration, and it is
# not what this step is about. The module keeps useTestNG(), so its two
# @org.testng.annotations.Test methods keep running and keep their place in
# the recorded suite; its two JUnit 4 test classes stay on JUnit 4 because
# TestNG's runner is what collects them.
TESTNG_SUITE = {
    'testng/src/test/java/org/jboss/arquillian/testng/TestNGTestRunnerTestCase.java',
    'testng/src/test/java/org/jboss/arquillian/testng/TestNGDeploymentAppenderTestCase.java',
}

IMPORTS = [
    (r'^import junit\.framework\.Assert;$',            'import org.junit.jupiter.api.Assertions;'),
    (r'^import org\.junit\.Assert;$',                  'import org.junit.jupiter.api.Assertions;'),
    (r'^import org\.junit\.Test;$',                    'import org.junit.jupiter.api.Test;'),
    (r'^import org\.junit\.Before;$',                  'import org.junit.jupiter.api.BeforeEach;'),
    (r'^import org\.junit\.After;$',                   'import org.junit.jupiter.api.AfterEach;'),
    (r'^import org\.junit\.BeforeClass;$',             'import org.junit.jupiter.api.BeforeAll;'),
    (r'^import org\.junit\.AfterClass;$',              'import org.junit.jupiter.api.AfterAll;'),
    (r'^import org\.junit\.Ignore;$',                  'import org.junit.jupiter.api.Disabled;'),
    (r'^import org\.junit\.runner\.RunWith;$',         'import org.junit.jupiter.api.extension.ExtendWith;'),
    (r'^import org\.mockito\.runners\.MockitoJUnitRunner;$',
                                                       'import org.mockito.junit.jupiter.MockitoExtension;\n'
                                                       'import org.mockito.junit.jupiter.MockitoSettings;\n'
                                                       'import org.mockito.quality.Strictness;'),
    (r'^import static org\.mockito\.Matchers\.(\w+);$', r'import static org.mockito.ArgumentMatchers.\1;'),
    (r'^import static org\.junit\.Assert\.(\*|\w+);$', r'import static org.junit.jupiter.api.Assertions.\1;'),
]

# How many arguments the JUnit 4 *message* overload of each assertion takes.
# Per-method, not a single ">= 3": JUnit 4's assertTrue(String, boolean) has
# two. Getting this wrong is not a compile error -- Jupiter's
# assertNotNull(Object actual, String message) happily accepts a
# (message, object) call and then asserts that the *message* is non-null,
# which can never fail. `fail` is absent on purpose: its message is the first
# parameter in both frameworks.
MSG_ARITY = {
    'assertEquals': 3, 'assertArrayEquals': 3, 'assertSame': 3, 'assertNotSame': 3,
    'assertTrue': 2, 'assertFalse': 2, 'assertNotNull': 2, 'assertNull': 2,
}


def split_args(s):
    out, depth, cur, i, n, instr = [], 0, '', 0, len(s), False
    while i < n:
        c = s[i]
        if instr:
            cur += c
            if c == '\\':
                cur += s[i + 1]; i += 2; continue
            if c == '"':
                instr = False
            i += 1; continue
        if c == '"':
            instr = True; cur += c; i += 1; continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(cur); cur = ''; i += 1; continue
        cur += c; i += 1
    out.append(cur)
    return out


def move_message_last(src, bare=False):
    """Assert.assertEquals("msg", a, b) -> Assert.assertEquals(a, b, "msg").

    Only rewrites calls whose first argument is a plain string literal, which
    is the only shape this tree uses and the only one where "leading String"
    unambiguously means "message" rather than "expected".

    Matches the JUnit 4 name `Assert.`, and the caller renames Assert ->
    Assertions only afterwards. That ordering is what makes the pass
    idempotent: on a second run there is no `Assert.` left to match. Keying on
    the post-rename name instead would re-move the one call in this tree whose
    *migrated* first argument is itself a string literal --
    assertEquals("some string", event.getString(), "...") -- silently
    corrupting it.

    `bare` widens the match to unqualified calls, for the two files that
    static-import their assertions. It is decided from the pre-migration
    imports for the same reason.
    """
    out, i, changed = '', 0, 0
    names = '|'.join(MSG_ARITY)
    if bare:
        pat = re.compile(r'(?<![\w.])(?:Assert\.)?(' + names + r')\(')
    else:
        pat = re.compile(r'\bAssert\.(' + names + r')\(')
    while True:
        m = pat.search(src, i)
        if not m:
            out += src[i:]
            break
        depth, j = 1, m.end()
        while j < len(src) and depth:
            if src[j] == '"':
                j += 1
                while j < len(src) and src[j] != '"':
                    j += 2 if src[j] == '\\' else 1
            elif src[j] == '(':
                depth += 1
            elif src[j] == ')':
                depth -= 1
            j += 1
        inner = src[m.end():j - 1]
        args = split_args(inner)
        # Arity picks the message overload; the leading quote is what keeps
        # assertEquals(expected, actual, delta) -- also three arguments, also
        # real JUnit 4 -- from having its delta shuffled into the message
        # slot. A message built by concatenation ("a " + x + " b") starts with
        # a quote and ends with a call, so only the start is tested.
        if (len(args) == MSG_ARITY[m.group(1)]
                and args[0].strip().startswith('"')):
            msg = args[0].strip()
            rest = [a.strip() for a in args[1:]]
            inner = ', '.join(rest) + ', ' + msg
            changed += 1
        out += src[i:m.end()] + inner + ')'
        i = j
    return out, changed


def expected_to_assert_throws(text):
    """@Test(expected = X.class) -> Assertions.assertThrows(X.class, Executable).

    Jupiter dropped the `expected` attribute. The replacement idiom is
    normally assertThrows with a lambda, but this project compiles at
    -source 1.6 -- its published bytecode is Java 6 and stays that way -- so
    the Executable is an anonymous class instead. Every one of these method
    bodies is self-contained (no locals declared outside it), so moving the
    whole body inside captures nothing and needs no `final`.
    """
    out, i, n = '', 0, 0
    pat = re.compile(r'( *)@Test\(expected\s*=\s*([\w.]+)\.class\)\n')
    while True:
        m = pat.search(text, i)
        if not m:
            out += text[i:]
            break
        indent, exc = m.group(1), m.group(2)
        # The signature runs from here to the '{' that opens the body. The
        # brace is on its own line in this tree's style, but scanning for it
        # rather than assuming that keeps the pass honest either way.
        b = text.index('{', m.end())
        sig = text[m.end():b]
        depth, j = 1, b + 1
        while depth:
            if text[j] == '"':
                j += 1
                while text[j] != '"':
                    j += 2 if text[j] == '\\' else 1
            elif text[j] == "'":
                j += 1
                while text[j] != "'":
                    j += 2 if text[j] == '\\' else 1
            elif text[j] == '{':
                depth += 1
            elif text[j] == '}':
                depth -= 1
            j += 1
        body = text[b + 1:j - 1].strip('\n')
        shifted = '\n'.join(('      ' + ln) if ln.strip() else '' for ln in body.split('\n'))
        out += text[i:m.start()]
        out += ('{i}@Test\n{sig}{i}{{\n'
                '{i}   Assertions.assertThrows({exc}.class, new Executable()\n'
                '{i}   {{\n'
                '{i}      public void execute() throws Throwable\n'
                '{i}      {{\n'
                '{body}\n'
                '{i}      }}\n'
                '{i}   }});\n'
                '{i}}}').format(i=indent, sig=sig.rstrip() + '\n', exc=exc, body=shifted)
        n += 1
        i = j
    if n:
        # assertThrows needs Assertions, which a class that previously only
        # declared `expected` may never have imported, and Executable. Both go
        # in sorted among whatever org.junit.jupiter imports are already there.
        want = ['import org.junit.jupiter.api.Assertions;',
                'import org.junit.jupiter.api.function.Executable;']
        lines = out.split('\n')
        have = [k for k, ln in enumerate(lines) if ln.startswith('import org.junit.jupiter.')]
        if not have:
            raise SystemExit('assertThrows rewrite with no jupiter import to anchor on')
        block = sorted(set([lines[k] for k in have] + want))
        for k in reversed(have):
            del lines[k]
        lines[have[0]:have[0]] = block
        out = '\n'.join(lines)
    return out, n


def migrate(path, text):
    # Decided before the imports are rewritten: a statically-imported
    # assertion has no `Assert.` prefix to key the message move on, and the
    # JUnit 4 static import is the only pre-migration marker at hand.
    bare = re.search(r'^import static (?:org\.junit|junit\.framework)\.Assert\.',
                     text, re.M) is not None

    lines = text.split('\n')
    for k, ln in enumerate(lines):
        for pat, rep in IMPORTS:
            new = re.sub(pat, rep, ln)
            if new != ln:
                ln = new
        lines[k] = ln
    text = '\n'.join(lines)

    # Annotations. Anchored on '@' so the several plain `BeforeClass` /
    # `AfterClass` *event types* this tree fires -- e.g.
    # org.jboss.arquillian.spi.event.suite.BeforeClass, constructed as
    # fire(new BeforeClass(getClass())) -- are left alone.
    text = re.sub(r'@Before\b(?!Each|All|Class)', '@BeforeEach', text)
    text = re.sub(r'@After\b(?!Each|All|Class)',  '@AfterEach',  text)
    text = re.sub(r'@BeforeClass\b',              '@BeforeAll',  text)
    text = re.sub(r'@AfterClass\b',               '@AfterAll',   text)
    text = re.sub(r'@Ignore\b',                   '@Disabled',   text)
    # MockitoExtension defaults to STRICT_STUBS; Mockito 1.8.3's
    # MockitoJUnitRunner had no strictness at all, so sixteen of this suite's
    # classes leave stubs unused in some of their tests and would now fail with
    # UnnecessaryStubbingException. The migration is of the test *framework*,
    # and the recorded behaviour of these tests has to survive it, so the
    # runner's leniency is carried over explicitly rather than by editing the
    # tests' stubbings.
    text = re.sub(r'@RunWith\(MockitoJUnitRunner\.class\)',
                  '@ExtendWith(MockitoExtension.class)\n'
                  '@MockitoSettings(strictness = Strictness.LENIENT)', text)

    text, n_reorder = move_message_last(text, bare=bare)
    text = re.sub(r'(?<![\w.])Assert\.', 'Assertions.', text)

    text, n_throws = expected_to_assert_throws(text)
    global THROWS
    THROWS += n_throws

    # Jupiter requires @BeforeAll/@AfterAll to be static; JUnit 4 required the
    # same of @BeforeClass/@AfterClass, so this holds already and is asserted
    # rather than fixed.
    for ann in ('@BeforeAll', '@AfterAll'):
        for m in re.finditer(re.escape(ann) + r'\s*\n\s*((?:public |private |protected )?)(\w[\w\s<>\[\],.]*?)\s+\w+\s*\(', text):
            if 'static' not in m.group(0):
                raise SystemExit('%s: %s on a non-static method:\n%s' % (path, ann, m.group(0)))
    return text, n_reorder


# --- the two hybrids -------------------------------------------------------
#
# Each is split down the middle. The methods that are genuinely this project's
# tests move to Jupiter; the parts that are *fixtures* for the product's own
# JUnit 4 integration stay on JUnit 4, fully qualified so the Jupiter imports
# at the top of the file do not collide with them. The tree already contains
# this exact shape by hand for TestNG, in TestNGTestRunnerTestCase.

RUNNER = 'junit/src/test/java/org/jboss/arquillian/junit/JUnitTestRunnerTestCase.java'
INTEG = 'junit/src/test/java/org/jboss/arquillian/junit/JUnitIntegrationTestCase.java'


def patch_runner(text):
    # shouldProvidePassingTestToRunner and shouldProvideExpectedExceptionToRunner
    # are arguments to JUnitTestRunner.execute(Class, String), which runs them
    # through JUnitCore + Request.method. JUnitTestRunner's own
    # ExpectedExceptionHolder reads description.getAnnotation(org.junit.Test)
    # and calls test.expected() -- so the annotation has to stay a real JUnit 4
    # @Test with a real `expected` attribute, which Jupiter has no equivalent
    # of. Anchoring on the method names is what leaves the two real tests above
    # them free to move.
    text = re.sub(
        r'@Test\(expected = IllegalArgumentException\.class\)\n(\s*public void shouldProvideExpectedExceptionToRunner)',
        r'@org.junit.Test(expected = IllegalArgumentException.class)\n\1', text)
    text = re.sub(
        r'@Test\n(\s*public void shouldProvidePassingTestToRunner)',
        r'@org.junit.Test\n\1', text)
    return text


def patch_integ(text):
    # ArquillianClass1 is driven by JUnitCore over @RunWith(Arquillian.class),
    # and Arquillian extends org.junit.runners.BlockJUnit4ClassRunner -- a
    # published type this migration does not move. The whole nested class stays
    # JUnit 4.
    head, marker, tail = text.partition('   @RunWith(Arquillian.class)')
    if not marker:
        raise SystemExit('JUnitIntegrationTestCase: nested fixture not found')
    tail = marker + tail
    tail = tail.replace('@RunWith(Arquillian.class)',
                        '@org.junit.runner.RunWith(Arquillian.class)')
    for ann in ('BeforeClass', 'AfterClass', 'Before', 'After', 'Test'):
        tail = re.sub(r'@%s\b' % ann, '@org.junit.%s' % ann, tail)
    # Those imports now have no unqualified use left in the file; drop them so
    # the generic pass does not rewrite them into unused Jupiter ones.
    head = re.sub(r'^import org\.junit\.(?:After|AfterClass|Before|BeforeClass);\n', '',
                  head, flags=re.M)
    head = re.sub(r'^import org\.junit\.runner\.RunWith;\n', '', head, flags=re.M)
    return head + tail


THROWS = 0
total, reorders, touched = 0, 0, []
for root, _, files in os.walk(TREE):
    if os.sep + 'src' + os.sep + 'test' + os.sep not in root + os.sep:
        continue
    for f in sorted(files):
        if not f.endswith('.java'):
            continue
        p = os.path.join(root, f)
        rel = os.path.relpath(p, TREE)
        with open(p, encoding='utf-8') as fh:
            src = fh.read()
        if rel in FIXTURES or rel in TESTNG_SUITE:
            total += 1
            continue
        text = src
        if rel == RUNNER:
            text = patch_runner(text)
        elif rel == INTEG:
            text = patch_integ(text)
        text, n = migrate(rel, text)
        total += 1
        if text != src:
            with open(p, 'w', encoding='utf-8') as fh:
                fh.write(text)
            touched.append(rel)
            reorders += n
        elif rel in HYBRIDS:
            raise SystemExit('hybrid %s unchanged -- patch did not apply' % rel)

print('solve02: test sources scanned=%d rewritten=%d message-arg-moves=%d '
      'expected-exception-rewrites=%d' % (total, len(touched), reorders, THROWS))
if len(touched) != 53:
    raise SystemExit('solve02: expected 53 rewritten test sources, got %d' % len(touched))
if THROWS != 19:
    raise SystemExit('solve02: expected 19 @Test(expected=) rewrites, got %d' % THROWS)
PY

# ==========================================================================
# 3. The build.
# ==========================================================================
python3 - "${JUNIT4}" "${JUPITER}" "${PLATFORM}" "${MOCKITO}" <<'PY'
import re
import sys

JUNIT4, JUPITER, PLATFORM, MOCKITO = sys.argv[1:5]


def sub1(s, old, new, where):
    if s.count(old) != 1:
        raise SystemExit('%s: expected exactly one occurrence of %r, found %d'
                         % (where, old, s.count(old)))
    return s.replace(old, new)


# --- root build.gradle -----------------------------------------------------
root = open('build.gradle', encoding='utf-8').read()

root = sub1(root, """      tasks.withType(JavaCompile).configureEach {""", """      // Every library's own suite now runs on the JUnit Platform.
      dependencies {
         testImplementation 'org.junit.jupiter:junit-jupiter:%s'
         testRuntimeOnly 'org.junit.platform:junit-platform-launcher:%s'
      }

      // Gradle turns targetCompatibility 1.6 into a hard requirement on
      // every resolved dependency -- it asks for org.gradle.jvm.version = 6 --
      // and both JUnit 5 and Mockito 4 declare themselves Java 8, so
      // resolution refuses them outright. That requirement is really a
      // statement about what this project *publishes*, and that has not
      // changed: -source/-target 1.6 still cap what javac emits, so every
      // class file this build produces, test classes included, stays at major
      // version 50. So the attribute is overridden on the four *resolvable*
      // classpaths only. The outgoing variants (apiElements, runtimeElements)
      // are untouched, which is why the published poms and module metadata are
      // byte-for-byte what step 01 published.
      ['compileClasspath', 'runtimeClasspath',
       'testCompileClasspath', 'testRuntimeClasspath'].each { cfg ->
         configurations.named(cfg) {
            attributes {
               attribute(org.gradle.api.attributes.java.TargetJvmVersion.TARGET_JVM_VERSION_ATTRIBUTE, 8)
            }
         }
      }

      tasks.withType(JavaCompile).configureEach {""" % (JUPITER, PLATFORM), 'build.gradle')

root = sub1(root, """      test {
         // The reactor's surefire ran these two patterns and no others.
         include '**/*TestCase.class'
         include '**/*TestSuite.class'
      }""", """      test {
         useJUnitPlatform()
         // The reactor's surefire ran these two patterns and no others. They
         // still matter: they are what keeps JUnitIntegrationTestCase's nested
         // ArquillianClass1 fixture from being collected as a test in its own
         // right.
         include '**/*TestCase.class'
         include '**/*TestSuite.class'
      }""", 'build.gradle')

open('build.gradle', 'w', encoding='utf-8').write(root)

# --- per-module build.gradle ----------------------------------------------
# JUnit 4 and old Mockito come off every module's test classpath. The root
# script has already put Jupiter on all of them.
MODULES = ['api', 'spi', 'impl-base', 'junit',
           'protocols/servlet', 'protocols/jmx', 'protocols/jmx-osgi',
           'testenrichers/cdi', 'testenrichers/ejb', 'testenrichers/osgi',
           'testenrichers/resource']
dropped = 0
for m in MODULES:
    p = m + '/build.gradle'
    s = open(p, encoding='utf-8').read()
    new = re.sub(r"^ *testImplementation 'junit:junit:[^']*'\n", '', s, flags=re.M)
    new = re.sub(r"^ *testImplementation 'org\.mockito:mockito-all:[^']*'\n", '', new, flags=re.M)
    if new != s:
        dropped += 1
        open(p, 'w', encoding='utf-8').write(new)
print('solve02: dropped JUnit 4 / mockito-all from %d module build files' % dropped)
# 8, not 9: testng keeps junit:junit for its own two JUnit 4 test classes.
if dropped != 8:
    raise SystemExit('solve02: expected 8 modules to lose them, got %d' % dropped)

# impl-base: the 16 @RunWith(MockitoJUnitRunner.class) classes are now
# @ExtendWith(MockitoExtension.class), which lives in mockito-junit-jupiter.
p = 'impl-base/build.gradle'
s = open(p, encoding='utf-8').read()
s = sub1(s, "   api 'org.jboss.shrinkwrap.resolver:shrinkwrap-resolver-impl-maven:1.0.0-alpha-12'\n",
         "   api 'org.jboss.shrinkwrap.resolver:shrinkwrap-resolver-impl-maven:1.0.0-alpha-12'\n"
         "\n"
         "   testImplementation 'org.mockito:mockito-core:%s'\n"
         "   testImplementation 'org.mockito:mockito-junit-jupiter:%s'\n" % (MOCKITO, MOCKITO), p)
open(p, 'w', encoding='utf-8').write(s)

# junit: arquillian-junit *is* a JUnit 4 integration -- its published
# Arquillian extends BlockJUnit4ClassRunner -- so JUnit 4 stays, bumped to the
# last 4.x. The vintage engine is what still runs the two fixture methods that
# had to keep their JUnit 4 annotations.
p = 'junit/build.gradle'
s = open(p, encoding='utf-8').read()
s = sub1(s, "   compileOnly 'junit:junit:4.8.1'",
         "   compileOnly 'junit:junit:%s'" % JUNIT4, p)
s = sub1(s, "   testImplementation 'org.jboss.shrinkwrap:shrinkwrap-impl-base:1.0.0-alpha-12'",
         "   testImplementation 'junit:junit:%s'\n"
         "   testRuntimeOnly 'org.junit.vintage:junit-vintage-engine:%s'\n"
         "   testImplementation 'org.jboss.shrinkwrap:shrinkwrap-impl-base:1.0.0-alpha-12'"
         % (JUNIT4, JUPITER), p)
open(p, 'w', encoding='utf-8').write(s)

# protocols/jmx exercises JMXTestRunner, which looks a TestRunner up through
# the Arquillian SPI and finds arquillian-junit's. That runner is a JUnit 4
# integration, so JUnit 4 stays on this module's test classpath -- to compile
# the DummyTestCase fixture it drives, and at runtime for the runner itself --
# and the vintage engine reports the fixture's own result.
p = 'protocols/jmx/build.gradle'
s = open(p, encoding='utf-8').read()
s = sub1(s, "   testImplementation project(':junit')",
         "   testImplementation project(':junit')\n"
         "   testImplementation 'junit:junit:%s'\n"
         "   testRuntimeOnly 'org.junit.vintage:junit-vintage-engine:%s'"
         % (JUNIT4, JUPITER), p)
open(p, 'w', encoding='utf-8').write(s)

# testng is deliberately not rewritten: see TESTNG_SUITE above. It keeps
# TestNG 5.14.6, keeps junit:junit for its own two JUnit 4 test classes, and
# keeps useTestNG(), which overrides the shared useJUnitPlatform() because a
# subproject's own build script is configured after the root's subprojects
# block. Asserted rather than assumed, so a later edit to either file that
# silently moved this module onto the platform fails the solution instead of
# the verifier.
p = 'testng/build.gradle'
s = open(p, encoding='utf-8').read()
for needle in ("   compileOnly 'org.testng:testng:5.14.6'",
               "   testImplementation 'org.testng:testng:5.14.6'",
               "   testImplementation 'junit:junit:4.8.1'",
               '   useTestNG()'):
    if s.count(needle) != 1:
        raise SystemExit('testng/build.gradle: expected exactly one %r, found %d'
                         % (needle, s.count(needle)))
print('solve02: build files rewritten')
PY

echo "solve: step 02 complete"
