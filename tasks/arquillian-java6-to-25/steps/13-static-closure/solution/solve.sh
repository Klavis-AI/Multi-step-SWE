#!/usr/bin/env bash
# Golden solution, step 13 -- 13-static-closure.
#
# THE LAST RUNG, AND THE ONLY ONE THAT MOVES NO LEVEL. Step 12 left this tree
# compiling at 25 and carrying 44 @SuppressWarnings annotations, which between
# them hold 48 values across 26 files and cut what javac says about the code
# from 36 warnings to 11. This step deletes all 44, fixes what the deletion
# exposes, and writes four of them back by name.
#
#   the sweep        every @SuppressWarnings line on the tree, deleted without
#                    being read -- 44 lines, 26 files, 48 values
#   the repairs      27 edits over 18 files: the warnings the sweep exposes,
#                    fixed in the code rather than re-silenced above it
#   the budget       4 annotations written back, each on the declaration it
#                    guards, each carrying its reason on the same line, and not
#                    one of them on a type
#   the level        unchanged. 25 in, 25 out, and this script checks that
#                    rather than setting it
#
# WHY THE SWEEP IS NOT THE ANSWER ON ITS OWN, which is the whole shape of the
# rung. `sed -i '/@SuppressWarnings/d'` takes the suppression leg outright:
# nothing suppressed is nothing over budget. It takes zero on the warning leg,
# because that leg counts warnings against `fixable` -- the 30 honest warnings
# that are not forced -- and pulling the annotations off is what brings all 36
# back. The two legs are bound by a min(). The only way to hold both is to fix
# the code, and the 27 repairs below are that work.
#
# WHY FOUR GO BACK AND NOT ZERO. Four of the 36 warnings name something this
# tree does not own. `UncheckedThrow.throwsUnchecked` exists to launder a
# checked exception past the compiler and the unchecked cast IS that mechanism.
# The JUnit runner registers itself with the manager before its constructor
# returns because JUnit constructs it and then immediately calls it, and the
# other fix -- making the class final -- rewrites eleven pinned api.txt rows.
# Two TestNG transformers override `IAnnotationTransformer.transform`, which
# 5.14.6 declares with three raw parameters: narrow them and the methods stop
# overriding anything. Those four cover 6 warnings, which is why 36 - 6 = 30.
#
# THE TREE THIS SCRIPT IS HANDED IS NOT THE TREE ITS ANCHORS WERE CUT FROM.
# `task.toml` gives this ladder no per-step `environment/`: one agent image
# serves every step and /app carries forward inside a run, so the oracle arm
# chains golden onto golden while the blind arm chains agent onto agent, and by
# this rung the two lineages differ in 99 .java files. Both were measured
# (NOTES 16.99) and they agree on every figure scored here: 44 suppression
# sites over 26 files carrying 48 values, 7 open raw declarations in 5 files,
# 4 forced ones. So the assertions below are counts and names, never line
# numbers -- the one survivor that moves between the lineages is the JUnit
# runner's annotation, at :67 on one chain and :66 on the other, and nothing
# reads it by line.
#
# The block below is embedded verbatim from authoring/solution-src/lint25.py by
# authoring/mk-solve13.py, which reads it back out of this file afterwards and
# diffs it against the source. Edit the source and re-run the generator; do not
# edit the block here.
#
# Run inside the agent container, from anywhere.
set -euo pipefail

TREE=${TREE:-/app/arquillian}
cd "${TREE}"

# ==========================================================================
# 1. The transform.
# ==========================================================================
# Captured rather than streamed, because the census it prints is the assertion.
# lint25.py refuses a tree whose suppression population is not the one it was
# computed against -- which is also its re-run guard, since a tree it has
# already swept holds 4 and not 44 -- but a solve.sh that let those lines
# scroll past unread would still be trusting that the program ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it the commonest failure -- run twice, or run against a tree that is not one
# of the two step 12 produces -- kills the script on the line below with
# lint25.py's whole explanation still sitting unprinted inside ${l25}.
l25=$(python3 - "${TREE}" 2>&1 <<'PY_LINT25'
#!/usr/bin/env python3
"""Step 13's migration, applied to a tree in place.

THE LAST RUNG, AND THE ONLY ONE THAT MOVES NO LEVEL. The tree arrives at major
69 minor 0 and leaves at major 69 minor 0. What is left when the level is gone
is the thing every level was hiding: compiled at 25 under `-Xlint:all
-Xlint:-deprecation` this tree emits 36 warnings and REPORTS 11, because 44
`@SuppressWarnings` sites across 26 files, carrying 48 values between them, are
silencing the other 25. Closing that gap honestly is the whole of this rung,
and it is closed in three passes:

  the sweep      every `@SuppressWarnings` line on the tree is deleted -- all
                 44, 26 files -- before a single warning is read. A suppression
                 is a claim that a warning was looked at and accepted, and
                 twelve rungs of migration have moved or removed almost every
                 site the claims were written for: 27 of the 48 values are
                 `unused` on scaffolding whose assertions are gone. Nothing
                 here reads what a suppression SAYS before deleting it. The
                 sweep is one regex and the judgement happens on the way back.
  the repairs    the warnings that then have a fix get it AT THE SITE: 27 edits
                 over 21 files. Casts give way to the Class object that was
                 already in scope, raw declarations are parameterised, four
                 methods are extracted so a wildcard reference can capture its
                 own type argument, and one method is renamed out of the
                 serialization namespace.
  the budget     four suppressions go back, by name, each on the line it guards
                 and each carrying its reason on that line. Four is not a round
                 number that was chosen: it is every warning on this tree whose
                 cause is outside the tree or IS the construct.

WHY THE SWEEP IS NOT THE ANSWER ON ITS OWN, and why this file is 27 edits long
rather than one `sed -i '/@SuppressWarnings/d'` and four additions. That sed
scores C-1 = 1.000000 -- it has exactly the permitted budget and no extras --
and leaves a tree that compiles with 30 warnings. C-2's denominator is
`fixable` and not `honest`: 36 minus the 6 the four permitted annotations
forgive, which is 30. So the sed reads frac(30-30, 30) = 0 on the leg that
carries 0.57 of this family, and collects 0.15 of it for the sweep. The 27
edits below are that 0.57.

WHY FOUR METHODS ARE EXTRACTED RATHER THAN SUPPRESSED. Four of the unchecked
casts are not casts in intent at all -- they are a wildcard reference being
told to forget that its type argument exists. `DeployableContainer<?>.setup`
takes that container's OWN configuration type and there is no way to name that
type at the call site. There is a way one frame down: pass the reference to a
method with a type parameter and the parameter CAPTURES it, after which
`getConfigurationClass().cast(..)` is a checked cast to a type that now has a
name. Same shape four times -- `ContainerLifecycleController.setup`,
`ManagerImpl.addContext`, `RemoteTestExecuter.getExecutor` and
`CDIInjectionEnricher.injectNonContextualInstance`.

AND THE TREE THIS PROGRAM IS HANDED IS NOT THE TREE ITS ANCHORS WERE CUT FROM.
`task.toml` gives this ladder no per-step `environment/`: one agent image
serves every step and /app carries forward inside a run, so the oracle arm
chains golden onto golden while the blind arm chains agent onto agent, and by
this rung the two lineages differ in 99 `.java` files. Every anchor below was
checked against BOTH -- 30 of the 31 are spelled identically in the two trees
and are matched literally; the one that is not is matched as a pattern and
says so where it stands. What does NOT differ between the lineages, measured
and not assumed, is everything this rung scores: 44 suppression sites over 26
files carrying 48 values, the same per-file census in both, and the same
eleven raw type declarations in the same eight buckets. NOTES 16.99.
"""
import os
import re
import sys

tree = sys.argv[1] if len(sys.argv) > 1 else "/app/arquillian"

SITES = []


def read(rel):
    with open(os.path.join(tree, rel), encoding="utf-8") as fh:
        return fh.read()


def write(rel, text):
    with open(os.path.join(tree, rel), "w", encoding="utf-8") as fh:
        fh.write(text)


def sub1(rel, old, new, why):
    """Replace `old` exactly once in `rel`, or fail loudly.

    The replacement is checked for absence as well: a re-run over a tree this
    program has already migrated would otherwise report success having done
    nothing, and the chained oracle runs every solution in order. That check is
    skipped where `new` is a piece of `old` -- a deletion whose replacement is
    the newline it leaves behind is present in every file ever written, and the
    count of `old` is the whole guard there.
    """
    text = read(rel)
    n = text.count(old)
    if n != 1:
        sys.exit("lint25: %s: expected 1 occurrence of %r, found %d (%s)"
                 % (rel, old[:70], n, why))
    if new not in old and new in text:
        sys.exit("lint25: %s: the replacement is already present (%s)"
                 % (rel, why))
    write(rel, text.replace(old, new))
    SITES.append((rel, why))


def subrx(rel, pattern, repl, why):
    """sub1 for the one site the two lineages spell differently.

    Same contract -- exactly one match or exit -- and the same re-run guard,
    which here is that the pattern cannot match what the replacement leaves
    behind: `(T)` is gone from it.
    """
    text = read(rel)
    hits = re.findall(pattern, text)
    if len(hits) != 1:
        sys.exit("lint25: %s: expected 1 match of %r, found %d (%s)"
                 % (rel, pattern, len(hits), why))
    write(rel, re.sub(pattern, repl, text))
    SITES.append((rel, why))


def sources(scope=None):
    """Every .java file on the tree, build output excluded."""
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames
                       if d not in (".git", "build", ".gradle")]
        for nm in sorted(names):
            if not nm.endswith(".java"):
                continue
            path = os.path.join(dirpath, nm)
            if scope and (os.sep + "src" + os.sep + scope + os.sep) not in path:
                continue
            yield path


