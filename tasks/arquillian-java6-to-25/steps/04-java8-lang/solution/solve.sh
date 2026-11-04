#!/usr/bin/env bash
# Golden solution, step 04 -- 04-java8-lang.
#
# Two transforms over the tree step 03 left behind, and one knob in the root
# build script. Nothing here compiles anything: the edits are text in and text
# out, and the build that proves them is the one the step is graded by.
#
#   1. 86 exact-text edits in 55 files
#        36 anonymous single-abstract-method classes -> lambdas
#        24 interfaces -> @FunctionalInterface
#        24 collecting for-loops -> stream pipelines
#        2 java.util.Arrays imports for the array-sourced pipelines
#   2. source/target 1.8      class-file major 51 -> 52
#
# WHY A LIST AND NOT A PROGRAM. The sites were found by a javac plugin reading
# the typed AST -- which interface a `new X(){}` implements and how many
# abstract methods it has, whether a body names its own `this`, whether an
# `add` takes one argument or two. A text program that approximated that would
# be right about most sites and quietly wrong about a few, and the few would be
# exactly the ones this step is scored on. So the finding happened at authoring
# time against the tree step 03 produces, and what ships is the result, with an
# md5 on every file both before and after as the proof that it landed on the
# tree it was computed for. apply8.py below says the rest.
#
# WHY THIRTEEN LAMBDAS CARRY A CAST. All thirteen are the sole argument of
# AccessController.doPrivileged, which is overloaded on PrivilegedAction and
# PrivilegedExceptionAction -- two unrelated functional interfaces with the
# same function type. An anonymous class chooses by naming its supertype; an
# implicitly typed lambda names nothing, and JLS 15.12.2.2 does not let the
# compiler look inside the body to choose. A cast to the interface the
# anonymous class already named is the fix, and it is the only one. The list is
# authoring/evidence/step04-lambda-cast.txt.
#
# WHY ONE LAMBDA BODY RENAMES A LOCAL. An anonymous class body opens a scope of
# its own, so a local in it may shadow a parameter of the enclosing method; a
# lambda body shares the enclosing scope, so the same local collides.
# JMXTestRunner's constructor takes `TestClassLoader classLoader` and the
# anonymous body declared its own `ClassLoader classLoader`, so the conversion
# renames it to `loader`, which is what the rest of that file calls a bare
# ClassLoader. The record is authoring/evidence/step04-lambda-manual.txt.
#
# WHAT IS DELIBERATELY NOT HERE. The loops become `.forEach(dst::add)` and not
# `.collect(Collectors.toList())`. Collectors belong to step 05, and spending
# them here would leave that step a row it cannot fill. Two `for` loops in
# ManagerImpl that have the collecting shape exactly are also left alone: one
# calls a two-argument `add`, which is a put and not an append, and one calls a
# method declaring a checked exception, which a lambda body may not throw. The
# gate does not count either of them, for the same reason.
#
# The blocks below are embedded verbatim from authoring/solution-src/*.py and
# authoring/evidence/step04-edits.txt by authoring/mk-solve04.py, which reads
# them back out of this file afterwards and diffs them against the sources.
# Edit the sources and re-run the generator; do not edit the blocks here.
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
cat > "${EDITS}" <<'EDITS04'
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java
@@MD5IN 62a6bf3966357b2b16e1e0db615c32c6
@@MD5OUT 74229c373928c4f71422bc6abae0ce9c
@@EDIT 1
@@OLD
      for(Class<? extends T> serviceClass: providers)
      {
         providerImpls.add(createInstance(serviceClass));
      }
@@NEW
      providers.stream()
         .map(serviceClass -> createInstance(serviceClass))
         .forEach(providerImpls::add);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/SecurityActions.java
@@MD5IN 97061df1ed58eb5a71df9597e2e10c68
@@MD5OUT 1357740d61d479354d1ee5a6d84ea199
@@EDIT 1
@@OLD
         return AccessController.doPrivileged(new PrivilegedExceptionAction<Constructor<? extends T>>()
         {
            public Constructor<? extends T> run() throws NoSuchMethodException
            {
               return clazz.getConstructor(argumentTypes);
            }
         }
@@NEW
         return AccessController.doPrivileged(
            (PrivilegedExceptionAction<Constructor<? extends T>>) () -> clazz.getConstructor(argumentTypes)
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/container/ContainerDeployController.java
@@MD5IN 95071343244c86a07f872d5560f0cf55
@@MD5OUT d705aa3eb58456a24bcd588ddad4a20b
@@EDIT 1
@@OLD
   public interface Operation<T, X>
@@NEW
   @FunctionalInterface
   public interface Operation<T, X>
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/container/ContainerLifecycleController.java
@@MD5IN 7cb1b9dd4b936f49597825edd1a3649a
@@MD5OUT 9d6a9084491c48d0c511f1c469822046
@@EDIT 1
@@OLD
   public interface Operation<T>
@@NEW
   @FunctionalInterface
   public interface Operation<T>
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/DeploymentGenerator.java
@@MD5IN bcf43f5dddb1791bdfc46bdcf4a65f04
@@MD5OUT 986abf99a629fe3a8665f3301eb47df1
@@EDIT 1
@@OLD
         for(AuxiliaryArchiveAppender archiveAppender : archiveAppenders)
         {
            archives.add(archiveAppender.createAuxiliaryArchive());
         }
@@NEW
         archiveAppenders.stream()
            .map(archiveAppender -> archiveAppender.createAuxiliaryArchive())
            .forEach(archives::add);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/ArquillianDescriptorImpl.java
@@MD5IN 38dd5373769af56de5acb7349000fd82
@@MD5OUT 60ce12a14f752b8c2033d18a7f98b093
@@EDIT 1
@@OLD
      for(Node container : model.get("container"))
      {
         containers.add(new ContainerDefImpl(getDescriptorName(), model, container));
      }
@@NEW
      model.get("container").stream()
         .map(container -> new ContainerDefImpl(getDescriptorName(), model, container))
         .forEach(containers::add);
@@EDIT 1
@@OLD
      for(Node group : model.get("group"))
      {
         groups.add(new GroupDefImpl(getDescriptorName(), model, group));
      }
@@NEW
      model.get("group").stream()
         .map(group -> new GroupDefImpl(getDescriptorName(), model, group))
         .forEach(groups::add);
@@EDIT 1
@@OLD
      for(Node extension: model.get("extension"))
      {
         extensions.add(new ExtensionDefImpl(getDescriptorName(), model, extension));
      }
@@NEW
      model.get("extension").stream()
         .map(extension -> new ExtensionDefImpl(getDescriptorName(), model, extension))
         .forEach(extensions::add);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/ContainerDefImpl.java
@@MD5IN 0b20438117cff259a99be9a9a4903ef5
@@MD5OUT 057d3a03530f6ff3ce9d7c1c92420955
@@EDIT 1
@@OLD
      for(Node proto : container.get("protocol"))
      {
         protocols.add(new ProtocolDefImpl(getDescriptorName(), getRootNode(), container, proto));
      }
@@NEW
      container.get("protocol").stream()
         .map(proto -> new ProtocolDefImpl(getDescriptorName(), getRootNode(), container, proto))
         .forEach(protocols::add);
@@EDIT 1
@@OLD
         for(Node dep : container.getSingle("dependencies").get("dependency"))
         {
            dependencies.add(dep.text());
         }
@@NEW
         container.getSingle("dependencies").get("dependency").stream()
            .map(dep -> dep.text())
            .forEach(dependencies::add);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/GroupContainerDefImpl.java
@@MD5IN c121644f1dbc0efa78bd07109952d605
@@MD5OUT d4fe678ec5c891ea9f5db27abacc8498
@@EDIT 1
@@OLD
      for(Node container : group.get("container"))
      {
         containers.add(new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, container));
      }
@@NEW
      group.get("container").stream()
         .map(container -> new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, container))
         .forEach(containers::add);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/GroupDefImpl.java
