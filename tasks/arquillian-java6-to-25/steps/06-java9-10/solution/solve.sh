#!/usr/bin/env bash
# Golden solution, step 06 -- 06-java9-10.
#
# One transform over the tree step 05 left behind: 120 exact-text edits in
# 61 files. Nothing here compiles anything; the build that proves the edits is
# the one the step is graded by.
#
#    3  build.gradle: --release 10 in place of source/targetCompatibility
#    2  Class.newInstance() -> getDeclaredConstructor().newInstance()
#   12  if (!x.isAccessible()) { x.setAccessible(true); } guards deleted
#   12  new Integer/Boolean/Long(...) -> valueOf or a literal
#    7  Arrays.asList / unmodifiable wrappers -> List.of, Set.of, Map.of,
#       List.copyOf
#    3  isPresent/get ladders -> Optional.or and ifPresentOrElse
#   80  local variable declarations -> var
#    1  import added for the above
#
# WHY A LIST AND NOT A PROGRAM. Same argument as steps 04 and 05, with step
# 06's own examples, and this rung has the sharpest one. `var` is legal only
# where the initialiser fixes the type: not on a field, not on a `catch`
# parameter, not where the declared type is a supertype the code later assigns
# a second implementation to, and not where the initialiser is `null` or a
# lambda or an array initialiser. The 80 sites here were found by reading the
# scan's own candidate list and then deciding each one; the 81st candidate,
# DynamicServiceLoader.java:196, is deliberately left alone because its
# declared type is the interface and its initialiser is one implementation of
# it. A text program that rewrote every candidate would be right 80 times and
# wrong once, and the once would not compile. Likewise `Class.newInstance` has
# two sites and both of them change what the surrounding code *catches* --
# see below -- which is a question about the method's throws clause and its
# callers, not about the text of the call.
#
# So the finding happened at authoring time against the tree step 05 produces,
# and what ships is the result, with an md5 on every file both before and after
# as the proof that it landed on the tree it was computed for. apply8.py below
# says the rest.
#
# WHAT THE TWO newInstance SITES DO TO THE RECORDED OUTPUT, ON PURPOSE.
# Class.newInstance() propagates a constructor's checked exception unwrapped;
# getDeclaredConstructor().newInstance() wraps it in
# InvocationTargetException. Container and ProtocolDefinition both instantiate
# a user-supplied configuration class reflectively, and three of the recorded
# fixtures drive exactly that path with a configuration class that has no
# no-arg constructor, a constructor that throws unchecked, and a constructor
# that throws checked. Those three transcripts change, and they are the only
# three that change: the step's own recording era carries the new text, and the
# check that nothing else moved is a full diff of the golden's recording
# against the step-05 era -- three files, named, and nothing else. Do not add
# a catch that re-unwraps the cause to make the old text come back; the point
# of the rung is that the wrapper is now visible.
#
# WHY THE GUARDS GO AND THE setAccessible CALLS STAY. `isAccessible()` is
# deprecated from Java 9 because it answers a question about the flag, not
# about whether access would succeed, and on a module path it can be true where
# access still fails. The 12 guards deleted here each wrapped a
# `setAccessible(true)` that now runs unconditionally: 12 `isAccessible()` calls
# leave the tree, all 14 `setAccessible` calls stay, and the scan checks both
# figures.
#
# ONE SIDE EFFECT, EXPECTED AND FROZEN. Replacing a
# `new ArrayList<>(Arrays.asList(...))` or an `unmodifiableList(...)` with a
# factory deletes the diamond it carried: 74 -> 73, the single site being
# DeploymentScenario. The baseline freezes 73 as the arrival figure and
# authoring/verify-baseline-step06.sh checks the departure arithmetic.
#
# The blocks below are embedded verbatim from authoring/solution-src/apply8.py
# and authoring/evidence/step06-edits.txt by authoring/mk-solve06.py, which
# reads them back out of this file afterwards and diffs them against the
# sources. Edit the sources and re-run the generator; do not edit the blocks
# here.
#
# Run inside the agent container, from anywhere.
set -euo pipefail

TREE=${TREE:-/app/arquillian}
cd "${TREE}"

# ==========================================================================
# 1. The edits.
# ==========================================================================

EDITS=$(mktemp)
trap 'rm -f "${EDITS}"' EXIT
cat > "${EDITS}" <<'EDITS06'
@@FILE build.gradle
@@MD5IN 9072b29d35eb5dd6d7c9cd53e981186a
@@MD5OUT 7872ccaa61dd5171c9b99ea3893fbfbc
@@EDIT 1
@@OLD
      java {
         // Java 8 bytecode -- class-file major version 52 -- built by JDK 8.
         sourceCompatibility = JavaVersion.VERSION_1_8
         targetCompatibility = JavaVersion.VERSION_1_8
         withSourcesJar()
      }
@@NEW
      java {
         withSourcesJar()
      }
