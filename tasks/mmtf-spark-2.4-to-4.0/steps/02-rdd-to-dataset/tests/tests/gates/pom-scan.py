#!/usr/bin/env python3
"""Turn Maven's own answers about a tree into sorted TSV facts.

Rule 11 says a gate reads artifacts, not text. For the build system the
artifact is the *effective* POM -- the model after inheritance, property
interpolation and profile activation -- plus the resolved dependency graph.
Re-deriving either from pom.xml by hand is a reimplementation of Maven that
will disagree with Maven exactly where it matters.

This file is only the parsing half, so it can be asked about a fixture whose
answer is written down without a Maven invocation in the way (--selftest). The
half that runs Maven is pom-scan.sh.

Fact kinds, one per line, tab separated, sorted:

    project      <g:a:v>  <packaging>
    prop         <name>   <value>
    dep          <g:a>    <version>  <scope>     declared, after interpolation
    depmgmt      <g:a>    <version>              managed only; NOT a dependency
    plugin       <g:a>    <version>              every plugin this build runs
    pluginconfig <g:a>    <key>      <value>     flattened <configuration>
    unpinned     <g:a>    <version>              a plugin the project does not pin
    repo         <where>  <id>  <url>  <snapshots> declared repositories
    compile      <key>    <value>                as maven-compiler-plugin says
    jdklevel     <key>    <value>    <origin>    after Maven's precedence
    compilerarg  <arg>
    resolved     <g:a>    <version>  <scope>     the transitive graph
    scalasuffix  <suffix> <count>                over resolved artifactIds
    error        <what>   <detail>

`depmgmt` is separate from `dep` on purpose. A managed version is not a
dependency, and a gate that counted it as one would report spark-xml present in
a tree that dropped it.

`jdklevel` and `unpinned` are the two derived kinds, and both exist for the
same reason. "What JDK does this build target" has two answers in a POM -- the
`maven.compiler.*` properties and the compiler plugin's own `<configuration>`
-- and the plugin wins. That precedence is Maven's, it is easy to get
backwards, and a rule that had to know it would be a rule with a bug in it. So
the scanner applies it once, here, where the selftest can ask.

`unpinned` is the same shape of question one level up: "is this plugin's
version the project's own, or the super POM's?". Maven's effective POM answers
neither -- it lists `maven-jar-plugin 3.4.1` in `<build><plugins>` whether the
project asked for that version or inherited it from whichever Maven is
installed -- so the only way to tell is to read the project's own pom.xml
beside the effective one and see which coordinates it states a `<version>` for.
That is a join across two documents, the rules DSL counts one fact stream at a
time, and a rule that tried to do the join would be a rule with a bug in it.
So the scanner does it once, here. The raw POM is read for nothing else: every
value reported anywhere in this file still comes from Maven's own answer.

`pluginconfig` is flattened from *every* `<configuration>` under a plugin, the
plugin's own and each `<execution>`'s alike, under the same dotted key. A rule
that only saw the top-level one would be answerable by moving the setting into
an execution block, which changes nothing about what the build does.
"""
import os
import re
import sys
import xml.etree.ElementTree as ET


def strip_ns(elem):
    """Maven writes a default namespace; every path here would need it."""
    for e in elem.iter():
        if isinstance(e.tag, str) and e.tag.startswith('{'):
            e.tag = e.tag.split('}', 1)[1]
    return elem


def text(node, tag, default=''):
    child = node.find(tag)
    return (child.text or '').strip() if child is not None else default


def ga(node, default_group='-'):
    return '%s:%s' % (text(node, 'groupId', default_group),
                      text(node, 'artifactId', '-'))


# Maven's effective POM omits <groupId> for plugins in its own group, so a key
# built straight off the element reads "-:maven-compiler-plugin" and no rule
# matching the real coordinate would ever fire.
PLUGIN_GROUP = 'org.apache.maven.plugins'


# maven-compiler-plugin's story about the JDK is told in up to three places and
# `release` wins over `source`/`target` when present. All of them are reported;
# deciding is the rule file's job, not the scanner's.
COMPILER_KEYS = ('source', 'target', 'release', 'encoding', 'testSource',
                 'testTarget', 'testRelease')

SCALA_SUFFIX = re.compile(r'_(2\.1[0-3]|3)$')