@@MD5IN 7f06d7405304950182cb9d39de28ad10
@@MD5OUT 68b50b266be086a45800df4c16dd538a
@@EDIT 1
@@OLD
      for(Node container : group.get("container"))
      {
         containers.add(new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, container));
      }
@@NEW
      group.get("container").stream()
         .map(container -> new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, container))
         .forEach(containers::add);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/ExtensionImpl.java
@@MD5IN 0fdd9b8e8ceb67057bbf37f156dd235b
@@MD5OUT b4e0333c48fe8433dec42ae6d44b4ef4
@@EDIT 1
@@OLD
      for(Method method : observerMethods)
      {
         result.add(ObserverImpl.of(extension, method));
      }
@@NEW
      observerMethods.stream()
         .map(method -> ObserverImpl.of(extension, method))
         .forEach(result::add);
@@EDIT 1
@@OLD
      for(Field field : injectionPoints)
      {
         result.add(InjectionPointImpl.of(extension, field));
      }
@@NEW
      injectionPoints.stream()
         .map(field -> InjectionPointImpl.of(extension, field))
         .forEach(result::add);
@@EDIT 1
@@OLD
      for(Field method : eventPoints)
      {
         result.add(EventPointImpl.of(extension, method));
      }
@@NEW
      eventPoints.stream().map(method -> EventPointImpl.of(extension, method)).forEach(result::add);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java
@@MD5IN 0f7d0abdcd86cb76486e244311cfb089
@@MD5OUT 2d0a66e6ca5b86a713dbe3817970eb4b
@@EDIT 1
@@OLD
         for(ObserverMethod observer : extension.getObservers())
         {
            if(Reflections.getType(observer.getType()).isAssignableFrom(eventType) && !Reflections.isType(observer.getType(), EventContext.class))
            {
               observers.add(observer);
            }
         }
@@NEW
         extension.getObservers().stream()
            .filter(observer -> Reflections.getType(observer.getType()).isAssignableFrom(eventType) && !Reflections.isType(observer.getType(), EventContext.class))
            .forEach(observers::add);
@@EDIT 1
@@OLD
      for(Context context : contexts)
      {
         if(context.isActive())
         {
            activeContexts.add(context);
         }
      }
@@NEW
      contexts.stream().filter(context -> context.isActive()).forEach(activeContexts::add);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/Reflections.java
@@MD5IN dad21079cf82e2b6bc4f0f414b6f8344
@@MD5OUT 0f243c7f5b65d420ce8b671b0d0ec3a4
@@EDIT 1
@@OLD
      for(Method method : clazz.getMethods())
      {
         if(isObserverMethod(method))
         {
            observerMethods.add(method);
         }
      }
@@NEW
      Arrays.stream(clazz.getMethods())
         .filter(method -> isObserverMethod(method))
         .forEach(observerMethods::add);
@@EDIT 1
@@OLD
      for(Field field : clazz.getDeclaredFields())
      {
         if(isInjectionPoint(field))
         {
            injectionPoints.add(field);
         }
      }
@@NEW
      Arrays.stream(clazz.getDeclaredFields())
         .filter(field -> isInjectionPoint(field))
         .forEach(injectionPoints::add);
@@EDIT 1
@@OLD
      for(Field field : clazz.getDeclaredFields())
      {
         if(isEventPoint(field))
         {
            eventPoints.add(field);
         }
      }
@@NEW
      Arrays.stream(clazz.getDeclaredFields())
         .filter(field -> isEventPoint(field))
         .forEach(eventPoints::add);