@@EDIT 1
@@OLD
      // Gradle turns targetCompatibility 1.6 into a hard requirement on
      // every resolved dependency -- it asks for org.gradle.jvm.version = 6 --
      // and both JUnit 5 and Mockito 4 declare themselves Java 8, so
      // resolution refuses them outright. That requirement is really a
      // statement about what this project *publishes*, and that has not
      // changed: -source/-target 1.6 still cap what javac emits, so every
      // class file this build produces, test classes included, stays at major
      // version 50. So the attribute is overridden on the four *resolvable*
      // classpaths only. The outgoing variants (apiElements, runtimeElements)
      // are untouched, which is why the published poms and module metadata are
      // byte-for-byte what step 01 published.
      ['compileClasspath', 'runtimeClasspath',
       'testCompileClasspath', 'testRuntimeClasspath'].each { cfg ->
         configurations.named(cfg) {
            attributes {
               attribute(org.gradle.api.attributes.java.TargetJvmVersion.TARGET_JVM_VERSION_ATTRIBUTE, 8)
@@NEW
      // Gradle turns the target level into a hard requirement on every
      // resolved dependency -- it asks for org.gradle.jvm.version = N -- so
      // the number here has to keep up with what the build emits, which is
      // now major version 54. The attribute is overridden on the four
      // *resolvable* classpaths only. The outgoing variants (apiElements,
      // runtimeElements) are untouched, which is why the published poms are
      // byte-for-byte what step 01 published.
      ['compileClasspath', 'runtimeClasspath',
       'testCompileClasspath', 'testRuntimeClasspath'].each { cfg ->
         configurations.named(cfg) {
            attributes {
               attribute(org.gradle.api.attributes.java.TargetJvmVersion.TARGET_JVM_VERSION_ATTRIBUTE, 10)
@@EDIT 1
@@OLD
         // -source/-target 1.6 is obsolete on JDK 8 and says so on every
         // invocation; the setting is deliberate, so the note is silenced
         // rather than left to scroll past.
         options.compilerArgs << '-Xlint:-options'
@@NEW
         // --release also pins the platform API the sources may call,
         // so the bootclasspath note -source/-target earned is gone
         // rather than silenced.
         options.release = 10
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/domain/Container.java
@@MD5IN cc1c0c25d84c9c6d21229e06c953dcc4
@@MD5OUT d76cb42be05867f9072995cd4ed79041
@@EDIT 1
@@OLD
      ContainerConfiguration config = deployableContainer.getConfigurationClass().newInstance();
@@NEW
      ContainerConfiguration config = deployableContainer.getConfigurationClass().getDeclaredConstructor().newInstance();
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/domain/ProtocolDefinition.java
@@MD5IN 06733c0a14bbf1e9fc49a5b8da232738
@@MD5OUT c28b2c0d1879d58a1786d6a6a084c81a
@@EDIT 1
@@OLD
      ProtocolConfiguration config = protocol.getProtocolConfigurationClass().newInstance();
@@NEW
      ProtocolConfiguration config = protocol.getProtocolConfigurationClass().getDeclaredConstructor().newInstance();
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java
@@MD5IN 362e7c8ef155a48a4fad3015a6516d75
@@MD5OUT 5436e7bec2ecdb6029152496ab3d9ad6
@@EDIT 1
@@OLD
      StringBuilder sb = new StringBuilder();
@@NEW
      var sb = new StringBuilder();
@@EDIT 1
@@OLD
                 BufferedReader reader = new BufferedReader(new InputStreamReader(is, "UTF-8")))
@@NEW
                 var reader = new BufferedReader(new InputStreamReader(is, "UTF-8")))
@@EDIT 1
@@OLD
         if (!constructor.isAccessible())
         {
            constructor.setAccessible(true);
         }
@@NEW
         constructor.setAccessible(true);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/EventPointImpl.java
@@MD5IN 78f56114860727ba141abc3683b6ddf8
@@MD5OUT f3c5592bb038681fbb1d368608ac5438
@@EDIT 1
@@OLD
         if(!field.isAccessible())
         {
            field.setAccessible(true);
         }
@@NEW
         field.setAccessible(true);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/InjectionPointImpl.java
@@MD5IN 210a98fdecfebd62051671d86e814dfc
@@MD5OUT 325751718088f5d7f9b3ad60451aa8b9
@@EDIT 1
@@OLD
         if(!field.isAccessible())
         {
            field.setAccessible(true);
         }
@@NEW
         field.setAccessible(true);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/Reflections.java
@@MD5IN 36ddfbc765c126e93e30bfc81a90d62d
@@MD5OUT a32e8a5895d3e663bd367e1e8fc06fc4
@@EDIT 1
@@OLD
      if(!constructor.isAccessible())
      {
         constructor.setAccessible(true);
      }
@@NEW
      constructor.setAccessible(true);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/ArquillianResourceTestEnricher.java
@@MD5IN 5a112e5e41a63038675526b62172e295
@@MD5OUT 7265ddf4d2032bece19faed8987ad0b3
@@EDIT 1
@@OLD
            if(!field.isAccessible())
            {
               field.setAccessible(true);            
            }
@@NEW
            field.setAccessible(true);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/SecurityActions.java
@@MD5IN 0b41b503bdffc242aef959073e34bb79
@@MD5OUT ffb0723d23b91dc46dbc626054d062f2
@@EDIT 1
@@OLD
                  if(!field.isAccessible()) 
                  {
                     field.setAccessible(true);
                  }
@@NEW
                  field.setAccessible(true);
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/util/ServiceLoader.java
@@MD5IN 13868851c91b497cae5454d179fb8af9
@@MD5OUT f9a8f3eeccaa601eceb9eea55e7ca6f6
@@EDIT 1
@@OLD
                    BufferedReader reader = new BufferedReader(new InputStreamReader(is, "UTF-8")))
@@NEW
                    var reader = new BufferedReader(new InputStreamReader(is, "UTF-8")))
@@EDIT 1
@@OLD
         if(!constructor.isAccessible()) {
            constructor.setAccessible(true);
         }
@@NEW
         constructor.setAccessible(true);
@@ENDFILE
@@FILE testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/SecurityActions.java
@@MD5IN 619ce1ae80785abc825c977b128078bd
@@MD5OUT f8b69b274656dbacfe07e09e871079fe
@@EDIT 1
@@OLD
               if(!field.isAccessible()) 
               {
                  field.setAccessible(true);
               }
@@NEW
               field.setAccessible(true);
@@ENDFILE
@@FILE testenrichers/ejb/src/main/java/org/jboss/arquillian/testenricher/ejb/SecurityActions.java
@@MD5IN 7e0c11d980602695fdcfe378437f6de8
@@MD5OUT b29e7a094a1b714b3023725648ecbef0
@@EDIT 1
@@OLD
                  if(!field.isAccessible()) 
                  {
                     field.setAccessible(true);
                  }
@@NEW
                  field.setAccessible(true);
@@EDIT 1
@@OLD
               if(!method.isAccessible()) 
               {
                  method.setAccessible(true);
               }
@@NEW
               method.setAccessible(true);
@@ENDFILE
@@FILE testenrichers/resource/src/main/java/org/jboss/arquillian/testenricher/resource/SecurityActions.java
@@MD5IN 5bbf7b99205e4eaab5d0044941645915
@@MD5OUT 338758bf71a0fbc4f8baa3bd179ba950
@@EDIT 1
@@OLD
                  if(!field.isAccessible()) 
                  {
                     field.setAccessible(true);
                  }
@@NEW
                  field.setAccessible(true);
@@EDIT 1
@@OLD
               if(!method.isAccessible()) 
               {
                  method.setAccessible(true);
               }
@@NEW
               method.setAccessible(true);
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/deployment/DeploymentScenario.java
@@MD5IN b952f44740fdfa3ba811270536ac9489
@@MD5OUT 7064352169250beca667ecea50c78350
@@EDIT 1
@@OLD
      Collections.sort(startupDeployments, (o1, o2) -> new Integer(o1.getOrder()).compareTo(o2.getOrder()));
@@NEW
      Collections.sort(startupDeployments, (o1, o2) -> Integer.compare(o1.getOrder(), o2.getOrder()));
@@EDIT 1
@@OLD
      return Collections.unmodifiableList(startupDeployments);
@@NEW
      return List.copyOf(startupDeployments);
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/ExceptionHandlingTestCase.java
@@MD5IN 81bc7249f18b8ee5c13cbedc6db2cc45
@@MD5OUT efd5d208a41b8ba4d6b5daf309303336
@@EDIT 1
@@OLD
         manager.fire(new Double(0.0));
@@NEW
         manager.fire(Double.valueOf(0.0));
@@EDIT 1
@@OLD
      Assertions.assertEquals(new Integer(1), TestEventFire.called);
@@NEW
      Assertions.assertEquals(Integer.valueOf(1), TestEventFire.called);
@@EDIT 1
@@OLD
      Assertions.assertEquals(new Integer(1), TestExceptionThrower.called);
@@NEW
      Assertions.assertEquals(Integer.valueOf(1), TestExceptionThrower.called);
@@EDIT 1
@@OLD
      Assertions.assertEquals(new Integer(1), TestExceptionHandler.called);
@@NEW
      Assertions.assertEquals(Integer.valueOf(1), TestExceptionHandler.called);
@@EDIT 1
@@OLD
         integer.fire(new Integer(10));
@@NEW
         integer.fire(Integer.valueOf(10));
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/IntegrationTestCase.java
@@MD5IN 2790ec0c77a894048bbf252c89a0c2cd
@@MD5OUT e3e84b14113b8c4c5a06718f5ab0ee16
@@EDIT 1
@@OLD
            object.set(new Float(2.2));
@@NEW
            object.set(Float.valueOf(2.2f));
@@EDIT 1
@@OLD
            event.fire(new Integer(100));
@@NEW
            event.fire(Integer.valueOf(100));
@@EDIT 1
@@OLD
         doub.set(new Double(2.0));
@@NEW
         doub.set(Double.valueOf(2.0));
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/context/ContextActivationTestCase.java
@@MD5IN ef7465e350cdcced1cd34e13b1a04e93
@@MD5OUT ce496d988110f5896f4559d074a6cf62
@@EDIT 2
@@OLD
         Assertions.assertEquals(new Boolean(true), store.get(Boolean.class), "Verify that we can get objects from a active context");
@@NEW
         Assertions.assertEquals(Boolean.valueOf(true), store.get(Boolean.class), "Verify that we can get objects from a active context");
@@EDIT 1
@@OLD
      final CountDownLatch latch = new CountDownLatch(1);
@@NEW
      final var latch = new CountDownLatch(1);
@@ENDFILE
@@FILE testenrichers/resource/src/test/java/org/jboss/arquillian/testenricher/resource/ResourceInjectionEnricherTestCase.java
@@MD5IN dd80f1cdc9141a6f461488bac750ce90
@@MD5OUT 6592630000424bd878cf2743dc68dacc
@@EDIT 1
@@OLD
      injectionValueMap.put("primitive_byte", new Byte("1"));
@@NEW
      injectionValueMap.put("primitive_byte", Byte.valueOf("1"));
@@EDIT 1
@@OLD
      InjectableTestClass testClass = new InjectableTestClass();
@@NEW
      var testClass = new InjectableTestClass();
@@ENDFILE
@@FILE junit/src/main/java/org/jboss/arquillian/junit/JUnitTestRunner.java
@@MD5IN 7d6ccd3d36f62ba5e95a2a5cb2277e29
@@MD5OUT 0fd6a656f289bdc05f54fac67ee7dad9
@@EDIT 1
@@OLD
      return Collections.emptyList();
@@NEW
      return List.of();
@@EDIT 1
@@OLD
      JUnitCore runner = new JUnitCore();
@@NEW
      var runner = new JUnitCore();
@@EDIT 1
@@OLD
      ExpectedExceptionHolder exceptionHolder = new ExpectedExceptionHolder();
@@NEW
      var exceptionHolder = new ExpectedExceptionHolder();
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/ArquillianProfile.java
@@MD5IN 4a7a255bc8fca0435b4642f97a5152a7
@@MD5OUT 2fec8c76dcb47f0abe065490a6fc08df
@@EDIT 1
@@OLD
import java.util.Collection;
@@NEW
import java.util.Collection;
import java.util.List;
@@EDIT 2
@@OLD
      return Arrays.asList
@@NEW
      return List.of
@@ENDFILE
@@FILE junit/src/main/java/org/jboss/arquillian/junit/Arquillian.java
@@MD5IN 7b1d027c4d5c1dcd2c5b467272256e67
@@MD5OUT 9744231844aad8caed70bb596eab8a91
@@EDIT 1
@@OLD
               throw new InitializationError(Arrays.asList((Throwable)e));
@@NEW
               throw new InitializationError(List.of((Throwable)e));
@@ENDFILE
@@FILE testng/src/main/java/org/jboss/arquillian/testng/TestNGTestRunner.java
@@MD5IN 6e62b0bbf902e542886f08dca12e773a
@@MD5OUT 200b244ddc96287e7de0929cedf3717a
@@EDIT 1
@@OLD
      TestListener resultListener = new TestListener();
@@NEW
      var resultListener = new TestListener();
@@EDIT 1
@@OLD
      TestNG runner = new TestNG(false);
@@NEW
      var runner = new TestNG(false);
@@EDIT 1
@@OLD
            Arrays.asList(createSuite(testClass, methodName)));
@@NEW
            List.of(createSuite(testClass, methodName)));