def compiler_facts(plugin, out):
    cfg = plugin.find('configuration')
    if cfg is None:
        return
    for key in COMPILER_KEYS:
        val = text(cfg, key)
        if val:
            out.append(('compile', key, val))
    for holder in ('compilerArgs', 'compilerArguments'):
        node = cfg.find(holder)
        if node is None:
            continue
        for arg in node:
            val = (arg.text or '').strip()
            if val:
                out.append(('compilerarg', val))
    single = text(cfg, 'compilerArgument')
    if single:
        out.append(('compilerarg', single))


def config_facts(plugin, key, out):
    """Every <configuration> under one plugin, flattened to dotted keys.

    Both the plugin's own and each execution's, into the same key space: a
    setting moved from one to the other is the same setting, and a gate that
    could be answered by moving it would be measuring where a line sits rather
    than what the build does. Container elements contribute their tag to the
    path and nothing else; only elements with text become facts.

    Deduplicated per plugin, because the effective POM copies a plugin's
    <configuration> into every one of its <execution>s: maven-compiler-plugin
    is bound to default-compile and default-testCompile, so an undeduplicated
    <source>1.8</source> arrives three times and a rule that counted would be
    counting the plugin's goal bindings. One setting is one fact.
    """
    seen = set()

    def walk(node, prefix):
        for child in node:
            if not isinstance(child.tag, str):
                continue
            name = prefix + child.tag
            val = (child.text or '').strip()
            if val:
                seen.add(('pluginconfig', key, name, val))
            walk(child, name + '.')

    for cfg in plugin.findall('configuration'):
        walk(cfg, '')
    for ex in plugin.findall('executions/execution'):
        for cfg in ex.findall('configuration'):
            walk(cfg, '')
    out.extend(sorted(seen))


def repo_facts(proj, out):
    """<repositories> and <pluginRepositories>, with the snapshot switch.

    Both, under one kind, with `where` naming the list: a snapshots-enabled
    plugin repository makes a build as unrepeatable as a snapshots-enabled
    dependency repository, so a rule about repeatability wants them together
    and wildcards `where`. The field is there because the two are not the same
    entry -- the super POM declares `central` in both lists, and without it
    one repository read as two.

    An entry with no <snapshots> block reports `false` rather than nothing:
    Maven's default is snapshots off, and a rule counting the enabled ones has
    to be able to tell "off" from "unread".
    """
    for where, holder in (('repository', 'repositories/repository'),
                          ('pluginRepository',
                           'pluginRepositories/pluginRepository')):
        for rep in proj.findall(holder):
            snap = rep.find('snapshots')
            out.append(('repo', where, text(rep, 'id', '-'),
                        text(rep, 'url', '-'),
                        text(snap, 'enabled', 'false') if snap is not None
                        else 'false'))


# Where a project may state a plugin version: the build section, the reporting
# section, and the same two inside any profile. Read off the *raw* pom.xml,
# where "stated" is a question that has an answer.
DECLARE_PATHS = (
    'build/plugins/plugin',
    'build/pluginManagement/plugins/plugin',
    'reporting/plugins/plugin',
    'profiles/profile/build/plugins/plugin',
    'profiles/profile/build/pluginManagement/plugins/plugin',
    'profiles/profile/reporting/plugins/plugin',
)


def parse_raw_pom(xml_text):
    """The set of plugin coordinates the project itself states a version for.

    The version *text* is deliberately not compared with the effective one: a
    project that writes ${some.version} has still stated it, and resolving the
    property here would be the reimplementation of Maven this file exists to
    avoid. What is asked is only whether the coordinate carries a <version> the
    project wrote.
    """
    try:
        root = strip_ns(ET.fromstring(xml_text))
    except ET.ParseError as exc:
        return None, [('error', 'pom.xml', str(exc))]
    declared = set()
    for path in DECLARE_PATHS:
        for pl in root.findall(path):
            if text(pl, 'version'):
                declared.add(ga(pl, PLUGIN_GROUP))
    return declared, []


def unpinned_facts(facts, declared):
    """Plugins the build runs whose version the project never stated."""
    return [('unpinned', f[1], f[2]) for f in facts
            if f[0] == 'plugin' and f[1] not in declared]


