# Agent image for the Java 6 -> 25 migration task.
#
# One image serves all thirteen steps (Harbor multi-step tasks have a single
# [environment]), so it carries every JDK the ladder builds on and the two build
# systems the ladder moves between. What it deliberately does NOT carry is the
# retired toolchain — there is no JDK 6 here and none is reachable — and nothing
# of the author's: no recording, no expectation, no scored test, no scorer
# config, no solution. The tree at /app/arquillian is the pinned upstream subset,
# byte for byte, with nothing added to its tests.
#
# Pinned commit: 3fa78dfce2e3db871be6352ea62cc552c1fa1c57

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates curl file git less make procps python3 ripgrep \
        unzip xz-utils zip \
    && rm -rf /var/lib/apt/lists/*

# --- JDKs -------------------------------------------------------------------
#
# /usr/lib/jvm is the platform's own convention and one of the directories
# Gradle's toolchain auto-detection scans, so declaring a toolchain in the build
# is enough to select one of these; no environment variable of the task's
# invention is involved.
#
# Which step builds on which JDK is fixed by what javac can still emit:
#   temurin 8   -source/-target 1.6 -> class-file major 50   (steps 1-2)
#   temurin 8   -source/-target 1.7 -> class-file major 51   (step 3)
#   temurin 8   -source/-target 1.8 -> class-file major 52   (steps 4-5)
#   temurin 11  --release 8/10/11   -> 52 / 54 / 55          (steps 6-8)
#   temurin 17  --release 17        -> 61                    (step 9)
#   temurin 21  --release 21        -> 65                    (steps 10-11)
#   temurin 25  --release 25        -> 69                    (steps 12-13)
# JDK 11 is the newest javac that still accepts 6, and JDK 8 the newest that
# emits 50 without it; that is why both ends of the ladder are here.
#
# Steps 3-5 stay on 8 rather than moving to 11 at the first source-level bump,
# because neither 51 nor 52 needs `--release`: `-source/-target` emits both,
# which is the same knob the tree already sets to 1.6. The JDK moves at step 6,
# where the level finally outruns what JDK 8 can emit.
#
# An earlier revision of this comment gave a second reason -- that the frozen
# 2011 probe shelf, guice 2.0 and TestNG 5.14.6 among it, does not survive a JDK
# move, so the shelf and the JDK had to move together at step 6. That reason was
# an assertion, and when it was finally measured it turned out to be false. The
# step-05 tree builds and tests clean on JDK 11 at `--release 10` with the 2011
# pins untouched, producing the same 451 class files and the same 160 green test
# cases the JDK 8 scorer reports (NOTES 11.1); and the behaviour suite records
# byte-identically on 8, 11, 17 and 21 (NOTES 10.10.1). So the shelf does not
# move at step 6, or at any rung through 11. Nothing above depends on the
# retracted claim -- these JDKs are here because the ladder's targets need them
# -- but a falsified reason does not get to stay in a shipped file.
COPY --from=eclipse-temurin:8-jdk  /opt/java/openjdk /usr/lib/jvm/temurin-8-jdk
COPY --from=eclipse-temurin:11-jdk /opt/java/openjdk /usr/lib/jvm/temurin-11-jdk
COPY --from=eclipse-temurin:17-jdk /opt/java/openjdk /usr/lib/jvm/temurin-17-jdk
COPY --from=eclipse-temurin:21-jdk /opt/java/openjdk /usr/lib/jvm/temurin-21-jdk
COPY --from=eclipse-temurin:25-jdk /opt/java/openjdk /usr/lib/jvm/temurin-25-jdk

# The inherited tree is a Java 6 source tree built by Maven on JDK 8, so that is
# what `java` and `javac` pick up by default. Every later step names its own JDK.
ENV JAVA_HOME=/usr/lib/jvm/temurin-8-jdk

# --- Maven ------------------------------------------------------------------
#
# 3.8.8, the version the inherited reactor was verified green against. Maven 3.8
# blocks plaintext-HTTP repositories, which is part of why the reactor is the
# curated subset it is.
#
# Installed here and removed again under `Retire Maven` at the end of the file:
# the delivered image has no mvn on it. The install stays so that the layers
# below it -- Gradle, the warmed wrapper cache, the dependency seed -- keep the
# cache keys they were built with.
ARG MAVEN_VERSION=3.8.8
ARG MAVEN_SHA512=332088670d14fa9ff346e6858ca0acca304666596fec86eea89253bd496d3c90deae2be5091be199f48e09d46cec817c6419d5161fb4ee37871503f472765d00
RUN curl -fsSL -o /tmp/maven.tar.gz \
        "https://archive.apache.org/dist/maven/maven-3/${MAVEN_VERSION}/binaries/apache-maven-${MAVEN_VERSION}-bin.tar.gz" \
    && echo "${MAVEN_SHA512}  /tmp/maven.tar.gz" | sha512sum -c - \
    && mkdir -p /opt/maven \
    && tar -xzf /tmp/maven.tar.gz -C /opt/maven --strip-components=1 \
    && rm /tmp/maven.tar.gz

# --- Gradle -----------------------------------------------------------------
#
# Two distributions, because no single one spans the ladder: 9.x needs Java 17+
# to run, and 8.x is the line that still runs on the JDK 8 the first steps build
# on. Both are on disk so `gradle wrapper` and `./gradlew` work without a
# download; the wrapper is free to name a different version and fetch it.
ARG GRADLE8_VERSION=8.14.5
ARG GRADLE8_SHA256=6f74b601422d6d6fc4e1f9a1ab6522f642c2fdcbc15ae33ebd30ba3d7198e854
ARG GRADLE9_VERSION=9.7.1
ARG GRADLE9_SHA256=acd53f1edaf02f1a8ff99879f8a34b302661a057d9b063ae9e35b552f804d20a
RUN set -eux; \
    for spec in "${GRADLE8_VERSION}:${GRADLE8_SHA256}" "${GRADLE9_VERSION}:${GRADLE9_SHA256}"; do \
        ver="${spec%%:*}"; sum="${spec##*:}"; \
        curl -fsSL -o /tmp/gradle.zip "https://services.gradle.org/distributions/gradle-${ver}-bin.zip"; \
        echo "${sum}  /tmp/gradle.zip" | sha256sum -c -; \
        unzip -q /tmp/gradle.zip -d /opt/gradle; \
        rm /tmp/gradle.zip; \
    done

ENV PATH="/usr/lib/jvm/temurin-8-jdk/bin:/opt/maven/bin:/opt/gradle/gradle-8.14.5/bin:${PATH}"

# Warm the wrapper's distribution cache for both lines, by running the wrapper
# rather than by guessing where it would have put things. A throwaway project is
# used and removed; nothing it produced is left in /app.
RUN set -eux; \
    mkdir -p /tmp/warm && cd /tmp/warm; \
    for ver in "${GRADLE8_VERSION}" "${GRADLE9_VERSION}"; do \
        rm -rf /tmp/warm/* /tmp/warm/.gradle; \
        : > settings.gradle; \
        /opt/gradle/gradle-8.14.5/bin/gradle --quiet --no-daemon wrapper --gradle-version "${ver}"; \
        JAVA_HOME=/usr/lib/jvm/temurin-21-jdk ./gradlew --quiet --no-daemon --version; \
    done; \
    cd /; rm -rf /tmp/warm; \
    ls /root/.gradle/wrapper/dists

# --- Warm third-party dependency cache --------------------------------------
#
# Exactly what the pinned reactor resolves for compile and test scope, from a
# repository the authoring host resolved once. The tree's own modules are NOT
# here: arquillian's 1.0.0-SNAPSHOT artifacts are what the build under test is
# supposed to produce, and shipping them pre-installed would let a broken build
# still resolve.
COPY m2-seed /root/.m2/repository

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

# mini-swe-agent's setup probes for gcc and make and, if either is missing,
# runs `apt-get install build-essential` (no --no-install-recommends) into this
# image. Pre-installed for the agent harness only; no step builds native code.
# A small late layer rather than an edit of the apt block at the top, so the
# JDK, Gradle, wrapper-cache and m2-seed layers keep their cache keys.
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential \
    && rm -rf /var/lib/apt/lists/* \
    && gcc -dumpfullversion \
    && make --version >/dev/null

# --- The tree ---------------------------------------------------------------
#
# The pinned upstream subset, unmodified. Author-owned files live beside it at
# /app, never inside it, so the repository the agent reads is the repository the
# behaviour suite was recorded against.
WORKDIR /app
COPY arquillian /app/arquillian
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# --- Assertions --------------------------------------------------------------
#
# Rule 6: the retired toolchain is absent, and absent by construction rather
# than by intent. A build that silently acquired a JDK 6 from a base-image change
# would fail here instead of in a score.
RUN set -eux; \
    for home in /usr/lib/jvm/*/; do \
        v="$("${home}bin/java" -XshowSettings:properties -version 2>&1 | sed -n 's/.*java\.version = //p')"; \
        echo "found JDK ${v} at ${home}"; \
        case "${v}" in 1.6*|6|6.*) echo "RULE-6 VIOLATION: a retired JDK 6 is installed at ${home}" >&2; exit 1;; esac; \
    done; \
    test "$(ls -d /usr/lib/jvm/*/ | wc -l)" = 5; \
    command -v javac >/dev/null && javac -version 2>&1 | grep -q '^javac 1\.8\.'; \
    gradle --version | sed -n 's/^Gradle /gradle /p'; \
    test ! -e /root/.m2/repository/org/jboss/arquillian; \
    test "$(find /app/arquillian -name '*.java' | wc -l)" = 324; \
    test "$(find /app/arquillian -name pom.xml | wc -l)" = 16; \
    test ! -e /app/arquillian/target

