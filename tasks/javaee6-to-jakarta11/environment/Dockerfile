# Agent environment for the mini-git-server migration task.
#
# Harbor 0.23 schema 1.4 has no per-step [environment], so this single image
# serves all ten steps. It therefore carries every JDK the ladder targets.
# Java 8 and earlier are the retired toolchain and are not installed; each
# step's verifier image pins exactly one JDK.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
      bash \
      bsdextrautils \
      ca-certificates \
      coreutils \
      curl \
      diffutils \
      file \
      findutils \
      gawk \
      git \
      gnupg \
      jq \
      less \
      make \
      netcat-openbsd \
      openssh-client \
      patch \
      procps \
      python3 \
      ripgrep \
      tar \
      unzip \
      util-linux \
      xz-utils \
      zip \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# JDKs. Eclipse Temurin, pinned by version and sha256, in the standard Debian
# /usr/lib/jvm location and registered with update-alternatives. The default is
# the OLDEST installed JDK (11): a step whose target release is newer than the
# default fails to compile with a plain javac error rather than producing a
# class file the scorer's runtime cannot load.
# ---------------------------------------------------------------------------
RUN set -eux; \
    install_jdk() { \
      ver="$1"; url="$2"; sum="$3"; \
      curl -fsSL -o /tmp/jdk.tar.gz "$url"; \
      echo "$sum  /tmp/jdk.tar.gz" | sha256sum -c -; \
      mkdir -p "/usr/lib/jvm/temurin-${ver}"; \
      tar -xzf /tmp/jdk.tar.gz -C "/usr/lib/jvm/temurin-${ver}" --strip-components=1; \
      rm -f /tmp/jdk.tar.gz; \
      "/usr/lib/jvm/temurin-${ver}/bin/java" -version; \
    }; \
    install_jdk 11 \
      'https://github.com/adoptium/temurin11-binaries/releases/download/jdk-11.0.32.1%2B1/OpenJDK11U-jdk_x64_linux_hotspot_11.0.32.1_1.tar.gz' \
      '5c3f68887c325d36d852ba534303e1f5f1f5cae7d6cc1e951d73e0d8e98a058d'; \
    install_jdk 17 \
      'https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.20.1%2B1/OpenJDK17U-jdk_x64_linux_hotspot_17.0.20.1_1.tar.gz' \
      '3808d1d15e3ec6bd5b84057fb5d84c33d8a1536a258146bcea2e603fc726e08e'; \
    install_jdk 21 \
      'https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.12.1%2B1/OpenJDK21U-jdk_x64_linux_hotspot_21.0.12.1_1.tar.gz' \
      'ce79869e1307ed8ee1e2baa86a412b1eb5b75d10a01006d788a6f968bcfaee94'

RUN set -eux; \
    for v in 11 17 21; do \
      prio=$((v * 100)); \
      for tool in java javac jar javap jarsigner jcmd jdeps jinfo jlink jmap jps jshell jstack keytool serialver; do \
        if [ -x "/usr/lib/jvm/temurin-${v}/bin/${tool}" ]; then \
          update-alternatives --install "/usr/bin/${tool}" "${tool}" \
            "/usr/lib/jvm/temurin-${v}/bin/${tool}" "$prio" >/dev/null; \
        fi; \
      done; \
    done; \
    for tool in java javac jar javap jarsigner jcmd jdeps jinfo jlink jmap jps jshell jstack keytool serialver; do \
      update-alternatives --set "$tool" "/usr/lib/jvm/temurin-11/bin/${tool}" >/dev/null; \
    done; \
    java -version 2>&1 | head -1; javac -version

ENV JAVA_HOME=/usr/lib/jvm/temurin-11

# ---------------------------------------------------------------------------
# Apache Maven, pinned.
# ---------------------------------------------------------------------------
RUN set -eux; \
    curl -fsSL -o /tmp/maven.tar.gz \
      'https://archive.apache.org/dist/maven/maven-3/3.9.16/binaries/apache-maven-3.9.16-bin.tar.gz'; \
    echo '831a8591fe20c8243b1dbe7d71e3244f31d1665b0804b2e825e38cbbe5ce0cafb8338851f90780735568773e0a6cd07bbec107cda0b896b008b861075358b6f6  /tmp/maven.tar.gz' \
      | sha512sum -c -; \
    mkdir -p /opt/maven; \
    tar -xzf /tmp/maven.tar.gz -C /opt/maven --strip-components=1; \
    rm -f /tmp/maven.tar.gz; \
    ln -s /opt/maven/bin/mvn /usr/local/bin/mvn; \
    mvn -v