def count(pattern, scope=None):
    rx = re.compile(pattern, re.M)
    n = 0
    for path in sources(scope):
        with open(path, encoding="utf-8", errors="replace") as fh:
            n += len(rx.findall(fh.read()))
    return n


def buildfiles():
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames
                       if d not in (".git", "build", ".gradle")]
        for nm in sorted(names):
            if nm.endswith(".gradle"):
                yield os.path.relpath(os.path.join(dirpath, nm), tree)


# ---------------------------------------------------------------------------
# 1. The sweep.
# ---------------------------------------------------------------------------
# FIRST, AND WITHOUT READING WHAT IT DELETES. Every repair below was worked out
# against the swept tree, so the anchors are post-sweep anchors and the order
# is not a preference. It is also the honest order: a program that decided
# which suppressions to keep by reading them would be keeping whatever the
# previous twelve rungs happened to leave, and what those rungs left is the
# subject of this one.
#
# STRICT IS THE GUARD ON THE SWEEP. The sweep deletes whole LINES, which is
# only safe while every site is an annotation alone on its line -- a
# `@SuppressWarnings("x") public void f()` would take the signature with it and
# the tree would not compile. STRICT matches annotation-plus-optional-comment
# and nothing else, and the two counts have to agree file by file.
SWEEP = re.compile(r"^[ \t]*@SuppressWarnings\b.*\n?", re.M)
STRICT = re.compile(r"^[ \t]*@SuppressWarnings\s*\([^\n]*\)[ \t]*(?://[^\n]*)?\n?",
                    re.M)
VALUES = re.compile(r'"([^"]*)"')

swept = swept_files = swept_values = 0
for path in sources():
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    hits = SWEEP.findall(text)
    if not hits:
        continue
    if len(STRICT.findall(text)) != len(hits):
        sys.exit("lint25: %s: a @SuppressWarnings line carries code as well as "
                 "the annotation; the sweep deletes lines and would take it"
                 % os.path.relpath(path, tree))
    swept += len(hits)
    swept_files += 1
    swept_values += sum(len(VALUES.findall(h)) for h in hits)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(SWEEP.sub("", text))

if (swept, swept_files, swept_values) != (44, 26, 48):
    sys.exit("lint25: swept %d @SuppressWarnings line(s) over %d file(s) "
             "carrying %d value(s), expected 44 over 26 carrying 48"
             % (swept, swept_files, swept_values))

# ---------------------------------------------------------------------------
# 2. The repairs.
# ---------------------------------------------------------------------------
# In path order, which is also the order they were derived in and the order
# they were proved in. Every `old` below is the swept tree's own text, widened
# with context until it occurs exactly once in its file, and `sub1` re-checks
# that at run time rather than trusting the widening.

# The container is reached through a registry that does not know its
# configuration type, so every reference to it is a wildcard -- and `setup`
# takes that container's OWN configuration. Capturing the type is what the
# helper is for, and the import is the price of naming the bound.
LIFECYCLE = "impl-base/src/main/java/org/jboss/arquillian/impl/client/container/ContainerLifecycleController.java"
sub1(LIFECYCLE,
     'import org.jboss.arquillian.impl.domain.ContainerRegistry;\n'
     'import org.jboss.arquillian.spi.client.container.DeployableContainer;\n',
     'import org.jboss.arquillian.impl.domain.ContainerRegistry;\n'
     'import org.jboss.arquillian.spi.client.container.ContainerConfiguration;\n'
     'import org.jboss.arquillian.spi.client.container.DeployableContainer;\n',
     'the bound the helper needs a name for')