# --- Retire Maven ------------------------------------------------------------
#
# Rule 6 again, for the build system this step retires rather than for the JDK:
# a toolchain the step has retired is installed in neither image, so shelling
# out to it is impossible by construction and not merely discouraged by the
# instruction.
#
# Removed here, at the end, rather than never installed. Every layer above is
# expensive -- five JDK copies, two Gradle distributions, a warmed wrapper
# cache and a 279-jar dependency seed -- and appending the removal leaves all of
# them reusable. The intermediate layers are not the delivered image; what a
# candidate can run is what the final stage exposes, and from here that is no
# longer Maven.
#
# /root/.m2/repository stays. It is a pool of third-party jars, not a toolchain,
# and a Gradle build declaring mavenLocal() reads it directly -- deleting it
# would retire the dependencies along with the build system that used to fetch
# them. Its size is asserted below so that a future edit cannot quietly take the
# cache out with the distribution.
#
# PATH is re-stated in full rather than edited: `ENV PATH="...:${PATH}"` would
# expand against the PATH set above and prepend a second copy of the entries it
# already carries, /opt/maven/bin among them.
RUN set -eux; \
    rm -rf /opt/maven
ENV PATH="/usr/lib/jvm/temurin-8-jdk/bin:/opt/gradle/gradle-8.14.5/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