def parse_effective_pom(xml_text):
    """Facts from `help:effective-pom` output; multi-module aware."""
    out = []
    try:
        root = strip_ns(ET.fromstring(xml_text))
    except ET.ParseError as exc:
        return [('error', 'effective-pom', str(exc))]
    projects = ([root] if root.tag == 'project'
                else [p for p in root.iter('project')])
    if not projects:
        return [('error', 'effective-pom', 'no <project> in %s' % root.tag)]
    for proj in projects:
        out.append(('project',
                    '%s:%s' % (ga(proj), text(proj, 'version', '-')),
                    text(proj, 'packaging', 'jar')))
        repo_facts(proj, out)
        props = proj.find('properties')
        for p in (props if props is not None else []):
            out.append(('prop', p.tag, (p.text or '').strip()))
        deps = proj.find('dependencies')
        for d in (deps.findall('dependency') if deps is not None else []):
            out.append(('dep', ga(d), text(d, 'version', '-'),
                        text(d, 'scope', 'compile')))
        mgmt = proj.find('dependencyManagement/dependencies')
        for d in (mgmt.findall('dependency') if mgmt is not None else []):
            out.append(('depmgmt', ga(d), text(d, 'version', '-')))
        build = proj.find('build')
        plugins = build.find('plugins') if build is not None else None
        for pl in (plugins.findall('plugin') if plugins is not None else []):
            key = ga(pl)
            key = ga(pl, PLUGIN_GROUP)
            out.append(('plugin', key, text(pl, 'version', '-')))
            config_facts(pl, key, out)
            if key == PLUGIN_GROUP + ':maven-compiler-plugin':
                compiler_facts(pl, out)
        out.extend(jdk_levels(out))
    return out


def jdk_levels(facts):
    """source/target/release after Maven's precedence, with where it came from.

    The plugin's <configuration> beats the property of the same name; `release`
    beating source/target is a separate question and is left to the rule, which
    is why all three are reported rather than one winner.
    """
    prop = {f[1].split('maven.compiler.', 1)[1]: f[2] for f in facts
            if f[0] == 'prop' and f[1].startswith('maven.compiler.')}
    cfg = {f[1]: f[2] for f in facts if f[0] == 'compile'}
    out = []
    for key in ('source', 'target', 'release'):
        if key in cfg:
            out.append(('jdklevel', key, cfg[key], 'plugin'))
        elif key in prop:
            out.append(('jdklevel', key, prop[key], 'property'))
    return out


# dependency:list writes a banner, then one artifact per indented line, then
# possibly a blank tail. The shape is g:a:type[:classifier]:version:scope.
#
# The tail is not optional decoration. On JDK 9 and later the plugin appends
# " -- module junit (auto)" to every line, so a pattern anchored at the scope
# matches nothing at all on a modular JDK -- and "nothing at all" is what a
# gate asking "is anything still on Scala 2.12?" would then report. That is the
# shape of a silently green step, and it is why the rehearsal runs on two rungs
# rather than one: r02 (JDK 8) passed this and r12 (JDK 21) did not.
DEP_LINE = re.compile(r'^\s{2,}([\w.\-]+):([\w.\-]+):([\w.\-]+)'
                      r'(?::([\w.\-]+))?:([\w.\-]+):([\w.\-]+)'
                      r'(?:\s+--\s+module\s+\S+(?:\s+\([^)]*\))?)?\s*$')


def parse_deps(listing):
    """Facts from `dependency:list -DoutputFile=`."""
    out, suffixes = [], {}
    seen = False
    for line in listing.splitlines():
        m = DEP_LINE.match(line)
        if not m:
            continue
        seen = True
        group, artifact, _typ, _cls, version, scope = m.groups()
        out.append(('resolved', '%s:%s' % (group, artifact), version, scope))
        s = SCALA_SUFFIX.search(artifact)
        if s:
            suffixes[s.group(1)] = suffixes.get(s.group(1), 0) + 1
    if not seen:
        out.append(('error', 'dependency-list', 'no artifact lines'))
    for suffix in sorted(suffixes):
        out.append(('scalasuffix', suffix, str(suffixes[suffix])))
    return out


def flat(field):
    """One fact is one line of tab-separated fields, and a value Maven hands
    back can carry a tab or a line break of its own: an <argLine> written over
    several lines is the ordinary case, and surefire itself reads its line
    breaks as spaces. Written raw, that value splits its fact across lines and
    rules.py rejects the whole stream, which voids every reading this scanner
    feeds. So a field holding a tab, CR or LF has each run of whitespace folded
    to one space; every other field is written exactly as it was."""
    if '\t' in field or '\n' in field or '\r' in field:
        return ' '.join(field.split())
    return field


def emit(facts, stream=sys.stdout):
    for f in sorted(tuple(flat(x) for x in f) for f in facts):
        stream.write('\t'.join(f) + '\n')