sub1(LIFECYCLE,
     '            containerProducer.set(container);  \n'
     '            DeployableContainer deployable = container.getDeployableContainer();\n'
     '            injector.get().inject(deployable);\n',
     '            containerProducer.set(container);  \n'
     '            DeployableContainer<?> deployable = container.getDeployableContainer();\n'
     '            injector.get().inject(deployable);\n',
     'the raw container reference')
sub1(LIFECYCLE,
     '            event.fire(new BeforeSetup(deployable));\n'
     '            deployable.setup(container.createDeployableConfiguration());\n'
     '            event.fire(new AfterSetup(deployable));\n',
     '            event.fire(new BeforeSetup(deployable));\n'
     '            setup(deployable, container.createDeployableConfiguration());\n'
     '            event.fire(new AfterSetup(deployable));\n',
     'the setup call, routed through the capture')
sub1(LIFECYCLE,
     '\n'
     '   @FunctionalInterface\n',
     '\n'
     '   /**\n'
     "    * Captures the container's configuration type so {@link DeployableContainer#setup(ContainerConfiguration)}\n"
     '    * can be called through a wildcard reference without the whole declaration going raw.\n'
     '    */\n'
     '   private static <T extends ContainerConfiguration> void setup(\n'
     '         DeployableContainer<T> deployable, ContainerConfiguration configuration)\n'
     '   {\n'
     '      deployable.setup(deployable.getConfigurationClass().cast(configuration));\n'
     '   }\n'
     '\n'
     '   @FunctionalInterface\n',
     'the helper that captures the configuration type')

# The method is handed the Class it is reading into. Casting by that object
# rather than by `(T)` moves the check off erasure, where it does not happen,
# and onto the Class, where it does.
#
# THE ONE EDIT ON THIS RUNG THAT IS WRITTEN AS A PATTERN, and the reason is the
# fact this whole file is built around: step 12 ran on two different trees and
# left this method in two different shapes. The agent lineage hoisted the
# VarHandle into a local; the golden lineage left the whole lookup on the
# return line. What both spell identically is `return (T)` .. `.get(obj);`, so
# that is what is matched, and the rest of the line is carried across.
FORMATTER = "impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/tool/ToolingDeploymentFormatter.java"
subrx(FORMATTER,
      r"return \(T\)(.*\.get\(obj\));",
      r"return type.cast(\1);",
      'the reflective read, cast by the Class the method was handed')

# `all(Protocol.class)` answers a raw `Collection<Protocol>` because it is
# asked with a raw Class literal, and no edit here can make that call generic.
# What can be done is to stop the rawness spreading: the answer is mapped to
# `Protocol<?>` once on the way in, and `findMatch` is re-typed to match.
REGISTRY = "impl-base/src/main/java/org/jboss/arquillian/impl/client/protocol/ProtocolRegistryCreator.java"
sub1(REGISTRY,
     '   {\n'
     '      Collection<Protocol> protocols = serviceLoader.get().all(Protocol.class); \n'
     '\n',
     '   {\n'
     '      List<Protocol<?>> protocols = serviceLoader.get().all(Protocol.class).stream()\n'
     '         .<Protocol<?>>map(p -> (Protocol<?>) p)\n'
     '         .toList();\n'
     '\n',
     "the raw Collection the loader's answer was held in")
sub1(REGISTRY,
     '   \n'
     '   private Optional<Protocol<?>> findMatch(ProtocolDescription description, Collection<Protocol> protocols)\n'
     '   {\n',
     '   \n'
     '   private Optional<Protocol<?>> findMatch(ProtocolDescription description, Collection<Protocol<?>> protocols)\n'
     '   {\n',
     "findMatch's parameter, to match")

# `getInterfaces()[0]` is a `Class<?>`, and the cast to `Class<Context>` was
# the unchecked one. `asSubclass` checks it; the helper's type parameter then
# ties key to value, so the store is filed at a type the value was checked at.
MANAGER = "impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java"
sub1(MANAGER,
     '      {\n'
     '         store.add((Class<Context>)context.getClass().getInterfaces()[0], context);\n'
     '      }\n'
     '   }\n',
     '      {\n'
     '         addContext(store, context.getClass().getInterfaces()[0].asSubclass(Context.class), context);\n'
     '      }\n'
     '   }\n'
     '\n'
     '   /**\n'
     "    * Captures the context interface's own type, so the instance is checked against the key it is\n"
     '    * filed under rather than cast to it blindly.\n'
     '    */\n'
     '   private static <T extends Context> void addContext(ObjectStore store, Class<T> type, Context context)\n'
     '   {\n'
     '      store.add(type, type.cast(context));\n'
     '   }\n',
     'the context, filed under the interface it implements')

# The TODO was accurate. The cast to raw `Protocol` was there because the
# protocol's configuration type cannot be named at the call site -- it can be
# named one frame down, so that is where the call now happens.
EXECUTER = "impl-base/src/main/java/org/jboss/arquillian/impl/execution/RemoteTestExecuter.java"
sub1(EXECUTER,
     '\n'
     '   // TODO: cast to raw type to get away from generic issue..\n'
     '   public ContainerMethodExecutor getContainerMethodExecutor(ProtocolDefinition protocol,\n',
     '\n'
     '   public ContainerMethodExecutor getContainerMethodExecutor(ProtocolDefinition protocol,\n',
     'the TODO the cast below it was there to excuse')
sub1(EXECUTER,
     '   {\n'
     '      ContainerMethodExecutor executor = ((Protocol)protocol.getProtocol()).getExecutor(protocolConfiguration, protocolMetadata.get());\n'
     '      return executor;\n'
     '   }\n',
     '   {\n'
     '      return getExecutor(protocol.getProtocol(), protocolConfiguration);\n'
     '   }\n'
     '\n'
     '   /**\n'
     "    * Captures the protocol's configuration type, so the executor can be asked for through a\n"
     '    * wildcard reference instead of going raw.\n'
     '    */\n'
     '   private <T extends ProtocolConfiguration> ContainerMethodExecutor getExecutor(\n'
     '         Protocol<T> protocol, ProtocolConfiguration protocolConfiguration)\n'
     '   {\n'
     '      return protocol.getExecutor(\n'
     '            protocol.getProtocolConfigurationClass().cast(protocolConfiguration),\n'
     '            protocolMetadata.get());\n'
     '   }\n',
     'the executor, asked for through a captured protocol type')

# Three mocked containers in three test cases, three raw declarations. Two of
# the three already have a `DummyContainerConfiguration` in scope to name; the
# third never touches the configuration and takes a wildcard.
EVENTTEST = "impl-base/src/test/java/org/jboss/arquillian/impl/client/ContainerEventControllerTestCase.java"
sub1(EVENTTEST,
     '   @Mock\n'
     '   private DeployableContainer deployableContainer1;\n'
     '\n',
     '   @Mock\n'
     '   private DeployableContainer<?> deployableContainer1;\n'
     '\n',
     'the raw mock')

