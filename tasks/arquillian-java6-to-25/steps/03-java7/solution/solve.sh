#!/usr/bin/env bash
# Golden solution, step 03 -- 03-java7.
#
# Three source transforms over the tree step 02 left behind, and one knob in
# the root build script. Nothing here compiles anything: the transforms are
# text in and text out, and the build that proves them is the one the step is
# graded by.
#
#   1. the diamond            93 sites in 47 files
#   2. try-with-resources     11 manual closes -> 9 resource clauses
#      and java.nio.file      13 java.io.File sites -> 11 moved, 2 kept
#   3. source/target 1.7      class-file major 50 -> 51
#
# THE ORDER IS LOAD-BEARING, and only between 1 and 2. The 21 lines the diamond
# pass must not touch are named by line number in the tree as it arrives, and
# diamond.py computes every line number against the text it read rather than
# against its own output, so those numbers stay true for as long as nothing
# else has edited the files. java7.py inserts and deletes whole lines. Run it
# first and the skip list names different code -- code that would then be
# converted, and would then not compile.
#
# WHY 93 AND NOT 116. The tree has 143 explicit `new X<A,B>(...)` sites, 27 of
# them on anonymous class bodies. `<>` on an anonymous class body is a Java 9
# construct, so those 27 stay; that leaves 116 the diamond could reach. Of
# those, 23 do not compile as `<>` at -source 1.7, because Java 7 infers a
# constructor's type arguments from the assignment target alone: a
# `new HashMap<>()` handed straight to a parameter of type Map<String,String>
# infers HashMap<Object,Object>. The 23 sit on 21 lines -- two lines hold two
# sites each -- and those 21 lines are the skip list below. They are not a
# shortfall. They are what -source 1.7 permits, and the compiler names every
# one of them by file and line.
#
# WHY TWO java.io.File SITES STAY. Migrating them changes what the code does,
# and the recorded transcripts say so. `Files.newOutputStream` on a directory
# throws java.nio.file.FileSystemException where `new FileOutputStream` throws
# java.io.FileNotFoundException with the message "<path> (Is a directory)", and
# `Files.createTempFile` accepts a prefix that `File.createTempFile` rejects --
# under three characters, or containing a separator. Four recorded fixtures
# pin all of that. So ArchiveDeploymentToolingExporter keeps its
# `new FileOutputStream` and wraps it in a resource clause instead, and
# OSGiDeploymentPackager becomes `File.createTempFile(...).toPath()`: the
# variable moves to Path, the validation stays where it was.
#
# The three Python programs below are embedded verbatim from
# authoring/solution-src/*.py by authoring/mk-solve03.py, which reads them back
# out of this file afterwards and diffs them against the sources. Edit the
# sources and re-run the generator; do not edit the blocks here.
#
# Run inside the agent container, from anywhere.
set -euo pipefail

TREE=${TREE:-/app/arquillian}
cd "${TREE}"

# ==========================================================================
# 1. The diamond.
# ==========================================================================

# The 21 lines carrying the 23 sites that -source 1.7 cannot infer. Produced
# by a revert loop -- convert all 116, compile, revert every converted site
# within three lines of an error, repeat -- which reached zero errors in one
# iteration. The list is authoring/evidence/step03-diamond-reverted.txt.
SKIP=(
   impl-base/src/main/java/org/jboss/arquillian/impl/core/EventContextImpl.java:62
   impl-base/src/main/java/org/jboss/arquillian/impl/core/EventContextImpl.java:63
   impl-base/src/main/java/org/jboss/arquillian/impl/domain/ProtocolDefinition.java:43
   impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java:107
   impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java:108
   impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java:62
   impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java:76
   impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java:77
   impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java:78
   impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java:92
   impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java:93
   impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java:94
   protocols/jmx-osgi/src/test/java/org/jboss/arquillian/packager/osgi/OSGiDeploymentPackagerTestCase.java:57
   protocols/jmx-osgi/src/test/java/org/jboss/arquillian/packager/osgi/OSGiDeploymentPackagerTestCase.java:75
   protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/v_2_5/ServletProtocolDeploymentPackager.java:131
   protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/v_3/ServletProtocolDeploymentPackager.java:118
   protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/v_3/ServletProtocolDeploymentPackager.java:131
   protocols/servlet/src/test/java/org/jboss/arquillian/protocol/servlet/v_2_5/ServletProtocolDeploymentPackagerTestCase.java:184
   protocols/servlet/src/test/java/org/jboss/arquillian/protocol/servlet/v_3/ServletProtocolDeploymentPackagerTestCase.java:164
   testenrichers/cdi/src/test/java/org/jboss/arquillian/testenricher/cdi/BeansXMLProtocolProcessorTestCase.java:82
   testenrichers/cdi/src/test/java/org/jboss/arquillian/testenricher/cdi/BeansXMLProtocolProcessorTestCase.java:93
)
if [ "${#SKIP[@]}" != 21 ]; then
   echo "solve03: skip list is ${#SKIP[@]} line(s), expected 21" >&2
   exit 1
