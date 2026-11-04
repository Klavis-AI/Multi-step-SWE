#!/bin/bash
# Compile and run the probe harness.
#
#   run.sh <arq-jar-dir-or-m2> <probe-lib> <src> <inputs> <id-list> <out-dir> [workdir]
#
#   PROBE_JAVA      the JVM the fixtures run on (default: java from PATH)
#
# <arq-jar-dir-or-m2> is searched recursively for the tree's published module
# jars. Everything else is scorer-owned.
set -euo pipefail

# PROBE_JAVA is the JVM the fixtures RUN on; javac stays whatever is on PATH,
# because a jlink image has no compiler in it unless jdk.compiler was linked and
# the harness must not require that. The jlink gate sets PROBE_JAVA to
# <image>/bin/java; everything else leaves it alone and gets the PATH java.
JAVA_BIN=${PROBE_JAVA:-java}

ARQ=$1
LIB=$2
SRC=$3
INPUTS=$4
IDS=$5
OUT=$6
WORK=${7:-$(mktemp -d)}

mkdir -p "$WORK/classes" "$WORK/boot" "$OUT"

# Only the artifacts this tree installed. A repository that has also seen the
# project's published nightlies holds timestamped jars of the same version
# (arquillian-impl-base-1.0.0-20110427.172213-348.jar); those are somebody
# else's build and must never reach the classpath. The osgi bundle is excluded
# too: it shades junit and shrinkwrap, and would shadow the real ones.
ARQ_JARS=$(find "$ARQ" -name 'arquillian-*-SNAPSHOT.jar' ! -name '*osgi-bundle*' \
  | sort | tr '\n' ':')
# The scorer-owned jars are listed, not wildcarded. A "$LIB/*" entry expands in
# an order the JVM specification leaves undefined, and this directory holds
# three servlet APIs: javax.servlet resolves to servlet-api-2.5 or to
# servlet-api-3.0.0.alpha-25 depending on which the loader reaches first, and
# those two disagree about getStatus, getHeader and the whole async surface. A
# transcript recorded against one and replayed against the other is an
# expectation that expires for a reason that has nothing to do with the tree.
# sort(1) under LC_ALL=C is total and stable, so the answer is the same on every
# rung of the ladder and in the scored image.
LIB_JARS=$(LC_ALL=C find "$LIB" -maxdepth 1 -name '*.jar' | LC_ALL=C sort | tr '\n' ':')
CP="$ARQ_JARS${LIB_JARS%:}"
echo "PROBE-LIB: $(echo "$LIB_JARS" | tr ':' '\n' | grep -c .) scorer jar(s), sorted" >&2

STRAY=$(find "$ARQ" -name 'arquillian-*.jar' ! -name '*-SNAPSHOT*.jar' | wc -l)
if [ "$STRAY" -gt 0 ]; then
  echo "PROBE-NOTE: ignoring $STRAY non-local arquillian jar(s) in $ARQ" >&2
fi
echo "PROBE-CP: $(echo "$ARQ_JARS" | tr ':' '\n' | grep -c .) arquillian jar(s)" >&2

# --- namespace binding ------------------------------------------------------
#
# Ten of the enterprise API packages the harness has to name are renamed partway
# up the ladder: javax.ejb becomes jakarta.ejb, javax.servlet becomes
# jakarta.servlet, and so on. A harness that hard-named either side would stop
# compiling at that step and take the whole behaviour suite down with it, which
# is the "inherited test its own requirements made unpassable" failure.
#
# So the harness is written against javax and bound, at compile time, to
# whichever namespace the tree under test actually declares. The tree is asked
# rather than guessed: arqprobe.boot.Era reads the answer out of a class file's
# constant pool. This rewrites the SCORER'S OWN sources only; the tree under
# test is never touched.
javac -nowarn -encoding UTF-8 -d "$WORK/boot" "$SRC/../boot/arqprobe/boot/Era.java" \
  2> "$WORK/era-javac.log" || { echo "PROBE-ERA-COMPILE-FAILED"; cat "$WORK/era-javac.log"; exit 4; }
# The packages that were renamed, named one at a time. javax.naming,
# javax.management, javax.xml, javax.tools, javax.net, javax.sql and
# javax.crypto are JDK packages that were NOT renamed, and a blanket
# s/javax/jakarta/ would silently break every one of them. javax.annotation is
# split: the JSR-250 annotations moved, javax.annotation.processing did not, so
# the four that moved are listed by name.
#
# Declared ABOVE the Era call now, because Era is asked about each of them
# separately rather than asked for one word about the tree.
RENAMED='javax.ejb javax.inject javax.enterprise javax.servlet javax.persistence
javax.interceptor javax.transaction javax.validation javax.ws.rs javax.json
javax.jms javax.mail javax.faces javax.batch javax.decorator javax.el
javax.annotation.Resource javax.annotation.Resources
javax.annotation.PostConstruct javax.annotation.PreDestroy
javax.annotation.Priority javax.annotation.Generated'

