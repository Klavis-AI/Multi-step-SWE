#!/usr/bin/env bash
# Golden solution, step 08 -- 08-jpms.
#
# Three transforms over the tree step 07 left behind, and one decision not to
# make a fourth. Nothing here compiles anything; the build that proves the
# edits is the one the step is graded by.
#
#   the names     each of the twelve projects that publishes code claims a
#                 reverse-DNS module name in its own jar manifest, through a
#                 new gradle/modules.gradle applied from the root build
#   impl loader   DynamicServiceLoader#load stops reading META-INF/services
#                 through ClassLoader.getResources and asks
#                 java.util.ServiceLoader instead
#   spi loader    the same for org.jboss.arquillian.spi.util.ServiceLoader
#                 #reload
#
# WHY PHASE ONE AND NOTHING FURTHER. A JPMS migration has an obvious-looking
# end state -- a module-info.java in every project, `requires` and `exports`
# spelled out -- and this tree cannot reach it, for a reason that is a property
# of the dependencies and not of the effort available. A module with a
# module-info may not read an automatic module by name at compile time without
# naming it, and naming it requires the dependency to be on the module path;
# put this tree's dependencies on the module path and seventy-four of the
# eighty jars underneath it are automatic modules whose names are derived from
# their file names. The first project to declare `requires` would be pinning
# itself to a name nobody promised, which is precisely the failure this step
# exists to prevent for its own consumers. So the step claims names and stops,
# which is the same order the JDK's own documentation recommends and the same
# order every library that did this in practice followed.
#
# Split packages would be the other blocker and are not one here: no two of
# the twelve share a package. `opens` is not needed either, because reflection
# into an automatic module is unrestricted.
#
# THE ONE FILE THAT WOULD MAKE THE TWELFTH JAR A MODULE, AND STAYS.
# impl-base carries META-INF/services/org.jboss.arquillian.impl.configuration\
# .api.ArquillianDescriptor, whose three lines are `implClass=`,
# `importerClass=` and `defaultName=`. META-INF/services is reserved by the
# platform: every line of a file in it must name a provider class carried by
# the same artifact, and `implClass=org.jboss...ArquillianDescriptorImpl` is
# not a class name. So `jar --describe-module` refuses that jar and so does
# ModuleFinder, and arquillian-impl-base alone among the twelve cannot have a
# module descriptor derived for it.
#
# Deleting it is one line and it is the wrong line. ShrinkWrap Descriptors
# READS that file at run time to find out which class to instantiate, so
# `Descriptors.create(ArquillianDescriptor.class)` and
# `Descriptors.importAs(ArquillianDescriptor.class)` both stop working the
# moment it goes; the file cannot be converted to a `provides` directive,
# because its lines are not provider class names; and moving the table into a
# factory class means changing impl-base's published API, and every consumer
# calling Descriptors on this type, to tidy a manifest. Eleven of twelve
# artifacts become derivable modules, the twelfth claims its name in the
# manifest like the rest, and the residue is documented rather than papered
# over. A step that breaks an API to improve a manifest is doing a different
# job than the one it was given.
#
# spi's ServiceLoader#createInstance is kept on the same argument: reload() no
# longer calls it, but it is a public method on a published, exported class and
# removing it is a breaking change, not a modularisation.
#
# ONE TRAP WORTH NAMING, because the obvious rewrite hits it. reload()'s new
# body is a STREAM and not a for-each. The like-for-like translation of the old
# hand read is `for (Provider<S> p : loader) providers.add(p.get());` -- which
# is exactly the collecting loop step 05 took out of this tree, and steps 05
# and 06's gates are still held at this rung. Finishing this step that way
# would undo a rung below it.
#
# The block below is embedded verbatim from authoring/solution-src/jpms.py by
# authoring/mk-solve08.py, which reads it back out of this file afterwards and
# diffs it against the source. Edit the source and re-run the generator; do not
# edit the block here.
#
# Run inside the agent container, from anywhere.
set -euo pipefail

TREE=${TREE:-/app/arquillian}
cd "${TREE}"