DEPLOYTEST = "impl-base/src/test/java/org/jboss/arquillian/impl/client/container/ContainerDeployControllerTestCase.java"
sub1(DEPLOYTEST,
     '   @Mock\n'
     '   private DeployableContainer deployableContainer1;\n'
     '\n'
     '   @Mock\n'
     '   private DeployableContainer deployableContainer2;\n'
     '\n',
     '   @Mock\n'
     '   private DeployableContainer<DummyContainerConfiguration> deployableContainer1;\n'
     '\n'
     '   @Mock\n'
     '   private DeployableContainer<DummyContainerConfiguration> deployableContainer2;\n'
     '\n',
     'the two raw mocks')

LIFETEST = "impl-base/src/test/java/org/jboss/arquillian/impl/client/container/ContainerLifecycleControllerTestCase.java"
sub1(LIFETEST,
     '   @Mock\n'
     '   private DeployableContainer deployableContainer;\n'
     '\n',
     '   @Mock\n'
     '   private DeployableContainer<DummyContainerConfiguration> deployableContainer;\n'
     '\n',
     'the raw mock')

# `newProxyInstance` is already `<T> T`, inferred from the `Class<T>` it is
# handed. The cast re-erased what inference had just established -- twice, the
# helper having been copied into the protocol's own test.
JMXEXEC = "protocols/jmx/src/main/java/org/jboss/arquillian/protocol/jmx/JMXMethodExecutor.java"
sub1(JMXEXEC,
     '   {\n'
     '      return (T)MBeanServerInvocationHandler.newProxyInstance(mbeanServer, name, interf, false);\n'
     '   }\n',
     '   {\n'
     '      return MBeanServerInvocationHandler.newProxyInstance(mbeanServer, name, interf, false);\n'
     '   }\n',
     "the cast the proxy factory's signature already makes")

JMXTEST = "protocols/jmx/src/test/java/org/jboss/arquillian/jmx/JMXTestRunnerTestCase.java"
sub1(JMXTEST,
     '   {\n'
     '      return (T)MBeanServerInvocationHandler.newProxyInstance(mbeanServer, name, interf, false);\n'
     '   }\n',
     '   {\n'
     '      return MBeanServerInvocationHandler.newProxyInstance(mbeanServer, name, interf, false);\n'
     '   }\n',
     "the same cast, in the protocol's own test")

# `writeObject` in a Serializable class is a name javac's `serial` lint
# reserves: a private method by that name with the wrong signature is exactly
# how a serialization hook that will never be called gets written. This one is
# not a hook and never was, so it is renamed to what it does.
#
# THE RENAME IS THE ONE EDIT ON THIS RUNG THAT COSTS SOMETHING. Two inherited
# gates key their populations by <file>#<method>: B-14.3 falls to 42 of 43 and
# B-18.3 to 49 of 51, and family B lands at 0.998600 where an untouched tree
# reads 1.000000. NOTES 16.103 prices that 0.001400 at 0.000245 of reward, and
# what it buys is one warning out of the 30 that C-2 divides by. It is bought.
SERVLET = "protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/runner/ServletTestRunner.java"
sub1(SERVLET,
     '         {\n'
     '            writeObject(testResult, response);\n'
     '         } \n',
     '         {\n'
     '            writeSerializedResult(testResult, response);\n'
     '         } \n',
     'the first call to the renamed writer')
sub1(SERVLET,
     '         {\n'
     '            writeObject(createFailedResult(e), response);\n'
     '         } \n',
     '         {\n'
     '            writeSerializedResult(createFailedResult(e), response);\n'
     '         } \n',
     'the second call to the renamed writer')
sub1(SERVLET,
     '   \n'
     '   private void writeObject(Object object, HttpServletResponse response) \n'
     '   {\n',
     '   \n'
     '   private void writeSerializedResult(Object object, HttpServletResponse response) \n'
     '   {\n',
     'the writer itself, out of the serialization namespace')

# THIS FILE IS TAB-INDENTED and the two anchors below carry literal tabs.
# `getConstructor` is asked on a `Class<? extends T>`, so the constructor it
# answers builds a T and can say so -- and once it says so, `newInstance`
# needs no cast at all.
PROXY = "spi/src/main/java/org/jboss/arquillian/spi/ExceptionProxy.java"
sub1(PROXY,
     '\t\t\tClass<?>[] signature, Object[] params) {\n'
     '\t\tConstructor<?> constructor = null;\n'
     '\t\t// try the message,cause constructor first\n',
     '\t\t\tClass<?>[] signature, Object[] params) {\n'
     '\t\tConstructor<? extends T> constructor = null;\n'
     '\t\t// try the message,cause constructor first\n',
     'the constructor, narrowed to the type it builds')
sub1(PROXY,
     '\t\t\ttry {\n'
     '\t\t\t\tT result = (T) constructor.newInstance(params);\n'
     '\t\t\t\treturn Optional.of(result);\n',
     '\t\t\ttry {\n'
     '\t\t\t\tT result = constructor.newInstance(params);\n'
     '\t\t\t\treturn Optional.of(result);\n',
     'the cast the narrowed constructor makes unnecessary')

# Two shapes in one file. `getInstanceByType` was generic in its return type
# alone, and `<T> T` with no T among the parameters is an unchecked cast with
# extra steps; every caller here assigns to Object, so the method says Object.
# The injection target is the capture shape again -- built and used at the
# instance's own type instead of created at a capture and cast to Object.
CDI = "testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/CDIInjectionEnricher.java"
sub1(CDI,
     '   \n'
     '   private <T> T getInstanceByType(BeanManager manager, final int position, final Method method)\n'
     '   {\n'
     '      CreationalContext<?> cc = manager.createCreationalContext(null);\n'
     '      return (T)manager.getInjectableReference(\n'
     '            new MethodParameterInjectionPoint<>(method, position, manager), \n',
     '   \n'
     '   private Object getInstanceByType(BeanManager manager, final int position, final Method method)\n'
     '   {\n'
     '      CreationalContext<?> cc = manager.createCreationalContext(null);\n'
     '      return manager.getInjectableReference(\n'
     '            new MethodParameterInjectionPoint<>(method, position, manager), \n',
     'the injectable reference, which is an Object and now says so')