# One `<package>=javax|jakarta|absent` line per package, read out of the tree's
# own class files. Era exits non-zero, loudly, on a package that is present in
# both namespaces: see the class comment for why that is refused rather than
# resolved.
# ARQ_JARS and NOT $CP. $CP also carries the SCORER'S shelf, and that shelf
# stages both namespaces on purpose so that a jakarta rung has its API jars
# ready before the tree needs them. Reading the shelf instead of the tree made
# Era answer ERA-MIXED for javax.ejb on a tree with no jakarta byte in it --
# the instrument measuring itself. The tree's own published jars are the only
# thing that can answer the question being asked.
"$JAVA_BIN" -cp "$WORK/boot:$ARQ_JARS" arqprobe.boot.Era $RENAMED > "$WORK/era.txt" 2> "$WORK/era.log" \
  || { echo "PROBE-ERA-FAILED"; sed -n '1,12p' "$WORK/era.log"; exit 4; }

# The packages this tree has actually moved. Empty on every rung below the one
# that starts the rename, which is the no-op the javax trees rely on.
MOVED=$(sed -n 's/=jakarta$//p' "$WORK/era.txt" | tr '\n' ' ')

# Authoring-side self-check hook: forces EVERY renamed package to be treated as
# moved, so the jakarta path can be exercised against a tree that is still on
# javax. The scored runner leaves it unset. It may only ever widen what is
# checked, never narrow it.
if [ "${PROBE_NS_FORCE:-}" = jakarta ]; then
  MOVED=$(echo $RENAMED)
fi

# One word, for the log and for the era-name sanity checks that read it: jakarta
# if ANY renamed package has moved, javax if none has. It no longer decides
# anything -- the per-package list above does -- and it is printed so that a
# reader of an old log and a reader of a new one are looking at the same line.
if [ -n "${MOVED// /}" ]; then NS=jakarta; else NS=javax; fi
echo "PROBE-NS: $NS" >&2
echo "PROBE-NS-MOVED: ${MOVED:-<none>}" >&2

# The servlet API's namespace specifically, which is what the source overlay
# below is keyed on. It is NOT the whole-tree word: this tree reaches jakarta
# for its annotations several rungs before Jetty lets javax.servlet follow, and
# an overlay chosen by the whole-tree word would hand the compiler a
# jakarta.servlet subclass with no jakarta.servlet on the classpath.
case " $MOVED " in
  *' javax.servlet '*) NS_SERVLET=jakarta ;;
  *)                   NS_SERVLET=javax ;;
esac
echo "PROBE-NS-SERVLET: $NS_SERVLET" >&2

cp -r "$SRC" "$WORK/src"
if [ -n "${MOVED// /}" ]; then
  for p in $MOVED; do
    find "$WORK/src" -name '*.java' -print0 \
      | xargs -0 sed -i "s|\\b${p//./\\.}\\b|jakarta.${p#javax.}|g"
  done
  REWROTE=$(grep -rl 'jakarta\.' "$WORK/src" | wc -l)
  echo "PROBE-NS-REWROTE: $REWROTE source file(s) now name jakarta" >&2
  # A rewrite that changed nothing means the pattern list and the harness have
  # drifted apart, which would show up later as a compile error against a jar
  # that is not there. Say so here instead.
  if [ "$REWROTE" -eq 0 ]; then
    echo "PROBE-NS-NOOP: the tree is on jakarta but no harness source was rebound" >&2
    exit 4
  fi
fi

# --- namespace overlay ------------------------------------------------------
#
# The sed above rebinds a package name; it cannot rebind a method set. The two
# servlet APIs disagree about more than the prefix: javax.servlet's
# ServletOutputStream (Servlet 2.5) declares no abstract methods of its own,
# while jakarta.servlet's (Servlet 5.0) declares isReady() and
# setWriteListener(WriteListener), and WriteListener did not exist before
# Servlet 3.1. A subclass of it therefore cannot be written once. So it is
# written twice, once per namespace, and exactly one copy is compiled.
#
# Copied AFTER the rewrite on purpose: the javax copy names javax.servlet
# because that is the namespace it is for, and a rewrite would turn it into a
# jakarta file with the wrong method set.
if [ -d "$SRC/../src-ns" ]; then
  [ -d "$SRC/../src-ns/$NS_SERVLET" ] || {
    echo "PROBE-NS-OVERLAY-MISSING: src-ns has no $NS_SERVLET directory" >&2; exit 4; }
  cp -r "$SRC/../src-ns/$NS_SERVLET/." "$WORK/src/"
  echo "PROBE-NS-OVERLAY: $(find "$SRC/../src-ns/$NS_SERVLET" -name '*.java' | wc -l) file(s) from src-ns/$NS_SERVLET" >&2
fi

find "$WORK/src" -name '*.java' | sort > "$WORK/sources.txt"
javac -nowarn -encoding UTF-8 -cp "$CP" -d "$WORK/classes" @"$WORK/sources.txt" \
  2> "$WORK/javac.log" || { echo "PROBE-COMPILE-FAILED"; sed -n '1,60p' "$WORK/javac.log"; exit 3; }

# Service registrations the harness itself provides (scorer-side stubs).
if [ -d "$SRC/../services" ]; then
  mkdir -p "$WORK/classes/META-INF/services"
  cp -r "$SRC/../services/." "$WORK/classes/META-INF/services/"
fi

# PROBE_JAVA_OPTS is an authoring-side hook (coverage agent); the scored runner
# leaves it unset, so the recorded transcripts are produced by a plain JVM.
TZ=UTC LC_ALL=C "$JAVA_BIN" ${PROBE_JAVA_OPTS:-} \
  -Duser.timezone=UTC -Dfile.encoding=UTF-8 -Djava.awt.headless=true \
  -cp "$WORK/classes:$CP" arqprobe.Driver "$OUT" "$INPUTS" "$IDS"