SELFTEST_POM = '''<?xml version="1.0"?>
<project xmlns="http://maven.apache.org/POM/4.0.0">
  <groupId>g</groupId><artifactId>a</artifactId><version>1</version>
  <packaging>jar</packaging>
  <properties>
    <spark.version>2.4.8</spark.version>
    <maven.compiler.source>1.7</maven.compiler.source>
    <maven.compiler.release>11</maven.compiler.release>
  </properties>
  <dependencies>
    <dependency><groupId>o.s</groupId><artifactId>spark-core_2.11</artifactId>
      <version>2.4.8</version></dependency>
    <dependency><groupId>j</groupId><artifactId>junit</artifactId>
      <version>4.12</version><scope>test</scope></dependency>
  </dependencies>
  <dependencyManagement><dependencies>
    <dependency><groupId>c</groupId><artifactId>never-a-dep</artifactId>
      <version>9</version></dependency>
  </dependencies></dependencyManagement>
  <repositories>
    <repository><id>central</id><url>https://repo.maven.apache.org/maven2</url>
      <snapshots><enabled>false</enabled></snapshots></repository>
    <repository><id>sonatype-snapshots</id><url>https://oss.sonatype.org/s/</url>
      <snapshots><enabled>true</enabled></snapshots></repository>
    <repository><id>silent</id><url>https://example.invalid/m2</url></repository>
  </repositories>
  <pluginRepositories>
    <pluginRepository><id>plugsnap</id><url>https://example.invalid/p</url>
      <snapshots><enabled>true</enabled></snapshots></pluginRepository>
  </pluginRepositories>
  <build><plugins>
    <!-- no <groupId>: that is how Maven's own effective POM writes it, and
         a key built without the default would read "-:maven-compiler-plugin" -->
    <plugin><artifactId>maven-compiler-plugin</artifactId><version>3.8.1</version>
      <configuration><source>1.8</source><target>1.8</target>
        <compilerArgs><arg>-Xlint:all</arg><arg>-Werror</arg></compilerArgs>
      </configuration></plugin>
    <plugin><artifactId>maven-surefire-plugin</artifactId><version>2.19.1</version>
      <executions><execution><id>default-test</id><configuration>
        <excludes><exclude>**/SlowTest.java</exclude>
                  <exclude>**/OtherTest.java</exclude></excludes>
      </configuration></execution></executions></plugin>
    <plugin><artifactId>maven-jar-plugin</artifactId><version>3.4.1</version></plugin>
  </plugins></build>
</project>'''

# The project's own pom.xml for the same build: it pins the compiler plugin
# through a property and surefire outright, and says nothing at all about
# maven-jar-plugin, whose 3.4.1 above therefore came from the super POM.
SELFTEST_RAW = '''<?xml version="1.0"?>
<project xmlns="http://maven.apache.org/POM/4.0.0">
  <groupId>g</groupId><artifactId>a</artifactId><version>1</version>
  <build><plugins>
    <plugin><groupId>org.apache.maven.plugins</groupId>
      <artifactId>maven-compiler-plugin</artifactId>
      <version>${compiler.version}</version></plugin>
    <plugin><artifactId>maven-surefire-plugin</artifactId>
      <version>2.19.1</version></plugin>
    <plugin><artifactId>maven-antrun-plugin</artifactId></plugin>
  </plugins></build>
  <profiles><profile><id>p</id><build><pluginManagement><plugins>
    <plugin><artifactId>maven-shade-plugin</artifactId>
      <version>3.5.1</version></plugin>
  </plugins></pluginManagement></build></profile></profiles>
</project>'''

SELFTEST_DEPS = '''
The following files have been resolved:
   commons-cli:commons-cli:jar:1.4:compile
   org.apache.spark:spark-core_2.12:jar:3.0.3:compile
   org.apache.spark:spark-sql_2.13:jar:4.0.1:provided
   org.scala-lang:scala-library:jar:2.13.8:compile
   com.example:shaded:jar:linux-x86_64:1.2:runtime
   junit:junit:jar:4.12:test -- module junit (auto)
   org.hamcrest:hamcrest-core:jar:1.3:test -- module hamcrest.core
   not an artifact line at all
'''