# ==========================================================================
# 1. The transforms.
# ==========================================================================
# Captured rather than streamed, because the census it prints is the
# assertion. jpms.py refuses a tree that does not hold every anchor it was
# computed against -- but a solve.sh that let those lines scroll past unread
# would still be trusting that the program ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it the commonest failure -- run against a tree that is not the one step 07
# produces -- kills the script on the line below with jpms.py's whole
# explanation still sitting unprinted inside ${jp}.
jp=$(python3 - "${TREE}" 2>&1 <<'PY_JPMS'
#!/usr/bin/env python3
"""Step 08's migration, applied to a tree in place.

Phase one of a JPMS migration, and the only phase this tree can reach: every
published artifact claims its module name in the jar manifest, and the two
hand-rolled service lookups that the module system cannot see through are
rewritten onto java.util.ServiceLoader.

Three transforms:

  the names     gradle/modules.gradle maps each of the twelve projects that
                publish code to a reverse-DNS module name and writes it into
                that project's jar manifest as Automatic-Module-Name.
  impl loader   DynamicServiceLoader#load reads META-INF/services by hand
                through ClassLoader.getResources. Rewritten onto
                java.util.ServiceLoader, which is the lookup that knows about
                the module graph.
  spi loader    the same, for org.jboss.arquillian.spi.util.ServiceLoader
                #reload.

AND TWO THINGS THAT LOOK LIKE THEY BELONG HERE AND DELIBERATELY DO NOT MOVE.

impl-base ships a file under META-INF/services/ whose body is not a list of
provider classes. It is ShrinkWrap Descriptors' own lookup table, three
`key=value` lines, parked in a directory the platform reserves:

    implClass=org.jboss.arquillian.impl.configuration.ArquillianDescriptorImpl
    importerClass=org.jboss.shrinkwrap.descriptor.impl.base.XMLImporter
    defaultName=arquillian.xml

It is why arquillian-impl-base alone among the twelve cannot have a module
descriptor derived for it:

    Unable to derive module descriptor for: .../impl-base-1.0.0-SNAPSHOT.jar
    Provider class implClass=org.jboss.arquillian.impl.configuration.\\
      ArquillianDescriptorImpl not in module

THE FILE STAYS ANYWAY, and that is this rung's decision rather than an
oversight. `Descriptors.create(ArquillianDescriptor.class)` and
`Descriptors.importAs(ArquillianDescriptor.class)` READ it at run time to find
out which class to instantiate. It cannot be turned into a `provides`
directive, because its three lines are not provider class names; and it cannot
be deleted without moving every caller onto a new factory -- which is a change
to impl-base's published API, made for the sake of a manifest, and it breaks
every consumer that calls Descriptors on this type. So eleven of the twelve
artifacts become derivable modules and the twelfth is documented as not one.
An Automatic-Module-Name is still claimed for it, because that is the part
this phase can honestly deliver: consumers that put it on the module path get
`org.jboss.arquillian.impl` rather than a name derived from a file name.

spi's `ServiceLoader#createInstance` stays for the same class of reason.
reload() no longer calls it -- java.util.ServiceLoader constructs the
providers itself -- but it is a public method on a published, exported class,
and removing it is a breaking API change rather than a JPMS one. It keeps
working; it simply has one caller fewer.

EVERY EDIT IS ASSERTED. An anchor that is not found is an error and not a
silent no-op: this script is run against more than one tree -- the chained
golden, the blind snapshot, the calibration arms -- and a transform that
quietly matched nothing would produce a tree that builds, passes family A, and
scores zero on the rung it was supposed to demonstrate.

WHAT IS COUNTED AND WHAT IS NOT. The census below prints tree-wide `loadClass`
and `getResources` totals, but only the AFTER value of getResources (zero) and
the DELTA in loadClass (exactly one, the one the hand read used) are asserted.
The absolute totals are not constant across the trees this runs on: the tree a
blind run produces and the tree the shipped solve chain produces differ by a
call site inherited from a lower rung, and an assertion on the absolute number
passes on one of them by luck and crashes on the other.
"""

import os
import re
import sys

tree = sys.argv[1] if len(sys.argv) > 1 else "/app/arquillian"


def read(rel):
    with open(os.path.join(tree, rel), encoding="utf-8") as fh:
        return fh.read()


def write(rel, text):
    path = os.path.join(tree, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def sub1(rel, old, new, count=1):
    """Replace `old` exactly `count` times in `rel`, or fail loudly."""
    text = read(rel)
    n = text.count(old)
    if n != count:
        sys.exit("jpms: %s: expected %d occurrence(s) of %r, found %d"
                 % (rel, count, old[:70], n))
    write(rel, text.replace(old, new))


def replace_body(rel, signature, body):
    """Replace one method's body, leaving its declaration line untouched.

    Brace-balanced from the `{` that follows `signature`, rather than anchored
    on a text pattern for the closing brace: both bodies being replaced here
    hold nested try/catch blocks whose braces are indented exactly like the
    method's own, so a `\\n   }` anchor would stop at the first of them.

    Balancing counts braces in string and character literals too, which is
    wrong in general and safe here -- the two bodies contain
    "META-INF/services", "/", "UTF-8" and '#' and no literal with a brace in
    it. The check is the diff against the reference tree, not this comment.

    Leaving the declaration alone matters for a duller reason: both lines end
    in trailing whitespace in the original file, and a transform that rewrote
    them would have to reproduce that byte for byte to leave the rest of the
    file unchanged.
    """
    text = read(rel)
    n = text.count(signature)
    if n != 1:
        sys.exit("jpms: %s: expected 1 occurrence of %r, found %d"
                 % (rel, signature, n))
    at = text.index(signature)
    i = text.index("{", at)
    depth, j = 0, i
    while True:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    write(rel, text[:i + 1] + body + text[j:])


def count(pattern):
    """Occurrences of `pattern` across the tree's Java sources."""
    rx = re.compile(pattern)
    n = 0
    for dirpath, dirnames, names in os.walk(tree):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build")]
        for nm in names:
            if nm.endswith(".java"):
                with open(os.path.join(dirpath, nm), encoding="utf-8",
                          errors="replace") as fh:
                    n += len(rx.findall(fh.read()))
    return n


SQUATTER = ("impl-base/src/main/resources/META-INF/services/"
            "org.jboss.arquillian.impl.configuration.api.ArquillianDescriptor")

# Asserted on arrival as well as on departure. An earlier draft of this step
# DID evict this file, and the eviction is exactly the kind of edit that looks
# like tidying and is an API break; the guard is here so that a future edit
# that reintroduces it fails in this script rather than in a built tree.
if not os.path.isfile(os.path.join(tree, SQUATTER)):
    sys.exit("jpms: %s is not on the input tree" % SQUATTER)

before_getresources = count(r"\.getResources\(")
before_loadclass = count(r"\.loadClass\(")

# ---------------------------------------------------------------------------
# 1. The names.
# ---------------------------------------------------------------------------
# Reverse-DNS, and matching the root package of the code each artifact ships
# rather than the artifact id. Ten of the twelve are the same either way. Two
# are not, and both are in here because the source tree was read and not the
# coordinates:
#
#   arquillian-impl-base          ships org.jboss.arquillian.impl
#                                 (not ...impl.base -- there is no such package)
#   arquillian-protocol-jmx-osgi  ships org.jboss.arquillian.packager.osgi
#                                 (not ...protocol.jmx.osgi)
#
# Claiming the names is the point of the exercise. Without a manifest entry the
# module system derives a name from the FILE NAME, and the derived names here
# are `api`, `spi`, `junit`, `testng`, `servlet`, `cdi`, `ejb`, `osgi` and
# `resource` -- four of which collide outright with the very libraries this
# project integrates with, and all of which change the moment someone renames a
# jar. A derived name is a name nobody promised.
#
# Keyed on the project NAME, which in this tree is the artifact id each project
# publishes, so the map reads as the list of published artifacts it is.
MODULES_GRADLE = """/*
  vi:ts=2:sw=2:expandtab:

  Module names for the published artifacts.

  Phase one of a JPMS migration: claim a stable module name before anyone can
  depend on the one the module system would otherwise DERIVE from the jar file
  name. A derived name is a property of a file name and changes when the file
  name does; `Automatic-Module-Name` is a promise in the manifest.

  The name is the root package of the code the artifact ships, which is not
  always what the artifact id suggests -- arquillian-impl-base ships
  org.jboss.arquillian.impl, and arquillian-protocol-jmx-osgi ships
  org.jboss.arquillian.packager.osgi.

  Only the twelve projects that publish code are here. arquillian-build,
  arquillian-parent, arquillian-testenricher-parent and
  arquillian-protocol-parent publish no classes and are not modules.

  Keyed by PROJECT PATH and not by artifact id. settings.gradle includes these
  projects under their directory names -- `include 'api'`, not
  `include 'arquillian-api'` -- so project.name is `api`, and the artifact id
  the jar is published under is set separately, from the `libraries` map in the
  root build. The path is the only key that is stable in the build model; the
  artifact id each one publishes under is in the trailing comment.
*/

ext.arquillianModuleNames = [
  ':api'                    : 'org.jboss.arquillian.api',                   // arquillian-api
  ':spi'                    : 'org.jboss.arquillian.spi',                   // arquillian-spi
  ':impl-base'              : 'org.jboss.arquillian.impl',                  // arquillian-impl-base
  ':junit'                  : 'org.jboss.arquillian.junit',                 // arquillian-junit
  ':testng'                 : 'org.jboss.arquillian.testng',                // arquillian-testng
  ':protocols:servlet'      : 'org.jboss.arquillian.protocol.servlet',      // arquillian-protocol-servlet
  ':protocols:jmx'          : 'org.jboss.arquillian.protocol.jmx',          // arquillian-protocol-jmx
  ':protocols:jmx-osgi'     : 'org.jboss.arquillian.packager.osgi',         // arquillian-protocol-jmx-osgi
  ':testenrichers:cdi'      : 'org.jboss.arquillian.testenricher.cdi',      // arquillian-testenricher-cdi
  ':testenrichers:ejb'      : 'org.jboss.arquillian.testenricher.ejb',      // arquillian-testenricher-ejb
  ':testenrichers:osgi'     : 'org.jboss.arquillian.testenricher.osgi',     // arquillian-testenricher-osgi
  ':testenrichers:resource' : 'org.jboss.arquillian.testenricher.resource', // arquillian-testenricher-resource
]

subprojects {
  plugins.withId('java') {
    def moduleName = rootProject.ext.arquillianModuleNames[project.path]
    if (moduleName != null) {
      tasks.named('jar') {
        manifest {
          attributes('Automatic-Module-Name': moduleName)
        }
      }
    }
  }
}

// A key that names no project sets no attribute, and a jar with no
// Automatic-Module-Name is not an error the build would otherwise report --
// it just silently goes back to a derived name. Say so at configuration time.
gradle.projectsEvaluated {
  def unknown = rootProject.ext.arquillianModuleNames.keySet().findAll {
    rootProject.findProject(it) == null
  }
  if (!unknown.isEmpty()) {
    throw new GradleException("modules.gradle names unknown project(s): " + unknown)
  }
}
"""

write("gradle/modules.gradle", MODULES_GRADLE)

root = read("build.gradle")
if "modules.gradle" in root:
    sys.exit("jpms: build.gradle already applies modules.gradle")
write("build.gradle", root.rstrip("\n")
      + "\n\napply from: rootProject.file('gradle/modules.gradle')\n")

# Every key has to be a project settings.gradle actually includes, or the
# lookup silently yields null and the jar keeps its derived name. Check the
# keys against the tree rather than against this file's own literal.
keys = re.findall(r"^  '(:[\w:-]+)'\s*:", MODULES_GRADLE, re.M)
if len(keys) != 12:
    sys.exit("jpms: modules.gradle names %d projects, expected 12" % len(keys))
included = set()
for line in read("settings.gradle").splitlines():
    m = re.match(r"\s*include\s+'([^']+)'", line)
    if m:
        included.add(":" + m.group(1))
absent = [k for k in keys if k not in included]
if absent:
    sys.exit("jpms: modules.gradle keys name no included project: %s" % absent)

# ---------------------------------------------------------------------------
# 2. impl-base's loader.
# ---------------------------------------------------------------------------
# The method hands back CLASSES, not instances, and always has. `ServiceLoader
# .Provider::type` is the platform's way of asking for exactly that -- it names
# the implementation without constructing it -- so the contract is unchanged
# while the lookup becomes one the module graph participates in.
DSL = "impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java"

replace_body(
    DSL,
    "   private <T> Set<Class<? extends T>> load(Class<T> serviceClass, ClassLoader loader)",
    """
      // Provider::type names the implementation without instantiating it,
      // which is what this method has always handed back. Discovery goes
      // through java.util.ServiceLoader so that a provider declared by a
      // module is found as readily as one declared by a file on the class
      // path.
      return java.util.ServiceLoader.load(serviceClass, loader)
            .stream()
            .map(java.util.ServiceLoader.Provider::type)
            .collect(Collectors.toCollection(LinkedHashSet::new));
""" + "   ")

# SERVICES named the directory this class no longer opens.
sub1(DSL, '   private static final String SERVICES = "META-INF/services";\n   \n', "")

# ---------------------------------------------------------------------------
# 3. spi's loader.
# ---------------------------------------------------------------------------
# Same move, one level down. The file format java.util.ServiceLoader parses --
# one binary name per line, `#` comments -- is the format this method parsed
# itself, so no provider file on the tree changes.
#
# SERVICES, serviceFile and createInstance all stay. reload() was serviceFile's
# only real reader, but toString() still reports it and the constructor still
# builds it, and createInstance is public API; see the header.
SPI = "spi/src/main/java/org/jboss/arquillian/spi/util/ServiceLoader.java"

replace_body(
    SPI,
    "   public void reload()",
    """
      // java.util.ServiceLoader rather than a hand read of META-INF/services:
      // it is the lookup that knows about the module graph, so a provider
      // carried by a module is found on the same terms as one carried by a
      // jar on the class path. The file format it parses is the one this
      // method used to parse itself, comments and all.
      //
      // A STREAM AND NOT A FOR-EACH, and that is not a style preference. A
      // for-each whose body is `providers.add(...)` is exactly the collecting
      // loop step 05 took out of this tree, and steps 05 and 06's gates are
      // still held at this rung: writing one back in here would undo a rung
      // below to finish this one. Collectors.toCollection keeps the insertion
      // order the LinkedHashSet was chosen for.
      providers = java.util.ServiceLoader.load(expectedType, loader)
            .stream()
            .map(java.util.ServiceLoader.Provider::get)
            .collect(java.util.stream.Collectors.toCollection(LinkedHashSet::new));
""" + "   ")

# ---------------------------------------------------------------------------
# 4. The imports both loaders no longer need.
# ---------------------------------------------------------------------------
# javac says nothing about an unused import, so this is swept rather than
# noticed: for each import, look for its simple name anywhere in the file
# outside the import block, and drop the ones that are gone.
dropped_by = {}
for rel in (DSL, SPI):
    text = read(rel)
    body_only = re.sub(r"^import .*;$", "", text, flags=re.M)
    dropped = []
    for m in list(re.finditer(r"^import ([\w.]+)\.(\w+);$", text, re.M)):
        simple = m.group(2)
        if not re.search(r"\b" + re.escape(simple) + r"\b", body_only):
            dropped.append(m.group(0))
    for line in dropped:
        text = text.replace(line + "\n", "", 1)
    write(rel, text)
    dropped_by[rel] = len(dropped)

# Named rather than only counted. The count varies with what the input tree
# happened to import; these six are the ones the hand reads needed and nothing
# else on either file does.
for rel in (DSL, SPI):
    text = read(rel)
    for gone in ("java.io.BufferedReader", "java.io.InputStream",
                 "java.io.InputStreamReader", "java.net.URL",
                 "java.util.Enumeration"):
        if ("import %s;" % gone) in text:
            sys.exit("jpms: %s still imports %s" % (rel, gone))

# ---------------------------------------------------------------------------
# 5. The census.
# ---------------------------------------------------------------------------
after_getresources = count(r"\.getResources\(")
after_loadclass = count(r"\.loadClass\(")

for rel, want in ((DSL, "java.util.ServiceLoader.Provider::type"),
                  (SPI, "java.util.ServiceLoader.Provider::get")):
    if want not in read(rel):
        sys.exit("jpms: %s does not hold %s" % (rel, want))

if "public S createInstance(String line)" not in read(SPI):
    sys.exit("jpms: createInstance was removed from %s -- it is public API" % SPI)
if not os.path.isfile(os.path.join(tree, SQUATTER)):
    sys.exit("jpms: %s was removed -- Descriptors reads it at run time" % SQUATTER)

arrivals = count(r"java\.util\.ServiceLoader\.load\(")
if after_getresources != 0:
    sys.exit("jpms: %d getResources site(s) left on the tree, expected 0"
             % after_getresources)
if before_loadclass - after_loadclass != 1:
    sys.exit("jpms: loadClass went %d -> %d, expected a drop of exactly 1"
             % (before_loadclass, after_loadclass))

print("jpms: names: 12 artifacts named in gradle/modules.gradle, applied from build.gradle")
print("jpms: loaders: %d java.util.ServiceLoader.load site(s), %d getResources site(s) left"
      % (arrivals, after_getresources))
print("jpms: loaders: loadClass %d -> %d (the one the hand read used)"
      % (before_loadclass, after_loadclass))
print("jpms: imports: DynamicServiceLoader -%d, ServiceLoader -%d"
      % (dropped_by[DSL], dropped_by[SPI]))
print("jpms: kept: the ArquillianDescriptor service file and spi ServiceLoader#createInstance")
print("JPMS OK")
PY_JPMS
) || true
printf '%s\n' "${jp}"
for want in \
      'jpms: names: 12 artifacts named in gradle/modules.gradle, applied from build.gradle' \
      'jpms: loaders: 2 java.util.ServiceLoader.load site(s), 0 getResources site(s) left' \
      'jpms: kept: the ArquillianDescriptor service file and spi ServiceLoader#createInstance' \
      'JPMS OK'; do
   if ! printf '%s\n' "${jp}" | grep -qxF "${want}"; then
      echo "solve08: jpms.py did not report: ${want}" >&2
      exit 1
   fi
done
# Matched as shapes rather than as numbers, and that is step 07's lesson rather
# than caution. The absolute count of `loadClass` sites, and the number of
# imports each loader stops needing, are properties of the tree this runs on --
# the tree a blind run produces and the tree the shipped solve chain produces
# need not agree on either -- while the DELTA is a property of the migration.
# jpms.py asserts the delta itself and exits non-zero if it is not one; what is
# checked here is that it got as far as reporting it.
for shape in \
      '^jpms: loaders: loadClass [0-9]+ -> [0-9]+ \(the one the hand read used\)$' \
      '^jpms: imports: DynamicServiceLoader -[0-9]+, ServiceLoader -[0-9]+$'; do
   if ! printf '%s\n' "${jp}" | grep -qE "${shape}"; then
      echo "solve08: jpms.py did not report a line matching: ${shape}" >&2
      exit 1
   fi
done

# ==========================================================================
# 2. The twelve names, re-read off the tree.
# ==========================================================================
# Re-read rather than inferred from jpms.py having printed its census, because
# this is what the rung is scored on: a consumer that puts these jars on the
# module path gets the name in the manifest, or a name derived from a file
# name, and nothing in between.
if [ ! -f gradle/modules.gradle ]; then
   echo "solve08: gradle/modules.gradle is not on the tree" >&2
   exit 1
fi
if ! grep -qF "apply from: rootProject.file('gradle/modules.gradle')" build.gradle; then
   echo "solve08: the root build does not apply gradle/modules.gradle" >&2
   exit 1
fi
if ! grep -qF "Automatic-Module-Name" gradle/modules.gradle; then
   echo "solve08: gradle/modules.gradle sets no Automatic-Module-Name" >&2
   exit 1
fi
# Keyed by project PATH: settings.gradle includes these projects under their
# directory names, so project.name is `api` and never `arquillian-api`, and a
# map keyed by artifact id looks right and matches nothing. `|| true` because
# grep -c exits 1 on a count of zero and this runs under set -e, which is how
# that mistake stayed invisible the first time.
n=$( { grep -cE "^ *':[a-z:-]+' *: *'org\." gradle/modules.gradle || true; } )
if [ "${n}" != 12 ]; then
   echo "solve08: gradle/modules.gradle names ${n} project(s), wanted 12" >&2
   exit 1
fi
# Every name a legal module name AND under this project's own reverse-DNS
# root. A module name is a dot-separated sequence of Java identifiers: the
# artifact ids are hyphenated and a name copied from one would be rejected by
# the module system rather than merely being ugly.
bad=$(grep -oE ": *'[^']*'" gradle/modules.gradle \
      | sed -E "s/^: *'//; s/'$//" \
      | grep -vE '^org\.jboss\.arquillian(\.[a-z][a-z0-9]*)+$' || true)
if [ -n "${bad}" ]; then
   echo "solve08: these are not legal module names under org.jboss.arquillian:" >&2
   echo "${bad}" >&2
   exit 1
fi
# The two that are not derivable from the artifact id, named explicitly. These
# are the whole reason the map was written from the source tree rather than
# from the coordinates: impl-base ships org.jboss.arquillian.impl and there is
# no ...impl.base package at all, and protocol-jmx-osgi ships
# org.jboss.arquillian.packager.osgi.
# Asserted as whole lines, path and name and the artifact id in the trailing
# comment together, because the three have to agree and the comment is the only
# place the artifact id now appears.
for triple in ':impl-base|org.jboss.arquillian.impl|arquillian-impl-base' \
              ':protocols:jmx-osgi|org.jboss.arquillian.packager.osgi|arquillian-protocol-jmx-osgi'; do
   path=${triple%%|*}; rest=${triple#*|}; name=${rest%%|*}; id=${rest#*|}
   esc=$(printf '%s' "${name}" | sed 's/\./\\./g')
   if ! grep -qE "^ *'${path}' *: *'${esc}', *// *${id}\$" gradle/modules.gradle; then
      echo "solve08: ${path} is not mapped to ${name} (published as ${id})" >&2
      exit 1
   fi
done
echo "solve08: twelve artifacts claim a module name in the manifest"

# ==========================================================================
# 3. The two service lookups.
# ==========================================================================
# `--exclude-dir=build` throughout: a Gradle build leaves copies of sources and
# resources under each project's build/, and this tree additionally keeps two
# checked-in IDE test templates under build/ide that belong to no source set.
# Counting either would make the answer depend on whether anyone had run the
# build before running this.
left=$(grep -rl --include='*.java' --exclude-dir=build -F '.getResources(' . || true)
if [ -n "${left}" ]; then
   echo "solve08: a hand read of META-INF/services survives in: ${left}" >&2
   exit 1
fi
n=$(grep -rh --include='*.java' --exclude-dir=build -oF 'java.util.ServiceLoader.load(' . | wc -l)
if [ "${n}" != 2 ]; then
   echo "solve08: ${n} java.util.ServiceLoader.load site(s), wanted 2" >&2
   exit 1
fi
# Named, and in the right halves. Provider::type hands back the CLASS, which is
# what impl-base's method has always returned; Provider::get hands back the
# INSTANCE, which is what spi's has. Swapping them compiles on neither.
DSL=impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java
SPI=spi/src/main/java/org/jboss/arquillian/spi/util/ServiceLoader.java
if ! grep -qF 'java.util.ServiceLoader.Provider::type' "${DSL}"; then
   echo "solve08: ${DSL} does not ask for Provider::type" >&2
   exit 1
fi
if ! grep -qF 'java.util.ServiceLoader.Provider::get' "${SPI}"; then
   echo "solve08: ${SPI} does not ask for Provider::get" >&2
   exit 1
fi
# The for-each that would undo step 05. Asserted on both files by hand,
# because "the stream is not a style preference" is the kind of comment a
# later edit reads as advice.
if grep -nE 'for *\(.*:.*\)' "${DSL}" "${SPI}" | grep -qF 'Provider'; then
   echo "solve08: a Provider for-each is back in a loader -- step 05 took that out" >&2
   exit 1
fi
echo "solve08: both service lookups go through java.util.ServiceLoader"

# ==========================================================================
# 4. The two things this step deliberately does not do.
# ==========================================================================
# Asserted, not merely described. Both are one-line edits that look like
# finishing the job and are API breaks, and both were in an earlier draft of
# this step; a guard is cheaper than the argument.
SQUATTER=impl-base/src/main/resources/META-INF/services/org.jboss.arquillian.impl.configuration.api.ArquillianDescriptor
if [ ! -f "${SQUATTER}" ]; then
   echo "solve08: the ArquillianDescriptor service file was removed -- Descriptors reads it at run time" >&2
   exit 1
fi
if ! grep -qF 'public S createInstance(String line)' "${SPI}"; then
   echo "solve08: spi ServiceLoader#createInstance was removed -- it is published API" >&2
   exit 1
fi
mi=$(find . -name 'module-info.java' -not -path '*/build/*' || true)
if [ -n "${mi}" ]; then
   echo "solve08: a module-info.java was written: ${mi}" >&2
   echo "solve08: this rung claims names only; see the header for why a descriptor cannot be declared here" >&2
   exit 1
fi
echo "solve08: the descriptor service file and createInstance are still there, and no module-info was written"

echo "solve: step 08 complete"