sub1(CDI,
     '   {\n'
     '      CreationalContext<Object> creationalContext =  manager.createCreationalContext(null);\n'
     '      InjectionTarget<Object> injectionTarget = (InjectionTarget<Object>) manager.createInjectionTarget(\n'
     '            manager.createAnnotatedType(instance.getClass()));\n'
     '      injectionTarget.inject(instance, creationalContext);\n'
     '   }\n',
     '   {\n'
     '      injectNonContextualInstance(manager, instance.getClass(), instance);\n'
     '   }\n'
     '\n'
     '   /**\n'
     "    * Captures the instance's own type, so the injection target is built and used at one type\n"
     '    * rather than created at a capture and cast to Object.\n'
     '    */\n'
     '   private static <T> void injectNonContextualInstance(BeanManager manager, Class<T> type, Object instance)\n'
     '   {\n'
     '      CreationalContext<T> creationalContext = manager.createCreationalContext(null);\n'
     '      InjectionTarget<T> injectionTarget = manager.createInjectionTarget(\n'
     '            manager.createAnnotatedType(type));\n'
     '      injectionTarget.inject(type.cast(instance), creationalContext);\n'
     '   }\n',
     "the injection target, built at the instance's own type")

# `Arrays.asList` infers its element type from its argument, and the cast was
# there to steer that inference. Typing the call does the same steering and is
# checked while it does it.
CDITEST = "testenrichers/cdi/src/test/java/org/jboss/arquillian/testenricher/cdi/CDIInjectionEnricherTestCase.java"
sub1(CDITEST,
     '         {\n'
     '            return Arrays.asList((BeanDeploymentArchive)beanArchive);\n'
     '         }\n',
     '         {\n'
     '            return Arrays.<BeanDeploymentArchive>asList(beanArchive);\n'
     '         }\n',
     'the singleton list, typed at the call and not at the element')

# `loadClass` answers `Class<?>`. `asSubclass` is the checked spelling of
# precisely the cast that was written, and it fails where the cast would have
# lied. The same two lines, in two enrichers.
EJB = "testenrichers/ejb/src/main/java/org/jboss/arquillian/testenricher/ejb/EJBInjectionEnricher.java"
sub1(EJB,
     '      {\n'
     '         Class<? extends Annotation> ejbAnnotation = (Class<? extends Annotation>)SecurityActions.getThreadContextClassLoader().loadClass(ANNOTATION_NAME);\n'
     '         \n',
     '      {\n'
     '         Class<? extends Annotation> ejbAnnotation = SecurityActions.getThreadContextClassLoader().loadClass(ANNOTATION_NAME).asSubclass(Annotation.class);\n'
     '         \n',
     'the annotation class, narrowed by asSubclass')

RESOURCE = "testenrichers/resource/src/main/java/org/jboss/arquillian/testenricher/resource/ResourceInjectionEnricher.java"
sub1(RESOURCE,
     '      {\n'
     '         Class<? extends Annotation> resourceAnnotation = (Class<? extends Annotation>)SecurityActions.getThreadContextClassLoader().loadClass(ANNOTATION_NAME);\n'
     '         \n',
     '      {\n'
     '         Class<? extends Annotation> resourceAnnotation = SecurityActions.getThreadContextClassLoader().loadClass(ANNOTATION_NAME).asSubclass(Annotation.class);\n'
     '         \n',
     'the same, in the resource enricher')

# `this-escape`: the constructor hands `this` to a method the class does not
# have the last word on. Nothing extends this test case, so saying that in the
# declaration is the whole of the fix.
RESOURCETEST = "testenrichers/resource/src/test/java/org/jboss/arquillian/testenricher/resource/ResourceInjectionEnricherTestCase.java"
sub1(RESOURCETEST,
     '\n'
     'public class ResourceInjectionEnricherTestCase\n'
     '{\n',
     '\n'
     'public final class ResourceInjectionEnricherTestCase\n'
     '{\n',
     'the test class, closed against the escape')

# A javadoc comment attached to nothing, sitting above the package declaration
# -- `dangling-doc-comments`, the lint JDK 21 added, on a comment this tree has
# carried since it was written. It documented nothing, so it goes.
DATAPROVIDER = "testng/src/main/java/org/jboss/arquillian/testng/TestDataProviderTransformer.java"
sub1(DATAPROVIDER,
     '/**\n'
     ' * \n'
     ' */\n'
     'package org.jboss.arquillian.testng;\n',
     'package org.jboss.arquillian.testng;\n',
     'the empty javadoc that documented nothing')

# 27 edits, and the count is taken from what ran rather than from a
# comment: SITES is appended to by sub1 and subrx themselves.
n_repairs = len(SITES)
if n_repairs != 27:
    sys.exit("lint25: %d repair(s) applied, expected 27" % n_repairs)

# ---------------------------------------------------------------------------
# 3. The four that go back.
# ---------------------------------------------------------------------------
# THE BUDGET, AND ITS WHOLE CONTENTS. Four annotations, each on the line it
# guards, each carrying its reason in a trailing comment on that line, and not
# one of them on a type declaration -- `@SuppressWarnings("all")` on a class is
# the shape this rung exists to refuse. Between them the four cover 6 of the 36
# warnings: one unchecked, one this-escape and four rawtypes. Those 6 are
# `target.w.forced`, and subtracting them is what makes C-2's denominator 30.
#
# baseline/suppress.txt names these four by file and by value and scores
# nothing else. A tree that keeps a fifth loses C-1 outright; so does a tree
# that keeps one of these four somewhere other than where it is written here.
# The rethrow. `throwsUnchecked` exists to launder a checked exception past the
# compiler, and the unchecked cast to T IS that mechanism -- a fix here would
# delete the method and the two callers that depend on it.
RETHROW = "impl-base/src/main/java/org/jboss/arquillian/impl/core/UncheckedThrow.java"
sub1(RETHROW,
     '    */\n'
     '   private static <T extends Throwable> void throwsUnchecked(Throwable toThrow) throws T{\n',
     '    */\n'
     '   @SuppressWarnings("unchecked") // erased by design: the cast is what makes the rethrow work\n'
     '   private static <T extends Throwable> void throwsUnchecked(Throwable toThrow) throws T{\n',
     'the rethrow: the erasure IS the mechanism')

# The runner registers itself with the manager before its own constructor
# returns, because JUnit constructs it and then immediately calls it; there is
# no later. Closing the class is not on offer either -- a JUnit runner is
# extended by name from @RunWith.
RUNNER = "junit/src/main/java/org/jboss/arquillian/junit/Arquillian.java"
sub1(RUNNER,
     '   \n'
     '   public Arquillian(Class<?> klass) throws InitializationError\n',
     '   \n'
     '   @SuppressWarnings("this-escape") // the runner must register itself before construction ends\n'
     '   public Arquillian(Class<?> klass) throws InitializationError\n',
     'the registration, which cannot wait for construction to end')