def selftest():
    bad = []

    def want(got, expected, why):
        if got != expected:
            bad.append('%s: got %r want %r' % (why, got, expected))

    f = parse_effective_pom(SELFTEST_POM)
    want(sorted(x for x in f if x[0] == 'dep'),
         [('dep', 'j:junit', '4.12', 'test'),
          ('dep', 'o.s:spark-core_2.11', '2.4.8', 'compile')],
         'declared deps, scope defaulted to compile')
    want([x for x in f if x[0] == 'depmgmt'],
         [('depmgmt', 'c:never-a-dep', '9')],
         'a managed version is not a dependency')
    want(sorted(x for x in f if x[0] == 'compilerarg'),
         [('compilerarg', '-Werror'), ('compilerarg', '-Xlint:all')],
         'both compiler args, step 9 depends on seeing them')
    want(sorted(x for x in f if x[0] == 'compile'),
         [('compile', 'source', '1.8'), ('compile', 'target', '1.8')],
         'source and target, and no invented release')
    want(sorted(x for x in f if x[0] == 'prop'),
         [('prop', 'maven.compiler.release', '11'),
          ('prop', 'maven.compiler.source', '1.7'),
          ('prop', 'spark.version', '2.4.8')], 'properties')
    # The fixture sets source to 1.7 as a property and 1.8 in the plugin. The
    # plugin wins; release is only a property, so it survives with its origin
    # named. Getting this backwards is the bug this case exists for.
    want(sorted(x for x in f if x[0] == 'jdklevel'),
         [('jdklevel', 'release', '11', 'property'),
          ('jdklevel', 'source', '1.8', 'plugin'),
          ('jdklevel', 'target', '1.8', 'plugin')],
         "plugin configuration beats the property of the same name")
    want([x for x in f if x[0] == 'plugin'],
         [('plugin', 'org.apache.maven.plugins:maven-compiler-plugin', '3.8.1'),
          ('plugin', 'org.apache.maven.plugins:maven-surefire-plugin', '2.19.1'),
          ('plugin', 'org.apache.maven.plugins:maven-jar-plugin', '3.4.1')],
         'a plugin in the default group still has its group in the key')
    # Both repository lists, and a <repository> with no <snapshots> block is
    # `false` rather than absent: a rule counting snapshots-enabled repos must
    # see every repository there is, or "none enabled" would mean "none read".
    want(sorted(x for x in f if x[0] == 'repo'),
         [('repo', 'pluginRepository', 'plugsnap',
           'https://example.invalid/p', 'true'),
          ('repo', 'repository', 'central',
           'https://repo.maven.apache.org/maven2', 'false'),
          ('repo', 'repository', 'silent',
           'https://example.invalid/m2', 'false'),
          ('repo', 'repository', 'sonatype-snapshots',
           'https://oss.sonatype.org/s/', 'true')],
         'repositories and pluginRepositories, snapshot switch defaulted')
    # The excludes live in an <execution>, which is where they end up when
    # somebody moves them to dodge a gate that only read the plugin's own
    # <configuration>.
    want(sorted(x for x in f if x[0] == 'pluginconfig'
                and 'surefire' in x[1]),
         [('pluginconfig', 'org.apache.maven.plugins:maven-surefire-plugin',
           'excludes.exclude', '**/OtherTest.java'),
          ('pluginconfig', 'org.apache.maven.plugins:maven-surefire-plugin',
           'excludes.exclude', '**/SlowTest.java')],
         'execution configuration is flattened into the same key space')
    want(len([x for x in f if x[0] == 'pluginconfig' and x[2] == 'source']), 1,
         'a setting the effective POM copies into two executions is one fact')
    want(sorted(x for x in f if x[0] == 'pluginconfig'
                and 'compiler' in x[1]),
         [('pluginconfig', 'org.apache.maven.plugins:maven-compiler-plugin',
           'compilerArgs.arg', '-Werror'),
          ('pluginconfig', 'org.apache.maven.plugins:maven-compiler-plugin',
           'compilerArgs.arg', '-Xlint:all'),
          ('pluginconfig', 'org.apache.maven.plugins:maven-compiler-plugin',
           'source', '1.8'),
          ('pluginconfig', 'org.apache.maven.plugins:maven-compiler-plugin',
           'target', '1.8')],
         'nested containers contribute their tag and nothing else')

    declared, errs = parse_raw_pom(SELFTEST_RAW)
    want(errs, [], 'a good pom.xml has no error')
    want(sorted(declared),
         ['org.apache.maven.plugins:maven-compiler-plugin',
          'org.apache.maven.plugins:maven-shade-plugin',
          'org.apache.maven.plugins:maven-surefire-plugin'],
         'a ${property} version is still a version; a profile still counts;'
         ' maven-antrun-plugin has no <version> and so is not declared')
    want(sorted(unpinned_facts(f, declared)),
         [('unpinned', 'org.apache.maven.plugins:maven-jar-plugin', '3.4.1')],
         'only the plugin whose version came from the super POM')
    # The negative: an empty declaration set must not make every plugin vanish
    # from the finding, which is the shape of an `absent` rule passing because
    # the join silently produced nothing.
    want(len(unpinned_facts(f, set())), 3,
         'nothing declared means every plugin the build runs is unpinned')
    want(parse_raw_pom('<project><unclosed>')[0], None,
         'an unparseable pom.xml yields no declaration set at all')
    want([x[0] for x in parse_raw_pom('<project><unclosed>')[1]], ['error'],
         'and one error fact')
    want([x for x in f if x[0] == 'error'], [], 'a good pom has no error')

    # A value with its own line breaks stays one fact on one line; a value
    # without any is written byte for byte as it was.
    import io
    buf = io.StringIO()
    emit([('pluginconfig', 'g:a', 'argLine',
           '-Xmx1g\n\t\t\t--add-opens=java.base/sun.nio.ch=ALL-UNNAMED')], buf)
    want(buf.getvalue(), 'pluginconfig\tg:a\targLine\t-Xmx1g'
         ' --add-opens=java.base/sun.nio.ch=ALL-UNNAMED\n',
         'a multi-line value is folded onto its own fact line')
    buf = io.StringIO()
    emit([('prop', 'x', 'a  b')], buf)
    want(buf.getvalue(), 'prop\tx\ta  b\n',
         'a value with no tab or line break is written unchanged')

    # The negative that matters most: a malformed POM is a fact, not a crash.
    f = parse_effective_pom('<project><unclosed>')
    want([x[0] for x in f], ['error'], 'malformed pom -> one error fact')

    d = parse_deps(SELFTEST_DEPS)
    want(sorted(x for x in d if x[0] == 'scalasuffix'),
         [('scalasuffix', '2.12', '1'), ('scalasuffix', '2.13', '1')],
         'suffixes counted; scala-library:2.13.8 wears its 2.13 as a version')
    want(('resolved', 'com.example:shaded', '1.2', 'runtime') in d, True,
         'a classifier does not shift the version and scope columns')
    want(len([x for x in d if x[0] == 'resolved']), 7, 'prose is not an artifact')
    # JDK 9+ appends a module name; without it the count above is 5 and every
    # dependency fact on a modular rung goes missing (see DEP_LINE).
    want(('resolved', 'junit:junit', '4.12', 'test') in d, True,
         'the " -- module junit (auto)" tail does not hide the artifact')
    want(('resolved', 'org.hamcrest:hamcrest-core', '1.3', 'test') in d, True,
         'nor does the tail without the (auto) parenthetical')
    want([x for x in d if x[0] == 'error'], [], 'a real listing has no error')
    want([x[0] for x in parse_deps('nothing here')], ['error'],
         'an empty listing is an error, not a clean tree')

    for line in bad:
        print('SELFTEST FAIL ' + line, file=sys.stderr)
    if bad:
        raise SystemExit('pom-scan selftest: %d case(s) wrong' % len(bad))
    print('pom-scan selftest: 27 case(s) behaved')