fi

# Captured rather than streamed, because the two numbers it reports are the
# assertion: a transform that silently converted 4 sites instead of 93 would
# otherwise finish successfully and leave the failure for the verifier to
# discover.
conv=$(python3 - "${TREE}" --revert "${SKIP[@]}" <<'PY_DIAMOND'
#!/usr/bin/env python3
"""Replace explicit constructor type arguments with the diamond.

   diamond.py <tree> [--revert <file:line>...]

Text in, text out, but not a regular expression: the type-argument list is
found by balancing `<` and `>` from the `<` that follows the constructed name,
and the argument list by balancing `(` and `)` from the `(` that follows it.
Regular expressions cannot balance, and the constructs that matter here are
nested -- `new HashMap<String, List<Foo>>()` -- so a regex either stops at the
first `>` or runs to the last one in the file.

Two sites are skipped and the skip is the whole point:

  * an anonymous class body (`new Foo<Bar>() { ... }`). `<>` on one of those is
    a Java **9** construct; writing it at source 7 does not compile.
  * `<` that is not a type-argument list at all -- a comparison. Distinguished
    by position: this scanner only ever looks at the character immediately
    after a `new <QualifiedName>`, where a comparison cannot appear.

Lines are never joined or re-wrapped; the replacement is spliced into the
original text so that everything the migration does not change stays
byte-identical.
"""
import io
import os
import re
import sys

NEW = re.compile(r'\bnew\s+((?:[A-Za-z_$][\w$]*\s*\.\s*)*[A-Za-z_$][\w$]*)\s*<')


def match_angle(s, i):
    """Index just past the `>` that closes the `<` at s[i], or None."""
    depth = 0
    while i < len(s):
        c = s[i]
        if c == '<':
            depth += 1
        elif c == '>':
            depth -= 1
            if depth == 0:
                return i + 1
        elif c in ';{}()':
            # A type-argument list contains none of these. Hitting one means
            # the `<` was something else and the scan must give up rather than
            # run to the end of the file looking for a `>`.
            return None
        i += 1
    return None


def match_paren(s, i):
    """Index just past the `)` that closes the `(` at s[i], or None."""
    depth = 0
    in_str = in_chr = False
    while i < len(s):
        c = s[i]
        if in_str:
            if c == '\\':
                i += 2
                continue
            if c == '"':
                in_str = False
        elif in_chr:
            if c == '\\':
                i += 2
                continue
            if c == "'":
                in_chr = False
        elif c == '"':
            in_str = True
        elif c == "'":
            in_chr = True
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return None


def convert(text, path, skip):
    """Return (new_text, converted_lines, skipped_anon)."""
    out = []
    pos = 0
    done = []
    anon = 0
    for m in NEW.finditer(text):
        if m.start() < pos:
            continue
        lt = m.end() - 1
        gt = match_angle(text, lt)
        if gt is None:
            continue
        # Already a diamond: nothing between the brackets.
        if text[lt + 1:gt - 1].strip() == '':
            continue
        j = gt
        while j < len(text) and text[j] in ' \t\r\n':
            j += 1
        if j >= len(text) or text[j] != '(':
            continue
        close = match_paren(text, j)
        if close is None:
            continue
        k = close
        while k < len(text) and text[k] in ' \t\r\n':
            k += 1
        if k < len(text) and text[k] == '{':
            anom = True
        else:
            anom = False
        if anom:
            anon += 1
            continue
        line = text.count('\n', 0, m.start()) + 1
        if '%s:%d' % (path, line) in skip:
            continue
        out.append(text[pos:lt + 1])
        out.append('>')
        pos = gt
        done.append(line)
    out.append(text[pos:])
    return ''.join(out), done, anon


def main():
    args = sys.argv[1:]
    tree = args[0]
    skip = set()
    if '--revert' in args:
        skip = set(a for a in args[args.index('--revert') + 1:] if a)

    n_files = n_sites = n_anon = 0
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if d not in ('build', '.git', '.gradle')]
        if os.sep + 'src' + os.sep not in root + os.sep:
            continue
        for f in sorted(files):
            if not f.endswith('.java'):
                continue
            full = os.path.join(root, f)
            rel = os.path.relpath(full, tree)
            old = io.open(full, encoding='utf-8').read()
            new, done, anon = convert(old, rel, skip)
            n_anon += anon
            if new != old:
                io.open(full, 'w', encoding='utf-8').write(new)
                n_files += 1
                n_sites += len(done)
                for ln in done:
                    print('converted %s:%d' % (rel, ln))
    sys.stderr.write('diamond.py: %d site(s) in %d file(s); %d anonymous skipped\n'
                     % (n_sites, n_files, n_anon))