# TestNG 5.14.6 declares `IAnnotationTransformer.transform(ITestAnnotation,
# Class, Constructor, Method)` with three raw parameters. That signature is
# outside this tree, and an override that parameterised them would not
# override. Two transformers, one annotation each, four of the six forced
# warnings -- and the four raw declarations C-3 counts as forced.
DEPENDSON = "testng/src/main/java/org/jboss/arquillian/testng/RemoveDependsOnTransformer.java"
sub1(DEPENDSON,
     '   @Override\n'
     '   public void transform(ITestAnnotation annotation, Class testClass, Constructor testConstructor, Method testMethod)\n',
     '   @Override\n'
     '   @SuppressWarnings("rawtypes") // TestNG 5.14.6 declares IAnnotationTransformer raw\n'
     '   public void transform(ITestAnnotation annotation, Class testClass, Constructor testConstructor, Method testMethod)\n',
     "TestNG's raw IAnnotationTransformer")

sub1(DATAPROVIDER,
     '    */\n'
     '   public void transform(ITestAnnotation testAnnotation, Class clazz, Constructor constructor, Method method)\n',
     '    */\n'
     '   @SuppressWarnings("rawtypes") // TestNG 5.14.6 declares IAnnotationTransformer raw\n'
     '   public void transform(ITestAnnotation testAnnotation, Class clazz, Constructor constructor, Method method)\n',
     'the same interface, in the other transformer')

# ---------------------------------------------------------------------------
# 4. What the tree has to still be.
# ---------------------------------------------------------------------------
# Steps 08 through 12, still standing. The ninth trap in the long form: a
# step-k requirement that quietly makes a step-(k-1) test unpassable is
# invisible from inside a step-local mean -- and on THIS rung it has a specific
# shape, because the sweep above deleted 44 lines out of 26 files without
# reading them and the repairs rewrote method bodies in 21 more.
#
# Every figure below was measured on both lineages and agreed on both, which
# is the only reason any of them is asserted rather than printed.
suppressions = suppressed_files = suppressed_values = 0
for path in sources():
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    hits = SWEEP.findall(text)
    if hits:
        suppressions += len(hits)
        suppressed_files += 1
        suppressed_values += sum(len(VALUES.findall(h)) for h in hits)
if (suppressions, suppressed_files, suppressed_values) != (4, 4, 4):
    sys.exit("lint25: %d suppression(s) over %d file(s) carrying %d value(s) "
             "survive, expected the budget: 4, 4 and 4"
             % (suppressions, suppressed_files, suppressed_values))

mi = mi_files = 0
for path in sources():
    with open(path, encoding="utf-8") as fh:
        n = len(re.findall(r"^import module ", fh.read(), re.M))
    if n:
        mi += n
        mi_files += 1
if (mi, mi_files) != (188, 171):
    sys.exit("lint25: %d module import(s) over %d file(s), expected step 12's "
             "188 over 171" % (mi, mi_files))

tl = count(r"\bThreadLocal\s*<")
sv = count(r"\bScopedValue\s*[.<]")
if tl != 14 or sv != 3:
    sys.exit("lint25: %d ThreadLocal and %d ScopedValue use(s) on the tree, "
             "expected step 12's 14 and 3" % (tl, sv))
for term in ("AccessController", "doPrivileged", "PrivilegedAction",
             "PrivilegedActionException", "PrivilegedExceptionAction",
             "GetTcclAction"):
    n = count(r"\b" + term + r"\b")
    if n:
        sys.exit("lint25: %d occurrence(s) of %s survive -- step 11 retired "
                 "that API and this rung does not bring it back" % (n, term))
if count(r"\.unreflectVarHandle\(") != 4 or count(r"\.setAccessible\(") != 10:
    sys.exit("lint25: step 11's 4 VarHandle site(s) and 10 held setAccessible "
             "call(s) are no longer what the tree holds")
if count(r"^\s*case\s+[A-Z][\w$.]*(?:\s*<[^>\n]*>)?\s+[a-z][\w$]*\s*->") != 10:
    sys.exit("lint25: the tree no longer holds step 10's 10 pattern case "
             "labels")
if count(r"\.getFirst\(\)") != 7:
    sys.exit("lint25: the tree no longer holds step 10's 7 getFirst() reads")
if count(r'"""') != 2:
    sys.exit("lint25: the report page is no longer one text block -- step 09 "
             "wrote it and this rung does not revisit it")
if count(r"\bsealed\b") != 11:
    sys.exit("lint25: the tree no longer holds step 09's 11 sealed "
             "declarations")
if not [rel for rel in buildfiles()
        if "attributes('Automatic-Module-Name'" in read(rel)]:
    sys.exit("lint25: no build file writes Automatic-Module-Name -- step 08's "
             "twelve module names are published from there")

# THE LEVEL IS A HOLD AND THIS IS WHERE IT IS HELD. Not a named file: the two
# lineages declare it in two different ones -- the root `build.gradle` on the
# golden chain, `gradle/java-module.gradle` on the agent chain -- so every
# declaration on the tree is read and every one of them has to say 25.
LEVEL = re.compile(r"(?:sourceCompatibility|targetCompatibility|release"
                   r"|languageVersion)\s*[=( ]\s*'?\"?"
                   r"(?:JavaVersion\.VERSION_)?(\d+)")
levels = sorted({(rel, m) for rel in buildfiles()
                 for m in LEVEL.findall(read(rel))})
if not levels or {m for _, m in levels} != {"25"}:
    sys.exit("lint25: the build declares Java level(s) %r; this rung moves no "
             "level and every declaration has to still read 25" % (levels,))
coh = [rel for rel in buildfiles()
       if "-XX:+UseCompactObjectHeaders" in read(rel)]
if len(coh) != 1:
    sys.exit("lint25: %d build file(s) set -XX:+UseCompactObjectHeaders, "
             "expected the one step 12 wrote" % len(coh))
# Mentioned in a comment on one lineage and not the other, so the flag is
# looked for where it would be PASSED and not where it is discussed.
for rel in buildfiles():
    for line in read(rel).splitlines():
        bare = line.strip()
        if "--enable-preview" in bare and not bare.startswith(("//", "*", "/*")):
            sys.exit("lint25: %s passes --enable-preview; every feature this "
                     "ladder spends is final at 25" % rel)

print("lint25: sweep: %d @SuppressWarnings line(s) deleted from %d file(s), "
      "%d value(s) between them" % (swept, swept_files, swept_values))
print("lint25: repairs: %d site(s) over %d file(s)"
      % (n_repairs, len({rel for rel, _ in SITES[:n_repairs]})))
print("lint25: budget: 4 suppression(s) re-added by name, covering 6 of the "
      "36 honest warnings -- which is what leaves 30 fixable")