def main():
    args = sys.argv[1:]
    if args[:1] == ['--selftest']:
        return selftest()
    if len(args) != 3:
        raise SystemExit('usage: pom-scan.py <effective-pom.xml> <deps.txt>'
                         ' <pom.xml>   |   pom-scan.py --selftest')
    facts = []
    for path, parse in ((args[0], parse_effective_pom), (args[1], parse_deps)):
        if not os.path.exists(path):
            facts.append(('error', os.path.basename(path), 'absent'))
            continue
        with open(path, errors='replace') as fh:
            facts.extend(parse(fh.read()))
    # The raw POM, last, because `unpinned` is a statement about both documents.
    # A missing or unparseable pom.xml is an error fact and emits no `unpinned`
    # at all: `absent pom.unpinned` would otherwise pass loudest exactly when
    # the scanner had read nothing, and common.tsv's pom-resolves rule is what
    # turns that error into a void reading rather than a clean one.
    if not os.path.exists(args[2]):
        facts.append(('error', 'pom.xml', 'absent'))
    else:
        with open(args[2], errors='replace') as fh:
            declared, errs = parse_raw_pom(fh.read())
        facts.extend(errs)
        if declared is not None:
            facts.extend(unpinned_facts(facts, declared))
    emit(facts)


if __name__ == '__main__':
    main()