if __name__ == '__main__':
    main()
PY_DIAMOND
)
printf '%s\n' "${conv}"

sites=$(printf '%s\n' "${conv}" | grep -c '^converted ' || true)
files=$(printf '%s\n' "${conv}" | sed -n 's/^converted \(.*\):[0-9]*$/\1/p' \
        | sort -u | wc -l)
if [ "${sites}" != 93 ] || [ "${files}" != 47 ]; then
   echo "solve03: diamond converted ${sites} site(s) in ${files} file(s); wanted 93 in 47" >&2
   exit 1
fi
echo "solve03: diamond converted 93 sites across 47 files"

# ==========================================================================
# 2. try-with-resources and java.nio.file.
# ==========================================================================
#
# 21 hand-written patches, not a rewrite engine. Each is matched on stripped
# lines and required to occur exactly once, so a patch cannot be defeated by an
# indentation difference and then report success; and the count of patches
# applied is checked here for the same reason the diamond's two counts are.
j7=$(python3 - "${TREE}" 2>&1 <<'PY_JAVA7'
#!/usr/bin/env python3
"""Adopt try-with-resources and java.nio.file across the tree.

   java7.py <tree>

Hand-written patches, one per site, and not a rewrite engine: there are 11
manual-close sites in 8 files and 13 java.io.File sites in 8 files, each with
its own control flow, and a generic transform for 24 sites is a larger and less
trustworthy artifact than 24 explicit ones.

Every patch is matched on *stripped* lines and required to occur exactly once.
Matching on stripped lines is not laziness about whitespace -- it means a patch
cannot be silently defeated by an indentation difference and then report
success. Requiring exactly one occurrence is what makes "the edit landed" a
checked fact instead of an assumption: a patch that matches twice, or zero
times, aborts the run.

Two constraints shape the patches rather than the ideal being followed to the
letter:

  * Public and protected signatures do not move. `writeOutToFile(File, String)`
    keeps its `java.io.File` parameter and changes only its body; the migration
    is a source-level one and the published API surface is inherited from the
    previous step.
  * Third-party APIs that still take `java.io.File` are met at the boundary.
    ShrinkWrap's `ZipExporter.exportTo`/`exportZip` and `addAsResource` take a
    `File`, so the code owns a `Path` and calls `.toFile()` where it hands over.
    That is what the migration actually looks like; a tree with no `File` left
    anywhere would have had to stop using ShrinkWrap.
"""
import io
import os
import sys

TREE = None
APPLIED = 0


def block(rel, old, new):
    """Replace the single run of lines matching `old` (compared stripped)."""
    global APPLIED
    path = os.path.join(TREE, rel)
    src = io.open(path, encoding='utf-8').read()
    keep_final_nl = src.endswith('\n')
    lines = src.split('\n')
    old_l = [l.strip() for l in old.strip('\n').split('\n')]
    new_l = new.strip('\n').split('\n')
    hits = []
    for i in range(len(lines) - len(old_l) + 1):
        if [l.strip() for l in lines[i:i + len(old_l)]] == old_l:
            hits.append(i)
    if len(hits) != 1:
        sys.stderr.write('java7.py: %s: pattern matched %d times, wanted 1\n  %s\n'
                         % (rel, len(hits), old_l[0]))
        sys.exit(1)
    i = hits[0]
    out = lines[:i] + new_l + lines[i + len(old_l):]
    io.open(path, 'w', encoding='utf-8').write(
        '\n'.join(out) + ('' if keep_final_nl else ''))
    APPLIED += 1


def imports(rel, after, added):
    """Insert import lines once, in a stable place, skipping any already there."""
    path = os.path.join(TREE, rel)
    lines = io.open(path, encoding='utf-8').read().split('\n')
    have = set(l.strip() for l in lines)
    want = [a for a in added if a not in have]
    if not want:
        return
    idx = [n for n, l in enumerate(lines) if l.strip() == after]
    if len(idx) != 1:
        sys.stderr.write('java7.py: %s: anchor import %r matched %d times\n'
                         % (rel, after, len(idx)))
        sys.exit(1)
    n = idx[0]
    io.open(path, 'w', encoding='utf-8').write(
        '\n'.join(lines[:n + 1] + want + lines[n + 1:]))


def main():
    global TREE
    TREE = sys.argv[1]

    # ---------------------------------------------------------------- TWR ----

    # impl-base DynamicServiceLoader: the stream from url.openStream() is opened
    # outside the try, so a BufferedReader constructor that throws leaks it.
    # Both resources move into the header, which closes them in reverse order.
    f = 'impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java'
    block(f, '''
final InputStream is = url.openStream();
BufferedReader reader = null;

try
{
reader = new BufferedReader(new InputStreamReader(is, "UTF-8"));
''', '''
            try (InputStream is = url.openStream();
                 BufferedReader reader = new BufferedReader(new InputStreamReader(is, "UTF-8")))
            {
''')
    block(f, '''
finally
{
if (reader != null)
{
reader.close();
}
}
''', '')

    # impl-base ContainerRegistryCreator: the reader was closed by the private
    # helper it was passed to. Ownership moves to the caller that opens it,
    # which is what lets the helper stop having a finally at all.
    f = 'impl-base/src/main/java/org/jboss/arquillian/impl/client/container/ContainerRegistryCreator.java'
    block(f, '''
return readActivatedValue(
new BufferedReader(
new InputStreamReader(
Thread.currentThread().getContextClassLoader().getResourceAsStream(ARQUILLIAN_LAUNCH_DEFAULT))));
''', '''
         try (BufferedReader reader = new BufferedReader(
               new InputStreamReader(
                     Thread.currentThread().getContextClassLoader().getResourceAsStream(ARQUILLIAN_LAUNCH_DEFAULT))))
         {
            return readActivatedValue(reader);
         }
''')
    block(f, '''
String value;
try
{
while( (value = reader.readLine()) != null)
{
if(value.startsWith("#"))
{
continue;
}
return value;
}
}
finally
{
reader.close();
}
return null;
''', '''
      String value;
      while( (value = reader.readLine()) != null)
      {
         if(value.startsWith("#"))
         {
            continue;
         }
         return value;
      }
      return null;
''')

    # impl-base ArchiveDeploymentToolingExporter: try-with-resources only, and
    # the stream stays a FileOutputStream. Measured, not preferred:
    # `Files.newOutputStream` on a directory throws
    # `java.nio.file.FileSystemException`, where `new FileOutputStream` throws
    # `java.io.FileNotFoundException("<path> (Is a directory)")`, and the
    # recorded behaviour of `tool/write-target-is-directory` and
    # `tool/write-missing-parent` names that type and that message. The
    # migration does not get to change what the code does. The parameter stays a
    # File for the separate reason that it is protected on a public class and so
    # part of the inherited API surface.
    f = 'impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/tool/ArchiveDeploymentToolingExporter.java'
    block(f, '''
FileOutputStream output = null;
try
{
output = new FileOutputStream(target);
output.write(content.getBytes());
output.close();
}
catch (Exception e)
{
throw new RuntimeException("Could not write content to file", e);
}
finally
{
if(output != null)
{
try {output.close(); }
catch (Exception e)
{
throw new RuntimeException("Could not close output stream", e);
}
}
}
''', '''
      try (OutputStream output = new FileOutputStream(target))
      {
         output.write(content.getBytes());
      }
      catch (Exception e)
      {
         throw new RuntimeException("Could not write content to file", e);
      }
''')
    imports(f, 'import java.io.File;', ['import java.io.OutputStream;'])

    # protocols/jmx JMXTestRunner: close() was sequential, so an exception from
    # writeObject skipped it.
    f = 'protocols/jmx/src/main/java/org/jboss/arquillian/protocol/jmx/JMXTestRunner.java'
    block(f, '''
ByteArrayOutputStream baos = new ByteArrayOutputStream();
ObjectOutputStream oos = new ObjectOutputStream(baos);
oos.writeObject(result);
oos.close();
''', '''
         ByteArrayOutputStream baos = new ByteArrayOutputStream();
         try (ObjectOutputStream oos = new ObjectOutputStream(baos))
         {
            oos.writeObject(result);
         }
''')

    # protocols/servlet ServletMethodExecutor
    f = 'protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/ServletMethodExecutor.java'
    block(f, '''
ObjectInputStream ois = new ObjectInputStream(httpConnection.getInputStream());
Object o;
try
{
o = ois.readObject();
}
finally
{
ois.close();
}
''', '''
               Object o;
               try (ObjectInputStream ois = new ObjectInputStream(httpConnection.getInputStream()))
               {
                  o = ois.readObject();
               }
''')

    # protocols/servlet ServletTestRunner: also sequential.
    f = 'protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/runner/ServletTestRunner.java'
    block(f, '''
ObjectOutputStream oos = new ObjectOutputStream(response.getOutputStream());
oos.writeObject(object);
response.setStatus(HttpServletResponse.SC_OK);
oos.flush();
oos.close();
''', '''
         try (ObjectOutputStream oos = new ObjectOutputStream(response.getOutputStream()))
         {
            oos.writeObject(object);
            response.setStatus(HttpServletResponse.SC_OK);
            oos.flush();
         }
''')

    # protocols/servlet TestUtil (test code): the hand-rolled quiet close()
    # helper is what try-with-resources replaces, so the helper goes with it.
    f = 'protocols/servlet/src/test/java/org/jboss/arquillian/protocol/servlet/TestUtil.java'
    block(f, '''
ObjectInputStream input = null;
try
{
input = new ObjectInputStream(url.openStream());
return input.readObject();
}
catch (Exception e)
{
throw new RuntimeException("Could not fetch url " + url);
}
finally
{
close(input);
}
''', '''
      try (ObjectInputStream input = new ObjectInputStream(url.openStream()))
      {
         return input.readObject();
      }
      catch (Exception e)
      {
         throw new RuntimeException("Could not fetch url " + url);
      }
''')
    block(f, '''
private static void close(InputStream input)
{
if(input == null)
{
return;
}
try
{
input.close();
}
catch (Exception e)
{
// ignore
}
}
''', '')
    block(f, '''
public static String convertToString(InputStream in) throws Exception
{
ByteArrayOutputStream out = new ByteArrayOutputStream();
int b;
while ((b = in.read()) != -1)
{
out.write(b);
}
out.close();
in.close();
return new String(out.toByteArray(), "UTF-8");
}
''', '''
   public static String convertToString(InputStream in) throws Exception
   {
      try (InputStream input = in;
           ByteArrayOutputStream out = new ByteArrayOutputStream())
      {
         int b;
         while ((b = input.read()) != -1)
         {
            out.write(b);
         }
         return new String(out.toByteArray(), "UTF-8");
      }
   }
''')

    # spi ServiceLoader: same shape as impl-base's, without even a finally --
    # reader.close() is the last statement of the try and an exception skips it.
    f = 'spi/src/main/java/org/jboss/arquillian/spi/util/ServiceLoader.java'
    block(f, '''
final URL url = enumeration.nextElement();
final InputStream is = url.openStream();
final BufferedReader reader = new BufferedReader(new InputStreamReader(is, "UTF-8"));

String line = reader.readLine();
''', '''
               final URL url = enumeration.nextElement();
               try (InputStream is = url.openStream();
                    BufferedReader reader = new BufferedReader(new InputStreamReader(is, "UTF-8")))
               {
               String line = reader.readLine();
''')
    block(f, '''
line = reader.readLine();
}
reader.close();
}
catch (Exception e)
{
// try the next file
}
''', '''
                  line = reader.readLine();
               }
               }
            }
            catch (Exception e)
            {
               // try the next file
            }
''')

    # ------------------------------------------------------------ nio.file ---

    # impl-base ArchiveDeploymentExporter. mkdirs() returns false on failure and
    # createDirectories() throws, so the branch is rebuilt rather than
    # transliterated: the same two log messages on the same two conditions.
    f = 'impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/ArchiveDeploymentExporter.java'
    block(f, '''
File exportDir = new File(exportPath);
if (exportDir.isFile())
{
log.warning("Deployment export disabled. Export path points to an existing file: " + exportPath);
return;
}
else if (!exportDir.isDirectory() && !exportDir.mkdirs())
{
log.warning("Deployment export directory could not be created: " + exportPath);
return;
}
''', '''
         Path exportDir = Paths.get(exportPath);
         if (Files.isRegularFile(exportDir))
         {
            log.warning("Deployment export disabled. Export path points to an existing file: " + exportPath);
            return;
         }
         else if (!Files.isDirectory(exportDir))
         {
            try
            {
               Files.createDirectories(exportDir);
            }
            catch (IOException e)
            {
               log.warning("Deployment export directory could not be created: " + exportPath);
               return;
            }
         }
''')
    block(f, '''
deployment.as(ZipExporter.class).exportTo(
new File(exportDir, createFileName(event.getDeployment(), deployment)),
true);
''', '''
         deployment.as(ZipExporter.class).exportTo(
               exportDir.resolve(createFileName(event.getDeployment(), deployment)).toFile(),
               true);
''')
    imports(f, 'import java.io.File;',
            ['import java.io.IOException;', 'import java.nio.file.Files;',
             'import java.nio.file.Path;', 'import java.nio.file.Paths;'])

    # spi Validate
    f = 'spi/src/main/java/org/jboss/arquillian/spi/util/Validate.java'
    block(f, '''
if (string == null || string.length() == 0 || new File(string).isDirectory() == false)
''', '''
      if (string == null || string.length() == 0 || Files.isDirectory(Paths.get(string)) == false)
''')
    imports(f, 'import java.io.File;',
            ['import java.nio.file.Files;', 'import java.nio.file.Paths;'])

    # protocols/jmx-osgi OSGiDeploymentPackager. The variable becomes a Path and
    # the deletion goes through Files, but the temp file is still created by
    # `File.createTempFile`: that method rejects a prefix shorter than three
    # characters and one containing a separator, `Files.createTempFile` does
    # not, and `pack/name-too-short` and `pack/name-with-separator` record an
    # IllegalArgumentException that would simply stop happening. exportZip takes
    # a File, so that boundary conversion stays too.
    f = 'protocols/jmx-osgi/src/main/java/org/jboss/arquillian/packager/osgi/OSGiDeploymentPackager.java'
    block(f, '''
File target = File.createTempFile(archiveName + "-", ".jar");
try
{
ZipExporter exporter = archive.as(ZipExporter.class);
exporter.exportZip(target, true);
VirtualFile virtualFile = AbstractVFS.getRoot(target.toURI().toURL());
BundleInfo.createBundleInfo(virtualFile);
}
finally
{
target.delete();
}
''', '''
      Path target = File.createTempFile(archiveName + "-", ".jar").toPath();
      try
      {
         ZipExporter exporter = archive.as(ZipExporter.class);
         exporter.exportZip(target.toFile(), true);
         VirtualFile virtualFile = AbstractVFS.getRoot(target.toUri().toURL());
         BundleInfo.createBundleInfo(virtualFile);
      }
      finally
      {
         Files.deleteIfExists(target);
      }
''')
    imports(f, 'import java.io.File;',
            ['import java.nio.file.Files;', 'import java.nio.file.Path;'])

    # --- test code ---
    f = 'impl-base/src/test/java/org/jboss/arquillian/impl/client/deployment/ArchiveDeploymentExporterTestCase.java'
    block(f, '''
File file = new File(EXPORT_PATH + TARGET_NAME + "_" + DEPLOYMENT_NAME + "_" + ARCHIVE_NAME);

Assertions.assertEquals(bol, file.exists(), "File exists");

file.delete();
''', '''
      Path file = Paths.get(EXPORT_PATH + TARGET_NAME + "_" + DEPLOYMENT_NAME + "_" + ARCHIVE_NAME);

      Assertions.assertEquals(bol, Files.exists(file), "File exists");

      try
      {
         Files.deleteIfExists(file);
      }
      catch (IOException e)
      {
         throw new RuntimeException("Could not delete " + file, e);
      }
''')
    imports(f, 'import java.io.File;',
            ['import java.io.IOException;', 'import java.nio.file.Files;',
             'import java.nio.file.Path;', 'import java.nio.file.Paths;'])

    f = 'impl-base/src/test/java/org/jboss/arquillian/impl/client/deployment/tool/ArchiveDeploymentToolingExporterTestCase.java'
    block(f, '''
Assertions.assertTrue(new File(EXPORT_FOLDER + getClass().getName() + ".xml").exists());
''', '''
      Assertions.assertTrue(Files.exists(Paths.get(EXPORT_FOLDER + getClass().getName() + ".xml")));
''')
    imports(f, 'import java.io.File;',
            ['import java.nio.file.Files;', 'import java.nio.file.Paths;'])

    f = 'impl-base/src/test/java/org/jboss/arquillian/impl/client/deployment/tool/ToolingDeploymentFormatterTestCase.java'
    block(f, '''
.addAsResource(new File("src/test/resources/tooling/arquillian.xml"), ArchivePaths.create("resource.xml"))
.addAsResource("tooling/arquillian.xml", ArchivePaths.create("resource2.xml"))
.addAsResource(new File("src/test/resources/tooling/arquillian.xml").toURI().toURL(),
ArchivePaths.create("resource3.xml")).addClass(ToolingDeploymentFormatterTestCase.class)
''', '''
            .addAsResource(ARQUILLIAN_XML.toFile(), ArchivePaths.create("resource.xml"))
            .addAsResource("tooling/arquillian.xml", ArchivePaths.create("resource2.xml"))
            .addAsResource(ARQUILLIAN_XML.toUri().toURL(),
                  ArchivePaths.create("resource3.xml")).addClass(ToolingDeploymentFormatterTestCase.class)
''')
    block(f, '''
@Test
public void shouldBeAbleToExportArchive() throws Exception
{
''', '''
   private static final Path ARQUILLIAN_XML =
         Paths.get("src/test/resources/tooling/arquillian.xml");

   @Test
   public void shouldBeAbleToExportArchive() throws Exception
   {
''')
    imports(f, 'import java.io.File;',
            ['import java.nio.file.Path;', 'import java.nio.file.Paths;'])

    sys.stderr.write('java7.py: %d patch(es) applied\n' % APPLIED)


if __name__ == '__main__':
    main()
PY_JAVA7
)
printf '%s\n' "${j7}"
if ! printf '%s\n' "${j7}" | grep -qxF 'java7.py: 21 patch(es) applied'; then
   echo "solve03: java7.py did not apply all 21 patches" >&2
   exit 1
