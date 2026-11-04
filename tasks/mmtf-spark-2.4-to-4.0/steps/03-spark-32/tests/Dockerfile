# A verifier image for one rung of the ladder.
#
# The toolchain lines below name this step's rung. The service block in the
# middle of this file is the same mirror/Dockerfile.frag the agent image
# embeds, so a job replayed here reaches exactly the services it was recorded
# against.
#
# The toolchain is this step's and only this step's. That is not a size
# optimization: the verifier decides whether the tree runs on the Spark the step
# names, and a second distribution on the image is a second answer to that
# question.
# Directory order, made the same on every host. Hadoop's local listing does not
# sort, so a corpus directory's part files -- and with them a job's partition
# order, every seeded RDD.sample draw and every partition-ordered print -- come
# back in whatever order the host filesystem returns (ext4: hash order under a
# per-filesystem seed). sortdir.c is an LD_PRELOAD that returns every directory
# in byte order of its names; suite/run-suite.sh runs each scored job under it,
# and the recordings in suite/ref were taken under it (suite/rederive-ref.sh).
FROM ubuntu:22.04 AS sortdir
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/*
COPY sortdir/sortdir.c /src/sortdir.c
RUN gcc -O2 -Wall -Wno-deprecated-declarations -shared -fPIC \
        -o /src/libsortdir.so /src/sortdir.c -ldl

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
# util-linux carries setpriv, which is how candidate code runs as nobody.
RUN apt-get update && apt-get install -y --no-install-recommends \
        bash ca-certificates curl git procps python3 unzip zip xz-utils \
        tzdata gawk jq util-linux diffutils \
    && rm -rf /var/lib/apt/lists/*
ENV TZ=UTC

COPY toolchain/ /opt/toolchain/
# Empty by default: fetch.sh then downloads the pinned URL. A local build may
# point it at a local copy of the artifacts; the sha256 in pins.tsv still
# decides.
ARG TOOLCHAIN_MIRROR=
RUN set -eux; \
    /opt/toolchain/fetch.sh jdk8 /opt/jdk; \
    /opt/toolchain/fetch.sh maven /opt/maven; \
    /opt/toolchain/fetch.sh spark3.2.4-s212 /opt/spark
# (this distribution bundles Hadoop)
ENV JAVA_HOME=/opt/jdk
ENV SPARK_HOME=/opt/spark
ENV PATH=/opt/jdk/bin:/opt/maven/bin:/opt/spark/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
COPY --from=sortdir /src/libsortdir.so /opt/sortdir/libsortdir.so
RUN chmod 0755 /opt/sortdir /opt/sortdir/libsortdir.so


# Pin the logging config rather than inherit whichever stream this Spark
# version's built-in default chose; Spark 3.3 chose stdout, which is the
# recording's own channel. Both frameworks' files are shipped: each reads its
# own and ignores the other, so this needs no per-rung branch.
COPY logging/ /opt/spark/conf/
# COPY preserves the mode of the file in the build context, which can arrive
# 0600 root, and every scored job runs as nobody. The effect would be silent --
# Spark logs "Using Spark's default log4j profile" and carries on -- so the one
# file whose job is to stop the log stream moving between Spark versions would
# not be read by the half of the run that matters. The sweep at the end of
# this Dockerfile asserts it is readable.
RUN chmod -R a+rX /opt/spark/conf
# Containerized external services. Included by both the agent and the verifier
# image so the inherited tests see the same services on both sides.
#
# Expects the build context to contain:
#   mirror/serve.py  mirror/mmtf-mirror-up  mirror/tls/
#   mirror/blobs/v1.0/...   mirror/svc/<host>/...
#
# JAVA_HOME must be set before this fragment runs.

COPY mirror/serve.py       /opt/mmtf-mirror/serve.py
COPY mirror/mmtf-mirror-up /usr/local/bin/mmtf-mirror-up
COPY mirror/tls/server.pem /srv/mmtf-tls/server.pem
COPY mirror/tls/ca.crt     /usr/local/share/ca-certificates/mmtf-mirror-ca.crt
COPY mirror/blobs/         /srv/mmtf/
COPY mirror/svc/           /srv/svc/

RUN set -eux; \
    chmod 0755 /usr/local/bin/mmtf-mirror-up; \
    chmod 0600 /srv/mmtf-tls/server.pem; \
    # The mirror serves these blobs over the loopback as root, but that is not
    # the only way they are read: HarnessDriver-siftslocal opens
    # /srv/svc/ftp.ebi.ac.uk/sifts/split_xml straight off the filesystem, and it
    # does so as nobody. A tree that arrives 0700/0600 leaves that job retrying
    # its way to an empty table, so the tree is opened up and the result is
    # asserted as the user who does the reading rather than as the one who just
    # set the bits.
    #
    # /srv/mmtf-tls is not in the list, and the assertion below checks it stays
    # unreadable: it holds the mirror's private key, and the only process with
    # any business reading it is the one that binds 443.
    chmod -R a+rX /srv/mmtf /srv/svc; \
    n=$(find /srv/mmtf /srv/svc -type f | wc -l); \
    m=$(setpriv --reuid 65534 --regid 65534 --clear-groups \
            find /srv/mmtf /srv/svc -type f -readable | wc -l); \
    test "$n" -gt 0 && test "$n" = "$m"; \
    test "$(setpriv --reuid 65534 --regid 65534 --clear-groups \
            find /srv/mmtf-tls -type f -readable | wc -l)" = 0; \
    printf 'srv: %s file(s), all readable as nobody; the key is not\n' "$n"; \
    update-ca-certificates; \
    # The JVM does not read the system trust store, so the CA goes into cacerts
    # too. JDK 8 keeps it under jre/, 9+ under lib/.
    if [ -f "$JAVA_HOME/jre/lib/security/cacerts" ]; then \
        KS="$JAVA_HOME/jre/lib/security/cacerts"; \
    else \
        KS="$JAVA_HOME/lib/security/cacerts"; \
    fi; \
    keytool -importcert -noprompt -trustcacerts -alias mmtf-mirror-ca \
        -file /usr/local/share/ca-certificates/mmtf-mirror-ca.crt \
        -keystore "$KS" -storepass changeit; \
    keytool -list -keystore "$KS" -storepass changeit -alias mmtf-mirror-ca > /dev/null

# Every shell, interactive or not, brings the service up.
ENV BASH_ENV=/usr/local/bin/mmtf-mirror-up
RUN printf '. /usr/local/bin/mmtf-mirror-up\n' > /etc/profile.d/10-mmtf-mirror.sh \
    && printf '. /usr/local/bin/mmtf-mirror-up\n' >> /etc/bash.bashrc

# The same pinned corpus the agent image carries.
# build-context guard: this step's copy ships as data-03.tgz (BuildKit
# collision: all five copies are the same size); it is copied byte-for-byte.
COPY data-03.tgz /tmp/data.tgz
RUN set -eux; \
    mkdir -p /data; \
    tar -xzf /tmp/data.tgz -C /data; \
    rm -f /tmp/data.tgz; \
    test -d /data/full && test -d /data/reduced && test -d /data/cif && test -d /data/pdb; \
    # The corpus is read by the scored half of the run, and that half is
    # nobody. An archive that carries 0700 directories and 0600 files passes
    # root's `test -d` above while every job that later reads it as nobody dies
    # on the first open. The bits are fixed here, and the assertion is taken as
    # nobody rather than as the user who just set them.
    chmod -R a+rX /data; \
    n=$(find /data -type f | wc -l); \
    m=$(setpriv --reuid 65534 --regid 65534 --clear-groups \
            find /data -type f -readable | wc -l); \
    test "$n" -gt 0 && test "$n" = "$m"; \
    printf 'data: %s file(s), all readable as nobody\n' "$n"
ENV MMTF_FULL=/data/full
ENV MMTF_REDUCED=/data/reduced
ENV SPARK_LOCAL_IP=127.0.0.1

# Candidate code runs as nobody, so nobody needs a HOME it can write and a
# Maven cache it can read.
#
# The cache is warmed by performing this rung's build, in a throwaway copy of
# this rung's tree -- not by `dependency:go-offline`, which resolves the
# declared graph and misses every plugin and every artifact a plugin pulls at
# execution time. A verifier whose first act is a 300 MB download is a verifier
# that scores a slow mirror rather than a candidate.
#
# The repository path is set here rather than left to ~/.m2, and the reason is
# not tidiness: the JVM reads user.home from the passwd database on Linux, not
# from $HOME, so `HOME=/scratch mvn` warms /root/.m2 and then the scored build,
# running as nobody, finds nothing there. MAVEN_ARGS (Maven 3.9+) applies the
# setting to every invocation, including the ones test.sh makes on the
# candidate's tree.
ENV MAVEN_ARGS=-Dmaven.repo.local=/scratch/.m2/repository
COPY warm.tgz /tmp/warm.tgz
RUN set -eux; \
    mkdir -p /scratch /tmp/warm; \
    tar -xzf /tmp/warm.tgz -C /tmp/warm; \
    cd /tmp/warm; \
    mvn -B -q package -DskipTests; \
    mvn -B -q test-compile; \
    # `test-compile` stops one phase short of resolving surefire's provider, so
    # a verifier that ran the suite would pay for that download at scoring time
    # -- or fail on it. Selecting a test class that cannot exist does not help:
    # surefire returns before resolving its provider when nothing is selected
    # (the assertion below checks that the provider is in the cache). So one
    # real class runs. OutlierDetectorTest is 2 tests over pure arithmetic in
    # 0.1 s: no Spark session, no corpus, no service.
    mvn -B -q test -Dtest=OutlierDetectorTest -DfailIfNoTests=false; \
    ls target/mmtf-spark-*.jar > /dev/null; \
    test -n "$(find /scratch/.m2/repository -name 'surefire-junit*' -type d -print -quit)"; \
    cd /; rm -rf /tmp/warm /tmp/warm.tgz; \
    test -d /scratch/.m2/repository/org/apache/spark; \
    chown -R nobody:nogroup /scratch; \
    printf 'warm: %s file(s) in /scratch/.m2\n' "$(find /scratch/.m2 -type f | wc -l)"

# The migrated-ness gates read Maven's own answer rather than re-implementing
# property interpolation and dependency mediation: `help:effective-pom` for what
# the POM resolves to and `dependency:list` for the transitive graph a
# "Scala 2.13 only" requirement is a claim about. Neither plugin is pulled by
# this project's build, so neither is in the cache the block above warms, and a
# scored run that had to fetch them would be a scored run that depends on Maven
# Central being up.
#
# A separate layer, because the warm build above is the expensive one and
# adding to its RUN would rebuild it everywhere. The versions are pinned for the
# same reason every other tool here is -- an unpinned `help:effective-pom`
# resolves whatever Central calls latest today, which is not what was warmed.
#
# Both are verified on JDK 8 and JDK 21; the assertions below are what says so
# at build time rather than at scoring time.
RUN set -eux; \
    mkdir -p /tmp/gwarm; cd /tmp/gwarm; \
    printf '%s\n' \
      '<project xmlns="http://maven.apache.org/POM/4.0.0">' \
      '  <modelVersion>4.0.0</modelVersion>' \
      '  <groupId>gate</groupId><artifactId>warm</artifactId><version>0</version>' \
      '</project>' > pom.xml; \
    mvn -B -q org.apache.maven.plugins:maven-help-plugin:3.4.0:effective-pom \
        -Doutput=/tmp/gwarm/eff.xml; \
    mvn -B -q org.apache.maven.plugins:maven-dependency-plugin:3.6.1:list \
        -DoutputFile=/tmp/gwarm/deps.txt; \
    test -s /tmp/gwarm/eff.xml; \
    grep -q '<artifactId>warm</artifactId>' /tmp/gwarm/eff.xml; \
    test -f /tmp/gwarm/deps.txt; \
    cd /; rm -rf /tmp/gwarm; \
    chown -R nobody:nogroup /scratch; \
    printf 'gates: help+dependency plugins warmed (%s file(s) in /scratch/.m2)\n' \
        "$(find /scratch/.m2 -type f | wc -l)"

# The instrument guard. pom-scan.sh's two readings, the dependency listing and
# help:effective-pom, give their plugin its options as -D user properties, and
# Maven lets the pom's own plugin configuration and properties override them.
# This Maven core extension is loaded into those two runs only and drops what
# the poms say about the run's plugin before Maven plans the run
# (guard/InstrumentGuard.java says exactly what). Compiled here, against this
# image's own Maven, so no scored run compiles anything; root-owned, and read
# by nobody, which is who runs pom-scan. The two runs at the end are its proof
# of life on this image, one per plugin pom-scan guards: an empty project,
# offline, from a throwaway repository head over the warm cache exactly as
# pom-scan.sh chains them, and the guard's line for that plugin must be in the
# log.
COPY guard /opt/guardsrc
RUN set -eux; \
    mkdir -p /opt/guard/classes/META-INF/sisu; \
    javac -nowarn -cp '/opt/maven/lib/*' -d /opt/guard/classes \
      /opt/guardsrc/InstrumentGuard.java; \
    echo scoreguard.InstrumentGuard \
      > /opt/guard/classes/META-INF/sisu/javax.inject.Named; \
    jar cf /opt/guard/instrument-guard.jar -C /opt/guard/classes .; \
    rm -rf /opt/guard/classes /opt/guardsrc; \
    chmod 0755 /opt/guard; \
    chmod 0644 /opt/guard/instrument-guard.jar; \
    test -s /opt/guard/instrument-guard.jar; \
    mkdir -p /tmp/gchk; cd /tmp/gchk; \
    printf '%s\n' \
      '<project xmlns="http://maven.apache.org/POM/4.0.0">' \
      '  <modelVersion>4.0.0</modelVersion>' \
      '  <groupId>gate</groupId><artifactId>guard</artifactId><version>0</version>' \
      '</project>' > pom.xml; \
    MAVEN_ARGS="-Dmaven.repo.local=/tmp/gchk/m2 -Dmaven.repo.local.tail=/scratch/.m2/repository" \
      mvn -B -o -Dmaven.ext.class.path=/opt/guard/instrument-guard.jar \
        -Dscore.guard.plugin=org.apache.maven.plugins:maven-dependency-plugin \
        org.apache.maven.plugins:maven-dependency-plugin:3.6.1:list \
        -DoutputFile=/tmp/gchk/deps.txt > /tmp/gchk/log 2>&1 \
      || { cat /tmp/gchk/log; exit 1; }; \
    grep '^\[INFO\] \[score-guard\] org.apache.maven.plugins:maven-dependency-plugin: ' \
      /tmp/gchk/log || { cat /tmp/gchk/log; exit 1; }; \
    MAVEN_ARGS="-Dmaven.repo.local=/tmp/gchk/m2 -Dmaven.repo.local.tail=/scratch/.m2/repository" \
      mvn -B -o -Dmaven.ext.class.path=/opt/guard/instrument-guard.jar \
        -Dscore.guard.plugin=org.apache.maven.plugins:maven-help-plugin \
        org.apache.maven.plugins:maven-help-plugin:3.4.0:effective-pom \
        -Doutput=/tmp/gchk/eff.xml > /tmp/gchk/hlog 2>&1 \
      || { cat /tmp/gchk/hlog; exit 1; }; \
    grep '^\[INFO\] \[score-guard\] org.apache.maven.plugins:maven-help-plugin: ' \
      /tmp/gchk/hlog || { cat /tmp/gchk/hlog; exit 1; }; \
    grep -q '<artifactId>guard</artifactId>' /tmp/gchk/eff.xml; \
    cd /; rm -rf /tmp/gchk

COPY tests/ /tests/
# build-context guard: keeps each step's per-step data at a step-unique path
# (BuildKit collision). Every step's verifier is built from a directory named
# `tests`, and BuildKit's context cache treats a file as unchanged when its
# (relpath, size, mode, mtime) match; mtimes do not survive a git clone or a
# 1-second tar, so two steps' same-size copies of one file could hand this image
# the other step's bytes.
# The recorded references (suite/ref/) and the payload stamp (step.tsv)
# therefore ship as suite/ref-NN/ and step-NN.tsv (NN = this step's number) and
# are renamed back here, byte-for-byte, before anything reads them, so
# manifest.sha256 and every reader keep the in-image paths. Exactly one of each
# must arrive, no plain one, and both must carry the step id step.tsv names.
RUN set -eu; \
    n=$(ls -d /tests/suite/ref-[0-9][0-9] 2>/dev/null | wc -l); \
    if [ "$n" != 1 ] || [ -e /tests/suite/ref ]; then \
        echo "build-context guard: need exactly one /tests/suite/ref-NN and no /tests/suite/ref" >&2; exit 1; \
    fi; \
    n=$(ls /tests/step-[0-9][0-9].tsv 2>/dev/null | wc -l); \
    if [ "$n" != 1 ] || [ -e /tests/step.tsv ]; then \
        echo "build-context guard: need exactly one /tests/step-NN.tsv and no /tests/step.tsv" >&2; exit 1; \
    fi; \
    ref=$(ls -d /tests/suite/ref-[0-9][0-9]); stamp=$(ls /tests/step-[0-9][0-9].tsv); \
    mv "$ref" /tests/suite/ref; \
    mv "$stamp" /tests/step.tsv; \
    id=$(awk -F'\t' '$1 == "step" {print $2}' /tests/step.tsv); \
    [ "${ref##*-}" = "$id" ] && [ "$stamp" = "/tests/step-$id.tsv" ] || { \
        echo "build-context guard: $ref and $stamp do not both belong to step $id" >&2; exit 1; }; \
    echo "build-context guard: step $id: $ref -> /tests/suite/ref, $stamp -> /tests/step.tsv"
RUN chmod -R a+rX /tests && chmod 0755 /tests/test.sh

# The last layer, and the only one whose subject is the image rather than the
# software in it: every tree the scored half of a run reads, measured as the
# user that reads it. The corpus, the mirror's service blobs and the pinned
# log4j configs each assert their own tree where they are installed; this
# sweep states the rule they are all instances of, once:
#
#   a file root can read and nobody cannot is a file this image scores
#   differently from the way it was recorded.
#
# Two exclusions. /srv/mmtf-tls holds the mirror's private key and the service
# block above asserts separately that it stays unreadable. Hadoop's tarball
# ships its KMS and HTTPFS tomcat configs 0600; nothing here starts either
# service, and an upstream archive's modes are left as shipped. /opt/toolchain
# is out of scope because nothing reads it after this line.
#
# The trees are listed rather than globbed from /, so that giving a run a new
# tree to read means adding it here as well -- which is the point.
RUN set -eux; \
    trees=""; \
    for d in /tests /opt/jdk /opt/maven /opt/spark /opt/hadoop /opt/guard \
             /data /srv/mmtf /srv/svc /scratch; do \
        [ -e "$d" ] && trees="$trees $d"; \
    done; \
    test -n "$trees"; \
    find $trees -xdev -type f | sort > /tmp/sweep.all; \
    setpriv --reuid 65534 --regid 65534 --clear-groups \
        find $trees -xdev -type f -readable | sort > /tmp/sweep.ok; \
    comm -23 /tmp/sweep.all /tmp/sweep.ok \
        | grep -Ev '/tomcat/conf/' > /tmp/sweep.bad || true; \
    printf 'sweep: %s of %s file(s) readable as nobody over%s\n' \
        "$(wc -l < /tmp/sweep.ok)" "$(wc -l < /tmp/sweep.all)" "$trees"; \
    if [ -s /tmp/sweep.bad ]; then \
        echo "sweep: root can read these and nobody cannot:" >&2; \
        head -20 /tmp/sweep.bad >&2; \
        exit 1; \
    fi; \
    rm -f /tmp/sweep.all /tmp/sweep.ok /tmp/sweep.bad

WORKDIR /app

# build-context guard: keeps this file's size unique per step (BuildKit collision)
# ==============================