print("lint25: level: unchanged at 25, declared in %s" % levels[0][0])
print("LINT25 OK")
PY_LINT25
) || true
printf '%s\n' "${l25}"
for want in \
      'lint25: sweep: 44 @SuppressWarnings line(s) deleted from 26 file(s), 48 value(s) between them' \
      'lint25: repairs: 27 site(s) over 18 file(s)' \
      'lint25: budget: 4 suppression(s) re-added by name, covering 6 of the 36 honest warnings -- which is what leaves 30 fixable' \
      'LINT25 OK'; do
   if ! printf '%s\n' "${l25}" | grep -qxF "${want}"; then
      echo "solve13: lint25.py did not report: ${want}" >&2
      exit 1
   fi
done
# The one lineage-dependent line, asserted as a shape and not as a name: WHICH
# build file declares the level is a property of the tree this ran against --
# the root build.gradle on the golden chain, gradle/java-module.gradle on the
# agent one -- and that it still reads 25 is not.
if ! printf '%s\n' "${l25}" \
     | grep -qE '^lint25: level: unchanged at 25, declared in [^ ]+\.gradle$'; then
   echo "solve13: lint25.py did not report the level held at 25" >&2
   exit 1
fi

# ==========================================================================
# 2. The budget, re-read off the tree.
# ==========================================================================
# Four questions, and the first one is not "how many": it is whether the four
# that survived are the four that are allowed to. A tree that suppressed four
# of the fixable warnings and fixed four of the forced ones has the same count
# and is the tree this rung exists to refuse.
n=$(grep -rhoF --include='*.java' --exclude-dir=build '@SuppressWarnings' . | wc -l || true)
if [ "${n}" != 4 ]; then
   echo "solve13: ${n} @SuppressWarnings on the tree, wanted the budget of 4" >&2
   exit 1