@@EDIT 1
@@OLD
      XmlSuite suite = new XmlSuite();
@@NEW
      var suite = new XmlSuite();
@@EDIT 1
@@OLD
      XmlTest test = new XmlTest(suite);
@@NEW
      var test = new XmlTest(suite);
@@EDIT 1
@@OLD
      XmlClass testClass = new XmlClass(className);
@@NEW
      var testClass = new XmlClass(className);
@@ENDFILE
@@FILE testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/MethodParameterInjectionPoint.java
@@MD5IN d08e669f3be0f292dc0ae4b0a1e88871
@@MD5OUT a5e99a79651d68cc625d65dcc451b577
@@EDIT 1
@@OLD
         return new HashSet<>(Arrays.asList(method.getParameterAnnotations()[position]));
@@NEW
         return Set.of(method.getParameterAnnotations()[position]);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/domain/ProtocolRegistry.java
@@MD5IN fb94e3d3ab878061c199f68a03888fa7
@@MD5OUT 389b31ef007444531ab04ebd53c5422c
@@EDIT 1
@@OLD
      if(declaredDefault.isPresent() || protocols.size() != 1)
      {
         return declaredDefault;
      }
      return Optional.of(protocols.get(0));
@@NEW
      return declaredDefault.or(() -> protocols.size() == 1
         ? Optional.of(protocols.get(0))
         : Optional.<ProtocolDefinition>empty());
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/ExceptionProxy.java
@@MD5IN 2124f3422b8095619a8307c99851c853
@@MD5OUT 6fb427374b455718d28a62f4061941b7
@@EDIT 1
@@OLD
		ArquillianProxyException exception = new ArquillianProxyException(message, className, reason,
@@NEW
		var exception = new ArquillianProxyException(message, className, reason,
@@EDIT 1
@@OLD
	private Optional<?> buildObjectFromClassConstructors(Class<?> clazz) {
		// try the (String,Throwable) constructor first
		Optional<?> object = buildExceptionFromConstructor(clazz, new Class<?>[] {
				String.class, Throwable.class }, new Object[] { message,
				getCause() });
		if (object.isPresent()) {
			return object;
		}

		// try the (String,Exception) constructor first
		object = buildExceptionFromConstructor(clazz, new Class<?>[] {
				String.class, Exception.class }, new Object[] { message,
				getCause() });
		if (object.isPresent()) {
			return object;
		}

		// try the (String) constructor next
		object = buildExceptionFromConstructor(clazz,
				new Class<?>[] { String.class }, new Object[] { message });
		if (object.isPresent()) {
			return object;
		}

		return Optional.empty();
	}
@@NEW
	private <T> Optional<T> buildObjectFromClassConstructors(Class<T> clazz) {
		// (String,Throwable), then (String,Exception), then (String)
		return buildExceptionFromConstructor(clazz, new Class<?>[] {
					String.class, Throwable.class }, new Object[] { message,
					getCause() })
				.or(() -> buildExceptionFromConstructor(clazz, new Class<?>[] {
						String.class, Exception.class }, new Object[] { message,
						getCause() }))
				.or(() -> buildExceptionFromConstructor(clazz,
						new Class<?>[] { String.class }, new Object[] { message }));
	}
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/container/ContainerRegistryCreator.java
@@MD5IN 9442a1329e4c2c0125869c83818b172b
@@MD5OUT f2847f465d509b4bf7ba004e21ca6674
@@EDIT 1
@@OLD
      ContainerRegistry reg = new ContainerRegistry();
@@NEW
      var reg = new ContainerRegistry();
@@EDIT 1
@@OLD
      if(activeConfiguration.isPresent())
      {
         for(GroupDef group : event.getGroups())
         {
            if(activeConfiguration.filter(name -> name.equals(group.getGroupName())).isPresent())
            {
               for(ContainerDef container : group.getGroupContainers())
               {
                  reg.create(container, serviceLoader);
               }
            }
         }
      }
      else if(reg.getContainers().size() == 0)
      {
         try
         {
            DeployableContainer<?> deployableContainer = serviceLoader.onlyOne(DeployableContainer.class);
            if(deployableContainer != null)
            {
               reg.create(new ContainerDefImpl("arquillian.xml").setContainerName("default"), serviceLoader);
            }
         } 
         catch (Exception e) 
         {
            // ignore
         }
      }
@@NEW
      activeConfiguration.ifPresentOrElse(
         name ->
         {
            for(GroupDef group : event.getGroups())
            {
               if(name.equals(group.getGroupName()))
               {
                  for(ContainerDef container : group.getGroupContainers())
                  {
                     reg.create(container, serviceLoader);
                  }
               }
            }
         },
         () ->
         {
            if(reg.getContainers().size() == 0)
            {
               try
               {
                  DeployableContainer<?> deployableContainer = serviceLoader.onlyOne(DeployableContainer.class);
                  if(deployableContainer != null)
                  {
                     reg.create(new ContainerDefImpl("arquillian.xml").setContainerName("default"), serviceLoader);
                  }
               }
               catch (Exception e)
               {
                  // ignore
               }
            }
         });
@@EDIT 1
@@OLD
         try (BufferedReader reader = new BufferedReader(
@@NEW
         try (var reader = new BufferedReader(
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/AnnotationDeploymentScenarioGenerator.java
@@MD5IN c4b8a9df497a28b1bcc355e70d5aec09
@@MD5OUT 7b7e6518d097807f4795e089d350aacb
@@EDIT 1
@@OLD
      DeploymentScenario scenario = new DeploymentScenario();
@@NEW
      var scenario = new DeploymentScenario();
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/tool/ToolingDeploymentFormatter.java
@@MD5IN dc34cd7bb2e9a06a185ca1341e964f0e
@@MD5OUT 2472a1d7addc86d3ab620ff670a3a004
@@EDIT 1
@@OLD
      StringBuilder xml = new StringBuilder();
@@NEW
      var xml = new StringBuilder();
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/protocol/ProtocolRegistryCreator.java
@@MD5IN 2373c9fce9311583fdf56e4fca4dd109
@@MD5OUT 74cc59c915fc5b2da14dc883842a795f
@@EDIT 1
@@OLD
      ProtocolRegistry registry = new ProtocolRegistry();
@@NEW
      var registry = new ProtocolRegistry();
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/GroupContainerDefImpl.java
@@MD5IN 3d015b12e2a802bb3dd8bc7185546b03
@@MD5OUT bf68bb37c84c2c4884506f3af496ae8f
@@EDIT 1
@@OLD
      GroupContainerDefImpl contianer = new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, group.create("container"));
@@NEW
      var contianer = new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, group.create("container"));
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/GroupDefImpl.java
@@MD5IN 508c311bb7875b720ebbb9cc69f7ab34
@@MD5OUT 85a5681431daa7078f79022449480e74
@@EDIT 1
@@OLD
      GroupContainerDefImpl contianer = new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, group.create("container"));
@@NEW
      var contianer = new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, group.create("container"));
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java
@@MD5IN 2281df0416132c8c61c1eaa2c1cc4e50
@@MD5OUT 3e103a2b89284bc1e0d31f1b5e3f8b09
@@EDIT 1
@@OLD
      StringBuilder sb = new StringBuilder();
@@NEW
      var sb = new StringBuilder();
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/context/AbstractContext.java
@@MD5IN 7ddb5c9dd31a36ccf10bc1c601360da7
@@MD5OUT f8b5397aaa0d75cc105ced6114c774c1
@@EDIT 1
@@OLD
      ObjectStore store = new ObjectStore();
@@NEW
      var store = new ObjectStore();
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/execution/LocalTestExecuter.java
@@MD5IN 3a62b654ca08e6487028ac44ffc5e863
@@MD5OUT 14564b440542e12499e5dfe1653b2da5
@@EDIT 1
@@OLD
      TestResult result = new TestResult();
@@NEW
      var result = new TestResult();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/EventTestRunnerAdaptorTestCase.java
@@MD5IN 22bcbfde817962f24f2749b5b99867d1
@@MD5OUT c4e879e16f416363041b1c32285599b8
@@EDIT 1
@@OLD
      EventTestRunnerAdaptor adaptor = new EventTestRunnerAdaptor(manager);
@@NEW
      var adaptor = new EventTestRunnerAdaptor(manager);
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/EventImplTestCase.java
@@MD5IN 45a0d8525b44a4be50e3466949d71a2d
@@MD5OUT 70b438f120cbc8ca8ee00674c5c603fe
@@EDIT 1
@@OLD
      Object testObject = new Object();
@@NEW
      var testObject = new Object();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/ExtensionParseTestCase.java
@@MD5IN 56e335811d1c73a103f6b6af4906a81f
@@MD5OUT 7a9bed65f19b926dd13bcfdc59aa9551
@@EDIT 1
@@OLD
      ExtensionWithObservers target = new ExtensionWithObservers();
@@NEW
      var target = new ExtensionWithObservers();
@@EDIT 1
@@OLD
      ExtensionWithInjection target = new ExtensionWithInjection();
@@NEW
      var target = new ExtensionWithInjection();
@@EDIT 1
@@OLD
      ExtensionWithEvent target = new ExtensionWithEvent();
@@NEW
      var target = new ExtensionWithEvent();
@@EDIT 1
@@OLD
      DummyEventImpl event = new DummyEventImpl();
@@NEW
      var event = new DummyEventImpl();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/InjectorTestCase.java
@@MD5IN a460082d937161a184d796bc7cac286c
@@MD5OUT d79187ff67e1c4e8ae283f31ce0f4ef2
@@EDIT 1
@@OLD
         TestStaticInjected target = new TestStaticInjected();
@@NEW
         var target = new TestStaticInjected();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/InstanceImplTestCase.java
@@MD5IN bb87343b2ed2ebb07df1112a2520b014
@@MD5OUT e97b9a9a673ef3f2672bf833cb894a20
@@EDIT 1
@@OLD
      Object testObject = new Object();
@@NEW
      var testObject = new Object();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/ManagerImplTestCase.java
@@MD5IN 87e6957ca03c022ba5da70e236574adf
@@MD5OUT b83221d21189ffe94c810fa3a35f1019
@@EDIT 1
@@OLD
         Object testObject = new Object();
@@NEW
         var testObject = new Object();
@@EDIT 1
@@OLD
      TestNonManagedObserver observer = new TestNonManagedObserver();
@@NEW
      var observer = new TestNonManagedObserver();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/domain/ContainerRegistryTestCase.java
@@MD5IN 53fbaee4bd29e4ab5b45947a43fd2e01
@@MD5OUT aaf470150787d5a24199630f721b5ebe
@@EDIT 5
@@OLD
      ContainerRegistry registry = new ContainerRegistry();
@@NEW
      var registry = new ContainerRegistry();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/enricher/resource/ArquillianTestEnricherContextTestCase.java
@@MD5IN 99da48183d38e5736290191022665797
@@MD5OUT e6058c24e9682e2400b4cff00f5a8008
@@EDIT 1
@@OLD
      URLBaseContextClass test = new URLBaseContextClass();
@@NEW
      var test = new URLBaseContextClass();
@@EDIT 1
@@OLD
      URLServletContextClass test = new URLServletContextClass();
@@NEW
      var test = new URLServletContextClass();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/enricher/resource/ArquillianTestEnricherURLTestCase.java
@@MD5IN 802dd764c55a122aff68913fdf8c0b74
@@MD5OUT 468669d7287c0b548ea30ebc719933d2
@@EDIT 2
@@OLD
      InitialContext context = new InitialContext();
@@NEW
      var context = new InitialContext();
@@EDIT 1
@@OLD
      ContextClass test = new ContextClass();
@@NEW
      var test = new ContextClass();
@@EDIT 1
@@OLD
      InitialContextClass test = new InitialContextClass();
@@NEW
      var test = new InitialContextClass();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/execution/LocalTestExecuterTestCase.java
@@MD5IN a208393aa2fc724d7a835b33ccdc8bfd
@@MD5OUT ba6069edbb45f6cb69c9d678695cf3c5
@@EDIT 1
@@OLD
      Exception exception = new Exception();
@@NEW
      var exception = new Exception();
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/execution/RemoteTestExecuterTestCase.java
@@MD5IN d40f893d8b6410190550f936647b021a
@@MD5OUT 6aedd510443c681e6c488a298494166e
@@EDIT 2
@@OLD
      TestResult result = new TestResult(Status.PASSED);
@@NEW
      var result = new TestResult(Status.PASSED);
@@EDIT 2
@@OLD
      org.jboss.arquillian.spi.event.suite.Test event = new org.jboss.arquillian.spi.event.suite.Test(testExecutor);
@@NEW
      var event = new org.jboss.arquillian.spi.event.suite.Test(testExecutor);
@@ENDFILE
@@FILE junit/src/test/java/org/jboss/arquillian/junit/JUnitIntegrationTestCase.java
@@MD5IN fc13a686fd096274216ba8887f0e789e
@@MD5OUT dfa43c2573130c292fd22f94b53d7234
@@EDIT 1
@@OLD
      JUnitCore runner = new JUnitCore();
@@NEW
      var runner = new JUnitCore();
@@ENDFILE
@@FILE junit/src/test/java/org/jboss/arquillian/junit/JUnitTestRunnerTestCase.java
@@MD5IN 25814fd220291a6ed074c0958e495daf
@@MD5OUT 1f5911a6a24977ffb70ae11c4a3ff592
@@EDIT 2
@@OLD
      JUnitTestRunner runner = new JUnitTestRunner();
@@NEW
      var runner = new JUnitTestRunner();
@@ENDFILE
@@FILE protocols/jmx/src/main/java/org/jboss/arquillian/protocol/jmx/JMXMethodExecutor.java
@@MD5IN d32097737ad69b0213cfc93458ef8138
@@MD5OUT 463a60716f45a6d44cf5be6fea660083
@@EDIT 1
@@OLD
         ObjectName objectName = new ObjectName(JMXTestRunnerMBean.OBJECT_NAME);
@@NEW
         var objectName = new ObjectName(JMXTestRunnerMBean.OBJECT_NAME);
@@EDIT 1
@@OLD
         ObjectInputStream ois = new ObjectInputStream(resultStream);
@@NEW
         var ois = new ObjectInputStream(resultStream);
@@ENDFILE
@@FILE protocols/jmx/src/main/java/org/jboss/arquillian/protocol/jmx/JMXTestRunner.java
@@MD5IN f6a91a149cbb0b255c62406d19aab0d6
@@MD5OUT 52308493c88860217bed25d2f8864b4d
@@EDIT 2
@@OLD
      ObjectName oname = new ObjectName(JMXTestRunnerMBean.OBJECT_NAME);
@@NEW
      var oname = new ObjectName(JMXTestRunnerMBean.OBJECT_NAME);
@@EDIT 1
@@OLD
         ByteArrayOutputStream baos = new ByteArrayOutputStream();
@@NEW
         var baos = new ByteArrayOutputStream();
@@EDIT 1
@@OLD
         try (ObjectOutputStream oos = new ObjectOutputStream(baos))
@@NEW
         try (var oos = new ObjectOutputStream(baos))
@@ENDFILE
@@FILE protocols/jmx/src/test/java/org/jboss/arquillian/jmx/JMXTestRunnerTestCase.java
@@MD5IN 7fa132897c71be39e76c68c456a921b7
@@MD5OUT 29068993cd9c2d65e446a0a2a7ec5913
@@EDIT 1
@@OLD
      JMXTestRunner jmxTestRunner = new JMXTestRunner(null);
@@NEW
      var jmxTestRunner = new JMXTestRunner(null);
@@ENDFILE
@@FILE protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/ServletMethodExecutor.java
@@MD5IN f21ab8d99d74a20796cbc15a68817f64
@@MD5OUT b8826f79d7e2f82cb6f16aa2688653d3
@@EDIT 1
@@OLD
               try (ObjectInputStream ois = new ObjectInputStream(httpConnection.getInputStream()))
@@NEW
               try (var ois = new ObjectInputStream(httpConnection.getInputStream()))
@@ENDFILE
@@FILE protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/runner/ServletTestRunner.java
@@MD5IN aac13ca6711b242600871e2ddb062cca
@@MD5OUT a986481a37dc1f943f5b6549961e10ce
@@EDIT 1
@@OLD
         try (ObjectOutputStream oos = new ObjectOutputStream(response.getOutputStream()))
@@NEW
         try (var oos = new ObjectOutputStream(response.getOutputStream()))
@@ENDFILE
@@FILE protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/v_2_5/ServletProtocolDeploymentPackager.java
@@MD5IN d65ede43cec835ab64e64cbe4a2dda30
@@MD5OUT 5d7a92b443a3fa82c02b584fed106bc2
@@EDIT 1
@@OLD
      Processor processor = new Processor(testDeployment, processors);
@@NEW
      var processor = new Processor(testDeployment, processors);
@@ENDFILE
@@FILE protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/v_3/ServletProtocolDeploymentPackager.java
@@MD5IN ef8dea985823651218534535bd25f47f
@@MD5OUT 2c41ffe4be64f05e73cabc027fba25a4
@@EDIT 1
@@OLD
      Processor processor = new Processor(testDeployment, processors);
@@NEW
      var processor = new Processor(testDeployment, processors);
@@ENDFILE
@@FILE protocols/servlet/src/test/java/org/jboss/arquillian/protocol/servlet/BaseServletProtocolTestCase.java
@@MD5IN 545176a212d6a2e2107cc69ed48ddcc2
@@MD5OUT 91c6f5bda5de9eed82663fe16766cd19
@@EDIT 2
@@OLD
      ServletProtocolConfiguration config = new ServletProtocolConfiguration();
@@NEW
      var config = new ServletProtocolConfiguration();
@@EDIT 1
@@OLD
      ServletProtocol protocol = new ServletProtocol();
@@NEW
      var protocol = new ServletProtocol();
@@ENDFILE
@@FILE protocols/servlet/src/test/java/org/jboss/arquillian/protocol/servlet/ProtocolTestCase.java
@@MD5IN 7c5bc5ddba6e180d6973f9264658850a
@@MD5OUT f3528cd636ffc184cef57305728e9b98
@@EDIT 1
@@OLD
      Context root = new Context(server, "/arquillian-protocol", Context.SESSIONS);
@@NEW
      var root = new Context(server, "/arquillian-protocol", Context.SESSIONS);
@@EDIT 2
@@OLD
      ServletMethodExecutor executor = new ServletMethodExecutor(createBaseURL());
@@NEW
      var executor = new ServletMethodExecutor(createBaseURL());
@@EDIT 1
@@OLD
      StringBuilder url = new StringBuilder(createBaseURL().toASCIIString() + "/ArquillianServletRunner");
@@NEW
      var url = new StringBuilder(createBaseURL().toASCIIString() + "/ArquillianServletRunner");
@@ENDFILE
@@FILE protocols/servlet/src/test/java/org/jboss/arquillian/protocol/servlet/TestUtil.java
@@MD5IN 69419196110ab82b39a2117335b4b5ee
@@MD5OUT ed979c639b428143e5f24d74d5b78c6a
@@EDIT 1
@@OLD
      try (ObjectInputStream input = new ObjectInputStream(url.openStream()))
@@NEW
      try (var input = new ObjectInputStream(url.openStream()))
@@EDIT 1
@@OLD
           ByteArrayOutputStream out = new ByteArrayOutputStream())
@@NEW
           var out = new ByteArrayOutputStream())
