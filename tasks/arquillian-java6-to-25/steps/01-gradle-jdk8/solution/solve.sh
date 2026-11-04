#!/usr/bin/env bash
# Golden solution, step 01 -- 01-gradle-jdk8.
#
# Replaces the 16-module Maven reactor with a Gradle build that publishes the
# same 43 artifacts under the same coordinates, compiled to Java 6 bytecode on
# JDK 8.
#
# Run inside the agent container, from anywhere.
set -euo pipefail

TREE=${TREE:-/app/arquillian}
cd "${TREE}"

# JDK 8 is what step 01 grades on, and `sourceCompatibility 1.6` needs a
# compiler that still accepts -source 1.6.
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/temurin-8-jdk}
export PATH="${JAVA_HOME}/bin:${PATH}"

V=1.0.0-SNAPSHOT
SW=1.0.0-alpha-12          # shrinkwrap, incl. the resolver modules
SWD=0.1.4                  # shrinkwrap-descriptors
JUNIT=4.8.1
TESTNG=5.14.6
MOCKITO=1.8.3
OSGI=4.2.0
JBOSS_LOGGING=2.1.0.GA
JBOSS_OSGI_SPI=1.0.10
JBOSS_OSGI_VFS=1.0.0
JETTY=6.1.9

# --------------------------------------------------------------------------
# `build/` is a Maven module (it publishes arquillian-build) and it is also the
# directory Gradle writes every project's output into. Renaming it once, here,
# is cheaper than overriding the build directory of sixteen projects and
# leaving the collision in the tree for every later step to trip over.
# --------------------------------------------------------------------------
if [ -d build ] && [ ! -d build-config ]; then
   mv build build-config
fi

# --------------------------------------------------------------------------
# settings.gradle
# --------------------------------------------------------------------------
cat > settings.gradle <<'EOF'
rootProject.name = 'arquillian-parent'

include 'build-config'
include 'api'
include 'spi'
include 'impl-base'
include 'junit'
include 'testng'
include 'protocols'
include 'protocols:servlet'
include 'protocols:jmx'
include 'protocols:jmx-osgi'
include 'testenrichers'
include 'testenrichers:cdi'
include 'testenrichers:ejb'
include 'testenrichers:osgi'
include 'testenrichers:resource'
EOF

# --------------------------------------------------------------------------
# Root build.gradle: everything common to all sixteen projects.
#
# The artifact ids do not follow the directory names (protocols/jmx publishes
# arquillian-protocol-jmx under group org.jboss.arquillian.protocol), so both
# are set explicitly rather than derived.
# --------------------------------------------------------------------------
cat > build.gradle <<EOF
// Gradle replacement for the Maven reactor. Coordinates, packaging and
// classifiers are the ones the reactor published; the bytecode is Java 6.

allprojects {
   version = '${V}'

   repositories {
      mavenLocal()
      mavenCentral()
      maven { url = uri('https://repository.jboss.org/nexus/content/groups/public/') }
   }
}

// The four aggregators publish a pom and nothing else, exactly as
// <packaging>pom</packaging> did.
def aggregators = [
   ':'                 : ['org.jboss.arquillian',              'arquillian-parent'],
   ':build-config'     : ['org.jboss.arquillian',              'arquillian-build'],
   ':protocols'        : ['org.jboss.arquillian.protocol',     'arquillian-protocol-parent'],
   ':testenrichers'    : ['org.jboss.arquillian.testenricher', 'arquillian-testenricher-parent'],
]

def libraries = [
   ':api'                      : ['org.jboss.arquillian',              'arquillian-api'],
   ':spi'                      : ['org.jboss.arquillian',              'arquillian-spi'],
   ':impl-base'                : ['org.jboss.arquillian',              'arquillian-impl-base'],
   ':junit'                    : ['org.jboss.arquillian',              'arquillian-junit'],
   ':testng'                   : ['org.jboss.arquillian',              'arquillian-testng'],
   ':protocols:servlet'        : ['org.jboss.arquillian.protocol',     'arquillian-protocol-servlet'],
   ':protocols:jmx'            : ['org.jboss.arquillian.protocol',     'arquillian-protocol-jmx'],
   ':protocols:jmx-osgi'       : ['org.jboss.arquillian.protocol',     'arquillian-protocol-jmx-osgi'],
   ':testenrichers:cdi'        : ['org.jboss.arquillian.testenricher', 'arquillian-testenricher-cdi'],
   ':testenrichers:ejb'        : ['org.jboss.arquillian.testenricher', 'arquillian-testenricher-ejb'],
   ':testenrichers:osgi'       : ['org.jboss.arquillian.testenricher', 'arquillian-testenricher-osgi'],
   ':testenrichers:resource'   : ['org.jboss.arquillian.testenricher', 'arquillian-testenricher-resource'],
]