# ---------------------------------------------------------------------------
# Node.js, pinned. Only the harness agent uses it.
# ---------------------------------------------------------------------------
RUN set -eux; \
    curl -fsSL -o /tmp/node.tar.xz \
      'https://nodejs.org/dist/v22.23.2/node-v22.23.2-linux-x64.tar.xz'; \
    echo 'd60acfe00a2932254bb0ad20e01b0d74397a0875595de719654b214f4b03f307  /tmp/node.tar.xz' \
      | sha256sum -c -; \
    mkdir -p /opt/node; \
    tar -xJf /tmp/node.tar.xz -C /opt/node --strip-components=1; \
    rm -f /tmp/node.tar.xz; \
    ln -s /opt/node/bin/node /usr/local/bin/node; \
    ln -s /opt/node/bin/npm /usr/local/bin/npm; \
    ln -s /opt/node/bin/npx /usr/local/bin/npx; \
    node --version; npm --version

# ---------------------------------------------------------------------------
# Warm the Maven dependency cache (third-party caches are warm; project code
# is always built from clean). The warming source is the inherited
# tree itself, so the cache holds exactly what the tree already declares and
# nothing that points at a later step. `-fn` is required: the inherited tree
# does not compile on JDK 11, which is step 1's work.
# ---------------------------------------------------------------------------
COPY warm-src /warm
RUN set -eux; \
    cd /warm; \
    mvn -B -ntp -fn install >/tmp/warm-a.log 2>&1 || true; \
    mvn -B -ntp -fn dependency:go-offline >/tmp/warm-b.log 2>&1 || true; \
    # The reactor snapshots must not linger: a stale sibling in the cache can
    # satisfy a dependency the agent has actually broken.
    rm -rf /root/.m2/repository/com/madgag/mini-git-server-parent \
           /root/.m2/repository/com/madgag/mini-git-server-server \
           /root/.m2/repository/com/madgag/mini-git-server-httpd \
           /root/.m2/repository/com/madgag/mini-git-server-sshd \
           /root/.m2/repository/com/madgag/mini-git-server-util-cli \
           /root/.m2/repository/com/madgag/mini-git-server-util-ssl \
           /root/.m2/repository/com/madgag/mini-git-server-war; \
    cd /; rm -rf /warm; \
    # Assert the warm cache really landed, in numbers that must change.
    jars=$(find /root/.m2/repository -name '*.jar' ! -name '*-sources.jar' | wc -l); \
    echo "warm cache jars=$jars"; \
    test "$jars" -ge 200; \
    for a in \
      org/apache/maven/plugins/maven-war-plugin/2.1.1/maven-war-plugin-2.1.1.jar \
      org/apache/maven/plugins/maven-compiler-plugin/2.3.2/maven-compiler-plugin-2.3.2.jar \
      com/google/inject/guice/2.0/guice-2.0.jar \
      com/madgag/org.eclipse.jgit/0.10.99.7-UNOFFICIAL-ROBERTO-RELEASE/org.eclipse.jgit-0.10.99.7-UNOFFICIAL-ROBERTO-RELEASE.jar \
      org/apache/sshd/sshd-core/0.5.0/sshd-core-0.5.0.jar \
      org/apache/tomcat/servlet-api/6.0.24/servlet-api-6.0.24.jar \
      junit/junit/4.8.2/junit-4.8.2.jar \
    ; do test -s "/root/.m2/repository/$a" || { echo "MISSING $a"; exit 1; }; done; \
    for p in maven-surefire-plugin surefire-junit4 maven-jar-plugin maven-resources-plugin; do \
      find /root/.m2/repository -name "${p}-*.jar" ! -name '*-sources.jar' | grep -q . \
        || { echo "MISSING plugin $p"; exit 1; }; \
    done; \
    test ! -e /root/.m2/repository/com/madgag/mini-git-server-server

# ---------------------------------------------------------------------------
# build-essential, for the agent harness only. mini-swe-agent's setup probes
# for gcc and make and, if either is missing, runs `apt-get install
# build-essential` (no --no-install-recommends) into this image. No step builds
# native code. A small late layer rather than an edit of the apt block at the
# top, so the JDK, Maven, Node and warm-cache layers keep their cache keys.
# ---------------------------------------------------------------------------
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential \
    && rm -rf /var/lib/apt/lists/* \
    && gcc -dumpfullversion \
    && make --version >/dev/null

COPY timer.sh /app/timer.sh
WORKDIR /app