@@ENDFILE
@@FILE testenrichers/cdi/src/test/java/org/jboss/arquillian/testenricher/cdi/CDIInjectionEnricherTestCase.java
@@MD5IN 5c200706b6efd2d2a0f6a81e854ae124
@@MD5OUT 90677c91b6ded270e837a257a17a40e0
@@EDIT 4
@@OLD
      TestClass testClass = new TestClass();
@@NEW
      var testClass = new TestClass();
@@ENDFILE
@@FILE testenrichers/ejb/src/main/java/org/jboss/arquillian/testenricher/ejb/EJBInjectionEnricher.java
@@MD5IN afdd1e3ab1f4e150b22dc6cb9a55e7b0
@@MD5OUT c20ac269373ed1766ca859728a14656c
@@EDIT 1
@@OLD
      StringBuilder sb = new StringBuilder();
@@NEW
      var sb = new StringBuilder();
@@ENDFILE
@@FILE testenrichers/osgi/src/main/java/org/jboss/arquillian/testenricher/osgi/OSGiTestEnricher.java
@@MD5IN 0cd35a045bea3a36b3f3010d3fd59647
@@MD5OUT 5b40b63a74b40c479b07f3aa3bf4ed1e
@@EDIT 1
@@OLD
         ObjectName oname = new ObjectName(BundleContextHolder.OBJECT_NAME);