# Gone, and gone by measurement. `command -v mvn` answers for PATH only, so the
# filesystem is swept the way the JDK sweep above sweeps it: any executable
# named mvn, mvnDebug or mvnyjp that survived would still be reachable by
# absolute path. The listing goes to a file rather than into `grep -q`, which
# can take EPIPE mid-write from find.
RUN set -eux; \
    if command -v mvn >/dev/null 2>&1; then \
        echo "RULE-6 VIOLATION: Maven is still reachable at $(command -v mvn)" >&2; exit 1; \
    fi; \
    case ":${PATH}:" in \
        *:/opt/maven/bin:*) echo "RULE-6 VIOLATION: /opt/maven/bin is still on PATH" >&2; exit 1;; \
    esac; \
    test ! -e /opt/maven; \
    find / -xdev -type f -name 'mvn*' -perm -u+x > /tmp/mvn.txt 2>/dev/null || true; \
    if [ -s /tmp/mvn.txt ]; then \
        echo "RULE-6 VIOLATION: a Maven launcher survived the removal:" >&2; \
        cat /tmp/mvn.txt >&2; exit 1; \
    fi; \
    rm -f /tmp/mvn.txt; \
    test -d /root/.m2/repository; \
    m2jars="$(find /root/.m2/repository -name '*.jar' | wc -l)"; \
    echo "maven retired; m2 dependency cache kept: ${m2jars} jars"; \
    test "${m2jars}" -ge 200; \
    gradle --version | sed -n 's/^Gradle /gradle /p'; \
    java -version 2>&1 | head -1
