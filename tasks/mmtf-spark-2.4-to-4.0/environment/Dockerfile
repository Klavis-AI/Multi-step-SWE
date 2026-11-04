# The agent's image: a working Spark 2.4 developer box for this repository.
#
# The service block in the middle of this file follows mirror/Dockerfile.frag,
# so the containerized services here and in every verifier come from one
# definition.
#
# The image carries the toolchain the repository builds with today -- JDK 8,
# Maven, Spark 2.4.8 -- and none of the Spark distributions a later step
# targets: a tree of /opt/spark-3.0.3 ... /opt/spark-4.0.1 would hand step 1
# the whole ladder. Each step's instruction names the distribution that step
# targets and archive.apache.org serves it (the mirror pins no apache.org host).
#
# The four LTS JDKs are installed because that is what a Java developer's
# machine looks like, and which JDK is installed says nothing about the API
# changes that are the actual content of any step. The mirror pins github.com
# to the loopback, but relays paths outside the tree's own data publisher to
# the real host (see the note above BASH_ENV), so a Temurin download from
# github.com works as well.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        bash ca-certificates curl git procps python3 unzip zip xz-utils \
        tzdata gawk jq less ripgrep vim-tiny bzip2 file diffutils patch \
    && rm -rf /var/lib/apt/lists/*
ENV TZ=UTC

# Agent harnesses need node and npm on PATH; without them Harbor's setup runs
# `apt-get install nodejs npm` (no --no-install-recommends) into this image.
# curl, bash and pgrep come from the apt block, node and npm from this block.
# Keep this block (or anything else that provides `node` and `npm`) unless the
# task deliberately wants Debian's node packages.
ARG NODE_VERSION=24.22.0
RUN set -eux; \
    arch="$(dpkg --print-architecture)"; \
    case "$arch" in \
      amd64) node_arch=x64 ;; \
      arm64) node_arch=arm64 ;; \
      *) echo "unsupported architecture: $arch" >&2; exit 1 ;; \
    esac; \
    tarball="node-v${NODE_VERSION}-linux-${node_arch}.tar.xz"; \
    curl -fsSLo "/tmp/${tarball}" "https://nodejs.org/dist/v${NODE_VERSION}/${tarball}"; \
    curl -fsSLo /tmp/SHASUMS256.txt "https://nodejs.org/dist/v${NODE_VERSION}/SHASUMS256.txt"; \
    (cd /tmp && grep " ${tarball}\$" SHASUMS256.txt | sha256sum -c -); \
    tar -xJf "/tmp/${tarball}" -C /usr/local --strip-components=1 --no-same-owner; \
    rm -f "/tmp/${tarball}" /tmp/SHASUMS256.txt; \
    test ! -e /usr/share/nodejs; \
    node --version; npm --version

# Toolchain, by checksum. toolchain/pins.tsv holds the URLs and hashes of the
# six components this image installs; the verifier images carry the full table,
# which names every Spark on the ladder in order and so is not shipped here.
COPY toolchain/ /opt/toolchain/
# Empty by default: fetch.sh then downloads the pinned URL. A local build may
# point it at a local copy of the artifacts; the sha256 in pins.tsv still
# decides.
ARG TOOLCHAIN_MIRROR=
RUN set -eux; \
    /opt/toolchain/fetch.sh jdk8  /opt/jdk/8; \
    /opt/toolchain/fetch.sh jdk11 /opt/jdk/11; \
    /opt/toolchain/fetch.sh jdk17 /opt/jdk/17; \
    /opt/toolchain/fetch.sh jdk21 /opt/jdk/21; \
    /opt/toolchain/fetch.sh maven /opt/maven; \
    /opt/toolchain/fetch.sh spark2.4.8-s211 /opt/spark

# JAVA_HOME has to be set before the service fragment: it imports the mirror's
# CA into the JDK's own cacerts, which it finds through this variable.
ENV JAVA_HOME=/opt/jdk/8
ENV SPARK_HOME=/opt/spark
ENV PATH=/opt/jdk/8/bin:/opt/maven/bin:/opt/spark/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

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
    # too. JDK 8 keeps it under jre/, 9+ under lib/. This image carries four
    # JDKs and the ladder asks the build to move off 8, so every one of them
    # gets the CA, each through its own keytool -- not only $JAVA_HOME's, or a
    # candidate who switches to 11, 17 or 21 meets PKIX failures from the
    # mirror. The verifier images carry one JDK each and keep the single-JDK
    # form of this fragment.
    for J in /opt/jdk/*/; do \
        if [ -f "${J}jre/lib/security/cacerts" ]; then \
            KS="${J}jre/lib/security/cacerts"; \
        else \
            KS="${J}lib/security/cacerts"; \
        fi; \
        "${J}bin/keytool" -importcert -noprompt -trustcacerts -alias mmtf-mirror-ca \
            -file /usr/local/share/ca-certificates/mmtf-mirror-ca.crt \
            -keystore "$KS" -storepass changeit; \
        "${J}bin/keytool" -list -keystore "$KS" -storepass changeit \
            -alias mmtf-mirror-ca > /dev/null; \
    done