@@NEW
         var oname = new ObjectName(BundleContextHolder.OBJECT_NAME);
@@ENDFILE
@@FILE testng/src/test/java/org/jboss/arquillian/testng/TestNGTestRunnerTestCase.java
@@MD5IN a8d4bd93d8b097bb73b0d847d3e68ccb
@@MD5OUT 58737b045a129097d654624b90e47bae
@@EDIT 2
@@OLD
      TestNGTestRunner runner = new TestNGTestRunner();
@@NEW
      var runner = new TestNGTestRunner();
@@ENDFILE
EDITS06

# Captured rather than streamed, because the line it prints is the assertion:
# apply8.py refuses a tree whose files do not arrive at the md5 the edits were
# computed against, and refuses a file that does not leave at the md5 the
# generator computed -- but a solve.sh that let the one summary line scroll past
# unread would still be trusting that it ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it, the commonest failure -- run against a tree that is not the one step 05
# produces -- kills the script on the line below with apply8.py's whole
# explanation still sitting unprinted inside ${ap}. The grep two lines further
# down is what decides; the exit status of the substitution is not needed and
# swallowing the diagnosis to get at it is a bad trade.
ap=$(python3 - "${TREE}" "${EDITS}" 2>&1 <<'PY_APPLY8'
#!/usr/bin/env python3
"""Apply a list of exact-text edits to a tree, and prove each one landed.

   apply8.py <tree> <edits>

Step 03's transforms were programs: `diamond.py` found its own sites by
balancing brackets, and it found them in whatever tree it was pointed at. This
one is a list, and the difference is deliberate.

The sites the steps that use this program edit were found against the *typed*
AST, by a javac plugin. For step 04 that is `arqgate.Plan`, which knows that
`Runnable` has one abstract method, that a body naming `this` cannot become a
lambda, and that `store.add(key, value)` is a put rather than an append; for
step 05 it is `arqgate.SourceScan`, which knows which of two dozen
null-returning methods are `private` and which are published API, and which
collection a `forEach(sink::add)` accumulates into. None of that is recoverable
from text, and a text program that approximated it would be a worse answer key
than a list: it would be right about most sites and quietly wrong about a few,
and the few would be exactly the ones the gate is scored on.

So the finding is done at authoring time, against the tree the step before
produces, and what ships is the result. What ships is therefore also a hostage
to that tree: change the previous step's solution by one byte and these anchors
describe code that no longer exists. That is the reason for every assertion
below.

THE EDIT FILE. A flat, readable format -- a reviewer has to be able to see what
this does without running it:

    @@FILE <path relative to the tree>
    @@MD5IN <md5 of the file as it must arrive>
    @@MD5OUT <md5 of the file as it must leave>
    @@EDIT <n>
    @@OLD
    <the exact text to replace, n occurrences of it>
    @@NEW
    <what replaces it>
    @@EDIT ...
    @@ENDFILE

`@@EDIT n` is the occurrence count and is checked: two anonymous
`PrivilegedExceptionAction`s with identical bodies in one file are a real thing
in this tree, and "replace the first one" would leave the second behind while
reporting success.

WHY BOTH MD5S. MD5IN turns "this anchor is missing" into "this is not the tree
these edits were computed against", which is the difference between a bug in
the list and a change upstream of it. MD5OUT is the one that cannot be argued
with: the file is required to end up byte-for-byte as the generator computed it,
so no combination of interacting edits, re-matched replacements or
mis-sequenced records can leave a tree that merely looks edited. Neither is a
substitute for the per-edit count -- both md5s would still pass if a record
were dropped from the file *and* MD5OUT recomputed, which is why the generator
that writes them is not this program.
"""
import hashlib
import io
import os
import sys

MARKS = ('@@FILE', '@@MD5IN', '@@MD5OUT', '@@EDIT', '@@OLD', '@@NEW',
         '@@ENDFILE')


def die(msg):
    sys.stderr.write('apply8.py: %s\n' % msg)
    sys.exit(1)


def parse(path):
    """The edit file as [(relpath, md5in, md5out, [(count, old, new), ...])].

    Hand-rolled rather than a format with a library, because the payload is
    Java source: any encoding that escapes it stops a reviewer reading it, and
    any format with its own quoting rules adds a way for the generator and this
    parser to disagree. The marks are checked for absence from every payload by
    the generator, so a line that looks like a mark here IS one.
    """
    lines = io.open(path, encoding='utf-8').read().split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    files, i, n = [], 0, len(lines)
    while i < n:
        line = lines[i]
        if not line.startswith('@@FILE '):
            die('line %d: expected @@FILE, got %r' % (i + 1, line[:60]))
        rel = line[len('@@FILE '):]
        i += 1
        if not lines[i].startswith('@@MD5IN '):
            die('line %d: expected @@MD5IN' % (i + 1))
        md5in = lines[i][len('@@MD5IN '):]
        i += 1
        if not lines[i].startswith('@@MD5OUT '):
            die('line %d: expected @@MD5OUT' % (i + 1))
        md5out = lines[i][len('@@MD5OUT '):]
        i += 1
        edits = []
        while i < n and lines[i].startswith('@@EDIT '):
            count = int(lines[i][len('@@EDIT '):])
            i += 1
            if lines[i] != '@@OLD':
                die('line %d: expected @@OLD' % (i + 1))
            i += 1
            old = []
            while lines[i] != '@@NEW':
                old.append(lines[i])
                i += 1
            i += 1
            new = []
            while lines[i] not in ('@@EDIT', '@@ENDFILE') \
                    and not lines[i].startswith('@@EDIT '):
                new.append(lines[i])
                i += 1
            edits.append((count, '\n'.join(old), '\n'.join(new)))
        if i >= n or lines[i] != '@@ENDFILE':
            die('line %d: expected @@ENDFILE' % (i + 1))
        i += 1
        if not edits:
            die('%s: no edits' % rel)
        files.append((rel, md5in, md5out, edits))
    return files


def main():
    if len(sys.argv) != 3:
        die('usage: apply8.py <tree> <edits>')
    tree, edits_path = sys.argv[1], sys.argv[2]
    files = parse(edits_path)

    n_edits = 0
    for rel, md5in, md5out, edits in files:
        path = os.path.join(tree, rel)
        if not os.path.isfile(path):
            die('%s is not in the tree' % rel)
        # newline='' both ways. Two files in this tree have CRLF endings, and
        # Python's default translation would rewrite every line of them -- a
        # diff of 200 lines where the edit is 3, and an md5 that never matches.
        raw = io.open(path, 'rb').read()
        got = hashlib.md5(raw).hexdigest()
        if got != md5in:
            die('%s arrived as md5 %s, these edits were computed against %s\n'
                '           -- the tree this ran on is not the tree these '
                'edits were computed against' % (rel, got, md5in))
        text = raw.decode('utf-8')

        for count, old, new in edits:
            seen = text.count(old)
            if seen != count:
                die('%s: the anchor occurs %d time(s), expected %d\n'
                    '           anchor: %s' % (rel, seen, count,
                                               old.split('\n')[0][:70]))
            text = text.replace(old, new)
            n_edits += count

        out = text.encode('utf-8')
        if hashlib.md5(out).hexdigest() != md5out:
            die('%s: edits applied but the result is md5 %s, wanted %s'
                % (rel, hashlib.md5(out).hexdigest(), md5out))
        io.open(path, 'wb').write(out)

    sys.stderr.write('apply8.py: %d edit(s) in %d file(s), every result md5 '
                     'as computed\n' % (n_edits, len(files)))


if __name__ == '__main__':
    main()
PY_APPLY8
) || true
printf '%s\n' "${ap}"
want='apply8.py: 120 edit(s) in 61 file(s), every result md5 as computed'
if ! printf '%s\n' "${ap}" | grep -qxF "${want}"; then
   echo "solve06: the edits did not all apply" >&2
   exit 1