// impl-base, junit and testng published a -tests jar; nothing else did.
def testJarProjects = [':impl-base', ':junit', ':testng'] as Set

aggregators.each { path, coords ->
   project(path) {
      apply plugin: 'maven-publish'
      group = coords[0]
      publishing {
         publications {
            maven(MavenPublication) {
               artifactId = coords[1]
               pom.packaging = 'pom'
            }
         }
      }
   }
}

libraries.each { path, coords ->
   project(path) {
      apply plugin: 'java-library'
      apply plugin: 'maven-publish'
      group = coords[0]

      java {
         // Java 6 bytecode -- class-file major version 50 -- built by JDK 8.
         sourceCompatibility = JavaVersion.VERSION_1_6
         targetCompatibility = JavaVersion.VERSION_1_6
         withSourcesJar()
      }

      tasks.withType(JavaCompile).configureEach {
         options.encoding = 'UTF-8'
         // -source/-target 1.6 is obsolete on JDK 8 and says so on every
         // invocation; the setting is deliberate, so the note is silenced
         // rather than left to scroll past.
         options.compilerArgs << '-Xlint:-options'
      }

      test {
         // The reactor's surefire ran these two patterns and no others.
         include '**/*TestCase.class'
         include '**/*TestSuite.class'
      }

      if (path in testJarProjects) {
         tasks.register('testJar', Jar) {
            archiveClassifier = 'tests'
            from sourceSets.test.output
         }
      }

      publishing {
         publications {
            maven(MavenPublication) {
               from components.java
               artifactId = coords[1]
               if (path in testJarProjects) {
                  artifact tasks.named('testJar')
               }
            }
         }
      }
   }
}
EOF

# --------------------------------------------------------------------------
# Per-module dependencies.
#
# Maven `compile`  -> api            (transitive, as it was)
# Maven `provided` -> compileOnly + testImplementation
#                     (Maven puts provided on the test classpath too)
# Maven `test`     -> testImplementation
# --------------------------------------------------------------------------
cat > api/build.gradle <<EOF
dependencies {
   api 'org.jboss.shrinkwrap:shrinkwrap-api:${SW}'
   api 'org.jboss.shrinkwrap.descriptors:shrinkwrap-descriptors-api:${SWD}'
}
EOF

cat > spi/build.gradle <<'EOF'
dependencies {
   api project(':api')
}
EOF

cat > impl-base/build.gradle <<EOF
dependencies {
   api project(':api')
   api project(':spi')
   api 'org.jboss.shrinkwrap:shrinkwrap-api:${SW}'
   api 'org.jboss.shrinkwrap:shrinkwrap-impl-base:${SW}'
   api 'org.jboss.shrinkwrap:shrinkwrap-spi:${SW}'
   api 'org.jboss.shrinkwrap.resolver:shrinkwrap-resolver-api:${SW}'
   api 'org.jboss.shrinkwrap.resolver:shrinkwrap-resolver-api-maven:${SW}'
   api 'org.jboss.shrinkwrap.resolver:shrinkwrap-resolver-impl-maven:${SW}'

   testImplementation 'org.mockito:mockito-all:${MOCKITO}'
   testImplementation 'junit:junit:${JUNIT}'
}
EOF

cat > junit/build.gradle <<EOF
dependencies {
   api project(':impl-base')
   compileOnly 'junit:junit:${JUNIT}'

   testImplementation 'junit:junit:${JUNIT}'
   testImplementation 'org.jboss.shrinkwrap:shrinkwrap-impl-base:${SW}'
}
EOF

cat > testng/build.gradle <<EOF
dependencies {
   api project(':impl-base')
   compileOnly 'org.testng:testng:${TESTNG}'

   testImplementation 'org.testng:testng:${TESTNG}'
   testImplementation 'junit:junit:${JUNIT}'
}