fi

# ==========================================================================
# 3. The compile level.
# ==========================================================================
lv=$(python3 - "${TREE}" 2>&1 <<'PY_LEVEL7'
#!/usr/bin/env python3
"""Raise the compile level from Java 6 to Java 7.

   level7.py <tree>

One knob in the root build script, and the assertion that it was there to turn:
a migration that reports success having edited nothing is the failure this
checks for. The `-Xlint:-options` already on the compiler arguments stays --
JDK 8's javac warns about a bootstrap classpath for 1.7 exactly as it does for
1.6, and the previous step chose to silence it.
"""
import io
import sys

path = sys.argv[1] + '/build.gradle'
src = io.open(path, encoding='utf-8').read()
subs = [
    ('// Java 6 bytecode -- class-file major version 50 -- built by JDK 8.',
     '// Java 7 bytecode -- class-file major version 51 -- built by JDK 8.'),
    ('sourceCompatibility = JavaVersion.VERSION_1_6',
     'sourceCompatibility = JavaVersion.VERSION_1_7'),
    ('targetCompatibility = JavaVersion.VERSION_1_6',
     'targetCompatibility = JavaVersion.VERSION_1_7'),
]
for old, new in subs:
    if src.count(old) != 1:
        sys.stderr.write('level7.py: %r occurs %d times, wanted 1\n' % (old, src.count(old)))
        sys.exit(1)
    src = src.replace(old, new)
io.open(path, 'w', encoding='utf-8').write(src)
sys.stderr.write('level7.py: build.gradle now at 1.7\n')
PY_LEVEL7
)
printf '%s\n' "${lv}"
if ! printf '%s\n' "${lv}" | grep -qxF 'level7.py: build.gradle now at 1.7'; then
   echo "solve03: build.gradle was not raised to 1.7" >&2
   exit 1
fi

echo "solve: step 03 complete"