fi

# ==========================================================================
# 2. The compile level, which this step DOES move.
# ==========================================================================
# Three of the 120 edits above are in build.gradle, and between them they take
# the level from the source/targetCompatibility pair step 04 set to 1.8 up to
# `options.release = 10` -- major version 54. Re-read off the file rather than
# assumed from the fact that apply8.py printed its summary line, because the
# md5 apply8.py checks is a statement about the whole file and this is the one
# property of it the rest of the task is scored on.
n=$(grep -c -F 'options.release = 10' build.gradle || true)
if [ "${n}" != 1 ]; then
   echo "solve06: build.gradle has ${n} 'options.release = 10' line(s), wanted 1" >&2
   exit 1
fi
for lvl in sourceCompatibility targetCompatibility; do
   n=$(grep -c -F "${lvl}" build.gradle || true)
   if [ "${n}" != 0 ]; then
      echo "solve06: build.gradle still has ${n} ${lvl} line(s), wanted 0" >&2
      exit 1
   fi
done
# The resolvable classpaths carry org.gradle.jvm.version as a hard requirement
# on every dependency. Left at 8 it does not fail the build -- it silently asks
# resolution for artifacts a level below what this build now emits -- so it is
# checked here rather than left to surface as a dependency that stops resolving
# several rungs later.
n=$(grep -c -F 'TARGET_JVM_VERSION_ATTRIBUTE, 10' build.gradle || true)
if [ "${n}" != 1 ]; then
   echo "solve06: build.gradle has ${n} TargetJvmVersion line(s) at 10, wanted 1" >&2
   exit 1
fi
echo "solve06: build.gradle now at --release 10"

echo "solve: step 06 complete"