// This module's own suite is TestNG; every other module's is JUnit.
test {
   useTestNG()
}
EOF

cat > protocols/servlet/build.gradle <<EOF
dependencies {
   api project(':spi')
   api 'org.jboss.shrinkwrap:shrinkwrap-impl-base:${SW}'
   compileOnly 'jboss.web:servlet-api:3.0.0.alpha-25'

   testImplementation 'jboss.web:servlet-api:3.0.0.alpha-25'
   testImplementation 'junit:junit:${JUNIT}'
   testImplementation 'org.mortbay.jetty:jetty-embedded:${JETTY}'
}
EOF

cat > protocols/jmx/build.gradle <<EOF
dependencies {
   api project(':spi')
   api 'org.jboss.logging:jboss-logging-spi:${JBOSS_LOGGING}'

   testImplementation project(':junit')
   testImplementation 'junit:junit:${JUNIT}'
}
EOF

cat > protocols/jmx-osgi/build.gradle <<EOF
dependencies {
   api project(':spi')
   api 'org.jboss.osgi:jboss-osgi-spi:${JBOSS_OSGI_SPI}'
   api 'org.jboss.shrinkwrap:shrinkwrap-impl-base:${SW}'
   api 'org.jboss.shrinkwrap:shrinkwrap-spi:${SW}'
   compileOnly 'org.osgi:org.osgi.core:${OSGI}'

   testImplementation 'org.osgi:org.osgi.core:${OSGI}'
   testImplementation 'org.jboss.osgi.vfs:jboss-osgi-vfs30:${JBOSS_OSGI_VFS}'
   testImplementation 'org.mockito:mockito-all:${MOCKITO}'
   testImplementation 'junit:junit:${JUNIT}'
}
EOF

cat > testenrichers/cdi/build.gradle <<EOF
dependencies {
   api project(':spi')
   compileOnly 'javax.enterprise:cdi-api:1.0'
   compileOnly 'org.jboss.shrinkwrap:shrinkwrap-impl-base:${SW}'

   testImplementation 'javax.enterprise:cdi-api:1.0'
   testImplementation 'org.jboss.shrinkwrap:shrinkwrap-impl-base:${SW}'
   testImplementation 'junit:junit:${JUNIT}'
   testImplementation 'org.jboss.weld:weld-core:1.1.0.Beta1'
   testImplementation 'org.slf4j:slf4j-simple:1.5.10'
   testImplementation 'ch.qos.cal10n:cal10n-api:0.7.2'
   testImplementation 'javax.el:el-api:2.2'
}
EOF

cat > testenrichers/ejb/build.gradle <<EOF
dependencies {
   api project(':spi')
   compileOnly 'javax.ejb:ejb-api:3.0'

   testImplementation 'javax.ejb:ejb-api:3.0'
   testImplementation 'org.jboss.shrinkwrap:shrinkwrap-impl-base:${SW}'
   testImplementation 'junit:junit:${JUNIT}'
}
EOF

cat > testenrichers/osgi/build.gradle <<EOF
dependencies {
   api project(':spi')
   api 'javax.inject:javax.inject:1'
   api 'org.jboss.logging:jboss-logging-spi:${JBOSS_LOGGING}'
   compileOnly 'org.osgi:org.osgi.core:${OSGI}'

   testImplementation 'org.osgi:org.osgi.core:${OSGI}'
}
EOF

cat > testenrichers/resource/build.gradle <<EOF
dependencies {
   api project(':spi')
   compileOnly 'javax.annotation:jsr250-api:1.0'

   testImplementation 'javax.annotation:jsr250-api:1.0'
   testImplementation 'org.jboss.shrinkwrap:shrinkwrap-impl-base:${SW}'
   testImplementation 'junit:junit:${JUNIT}'
}
EOF

# --------------------------------------------------------------------------
# The Maven build is gone, not disabled.
# --------------------------------------------------------------------------
find . -name pom.xml -not -path './.git/*' -delete

# --------------------------------------------------------------------------
# A committed wrapper, so a clone with only a JDK can build the tree.
# Gradle 8 and not 9: Gradle 9 needs Java 17 to run, and this step's toolchain
# is Java 8.
# --------------------------------------------------------------------------
gradle --no-daemon wrapper --gradle-version 8.14.5 --distribution-type bin -q
./gradlew --no-daemon --version > /dev/null

echo "solve: step 01 complete"