@@EDIT 1
@@OLD
import java.util.List;
@@NEW
import java.util.Arrays;
import java.util.List;
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/spi/NonManagedObserver.java
@@MD5IN d7ab83cb6e6ef497bef6cfcdad24bdf1
@@MD5OUT 8c6b34e440a90386f8462b60b9b55182
@@EDIT 1
@@OLD
public interface NonManagedObserver<T>
@@NEW
@FunctionalInterface
public interface NonManagedObserver<T>
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/spi/Typed.java
@@MD5IN 31d3f2150db1c41e574c7def727d85db
@@MD5OUT 99e8fed75cb41e6eeaced886c144d7ca
@@EDIT 1
@@OLD
public interface Typed
@@NEW
@FunctionalInterface
public interface Typed
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/ResourceProvider.java
@@MD5IN 4172cf55ce58dabc637a9c1ef09bd5de
@@MD5OUT f1330dc6ebbd6b8779623b71f8bcff5a
@@EDIT 1
@@OLD
public interface ResourceProvider
@@NEW
@FunctionalInterface
public interface ResourceProvider
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/SecurityActions.java
@@MD5IN 8c8283a050d3b11aab4307f87012f694
@@MD5OUT 0b41b503bdffc242aef959073e34bb79
@@EDIT 1
@@OLD
         return AccessController.doPrivileged(new PrivilegedExceptionAction<Constructor<?>>()
         {
            public Constructor<?> run() throws NoSuchMethodException
            {
               return clazz.getConstructor(argumentTypes);
            }
         }
@@NEW
         return AccessController.doPrivileged(
            (PrivilegedExceptionAction<Constructor<?>>) () -> clazz.getConstructor(argumentTypes)
@@EDIT 1
@@OLD
      List<Field> declaredAccessableFields = AccessController.doPrivileged(new PrivilegedAction<List<Field>>()
      {
         public List<Field> run()
         {
            List<Field> foundFields = new ArrayList<>();
            Class<?> nextSource = source;
            while (nextSource != Object.class) {
               for(Field field : nextSource.getDeclaredFields())
               {
                  if(field.isAnnotationPresent(annotationClass))
                  {
                     if(!field.isAccessible()) 
                     {
                        field.setAccessible(true);
                     }
                     foundFields.add(field);
                  }
               }
               nextSource = nextSource.getSuperclass();
            }
            return foundFields;
         }
      }
@@NEW
      List<Field> declaredAccessableFields = AccessController.doPrivileged((PrivilegedAction<List<Field>>) () ->
      {
         List<Field> foundFields = new ArrayList<>();
         Class<?> nextSource = source;
         while (nextSource != Object.class) {
            for(Field field : nextSource.getDeclaredFields())
            {
               if(field.isAnnotationPresent(annotationClass))
               {
                  if(!field.isAccessible()) 
                  {
                     field.setAccessible(true);
                  }
                  foundFields.add(field);
               }
            }
            nextSource = nextSource.getSuperclass();
         }
         return foundFields;
      }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/URLResourceProvider.java
@@MD5IN 656975b2c97baa5d7b68c65c1008e54c
@@MD5OUT ae6c4f95771a08383042877f7df58325
@@EDIT 1
@@OLD
      for (Servlet servlet : servlets)
      {
         context.add(servlet.getContextRoot());
      }
@@NEW
      servlets.stream().map(servlet -> servlet.getContextRoot()).forEach(context::add);
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/execution/event/ExecutionEvent.java
@@MD5IN b7238f0bb517b33cc7a118c4f11c3a1d
@@MD5OUT f7f5e3ba0d015a3cbf5cd2bdbc88e5f2
@@EDIT 1
@@OLD
public interface ExecutionEvent extends Event
@@NEW
@FunctionalInterface
public interface ExecutionEvent extends Event
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/DynamicServiceLoaderTestCase.java
@@MD5IN 78729c9e3ce60763eefe06c17fb23eed
@@MD5OUT 15f0f841f26e4e038e76167802e4494b
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalStateException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            new DynamicServiceLoader().onlyOne(Service.class);

         }
      }
@@NEW
      Assertions.assertThrows(IllegalStateException.class, () -> new DynamicServiceLoader().onlyOne(Service.class)
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/client/container/DeploymentExceptionHandlerTestCase.java
@@MD5IN f8f6595325c05c9c5613a18be08ad2ed
@@MD5OUT 745e21056b1aeabba77e5f1f8e219a8a
@@EDIT 1
@@OLD
      Assertions.assertThrows(DeploymentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            TestExceptionDeployThrower.shouldThrow = new DeploymentException("Could not handle ba", new NullPointerException()); 
            Mockito.when(serviceLoader.all(DeploymentExceptionTransformer.class)).thenReturn(Arrays.asList(transformer));

            fire(new DeployDeployment(
                  container, 
                  new DeploymentDescription("test", ShrinkWrap.create(JavaArchive.class))
                     .setExpectedException(IllegalArgumentException.class)));

         }
      }
@@NEW
      Assertions.assertThrows(DeploymentException.class, () ->
      {
         TestExceptionDeployThrower.shouldThrow = new DeploymentException("Could not handle ba", new NullPointerException()); 
         Mockito.when(serviceLoader.all(DeploymentExceptionTransformer.class)).thenReturn(Arrays.asList(transformer));

         fire(new DeployDeployment(
               container, 
               new DeploymentDescription("test", ShrinkWrap.create(JavaArchive.class))
                  .setExpectedException(IllegalArgumentException.class)));

      }
@@EDIT 1
@@OLD
      Assertions.assertThrows(DeploymentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            TestExceptionDeployThrower.shouldThrow = new DeploymentException("Could not handle ba", new NullPointerException()); 

            fire(new DeployDeployment(
                  container, 
                  new DeploymentDescription("test", ShrinkWrap.create(JavaArchive.class))));

         }
      }
@@NEW
      Assertions.assertThrows(DeploymentException.class, () ->
      {
         TestExceptionDeployThrower.shouldThrow = new DeploymentException("Could not handle ba", new NullPointerException()); 

         fire(new DeployDeployment(
               container, 
               new DeploymentDescription("test", ShrinkWrap.create(JavaArchive.class))));

      }
@@EDIT 1
@@OLD
      Assertions.assertThrows(RuntimeException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            TestExceptionDeployThrower.shouldThrow = null;

            fire(new DeployDeployment(
                  container, 
                  new DeploymentDescription("test", ShrinkWrap.create(JavaArchive.class))
                     .setExpectedException(IllegalArgumentException.class)));

         }
      }
@@NEW
      Assertions.assertThrows(RuntimeException.class, () ->
      {
         TestExceptionDeployThrower.shouldThrow = null;

         fire(new DeployDeployment(
               container, 
               new DeploymentDescription("test", ShrinkWrap.create(JavaArchive.class))
                  .setExpectedException(IllegalArgumentException.class)));

      }
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/client/deployment/AnnotationDeploymentScenarioGeneratorTestCase.java
@@MD5IN 7767f627ead102594abd43c5df61ecc5
@@MD5OUT f360f35f71fb1538fedb29bf46383f40
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            new AnnotationDeploymentScenarioGenerator().generate(
                  new TestClass(DeploymentNotPresent.class));

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () -> new AnnotationDeploymentScenarioGenerator().generate(
                  new TestClass(DeploymentNotPresent.class))
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            new AnnotationDeploymentScenarioGenerator().generate(
                  new TestClass(DeploymentNotStatic.class));

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () -> new AnnotationDeploymentScenarioGenerator().generate(
                  new TestClass(DeploymentNotStatic.class))
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            new AnnotationDeploymentScenarioGenerator().generate(
                  new TestClass(DeploymentWrongReturnType.class));

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () -> new AnnotationDeploymentScenarioGenerator().generate(
                  new TestClass(DeploymentWrongReturnType.class))
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/EventFireTestCase.java
@@MD5IN cfe6f4b0b35f74c11eb8fa244c0ad4d6
@@MD5OUT 91b3b896a44b2af32f1f30f73e528387
@@EDIT 1
@@OLD
      Assertions.assertThrows(IOException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            ManagerImpl manager = ManagerBuilder.from()
               .extensions(ExtensionObservingExceptionLoop.class).create();

            manager.fire(new IOException("should cause exception"));


         }
      }
@@NEW
      Assertions.assertThrows(IOException.class, () ->
      {
         ManagerImpl manager = ManagerBuilder.from()
            .extensions(ExtensionObservingExceptionLoop.class).create();

         manager.fire(new IOException("should cause exception"));


      }
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/InstanceImplTestCase.java
@@MD5IN fae448971bebc369f5015d81c6d88f76
@@MD5OUT bb87343b2ed2ebb07df1112a2520b014
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalStateException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            ManagerImpl manager = ManagerBuilder.from().create();
            InstanceProducer<Object> instance = InstanceImpl.of(Object.class, null, manager);

            instance.set(new Object());
            Assertions.fail("Should have thrown " + IllegalStateException.class);

         }
      }
@@NEW
      Assertions.assertThrows(IllegalStateException.class, () ->
      {
         ManagerImpl manager = ManagerBuilder.from().create();
         InstanceProducer<Object> instance = InstanceImpl.of(Object.class, null, manager);

         instance.set(new Object());
         Assertions.fail("Should have thrown " + IllegalStateException.class);

      }
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/core/ManagerImplTestCase.java
@@MD5IN 22ab57eb6e74543fbeedf268f210f06b
@@MD5OUT 87e6957ca03c022ba5da70e236574adf
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            ManagerImpl manager = ManagerBuilder.from().create();
            manager.bind(SuiteScoped.class, Object.class, new Object());

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () ->
      {
         ManagerImpl manager = ManagerBuilder.from().create();
         manager.bind(SuiteScoped.class, Object.class, new Object());

      }
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            ManagerImpl manager = ManagerBuilder.from()
               .context(SuiteContextImpl.class).create();

            manager.bind(SuiteScoped.class, Object.class, new Object());

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () ->
      {
         ManagerImpl manager = ManagerBuilder.from()
            .context(SuiteContextImpl.class).create();

         manager.bind(SuiteScoped.class, Object.class, new Object());

      }
@@ENDFILE
@@FILE impl-base/src/test/java/org/jboss/arquillian/impl/domain/ProtocolRegistryTestCase.java
@@MD5IN c1c7679468a9d0a4b274e8f6c219e716
@@MD5OUT 85bcd1e0f36631cff51ad56bb3056dfa
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            createRegistry()
               .addProtocol(new ProtocolDefinition(protocol, new HashMap<String, String>()))
               .addProtocol(new ProtocolDefinition(protocol, new HashMap<String, String>()));

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () -> createRegistry()
               .addProtocol(new ProtocolDefinition(protocol, new HashMap<String, String>()))
               .addProtocol(new ProtocolDefinition(protocol, new HashMap<String, String>()))
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            createRegistry().addProtocol(null);

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () -> createRegistry().addProtocol(null)
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            createRegistry().getProtocol(null);

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () -> createRegistry().getProtocol(null)
@@ENDFILE
@@FILE protocols/jmx-osgi/src/test/java/org/jboss/arquillian/packager/osgi/OSGiDeploymentPackagerTestCase.java
@@MD5IN ab4766428bafce9f5ebd422129b68587
@@MD5OUT 6c15b877ff05741ec19d93f7d36457c4
@@EDIT 1
@@OLD
      archive.setManifest(new Asset()
      {
         public InputStream openStream()
         {
            OSGiManifestBuilder builder = OSGiManifestBuilder.newInstance();
            builder.addBundleSymbolicName(archive.getName());
            builder.addBundleManifestVersion(2);
            return builder.openStream();
         }
      }
@@NEW
      archive.setManifest(() ->
      {
         OSGiManifestBuilder builder = OSGiManifestBuilder.newInstance();
         builder.addBundleSymbolicName(archive.getName());
         builder.addBundleManifestVersion(2);
         return builder.openStream();
      }
@@EDIT 1
@@OLD
      archive.setManifest(new Asset()
      {
         public InputStream openStream()
         {
            OSGiManifestBuilder builder = OSGiManifestBuilder.newInstance();
            return builder.openStream();
         }
      }
@@NEW
      archive.setManifest(() ->
      {
         OSGiManifestBuilder builder = OSGiManifestBuilder.newInstance();
         return builder.openStream();
      }
@@ENDFILE
@@FILE protocols/jmx/src/main/java/org/jboss/arquillian/protocol/jmx/JMXTestRunner.java
@@MD5IN a2d5015d99f0d08734a5bb7b94b3dd84
@@MD5OUT f6a91a149cbb0b255c62406d19aab0d6
@@EDIT 1
@@OLD
   public interface TestClassLoader
@@NEW
   @FunctionalInterface
   public interface TestClassLoader
@@EDIT 1
@@OLD
         testClassLoader = new TestClassLoader()
         {
            public Class<?> loadTestClass(String className) throws ClassNotFoundException
            {
               ClassLoader classLoader = JMXTestRunner.class.getClassLoader();
               return classLoader.loadClass(className);
            }
         }
@@NEW
         testClassLoader = className ->
         {
            ClassLoader loader = JMXTestRunner.class.getClassLoader();
            return loader.loadClass(className);
         }
@@ENDFILE
@@FILE protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/runner/SecurityActions.java
@@MD5IN 94f65d24394f0d0456a9856798d3dbab
@@MD5OUT 14baaca5c09ee1180852ad4c6abb1edc
@@EDIT 1
@@OLD
         return AccessController.doPrivileged(new PrivilegedExceptionAction<Constructor<?>>()
         {
            public Constructor<?> run() throws NoSuchMethodException
            {
               return clazz.getConstructor(argumentTypes);
            }
         }
@@NEW
         return AccessController.doPrivileged(
            (PrivilegedExceptionAction<Constructor<?>>) () -> clazz.getConstructor(argumentTypes)
@@ENDFILE
@@FILE protocols/servlet/src/test/java/org/jboss/arquillian/protocol/servlet/v_2_5/ServletProtocolDeploymentPackagerTestCase.java
@@MD5IN a07a2e7b073a58b869d8480bc837f6ce
@@MD5OUT f62e7149e02032186842dbb0bc505f77
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            new ServletProtocolDeploymentPackager().generateDeployment(
                  new TestDeployment(ShrinkWrap.create(ResourceAdapterArchive.class), new ArrayList<Archive<?>>()),
                  processors()
            );

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () -> new ServletProtocolDeploymentPackager().generateDeployment(
                  new TestDeployment(ShrinkWrap.create(ResourceAdapterArchive.class), new ArrayList<Archive<?>>()),
                  processors()
            )
@@EDIT 1
@@OLD
      Assertions.assertThrows(UnsupportedOperationException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            new ServletProtocolDeploymentPackager().generateDeployment(
                  new TestDeployment(
                        ShrinkWrap.create(EnterpriseArchive.class, "applicationArchive.ear")
                           .addAsModule(ShrinkWrap.create(WebArchive.class))
                           .addAsModule(ShrinkWrap.create(WebArchive.class)), 
                        createAuxiliaryArchives()),
                  processors());

         }
      }
@@NEW
      Assertions.assertThrows(UnsupportedOperationException.class, () -> new ServletProtocolDeploymentPackager().generateDeployment(
                  new TestDeployment(
                        ShrinkWrap.create(EnterpriseArchive.class, "applicationArchive.ear")
                           .addAsModule(ShrinkWrap.create(WebArchive.class))
                           .addAsModule(ShrinkWrap.create(WebArchive.class)), 
                        createAuxiliaryArchives()),
                  processors())
@@ENDFILE
@@FILE protocols/servlet/src/test/java/org/jboss/arquillian/protocol/servlet/v_3/ServletProtocolDeploymentPackagerTestCase.java
@@MD5IN 1688edda55ecae551af48b6d90be2978
@@MD5OUT 666a0d03bc76a0e106ff73e6cf57ee6c
@@EDIT 1
@@OLD
      Assertions.assertThrows(IllegalArgumentException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            new ServletProtocolDeploymentPackager().generateDeployment(
                  new TestDeployment(ShrinkWrap.create(ResourceAdapterArchive.class), new ArrayList<Archive<?>>()),
                  processors()
            );

         }
      }
@@NEW
      Assertions.assertThrows(IllegalArgumentException.class, () -> new ServletProtocolDeploymentPackager().generateDeployment(
                  new TestDeployment(ShrinkWrap.create(ResourceAdapterArchive.class), new ArrayList<Archive<?>>()),
                  processors()
            )
@@EDIT 1
@@OLD
      Assertions.assertThrows(UnsupportedOperationException.class, new Executable()
      {
         public void execute() throws Throwable
         {
            new ServletProtocolDeploymentPackager().generateDeployment(
                  new TestDeployment(
                        ShrinkWrap.create(EnterpriseArchive.class, "applicationArchive.ear")
                           .addAsModule(ShrinkWrap.create(WebArchive.class))
                           .addAsModule(ShrinkWrap.create(WebArchive.class)), 
                        createAuxiliaryArchives()),
                  processors());

         }
      }
@@NEW
      Assertions.assertThrows(UnsupportedOperationException.class, () -> new ServletProtocolDeploymentPackager().generateDeployment(
                  new TestDeployment(
                        ShrinkWrap.create(EnterpriseArchive.class, "applicationArchive.ear")
                           .addAsModule(ShrinkWrap.create(WebArchive.class))
                           .addAsModule(ShrinkWrap.create(WebArchive.class)), 
                        createAuxiliaryArchives()),
                  processors())
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/ContainerMethodExecutor.java
@@MD5IN 39ad4cdf16431c6e6af11ed37775672d
@@MD5OUT 0e51dccf3692ec9f61066e47449113f9
@@EDIT 1
@@OLD
public interface ContainerMethodExecutor
@@NEW
@FunctionalInterface
public interface ContainerMethodExecutor
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/LifecycleMethodExecutor.java
@@MD5IN 10933180784d85579b503ad6f4c876c7
@@MD5OUT e6a4a1a144a6e95002db5789ad184f12
@@EDIT 1
@@OLD
public interface LifecycleMethodExecutor
@@NEW
@FunctionalInterface
public interface LifecycleMethodExecutor
@@EDIT 1
@@OLD
   public static final LifecycleMethodExecutor NO_OP = new LifecycleMethodExecutor()
   {
      public void invoke() throws Throwable
      {
      }
   }
@@NEW
   public static final LifecycleMethodExecutor NO_OP = () -> { }
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/TestClass.java
@@MD5IN b84e1b3690a3ebffbc04b57fe9cf21f6
@@MD5OUT 2d02bc759670b4e42b6f727c65020899
@@EDIT 1
@@OLD
      for(Method method: methods)
      {
         if(method.isAnnotationPresent(annotation)) 
         {
            foundMethods.add(method);
         }
      }
@@NEW
      Arrays.stream(methods)
         .filter(method -> method.isAnnotationPresent(annotation))
         .forEach(foundMethods::add);
@@EDIT 1
@@OLD
import java.util.List;
@@NEW
import java.util.Arrays;
import java.util.List;
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/TestRunner.java
@@MD5IN 20966df1b8693298d2b75b125fd0faa6
@@MD5OUT 5f701bf3dd53045834fcf1783bfdc186
@@EDIT 1
@@OLD
public interface TestRunner
@@NEW
@FunctionalInterface
public interface TestRunner
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/container/ContainerConfiguration.java
@@MD5IN 4c048ee1793689306fd33dfccd8e30c9
@@MD5OUT 4c7c233f0e106b1b41561c106b4fa445
@@EDIT 1
@@OLD
public interface ContainerConfiguration
@@NEW
@FunctionalInterface
public interface ContainerConfiguration
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/container/DeploymentExceptionTransformer.java
@@MD5IN 34434d0d93d16574233b19174bba8970
@@MD5OUT 995ffbbfb485bd910346a3d489e10450
@@EDIT 1
@@OLD
public interface DeploymentExceptionTransformer
@@NEW
@FunctionalInterface
public interface DeploymentExceptionTransformer
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/deployment/ApplicationArchiveProcessor.java
@@MD5IN a383a7ce017f9c7d7113cfe00f025074
@@MD5OUT 0c4d6b95b06e61e59d09bd132855cf97
@@EDIT 1
@@OLD
public interface ApplicationArchiveProcessor
@@NEW
@FunctionalInterface
public interface ApplicationArchiveProcessor
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/deployment/AuxiliaryArchiveAppender.java
@@MD5IN 34a26368895800364fc83ed6c56aa443
@@MD5OUT 8db1ed74315b7c14be8c988fb163cf45
@@EDIT 1
@@OLD
public interface AuxiliaryArchiveAppender
@@NEW
@FunctionalInterface
public interface AuxiliaryArchiveAppender
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/deployment/AuxiliaryArchiveProcessor.java
@@MD5IN 11223fb324c42a81d2204ea1e3b4881b
@@MD5OUT bc3a828fdb48cc560f34c0e458e56a74
@@EDIT 1
@@OLD
public interface AuxiliaryArchiveProcessor
@@NEW
@FunctionalInterface
public interface AuxiliaryArchiveProcessor
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/deployment/DeploymentPackager.java
@@MD5IN 84298b4419a372bf4edc1bbc310f038b
@@MD5OUT 51fa8de9b2194037b1b139ae33cd5b00
@@EDIT 1
@@OLD
public interface DeploymentPackager
@@NEW
@FunctionalInterface
public interface DeploymentPackager
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/deployment/DeploymentScenario.java
@@MD5IN f8b5d9393443dbd8591d40a4ab0fe6ac
@@MD5OUT c4276911cf34dd6baae0f05f65fe95af
@@EDIT 1
@@OLD
      for(DeploymentDescription desc : deployments)
      {
         targets.add(desc.getTarget());
      }
@@NEW
      deployments.stream().map(desc -> desc.getTarget()).forEach(targets::add);
@@EDIT 1
@@OLD
      for(DeploymentDescription desc : deployments)
      {
         protocols.add(desc.getProtocol());
      }
@@NEW
      deployments.stream().map(desc -> desc.getProtocol()).forEach(protocols::add);
@@EDIT 1
@@OLD
      for(DeploymentDescription deployment : deployments)
      {
         if(deployment.managed() && target.equals(deployment.getTarget()))
         {
            startupDeployments.add(deployment);
         }
      }
@@NEW
      deployments.stream()
         .filter(deployment -> deployment.managed() && target.equals(deployment.getTarget()))
         .forEach(startupDeployments::add);
@@EDIT 1
@@OLD
      Collections.sort(startupDeployments, new Comparator<DeploymentDescription>()
      {
         public int compare(DeploymentDescription o1, DeploymentDescription o2)
         {
            return new Integer(o1.getOrder()).compareTo(o2.getOrder());
         }
      }
@@NEW
      Collections.sort(startupDeployments, (o1, o2) -> new Integer(o1.getOrder()).compareTo(o2.getOrder())
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/deployment/DeploymentScenarioGenerator.java
@@MD5IN d42e7dfc09cfad109456a84615779277
@@MD5OUT 3b40bf8cedfba838b83202fb4ecbe002
@@EDIT 1
@@OLD
public interface DeploymentScenarioGenerator
@@NEW
@FunctionalInterface
public interface DeploymentScenarioGenerator
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/deployment/ProtocolArchiveProcessor.java
@@MD5IN f5599a55f0a461d2ae3630df795db12f
@@MD5OUT b6f718c584e1f41d3a33f225b7764789
@@EDIT 1
@@OLD
public interface ProtocolArchiveProcessor
@@NEW
@FunctionalInterface
public interface ProtocolArchiveProcessor
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/protocol/ProtocolArchiveAppender.java
@@MD5IN 25ac411c6b5b21dbd3cea7b33bd1a5cd
@@MD5OUT c2211736ae2fb5d01a2c19c58addf5fb
@@EDIT 1
@@OLD
public interface ProtocolArchiveAppender
@@NEW
@FunctionalInterface
public interface ProtocolArchiveAppender
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/core/Event.java
@@MD5IN 5e191a8995afb8a2e5992ca43faeb113
@@MD5OUT 23f81be909909c731d52b85b7c37b7d3
@@EDIT 1
@@OLD
public interface Event<T> 
@@NEW
@FunctionalInterface
public interface Event<T> 
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/core/Injector.java
@@MD5IN fd18ff781ff2cbd9dd5aa68fdb1d102f
@@MD5OUT d12214af49737fbef71e18187f6a5551
@@EDIT 1
@@OLD
public interface Injector
@@NEW
@FunctionalInterface
public interface Injector
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/core/Instance.java
@@MD5IN 2f0e1e9b0ed14697078c84ea2c8a23da
@@MD5OUT 9b856030f1572a88ca3e57969703f2a2
@@EDIT 1
@@OLD
public interface Instance<T> 
@@NEW
@FunctionalInterface
public interface Instance<T> 
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/event/suite/LifecycleEvent.java
@@MD5IN 31fd2bf12c28dd53ff7a414ffa4c1382
@@MD5OUT 402ad946b313413932fe2ae2c8d59e89
@@EDIT 1
@@OLD
public interface LifecycleEvent extends Event
@@NEW
@FunctionalInterface
public interface LifecycleEvent extends Event
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/util/SecurityActions.java
@@MD5IN 20cbe46a2ed2e54b056762db965c102f
@@MD5OUT 7b9c99202d04210a1f20b8ff5c587697
@@EDIT 1
@@OLD
         return AccessController.doPrivileged(new PrivilegedExceptionAction<Constructor<?>>()
         {
            public Constructor<?> run() throws NoSuchMethodException
            {
               return clazz.getConstructor(argumentTypes);
            }
         }
@@NEW
         return AccessController.doPrivileged(
            (PrivilegedExceptionAction<Constructor<?>>) () -> clazz.getConstructor(argumentTypes)
@@ENDFILE
@@FILE testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/MethodParameterInjectionPoint.java
@@MD5IN 1871c4beba156c4bed90b71d0af8cb66
@@MD5OUT d86330b86149d61a3974ff4a50123981
@@EDIT 1
@@OLD
      for(Annotation annotation : method.getParameterAnnotations()[position])
      {
         if(beanManager.isQualifier(annotation.annotationType()))
         {
            qualifiers.add(annotation);
         }
      }
@@NEW
      Arrays.stream(method.getParameterAnnotations()[position])
         .filter(annotation -> beanManager.isQualifier(annotation.annotationType()))
         .forEach(qualifiers::add);
@@ENDFILE
@@FILE testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/SecurityActions.java
@@MD5IN a5cc7350371011c30f9a978424fb3544
@@MD5OUT 619ce1ae80785abc825c977b128078bd
@@EDIT 1
@@OLD
         return AccessController.doPrivileged(new PrivilegedExceptionAction<Constructor<?>>()
         {
            public Constructor<?> run() throws NoSuchMethodException
            {
               return clazz.getConstructor(argumentTypes);
            }
         }
@@NEW
         return AccessController.doPrivileged(
            (PrivilegedExceptionAction<Constructor<?>>) () -> clazz.getConstructor(argumentTypes)
@@EDIT 1
@@OLD
      List<Field> declaredAccessableFields = AccessController.doPrivileged(new PrivilegedAction<List<Field>>()
      {
         public List<Field> run()
         {
            List<Field> foundFields = new ArrayList<>();
            for(Field field : source.getDeclaredFields())
            {
               if(field.isAnnotationPresent(annotationClass))
               {
                  if(!field.isAccessible()) 
                  {
                     field.setAccessible(true);
                  }
                  foundFields.add(field);
               }
            }
            return foundFields;
         }
      }
@@NEW
      List<Field> declaredAccessableFields = AccessController.doPrivileged((PrivilegedAction<List<Field>>) () ->
      {
         List<Field> foundFields = new ArrayList<>();
         for(Field field : source.getDeclaredFields())
         {
            if(field.isAnnotationPresent(annotationClass))
            {
               if(!field.isAccessible()) 
               {
                  field.setAccessible(true);
               }
               foundFields.add(field);
            }
         }
         return foundFields;
      }
@@ENDFILE
@@FILE testenrichers/cdi/src/test/java/org/jboss/arquillian/testenricher/cdi/CDIInjectionEnricherTestCase.java
@@MD5IN 9d8abea1d692dbbe2bc4a80e4bb131dd
@@MD5OUT 5c200706b6efd2d2a0f6a81e854ae124
@@EDIT 1
@@OLD
            for (Class<?> c : classes)
            {
               beanClasses.add(c.getName());
            }
@@NEW
            Arrays.stream(classes).map(c -> c.getName()).forEach(beanClasses::add);
@@ENDFILE
@@FILE testenrichers/ejb/src/main/java/org/jboss/arquillian/testenricher/ejb/SecurityActions.java
@@MD5IN 4c3cb7c34a4bed087eb3ed05361787f8
@@MD5OUT 7e0c11d980602695fdcfe378437f6de8
@@EDIT 1
@@OLD
         return AccessController.doPrivileged(new PrivilegedExceptionAction<Constructor<?>>()
         {
            public Constructor<?> run() throws NoSuchMethodException
            {
               return clazz.getConstructor(argumentTypes);
            }
         }
@@NEW
         return AccessController.doPrivileged(
            (PrivilegedExceptionAction<Constructor<?>>) () -> clazz.getConstructor(argumentTypes)
@@EDIT 1
@@OLD
      List<Field> declaredAccessableFields = AccessController.doPrivileged(new PrivilegedAction<List<Field>>()
      {
         public List<Field> run()
         {
            List<Field> foundFields = new ArrayList<>();
            Class<?> nextSource = source;
            while (nextSource != Object.class) {
               for(Field field : nextSource.getDeclaredFields())
               {
                  if(field.isAnnotationPresent(annotationClass))
                  {
                     if(!field.isAccessible()) 
                     {
                        field.setAccessible(true);
                     }
                     foundFields.add(field);
                  }
               }
               nextSource = nextSource.getSuperclass();
            }
            return foundFields;
         }
      }
@@NEW
      List<Field> declaredAccessableFields = AccessController.doPrivileged((PrivilegedAction<List<Field>>) () ->
      {
         List<Field> foundFields = new ArrayList<>();
         Class<?> nextSource = source;
         while (nextSource != Object.class) {
            for(Field field : nextSource.getDeclaredFields())
            {
               if(field.isAnnotationPresent(annotationClass))
               {
                  if(!field.isAccessible()) 
                  {
                     field.setAccessible(true);
                  }
                  foundFields.add(field);
               }
            }
            nextSource = nextSource.getSuperclass();
         }
         return foundFields;
      }
@@EDIT 1
@@OLD
      List<Method> declaredAccessableMethods = AccessController.doPrivileged(new PrivilegedAction<List<Method>>()
      {
         public List<Method> run()
         {
            List<Method> foundMethods = new ArrayList<>();
            for(Method method : source.getDeclaredMethods())
            {
               if(method.isAnnotationPresent(annotationClass))
               {
                  if(!method.isAccessible()) 
                  {
                     method.setAccessible(true);
                  }
                  foundMethods.add(method);
               }
            }
            return foundMethods;
         }
      }
@@NEW
      List<Method> declaredAccessableMethods = AccessController.doPrivileged((PrivilegedAction<List<Method>>) () ->
      {
         List<Method> foundMethods = new ArrayList<>();
         for(Method method : source.getDeclaredMethods())
         {
            if(method.isAnnotationPresent(annotationClass))
            {
               if(!method.isAccessible()) 
               {
                  method.setAccessible(true);
               }
               foundMethods.add(method);
            }
         }
         return foundMethods;
      }
@@ENDFILE
@@FILE testenrichers/osgi/src/main/java/org/jboss/arquillian/testenricher/osgi/BundleContextHolder.java
@@MD5IN 37a4d67183f6023adb5fa8191ec88442
@@MD5OUT b57945076dc7b2c1226e7dc7eaf9e5eb
@@EDIT 1
@@OLD
public interface BundleContextHolder
@@NEW
@FunctionalInterface
public interface BundleContextHolder
@@ENDFILE
@@FILE testenrichers/resource/src/main/java/org/jboss/arquillian/testenricher/resource/SecurityActions.java
@@MD5IN 4e4f31ca30948348fae4c91b2c8f2ced
@@MD5OUT 5bbf7b99205e4eaab5d0044941645915
@@EDIT 1
@@OLD
         return AccessController.doPrivileged(new PrivilegedExceptionAction<Constructor<?>>()
         {
            public Constructor<?> run() throws NoSuchMethodException
            {
               return clazz.getConstructor(argumentTypes);
            }
         }
@@NEW
         return AccessController.doPrivileged(
            (PrivilegedExceptionAction<Constructor<?>>) () -> clazz.getConstructor(argumentTypes)
@@EDIT 1
@@OLD
      List<Field> declaredAccessableFields = AccessController.doPrivileged(new PrivilegedAction<List<Field>>()
      {
         public List<Field> run()
         {
            List<Field> foundFields = new ArrayList<>();
            Class<?> nextSource = source;
            while (nextSource != Object.class) {
               for(Field field : nextSource.getDeclaredFields())
               {
                  if(field.isAnnotationPresent(annotationClass))
                  {
                     if(!field.isAccessible()) 
                     {
                        field.setAccessible(true);
                     }
                     foundFields.add(field);
                  }
               }
               nextSource = nextSource.getSuperclass();
            }
            return foundFields;
         }
      }
@@NEW
      List<Field> declaredAccessableFields = AccessController.doPrivileged((PrivilegedAction<List<Field>>) () ->
      {
         List<Field> foundFields = new ArrayList<>();
         Class<?> nextSource = source;
         while (nextSource != Object.class) {
            for(Field field : nextSource.getDeclaredFields())
            {
               if(field.isAnnotationPresent(annotationClass))
               {
                  if(!field.isAccessible()) 
                  {
                     field.setAccessible(true);
                  }
                  foundFields.add(field);
               }
            }
            nextSource = nextSource.getSuperclass();
         }
         return foundFields;
      }
@@EDIT 1
@@OLD
      List<Method> declaredAccessableMethods = AccessController.doPrivileged(new PrivilegedAction<List<Method>>()
      {
         public List<Method> run()
         {
            List<Method> foundMethods = new ArrayList<>();
            for(Method method : source.getDeclaredMethods())
            {
               if(method.isAnnotationPresent(annotationClass))
               {
                  if(!method.isAccessible()) 
                  {
                     method.setAccessible(true);
                  }
                  foundMethods.add(method);
               }
            }
            return foundMethods;
         }
      }
@@NEW
      List<Method> declaredAccessableMethods = AccessController.doPrivileged((PrivilegedAction<List<Method>>) () ->
      {
         List<Method> foundMethods = new ArrayList<>();
         for(Method method : source.getDeclaredMethods())
         {
            if(method.isAnnotationPresent(annotationClass))
            {
               if(!method.isAccessible()) 
               {
                  method.setAccessible(true);
               }
               foundMethods.add(method);
            }
         }
         return foundMethods;
      }
@@ENDFILE
EDITS04

# Captured rather than streamed, because the line it prints is the assertion:
# apply8.py refuses a tree whose files do not arrive at the md5 the edits were
# computed against, and refuses a file that does not leave at the md5 the
# generator computed -- but a solve.sh that let the one summary line scroll past
# unread would still be trusting that it ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it, the commonest failure -- run against a tree that is not the one step 03
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
want='apply8.py: 86 edit(s) in 55 file(s), every result md5 as computed'
if ! printf '%s\n' "${ap}" | grep -qxF "${want}"; then
   echo "solve04: the edits did not all apply" >&2
   exit 1
fi

# ==========================================================================
# 2. The compile level.
# ==========================================================================
lv=$(python3 - "${TREE}" 2>&1 <<'PY_LEVEL8'
#!/usr/bin/env python3
"""Raise the compile level from Java 7 to Java 8.

   level8.py <tree>

The same one knob `level7.py` turned, and the same assertion behind it: a
migration that reports success having edited nothing is the failure this checks
for.

The restatement table spells this row `--release 8`, and this turns
`sourceCompatibility`/`targetCompatibility` instead. That is not a shortfall --
it is what the JDK this step grades on can express. NOTES section 8.3 settles
steps 3-5 on JDK 8, where `--release` does not exist at all; major 52 comes out
of `-source 1.8 -target 1.8`, which is what these two Gradle properties
produce, and which is the same knob the tree has been using since step 01. The
`--release` form arrives at step 06 with the JDK that has it.

The `-Xlint:-options` already on the compiler arguments stays. It is dead from
this step on -- JDK 8's javac warns "source value is obsolete" for 1.6 and 1.7
and says nothing for 1.8, its own default -- but removing it is a change to
what the build reports rather than to what it targets, and this row is about
the target.
"""
import io
import sys

path = sys.argv[1] + '/build.gradle'
src = io.open(path, encoding='utf-8').read()
subs = [
    ('// Java 7 bytecode -- class-file major version 51 -- built by JDK 8.',
     '// Java 8 bytecode -- class-file major version 52 -- built by JDK 8.'),
    ('sourceCompatibility = JavaVersion.VERSION_1_7',
     'sourceCompatibility = JavaVersion.VERSION_1_8'),
    ('targetCompatibility = JavaVersion.VERSION_1_7',
     'targetCompatibility = JavaVersion.VERSION_1_8'),
]
for old, new in subs:
    if src.count(old) != 1:
        sys.stderr.write('level8.py: %r occurs %d times, wanted 1\n'
                         % (old, src.count(old)))
        sys.exit(1)
    src = src.replace(old, new)
io.open(path, 'w', encoding='utf-8').write(src)
sys.stderr.write('level8.py: build.gradle now at 1.8\n')
PY_LEVEL8
)
printf '%s\n' "${lv}"
if ! printf '%s\n' "${lv}" | grep -qxF 'level8.py: build.gradle now at 1.8'; then
   echo "solve04: build.gradle was not raised to 1.8" >&2
   exit 1
fi

echo "solve: step 04 complete"