# This image's mirror/serve.py differs from the verifiers' copy in one respect:
# a github.com request whose path is outside the tree's own data publisher
# (/sbl-sdsc/, which holds both github.com URLs the tree names) is relayed to
# the real github.com instead of answered 404. Agent harnesses install their
# tooling from github.com (for example a git clone of nvm) in a shell that
# BASH_ENV below has already pinned it in. The recorded ORC blob and every
# other /sbl-sdsc/ path are still answered from the fixture tree, and every
# verifier image -- the one that scores -- keeps the strict 404.
#
# Every shell, interactive or not, brings the service up.
ENV BASH_ENV=/usr/local/bin/mmtf-mirror-up
RUN printf '. /usr/local/bin/mmtf-mirror-up\n' > /etc/profile.d/10-mmtf-mirror.sh \
    && printf '. /usr/local/bin/mmtf-mirror-up\n' >> /etc/bash.bashrc

# The corpus the repository's own docs tell you to download and point
# MMTF_FULL / MMTF_REDUCED at (docs/MacOsInstallation.md), pinned to the subset
# the mirror serves so the two always agree. /data/cif and /data/pdb are the
# mmCIF and PDB-format inputs the file-converting jobs read.
COPY data.tgz /tmp/data.tgz
RUN set -eux; \
    mkdir -p /data; \
    tar -xzf /tmp/data.tgz -C /data; \
    rm -f /tmp/data.tgz; \
    test -d /data/full && test -d /data/reduced && test -d /data/cif && test -d /data/pdb; \
    # Same corpus and same permission fix as the verifier image: an archive
    # with 0700 directories is a corpus only root can read. The agent works as
    # root here and would not notice; the two images are meant to carry the
    # same /data, and one that differs in who can read it is one the candidate
    # can pass and the verifier cannot.
    chmod -R a+rX /data; \
    test -z "$(find /data -type d ! -perm -005 -print -quit)"; \
    test -z "$(find /data -type f ! -perm -004 -print -quit)"
ENV MMTF_FULL=/data/full
ENV MMTF_REDUCED=/data/reduced

# Spark binds to the loopback by name otherwise, and the container's hostname is
# not resolvable once mmtf-mirror-up has rewritten /etc/hosts.
ENV SPARK_LOCAL_IP=127.0.0.1

# Warm the Maven cache by performing the repository's own build, in a throwaway
# copy. Two reasons it is not built in /app: `mvn package` runs the shade plugin,
# which rewrites dependency-reduced-pom.xml in the tree it builds, and a warm
# cache is worth nothing if the tree the agent is handed is no longer the tree
# the pin describes. /app is unpacked fresh afterwards, so it is byte-for-byte
# the context's app.tgz.
#
# The cache is warmed against *this* pom -- Spark 2.4.0, Scala 2.11 -- and
# nothing else. Copying in a repository that had been built at several rungs
# would leave spark-core_2.13-4.0.1.jar in ~/.m2 for an agent at step 1 to find.
COPY app.tgz /tmp/app.tgz
RUN set -eux; \
    mkdir -p /tmp/warm; \
    tar -xzf /tmp/app.tgz -C /tmp/warm; \
    cd /tmp/warm; \
    mvn -B -q package -DskipTests; \
    mvn -B -q test-compile; \
    test -f target/mmtf-spark-0.3.0-SNAPSHOT.jar; \
    cd /; rm -rf /tmp/warm; \
    mkdir -p /app; \
    tar -xzf /tmp/app.tgz -C /app; \
    rm -f /tmp/app.tgz; \
    test -f /app/pom.xml && test -d /app/src/main/java && ! test -e /app/target

# Advisory countdown. Harbor enforces the real timeout; this is so the agent can
# see it. Keep it and every step's timeout_sec synchronized.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

WORKDIR /app

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
    for d in /tests /opt/jdk /opt/maven /opt/spark /opt/hadoop \
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