fi
# By file and by value. Each pair is one of baseline/suppress.txt's allow rows,
# and those rows are scored by name for this reason.
for pair in 'impl-base/src/main/java/org/jboss/arquillian/impl/core/UncheckedThrow.java|unchecked' \
            'junit/src/main/java/org/jboss/arquillian/junit/Arquillian.java|this-escape' \
            'testng/src/main/java/org/jboss/arquillian/testng/RemoveDependsOnTransformer.java|rawtypes' \
            'testng/src/main/java/org/jboss/arquillian/testng/TestDataProviderTransformer.java|rawtypes'; do
   src=${pair%%|*}; val=${pair#*|}
   if ! grep -qF "@SuppressWarnings(\"${val}\")" "${src}"; then
      echo "solve13: ${src} does not suppress ${val} -- that warning is forced" >&2
      echo "solve13: and suppress.txt names this file and this value as one of the four" >&2
      exit 1
   fi
done
# ON A DECLARATION AND NOT ON A TYPE. `@SuppressWarnings("all")` on a class is
# the shape this rung exists to refuse, and the general form of it is an
# annotation whose scope is a whole type rather than the one line it excuses.
if grep -rhA1 --include='*.java' --exclude-dir=build '@SuppressWarnings' . \
     | grep -qE '\b(class|interface|enum|record)\b'; then
   echo "solve13: a surviving @SuppressWarnings sits on a type declaration" >&2
   echo "solve13: its scope is then every warning in the file, which is the" >&2
   echo "solve13: attack the suppression leg is counting" >&2
   exit 1
fi
# AND EACH CARRIES ITS REASON ON ITS OWN LINE. Not decoration: a suppression
# whose justification is not written beside it is indistinguishable from one
# nobody justified, and all four of these are forced by something outside this
# repository that a reader cannot see from the annotation.
r=$(grep -rhoE --include='*.java' --exclude-dir=build '@SuppressWarnings\([^)]*\)[ ]*//' . | wc -l || true)
if [ "${r}" != 4 ]; then
   echo "solve13: ${r} of the 4 surviving suppressions carry a reason comment" >&2
   exit 1
fi
# None of the 27 unused-warning suppressions the seed carried under src/test
# survives. Those are the cheapest to keep and the least defensible: an unused
# local in a test is a test that stopped asserting something.
t=$(grep -rl --include='*.java' --exclude-dir=build '@SuppressWarnings' . | grep -c '/src/test/' || true)
if [ "${t}" != 0 ]; then
   echo "solve13: ${t} test source(s) still suppress a warning" >&2
   exit 1
fi
echo "solve13: 4 suppressions, the 4 suppress.txt allows, each on a declaration with its reason"

# ==========================================================================
# 3. The seven that were fixed rather than suppressed.
# ==========================================================================
# baseline/rawdecl.txt counts 11 raw declarations on the seed: 4 forced, 7
# open over 6 buckets in 5 files. The 4 forced ones are the TestNG parameters
# section 2 just checked are still suppressed AND still raw -- narrowing them
# would be the error. The 7 open ones are two types, and both lineages spell
# them the same way, so both can be asked for by name.
#
# Five of the seven are `DeployableContainer` written bare: one local in
# ContainerLifecycleController#setupContainer and four @Mock fields across
# three test cases. Asked of the code and not of the prose -- the type is named
# in javadoc and in Validate.notNull messages on lines that are not
# declarations, so comment lines and lines holding a string literal are
# dropped before counting.
dc=$(grep -rhnE --include='*.java' --exclude-dir=build '\bDeployableContainer[ ]+[a-z]' . \
       | grep -vE '^[0-9]+:[ ]*(//|\*|/\*)' | grep -vcF '"' || true)
if [ "${dc}" != 0 ]; then
   echo "solve13: ${dc} raw DeployableContainer declaration(s) survive" >&2
   echo "solve13: rawdecl.txt counts those among the 7 open ones, and open means fixable" >&2
   exit 1
fi
# The other two are `Protocol` as a raw type ARGUMENT -- `Collection<Protocol>`
# in ProtocolRegistryCreator#createRegistry and again in #findMatch. The
# container is parameterised and the argument is not, which is why a grep for a
# bare type name does not see them.
pr=$(grep -rhoE --include='*.java' --exclude-dir=build '[<,][ ]*Protocol[ ]*[>,]' . | wc -l || true)
if [ "${pr}" != 0 ]; then
   echo "solve13: ${pr} raw Protocol type argument(s) survive" >&2
   exit 1
fi
# The forced four, still raw. This is the assertion that says the fix above was
# narrowing and not a sweep of every bare type name on the tree: these two
# signatures MUST keep their raw parameters or they stop overriding TestNG's
# interface, and a script that "fixed" them would break the build while making
# rawdecl.txt's open count look the same.
for src in testng/src/main/java/org/jboss/arquillian/testng/RemoveDependsOnTransformer.java \
           testng/src/main/java/org/jboss/arquillian/testng/TestDataProviderTransformer.java; do
   if ! grep -qE 'public void transform\(ITestAnnotation [a-zA-Z]+, Class [a-zA-Z]+, Constructor ' "${src}"; then
      echo "solve13: ${src} no longer declares transform() with raw parameters" >&2
      echo "solve13: TestNG 5.14.6 declares IAnnotationTransformer that way; narrowing" >&2
      echo "solve13: them means the method overrides nothing" >&2
      exit 1
   fi
done
echo "solve13: 7 open raw declarations closed, 4 forced ones kept raw and suppressed"

# ==========================================================================
# 4. The level, which does not move.
# ==========================================================================
# The one rung whose answer here is that nothing changed, so it is asked as a
# sweep and never as a file name: the two lineages declare the level in two
# different files and both are trees this script must leave at 25.
lv=$(grep -rhoE --include='*.gradle' --exclude-dir=build \
        "(sourceCompatibility|targetCompatibility|release|languageVersion)[ ]*[=( ][ ]*'?\"?(JavaVersion\.VERSION_)?[0-9]+" . \
      | grep -oE '[0-9]+$' | sort -u | tr '\n' ' ' || true)
if [ "${lv}" != "25 " ]; then
   echo "solve13: the build declares Java level(s) [${lv}], wanted 25 and nothing else" >&2
   exit 1
fi
# Step 12's compact-headers flag, still written exactly once and still not
# unlocked. ASKED OF THE CODE AND NOT OF THE PROSE, as step 12's own script
# learned: the block that carries the flag EXPLAINS in a comment that the
# unlock prefix was needed through 24 and is wrong on 25.
h=$(grep -rhoF --include='*.gradle' --exclude-dir=build -- '-XX:+UseCompactObjectHeaders' . | wc -l || true)
if [ "${h}" != 1 ]; then
   echo "solve13: ${h} occurrence(s) of -XX:+UseCompactObjectHeaders, wanted step 12's 1" >&2
   exit 1
fi
if grep -rhF --include='*.gradle' --exclude-dir=build -- '-XX:+UnlockExperimentalVMOptions' . \
     | grep -qvE '^[[:space:]]*//'; then
   echo "solve13: the flag was unlocked in code -- JEP 519 made it a product option in 25" >&2
   exit 1
fi
# -Werror and --enable-preview, neither of which belongs here and both of which
# are discussed in comments on one lineage. Same lesson, same shape: the lines
# that mention them are fetched and the comment ones dropped.
if grep -rhE --include='*.gradle' --exclude-dir=build -- '-Werror|--enable-preview' . \
     | grep -qvE '^[[:space:]]*(//|\*|/\*)'; then
   echo "solve13: the build passes -Werror or --enable-preview" >&2
   echo "solve13: the lint flags this step is graded under are the scorer's, not the" >&2
   echo "solve13: build's, and every feature this ladder spends is final at 25" >&2
   exit 1
fi
echo "solve13: level held at 25, compact headers written once and not unlocked"

# ==========================================================================
# 5. Steps 08 through 12, still standing.
# ==========================================================================
# The ninth trap in the long form: a step-k requirement that quietly makes a
# step-(k-1) test unpassable is invisible from inside a step-local mean -- and
# on THIS rung it has a specific shape, because the sweep deleted 44 lines out
# of 26 files without reading them and the repairs rewrote method bodies in 18
# more. Every figure below was measured on both lineages and agreed on both.
d=$(grep -rhE --include='*.java' --exclude-dir=build '^import module ' . | wc -l || true)
f=$(grep -rlE --include='*.java' --exclude-dir=build '^import module ' . | wc -l || true)
if [ "${d}" != 188 ] || [ "${f}" != 171 ]; then
   echo "solve13: ${d} module import(s) over ${f} file(s), wanted step 12's 188 over 171" >&2
   exit 1
fi
tl=$(grep -rhoE --include='*.java' --exclude-dir=build '\bThreadLocal[ ]*<' . | wc -l || true)
sv=$(grep -rhoE --include='*.java' --exclude-dir=build '\bScopedValue[ ]*[.<]' . | wc -l || true)
if [ "${tl}" != 14 ] || [ "${sv}" != 3 ]; then
   echo "solve13: ${tl} ThreadLocal and ${sv} ScopedValue use(s), wanted step 12's 14 and 3" >&2
   exit 1
fi
for term in AccessController doPrivileged PrivilegedAction GetTcclAction; do
   if grep -rqE --include='*.java' --exclude-dir=build "\b${term}\b" .; then
      echo "solve13: ${term} is back -- step 11 retired that API" >&2
      exit 1
   fi
done
u=$(grep -rhoF --include='*.java' --exclude-dir=build '.unreflectVarHandle(' . | wc -l || true)
s=$(grep -rhoF --include='*.java' --exclude-dir=build '.setAccessible(' . | wc -l || true)
if [ "${u}" != 4 ] || [ "${s}" != 10 ]; then
   echo "solve13: ${u} VarHandle site(s) and ${s} setAccessible call(s), wanted 4 and 10" >&2
   exit 1
fi
p=$(grep -rhcE --include='*.java' --exclude-dir=build '^[[:space:]]*case[[:space:]]+[A-Z][A-Za-z0-9_$.]*([[:space:]]*<[^>]*>)?[[:space:]]+[a-z][A-Za-z0-9_$]*[[:space:]]*->' . \
      | awk '{n+=$1} END {print n+0}' || true)
g=$(grep -rhoF --include='*.java' --exclude-dir=build '.getFirst()' . | wc -l || true)
if [ "${p}" != 10 ] || [ "${g}" != 7 ]; then
   echo "solve13: ${p} pattern case label(s) and ${g} getFirst() read(s), wanted 10 and 7" >&2
   exit 1
fi
sl=$(grep -rhoE --include='*.java' --exclude-dir=build '\bsealed\b' . | wc -l || true)
if [ "${sl}" != 11 ]; then
   echo "solve13: ${sl} sealed declaration(s), wanted step 09's 11" >&2
   exit 1
fi
mi=$(find . -name 'module-info.java' -not -path '*/build/*' || true)
if [ -n "${mi}" ]; then
   echo "solve13: a module-info.java was written: ${mi}" >&2
   echo "solve13: step 08 claims names only, and a module IMPORT is not a module DECLARATION" >&2
   exit 1
fi
amn=$(grep -rlF --include='*.gradle' "attributes('Automatic-Module-Name'" . | wc -l || true)
if [ "${amn}" -lt 1 ]; then
   echo "solve13: no build file writes Automatic-Module-Name" >&2
   echo "solve13: step 08's twelve module names are published from there" >&2
   exit 1
fi
echo "solve13: steps 08 through 12 undisturbed"

echo "solve: step 13 complete"
