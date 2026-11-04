#!/usr/bin/env bash
# Golden solution, step 05 -- 05-java8-lib.
#
# One transform over the tree step 04 left behind: 90 exact-text edits in
# 28 files. Nothing here compiles anything, and nothing here touches the build
# script: the edits are text in and text out, and the build that proves them is
# the one the step is graded by.
#
#   29  null-returning private lookup helpers -> Optional, plus the call
#       sites that stop testing the result against null
#   23  terminal forEach(sink::add) pipelines -> collect(Collectors...)
#    6  members of hand-rolled equals/hashCode pairs -> java.util.Objects
#    4  System.currentTimeMillis arithmetic -> java.time
#   28  imports added for the above
#
# WHY A LIST AND NOT A PROGRAM. Same argument as step 04, with step 05's own
# examples. Whether a null-returning method may widen to Optional is a question
# about its modifiers and about every one of its call sites being inside the
# same compilation unit. Which collector a `forEach(sink::add)` becomes is a
# question about the declared type of the sink -- ArrayList, HashSet, or the one
# LinkedHashSet whose iteration order is load-bearing -- several lines above the
# pipeline and often in a different method. A text program that approximated
# either would be right about most sites and quietly wrong about a few, and the
# few would be exactly the ones this step is scored on. So the finding happened
# at authoring time against the tree step 04 produces, and what ships is the
# result, with an md5 on every file both before and after as the proof that it
# landed on the tree it was computed for. apply8.py below says the rest.
#
# WHY ONLY SIXTEEN OF THE TWENTY-TWO NULL RETURNS BECOME OPTIONAL. The gate
# this step inherits from step 03 -- gates/apisig.sh -- pins an EXACT set of
# public and protected signatures: 1147 wanted, 1147 hit, nothing extra.
# Widening a published `T find(...)` to `Optional<T> find(...)` leaves the old
# signature missing and a new one unaccounted for, which is charged exactly as
# heavily as deleting the method. All sixteen helpers converted here are
# `private`; their signatures are not in that set at all, so the conversion is
# free on them and impossible everywhere else. The scan's `retnull.api` figure
# reads 22 before these edits and 22 after, deliberately and by check.
#
# WHY EIGHT OF THE 23 COLLECTORS ARE toCollection. One is required:
# DynamicServiceLoader accumulates into a LinkedHashSet, and the order in which
# services come out of it is visible in the recorded test output, so the
# collector has to name the implementation. Six more are collected into a list
# or set that is mutated after it is built -- three `addAll` in Reflections, two
# `Collections.sort`, one further `add` in getQualifiers -- and neither
# Collectors.toList() nor Collectors.toSet() promises a mutable result.
# The seventh, ManagerImpl#resolveActiveContexts, is only read by its caller and
# is spelled the same way as its neighbour in the same file for consistency.
#
# WHAT IS DELIBERATELY NOT HERE. `Optional.or`, `Optional.stream` and
# `ifPresentOrElse` are Java 9 and later; this rung is Java 8 and the build runs
# on a JDK 8 toolchain, so they would not compile even if they were wanted. The
# call sites use orElse, orElseGet, orElseThrow, map, filter, ifPresent and
# isPresent, and `Optional.get` does not appear at all -- the scan counts it,
# and the count is zero. The class-file version does not move either: see below.
#
# ONE SIDE EFFECT, EXPECTED AND FROZEN. Replacing `List<X> dst = new
# ArrayList<>(); src.stream()...forEach(dst::add);` with a collector deletes the
# sink declaration, and with it its diamond. Nineteen diamonds leave the tree
# this way, 93 -> 74, every one of them a sink construction in a file this
# script also gives a collector. The baseline freezes 74 as the arrival figure
# and 93 as the departure denominator, and authoring/verify-baseline-step05.sh
# checks that the two still add up and that no diamond arrived.
#
# The blocks below are embedded verbatim from authoring/solution-src/apply8.py
# and authoring/evidence/step05-edits.txt by authoring/mk-solve05.py, which
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
cat > "${EDITS}" <<'EDITS05'
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/container/ContainerRegistryCreator.java
@@MD5IN c60f3412eb1c751d3e97d048822a8b89
@@MD5OUT 9442a1329e4c2c0125869c83818b172b
@@EDIT 1
@@OLD
import java.util.logging.Logger;
@@NEW
import java.util.Optional;
import java.util.logging.Logger;
@@EDIT 1
@@OLD
      String activeConfiguration = getActivatedConfiguration();
      for(ContainerDef container : event.getContainers())
      {
         if(
               (activeConfiguration != null && activeConfiguration.equals(container.getContainerName())) ||
               (activeConfiguration == null && container.isDefault()))
         {
            reg.create(container, serviceLoader);            
         }
      }
      if(activeConfiguration != null)
      {
         for(GroupDef group : event.getGroups())
         {
            if(activeConfiguration.equals(group.getGroupName()))
            {
               for(ContainerDef container : group.getGroupContainers())
               {
                  reg.create(container, serviceLoader);
               }
            }
         }
      }
@@NEW
      Optional<String> activeConfiguration = getActivatedConfiguration();
      for(ContainerDef container : event.getContainers())
      {
         if(activeConfiguration
               .map(name -> name.equals(container.getContainerName()))
               .orElseGet(() -> container.isDefault()))
         {
            reg.create(container, serviceLoader);
         }
      }
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
@@EDIT 1
@@OLD
   private String getActivatedConfiguration() 
   {
      try
      {
         if(System.getProperty(ARQUILLIAN_LAUNCH_PROPERTY) != null)
         {
            return System.getProperty(ARQUILLIAN_LAUNCH_PROPERTY);
         }
         
         try (BufferedReader reader = new BufferedReader(
               new InputStreamReader(
                     Thread.currentThread().getContextClassLoader().getResourceAsStream(ARQUILLIAN_LAUNCH_DEFAULT))))
         {
            return readActivatedValue(reader);
         }
         
      }
      catch (Exception e) 
      {
         log.info("Could not read active container configuration: " + e.getMessage());
      }
      return null;
   }
@@NEW
   private Optional<String> getActivatedConfiguration()
   {
      try
      {
         if(System.getProperty(ARQUILLIAN_LAUNCH_PROPERTY) != null)
         {
            return Optional.of(System.getProperty(ARQUILLIAN_LAUNCH_PROPERTY));
         }

         try (BufferedReader reader = new BufferedReader(
               new InputStreamReader(
                     Thread.currentThread().getContextClassLoader().getResourceAsStream(ARQUILLIAN_LAUNCH_DEFAULT))))
         {
            return readActivatedValue(reader);
         }

      }
      catch (Exception e)
      {
         log.info("Could not read active container configuration: " + e.getMessage());
      }
      return Optional.empty();
   }
@@EDIT 1
@@OLD
   private String readActivatedValue(BufferedReader reader)
      throws Exception
   {
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
   }
@@NEW
   private Optional<String> readActivatedValue(BufferedReader reader)
      throws Exception
   {
      String value;
      while( (value = reader.readLine()) != null)
      {
         if(value.startsWith("#"))
         {
            continue;
         }
         return Optional.of(value);
      }
      return Optional.empty();
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/tool/ToolingDeploymentFormatter.java
@@MD5IN cf1fef7f7684da22d7dc7273259f69db
@@MD5OUT dc34cd7bb2e9a06a185ca1341e964f0e
@@EDIT 1
@@OLD
import java.net.URL;
@@NEW
import java.net.URL;
import java.util.Optional;
@@EDIT 1
@@OLD
         String source = findResourceLocation(node.getAsset());
         
         xml.append("\t<asset")
            .append(" type=\"").append(node.getAsset().getClass().getSimpleName()).append("\"")
            .append(" path=\"").append(node.getPath().get()).append("\"");
            if(source != null)
            {
               xml.append(" source=\"").append(source).append("\"");
            }
@@NEW
         Optional<String> source = findResourceLocation(node.getAsset());

         xml.append("\t<asset")
            .append(" type=\"").append(node.getAsset().getClass().getSimpleName()).append("\"")
            .append(" path=\"").append(node.getPath().get()).append("\"");
            source.ifPresent(location -> xml.append(" source=\"").append(location).append("\""));
@@EDIT 1
@@OLD
   private String findResourceLocation(Asset asset) 
   {
      Class<?> assetClass = asset.getClass();
      
      if(assetClass == FileAsset.class)
      {
         return getInternalFieldValue(File.class, "file", asset).getAbsolutePath();
      }
      if(assetClass == ClassAsset.class)
      {
         return getInternalFieldValue(Class.class, "clazz", asset).getName();
      }
      if(assetClass == UrlAsset.class)
      {
         return getInternalFieldValue(URL.class, "url", asset).toExternalForm();
      }
      if(assetClass == ClassLoaderAsset.class)
      {
         return getInternalFieldValue(String.class, "resourceName", asset);
      }
      return null;
   }
@@NEW
   private Optional<String> findResourceLocation(Asset asset)
   {
      Class<?> assetClass = asset.getClass();

      if(assetClass == FileAsset.class)
      {
         return Optional.of(getInternalFieldValue(File.class, "file", asset).getAbsolutePath());
      }
      if(assetClass == ClassAsset.class)
      {
         return Optional.of(getInternalFieldValue(Class.class, "clazz", asset).getName());
      }
      if(assetClass == UrlAsset.class)
      {
         return Optional.of(getInternalFieldValue(URL.class, "url", asset).toExternalForm());
      }
      if(assetClass == ClassLoaderAsset.class)
      {
         return Optional.ofNullable(getInternalFieldValue(String.class, "resourceName", asset));
      }
      return Optional.empty();
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/protocol/ProtocolRegistryCreator.java
@@MD5IN 2c26d3dc3ce5c3c7eb1529c3185dd1ee
@@MD5OUT 2373c9fce9311583fdf56e4fca4dd109
@@EDIT 1
@@OLD
import java.util.Collection;
@@NEW
import java.util.Collection;
import java.util.Optional;
@@EDIT 1
@@OLD
      Protocol<?> defaultProtocol = null;
      DefaultProtocolDef defaultProtcolDef = event.getDefaultProtocol();
      if(defaultProtcolDef != null)
      {
         defaultProtocol = findMatch(new ProtocolDescription(defaultProtcolDef.getType()), protocols);
         if(defaultProtocol == null)
         {
            // TODO: add printout of found protocols
            throw new IllegalStateException("Defined default protocol " + defaultProtcolDef.getType() + " can not be found on classpath");
         }
      }
@@NEW
      Protocol<?> defaultProtocol = null;
      DefaultProtocolDef defaultProtcolDef = event.getDefaultProtocol();
      if(defaultProtcolDef != null)
      {
         // TODO: add printout of found protocols
         defaultProtocol = findMatch(new ProtocolDescription(defaultProtcolDef.getType()), protocols)
            .orElseThrow(() -> new IllegalStateException("Defined default protocol " + defaultProtcolDef.getType() + " can not be found on classpath"));
      }
@@EDIT 1
@@OLD
   private Protocol<?> findMatch(ProtocolDescription description, Collection<Protocol> protocols)
   {
      for(Protocol<?> protocol : protocols)
      {
         if(description.equals(protocol.getDescription()))
         {
            return protocol;
         }
      }
      return null;
   }
@@NEW
   private Optional<Protocol<?>> findMatch(ProtocolDescription description, Collection<Protocol> protocols)
   {
      for(Protocol<?> protocol : protocols)
      {
         if(description.equals(protocol.getDescription()))
         {
            return Optional.of(protocol);
         }
      }
      return Optional.empty();
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/EngineDefImpl.java
@@MD5IN c2dea2b2c3cf3a110fbfd71535015cfd
@@MD5OUT 768c7519f3079809a642c3b81cb18a1c
@@EDIT 1
@@OLD
import org.jboss.arquillian.impl.configuration.api.EngineDef;
@@NEW
import java.util.Optional;

import org.jboss.arquillian.impl.configuration.api.EngineDef;
@@EDIT 1
@@OLD
   @Override
   public String getDeploymentExportPath()
   {
      return getTextIfExists(exportPath);
   }
@@NEW
   @Override
   public String getDeploymentExportPath()
   {
      return getTextIfExists(exportPath).orElse(null);
   }
@@EDIT 1
@@OLD
   @Override
   public Integer getMaxTestClassesBeforeRestart()
   {
      return getTextIfExistsAsInteger(maxTestClasses);
   }
@@NEW
   @Override
   public Integer getMaxTestClassesBeforeRestart()
   {
      return getTextIfExistsAsInteger(maxTestClasses).orElse(null);
   }
@@EDIT 1
@@OLD
   private Integer getTextIfExistsAsInteger(Query query)
   {
      String text = getTextIfExists(query);
      if(text != null)
      {
         return Integer.parseInt(text);
      }
      return null;
   }
@@NEW
   private Optional<Integer> getTextIfExistsAsInteger(Query query)
   {
      return getTextIfExists(query).map(text -> Integer.parseInt(text));
   }
@@EDIT 1
@@OLD
   private String getTextIfExists(Query query)
   {
      Node propery = engine.getSingle(query);
      if(propery != null)
      {
         return propery.text();
      }
      return null;
   }
@@NEW
   private Optional<String> getTextIfExists(Query query)
   {
      Node propery = engine.getSingle(query);
      if(propery != null)
      {
         return Optional.ofNullable(propery.text());
      }
      return Optional.empty();
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java
@@MD5IN 2d0a66e6ca5b86a713dbe3817970eb4b
@@MD5OUT 2281df0416132c8c61c1eaa2c1cc4e50
@@EDIT 1
@@OLD
import java.util.List;
import java.util.Set;
import java.util.Stack;
@@NEW
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.Stack;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
      Context scopedContext = getScopedContext(scope);
      if(scopedContext == null)
      {
         throw new IllegalArgumentException("No Context registered with support for scope: " + scope);
      }
@@NEW
      Context scopedContext = getScopedContext(scope)
         .orElseThrow(() -> new IllegalArgumentException("No Context registered with support for scope: " + scope));
@@EDIT 1
@@OLD
   private List<ObserverMethod> resolveObservers(Class<?> eventType)
   {
      List<ObserverMethod> observers = new ArrayList<>();
      for(Extension extension : extensions)
      {
         extension.getObservers().stream()
            .filter(observer -> Reflections.getType(observer.getType()).isAssignableFrom(eventType) && !Reflections.isType(observer.getType(), EventContext.class))
            .forEach(observers::add);
      }
      Collections.sort(observers);
      return observers;
   }
@@NEW
   private List<ObserverMethod> resolveObservers(Class<?> eventType)
   {
      List<ObserverMethod> observers = extensions.stream()
         .flatMap(extension -> extension.getObservers().stream())
         .filter(observer -> Reflections.getType(observer.getType()).isAssignableFrom(eventType) && !Reflections.isType(observer.getType(), EventContext.class))
         .collect(Collectors.toCollection(ArrayList::new));
      Collections.sort(observers);
      return observers;
   }
@@EDIT 1
@@OLD
   private List<Context> resolveActiveContexts()
   {
      List<Context> activeContexts = new ArrayList<>();
      contexts.stream().filter(context -> context.isActive()).forEach(activeContexts::add);
      return activeContexts;
   }
@@NEW
   private List<Context> resolveActiveContexts()
   {
      return contexts.stream()
         .filter(context -> context.isActive())
         .collect(Collectors.toCollection(ArrayList::new));
   }
@@EDIT 1
@@OLD
   private Context getScopedContext(Class<? extends Annotation> scope)
   {
      for(Context context : contexts)
      {
         if(context.getScope() == scope)
         {
            return context;
         }
      }
      return null;
   }
@@NEW
   private Optional<Context> getScopedContext(Class<? extends Annotation> scope)
   {
      return contexts.stream()
         .filter(context -> context.getScope() == scope)
         .findFirst();
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/domain/ContainerRegistry.java
@@MD5IN e24393ec89b2578c6f05e8dd4a197fd8
@@MD5OUT 16026a5796446ba263dba80b5cd68a2b
@@EDIT 1
@@OLD
import java.util.List;
@@NEW
import java.util.List;
import java.util.Optional;
@@EDIT 1
@@OLD
   public Container getContainer(String name)
   {
      return findMatchingContainer(name);
   }
@@NEW
   public Container getContainer(String name)
   {
      return findMatchingContainer(name).orElse(null);
   }
@@EDIT 1
@@OLD
   public Container getContainer(TargetDescription target)
   {
      Validate.notNull(target, "Target must be specified");
      if(TargetDescription.DEFAULT.equals(target))
      {
         return findDefaultContainer();
      }
      return findMatchingContainer(target.getName());
   }
@@NEW
   public Container getContainer(TargetDescription target)
   {
      Validate.notNull(target, "Target must be specified");
      if(TargetDescription.DEFAULT.equals(target))
      {
         return findDefaultContainer().orElse(null);
      }
      return findMatchingContainer(target.getName()).orElse(null);
   }
@@EDIT 1
@@OLD
   private Container findDefaultContainer()
   {
      if(containers.size() == 1)
      {
         return containers.get(0);
      }
      for(Container container : containers)
      {
        if(container.getContainerConfiguration().isDefault())
        {
           return container;
        }
      }
      return null;
   }
@@NEW
   private Optional<Container> findDefaultContainer()
   {
      if(containers.size() == 1)
      {
         return Optional.of(containers.get(0));
      }
      return containers.stream()
         .filter(container -> container.getContainerConfiguration().isDefault())
         .findFirst();
   }
@@EDIT 1
@@OLD
   private Container findMatchingContainer(String name)
   {
      for(Container container: containers)
      {
         if(container.getName().equals(name))
         {
            return container;
         }
      }
      return null;
   }
@@NEW
   private Optional<Container> findMatchingContainer(String name)
   {
      return containers.stream()
         .filter(container -> container.getName().equals(name))
         .findFirst();
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/domain/ProtocolRegistry.java
@@MD5IN 724835c415410a7821c44105eabe46a4
@@MD5OUT fb94e3d3ab878061c199f68a03888fa7
@@EDIT 1
@@OLD
import java.util.List;
@@NEW
import java.util.List;
import java.util.Optional;
@@EDIT 1
@@OLD
      ProtocolDefinition protocolAllReadyRegistered = findSpecificProtocol(protocolDefinition.getProtocol().getDescription());
      if(protocolAllReadyRegistered != null)
      {
         throw new IllegalArgumentException(
               "Protocol with description " + protocolDefinition.getProtocol().getDescription() + " allready registered. " +
                "Registered " + protocolAllReadyRegistered.getClass() + ", trying to register " + protocolDefinition.getProtocol().getClass());
      }
@@NEW
      findSpecificProtocol(protocolDefinition.getProtocol().getDescription())
         .ifPresent(protocolAllReadyRegistered -> {
            throw new IllegalArgumentException(
                  "Protocol with description " + protocolDefinition.getProtocol().getDescription() + " allready registered. " +
                   "Registered " + protocolAllReadyRegistered.getClass() + ", trying to register " + protocolDefinition.getProtocol().getClass());
         });
@@EDIT 1
@@OLD
   public ProtocolDefinition getProtocol(ProtocolDescription protocolDescription)
   {
      Validate.notNull(protocolDescription, "ProtocolDescription must be specified");
      if(ProtocolDescription.DEFAULT.equals(protocolDescription))
      {
         return findDefaultProtocol();
      }
      return findSpecificProtocol(protocolDescription);
   }
@@NEW
   public ProtocolDefinition getProtocol(ProtocolDescription protocolDescription)
   {
      Validate.notNull(protocolDescription, "ProtocolDescription must be specified");
      if(ProtocolDescription.DEFAULT.equals(protocolDescription))
      {
         return findDefaultProtocol().orElse(null);
      }
      return findSpecificProtocol(protocolDescription).orElse(null);
   }
@@EDIT 1
@@OLD
   private ProtocolDefinition findDefaultProtocol()
   {
      for(ProtocolDefinition def : protocols)
      {
         if(def.isDefaultProtocol())
         {
            return def;
         }
      }
      if(protocols.size() == 1)
      {
         return protocols.get(0);
      }
      return null;
   }
@@NEW
   private Optional<ProtocolDefinition> findDefaultProtocol()
   {
      Optional<ProtocolDefinition> declaredDefault = protocols.stream()
         .filter(def -> def.isDefaultProtocol())
         .findFirst();
      if(declaredDefault.isPresent() || protocols.size() != 1)
      {
         return declaredDefault;
      }
      return Optional.of(protocols.get(0));
   }
@@EDIT 1
@@OLD
   private ProtocolDefinition findSpecificProtocol(ProtocolDescription protocolDescription)
   {
      for(ProtocolDefinition protocol : protocols)
      {
         if(protocolDescription.equals(protocol.getProtocol().getDescription()))
         {
            return protocol;
         }
      }
      return null;
   }
@@NEW
   private Optional<ProtocolDefinition> findSpecificProtocol(ProtocolDescription protocolDescription)
   {
      return protocols.stream()
         .filter(protocol -> protocolDescription.equals(protocol.getProtocol().getDescription()))
         .findFirst();
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/ArquillianResourceTestEnricher.java
@@MD5IN cd695ac27b1db2c3e6247e9aa052ec1f
@@MD5OUT 5a112e5e41a63038675526b62172e295
@@EDIT 1
@@OLD
import java.util.HashMap;
import java.util.Map;
@@NEW
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
@@EDIT 1
@@OLD
      for(int i = 0; i < parameterTypes.length; i++)
      {
         ArquillianResource resource = getResourceAnnotation(method.getParameterAnnotations()[i]);
         if(resource != null)
         {
            values[i] = lookup(method.getParameterTypes()[i], resource);   
         }
          
      }
@@NEW
      for(int i = 0; i < parameterTypes.length; i++)
      {
         final int index = i;
         getResourceAnnotation(method.getParameterAnnotations()[i])
            .ifPresent(resource -> values[index] = lookup(method.getParameterTypes()[index], resource));
      }
@@EDIT 1
@@OLD
   private ArquillianResource getResourceAnnotation(Annotation[] annotations)
   {
      for(Annotation annotation : annotations)
      {
         if(annotation.annotationType() == ArquillianResource.class)
         {
            return (ArquillianResource)annotation;
         }
      }
      return null;
   }
@@NEW
   private Optional<ArquillianResource> getResourceAnnotation(Annotation[] annotations)
   {
      return Arrays.stream(annotations)
         .filter(annotation -> annotation.annotationType() == ArquillianResource.class)
         .map(annotation -> (ArquillianResource)annotation)
         .findFirst();
   }
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/ExceptionProxy.java
@@MD5IN f3e060ec84c99f90209d6e09a293c7a0
@@MD5OUT 2124f3422b8095619a8307c99851c853
@@EDIT 1
@@OLD
import java.lang.reflect.Constructor;
@@NEW
import java.lang.reflect.Constructor;
import java.util.Optional;
@@EDIT 1
@@OLD
		Object object = buildObjectFromClassConstructors(clazz);
@@NEW
		Object object = buildObjectFromClassConstructors(clazz).orElse(null);
@@EDIT 1
@@OLD
	private Object buildObjectFromClassConstructors(Class<?> clazz) {
		// try the (String,Throwable) constructor first
		Object object = buildExceptionFromConstructor(clazz, new Class<?>[] {
				String.class, Throwable.class }, new Object[] { message,
				getCause() });
		if (object != null) {
			return object;
		}

		// try the (String,Exception) constructor first
		object = buildExceptionFromConstructor(clazz, new Class<?>[] {
				String.class, Exception.class }, new Object[] { message,
				getCause() });
		if (object != null) {
			return object;
		}

		// try the (String) constructor next
		object = buildExceptionFromConstructor(clazz,
				new Class<?>[] { String.class }, new Object[] { message });
		if (object != null) {
			return object;
		}

		return null;
	}
@@NEW
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
@@EDIT 1
@@OLD
	private <T> T buildExceptionFromConstructor(Class<T> clazz,
			Class<?>[] signature, Object[] params) {
		Constructor<?> constructor = null;
		// try the message,cause constructor first
		try {
			constructor = clazz.getConstructor(signature);
		} catch (SecurityException e) {
			// we'll try the next signature
		} catch (NoSuchMethodException e) {
			// we'll try the next signature
		}
		// if we found a working constructor, use it
		if (constructor != null) {
			try {
				@SuppressWarnings("unchecked")
				T result = (T) constructor.newInstance(params);
				return result;
			} catch (Throwable e) {
				return null;
			}
		}
		// no matching constructor, no result
		return null;

	}
@@NEW
	private <T> Optional<T> buildExceptionFromConstructor(Class<T> clazz,
			Class<?>[] signature, Object[] params) {
		Constructor<?> constructor = null;
		// try the message,cause constructor first
		try {
			constructor = clazz.getConstructor(signature);
		} catch (SecurityException e) {
			// we'll try the next signature
		} catch (NoSuchMethodException e) {
			// we'll try the next signature
		}
		// if we found a working constructor, use it
		if (constructor != null) {
			try {
				@SuppressWarnings("unchecked")
				T result = (T) constructor.newInstance(params);
				return Optional.of(result);
			} catch (Throwable e) {
				return Optional.empty();
			}
		}
		// no matching constructor, no result
		return Optional.empty();

	}
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/deployment/DeploymentScenario.java
@@MD5IN c4276911cf34dd6baae0f05f65fe95af
@@MD5OUT b952f44740fdfa3ba811270536ac9489
@@EDIT 1
@@OLD
import java.util.HashSet;
import java.util.List;
import java.util.Set;
@@NEW
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   public Set<TargetDescription> getTargets()
   {
      Set<TargetDescription> targets = new HashSet<>();
      
      deployments.stream().map(desc -> desc.getTarget()).forEach(targets::add);
      return targets;
   }
@@NEW
   public Set<TargetDescription> getTargets()
   {
      return deployments.stream()
         .map(desc -> desc.getTarget())
         .collect(Collectors.toSet());
   }
@@EDIT 1
@@OLD
   public Set<ProtocolDescription> getProtocols()
   {
      Set<ProtocolDescription> protocols = new HashSet<>();
      
      deployments.stream().map(desc -> desc.getProtocol()).forEach(protocols::add);
      return protocols;
   }
@@NEW
   public Set<ProtocolDescription> getProtocols()
   {
      return deployments.stream()
         .map(desc -> desc.getProtocol())
         .collect(Collectors.toSet());
   }
@@EDIT 1
@@OLD
   public DeploymentDescription getDeployment(DeploymentTargetDescription target)
   {
      Validate.notNull(target, "Target must be specified");
      if(DeploymentTargetDescription.DEFAULT.equals(target))
      {
         return findDefaultDeployment();
      }
      return findMatchingDeployment(target);
   }
@@NEW
   public DeploymentDescription getDeployment(DeploymentTargetDescription target)
   {
      Validate.notNull(target, "Target must be specified");
      if(DeploymentTargetDescription.DEFAULT.equals(target))
      {
         return findDefaultDeployment().orElse(null);
      }
      return findMatchingDeployment(target).orElse(null);
   }
@@EDIT 1
@@OLD
   private DeploymentDescription findDefaultDeployment()
   {
      if(deployments.size() == 1)
      {
         return deployments.get(0);
      }
      return null;
   }
@@NEW
   private Optional<DeploymentDescription> findDefaultDeployment()
   {
      if(deployments.size() == 1)
      {
         return Optional.of(deployments.get(0));
      }
      return Optional.empty();
   }
@@EDIT 1
@@OLD
   private DeploymentDescription findMatchingDeployment(DeploymentTargetDescription target)
   {
      for(DeploymentDescription deployment : deployments)
      {
         if(deployment.getName().equals(target.getName()))
         {
            return deployment;
         }
      }
      return null;
   }
@@NEW
   private Optional<DeploymentDescription> findMatchingDeployment(DeploymentTargetDescription target)
   {
      return deployments.stream()
         .filter(deployment -> deployment.getName().equals(target.getName()))
         .findFirst();
   }
@@EDIT 1
@@OLD
   public List<DeploymentDescription> getStartupDeploymentsFor(TargetDescription target)
   {
      Validate.notNull(target, "Target must be specified");
      List<DeploymentDescription> startupDeployments = new ArrayList<>();
      deployments.stream()
         .filter(deployment -> deployment.managed() && target.equals(deployment.getTarget()))
         .forEach(startupDeployments::add);
      // sort them by order
      Collections.sort(startupDeployments, (o1, o2) -> new Integer(o1.getOrder()).compareTo(o2.getOrder()));
      
      return Collections.unmodifiableList(startupDeployments);
   }
@@NEW
   public List<DeploymentDescription> getStartupDeploymentsFor(TargetDescription target)
   {
      Validate.notNull(target, "Target must be specified");
      List<DeploymentDescription> startupDeployments = deployments.stream()
         .filter(deployment -> deployment.managed() && target.equals(deployment.getTarget()))
         .collect(Collectors.toCollection(ArrayList::new));
      // sort them by order
      Collections.sort(startupDeployments, (o1, o2) -> new Integer(o1.getOrder()).compareTo(o2.getOrder()));

      return Collections.unmodifiableList(startupDeployments);
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java
@@MD5IN 74229c373928c4f71422bc6abae0ce9c
@@MD5OUT 362e7c8ef155a48a4fad3015a6516d75
@@EDIT 1
@@OLD
import java.util.logging.Logger;
@@NEW
import java.util.logging.Logger;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   private <T> Set<T> createInstances(Class<T> serviceType, Set<Class<? extends T>> providers)
   {
      Set<T> providerImpls = new LinkedHashSet<>();
      providers.stream()
         .map(serviceClass -> createInstance(serviceClass))
         .forEach(providerImpls::add);
      return providerImpls;
   }
@@NEW
   private <T> Set<T> createInstances(Class<T> serviceType, Set<Class<? extends T>> providers)
   {
      return providers.stream()
         .map(serviceClass -> createInstance(serviceClass))
         .collect(Collectors.toCollection(LinkedHashSet::new));
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/client/deployment/DeploymentGenerator.java
@@MD5IN 986abf99a629fe3a8665f3301eb47df1
@@MD5OUT 0e95453f157635cabe635e2db0fcf131
@@EDIT 1
@@OLD
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
@@NEW
import java.util.Collection;
import java.util.List;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   private List<Archive<?>> loadAuxiliaryArchives(DeploymentDescription deployment) 
   {
      List<Archive<?>> archives = new ArrayList<>();
      ClassLoader classLoader = containerRegistry.get().getContainer(deployment.getTarget()).getClassLoader();
      
      // load based on the Containers ClassLoader
      Collection<AuxiliaryArchiveAppender> archiveAppenders = serviceLoader.get().all(
            classLoader, AuxiliaryArchiveAppender.class);
      
      try
      {
         ThreadContext.set(classLoader);
         archiveAppenders.stream()
            .map(archiveAppender -> archiveAppender.createAuxiliaryArchive())
            .forEach(archives::add);
      }
      finally
      {
         ThreadContext.reset();
      }
      return archives;
   }
@@NEW
   private List<Archive<?>> loadAuxiliaryArchives(DeploymentDescription deployment) 
   {
      ClassLoader classLoader = containerRegistry.get().getContainer(deployment.getTarget()).getClassLoader();

      // load based on the Containers ClassLoader
      Collection<AuxiliaryArchiveAppender> archiveAppenders = serviceLoader.get().all(
            classLoader, AuxiliaryArchiveAppender.class);

      try
      {
         ThreadContext.set(classLoader);
         return archiveAppenders.stream()
            .map(archiveAppender -> archiveAppender.createAuxiliaryArchive())
            .collect(Collectors.toList());
      }
      finally
      {
         ThreadContext.reset();
      }
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/ArquillianDescriptorImpl.java
@@MD5IN 60ce12a14f752b8c2033d18a7f98b093
@@MD5OUT 0d231e867ced84b0052bb590df7eee3f
@@EDIT 1
@@OLD
import java.util.ArrayList;
import java.util.List;
@@NEW
import java.util.List;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   public List<ContainerDef> getContainers()
   {
      List<ContainerDef> containers = new ArrayList<>();
      model.get("container").stream()
         .map(container -> new ContainerDefImpl(getDescriptorName(), model, container))
         .forEach(containers::add);
      return containers;
   }
@@NEW
   public List<ContainerDef> getContainers()
   {
      return model.get("container").stream()
         .map(container -> new ContainerDefImpl(getDescriptorName(), model, container))
         .collect(Collectors.toList());
   }
@@EDIT 1
@@OLD
   public List<GroupDef> getGroups()
   {
      List<GroupDef> groups = new ArrayList<>();
      model.get("group").stream()
         .map(group -> new GroupDefImpl(getDescriptorName(), model, group))
         .forEach(groups::add);
      return groups;
   }
@@NEW
   public List<GroupDef> getGroups()
   {
      return model.get("group").stream()
         .map(group -> new GroupDefImpl(getDescriptorName(), model, group))
         .collect(Collectors.toList());
   }
@@EDIT 1
@@OLD
   public List<ExtensionDef> getExtensions()
   {
      List<ExtensionDef> extensions = new ArrayList<>();
      model.get("extension").stream()
         .map(extension -> new ExtensionDefImpl(getDescriptorName(), model, extension))
         .forEach(extensions::add);
      return extensions;
   }
@@NEW
   public List<ExtensionDef> getExtensions()
   {
      return model.get("extension").stream()
         .map(extension -> new ExtensionDefImpl(getDescriptorName(), model, extension))
         .collect(Collectors.toList());
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/ContainerDefImpl.java
@@MD5IN 057d3a03530f6ff3ce9d7c1c92420955
@@MD5OUT 979a3842ef48c6a953b50eede26546d7
@@EDIT 1
@@OLD
import java.util.Map;
@@NEW
import java.util.Map;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   public List<ProtocolDef> getProtocols()
   {
      List<ProtocolDef> protocols = new ArrayList<>();
      container.get("protocol").stream()
         .map(proto -> new ProtocolDefImpl(getDescriptorName(), getRootNode(), container, proto))
         .forEach(protocols::add);
      return protocols;
   }
@@NEW
   public List<ProtocolDef> getProtocols()
   {
      return container.get("protocol").stream()
         .map(proto -> new ProtocolDefImpl(getDescriptorName(), getRootNode(), container, proto))
         .collect(Collectors.toList());
   }
@@EDIT 1
@@OLD
   public List<String> getDependencies()
   {
      List<String> dependencies = new ArrayList<>();
      if(container.getSingle("dependencies") != null)
      {
         container.getSingle("dependencies").get("dependency").stream()
            .map(dep -> dep.text())
            .forEach(dependencies::add);
      }
      return dependencies;
   }
@@NEW
   public List<String> getDependencies()
   {
      if(container.getSingle("dependencies") == null)
      {
         return new ArrayList<>();
      }
      return container.getSingle("dependencies").get("dependency").stream()
         .map(dep -> dep.text())
         .collect(Collectors.toList());
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/GroupContainerDefImpl.java
@@MD5IN d4fe678ec5c891ea9f5db27abacc8498
@@MD5OUT 3d015b12e2a802bb3dd8bc7185546b03
@@EDIT 1
@@OLD
import java.util.ArrayList;
import java.util.List;
@@NEW
import java.util.List;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   public List<ContainerDef> getGroupContainers()
   {
      List<ContainerDef> containers = new ArrayList<>();
      group.get("container").stream()
         .map(container -> new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, container))
         .forEach(containers::add);
      return containers;
   }
@@NEW
   public List<ContainerDef> getGroupContainers()
   {
      return group.get("container").stream()
         .map(container -> new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, container))
         .collect(Collectors.toList());
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/configuration/GroupDefImpl.java
@@MD5IN 68b50b266be086a45800df4c16dd538a
@@MD5OUT 508c311bb7875b720ebbb9cc69f7ab34
@@EDIT 1
@@OLD
import java.util.ArrayList;
import java.util.List;
@@NEW
import java.util.List;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   public List<ContainerDef> getGroupContainers()
   {
      List<ContainerDef> containers = new ArrayList<>();
      group.get("container").stream()
         .map(container -> new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, container))
         .forEach(containers::add);
      return containers;
   }
@@NEW
   public List<ContainerDef> getGroupContainers()
   {
      return group.get("container").stream()
         .map(container -> new GroupContainerDefImpl(getDescriptorName(), getRootNode(), group, container))
         .collect(Collectors.toList());
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/ExtensionImpl.java
@@MD5IN b4e0333c48fe8433dec42ae6d44b4ef4
@@MD5OUT f4f5e36a0f731193b74afe55ec0c1f30
@@EDIT 1
@@OLD
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
@@NEW
import java.util.Collections;
import java.util.List;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   private static List<ObserverMethod> observers(Object extension, List<Method> observerMethods) 
   {
      List<ObserverMethod> result = new ArrayList<>();
      observerMethods.stream()
         .map(method -> ObserverImpl.of(extension, method))
         .forEach(result::add);
      return result;
   }
@@NEW
   private static List<ObserverMethod> observers(Object extension, List<Method> observerMethods) 
   {
      return observerMethods.stream()
         .map(method -> ObserverImpl.of(extension, method))
         .collect(Collectors.toList());
   }
@@EDIT 1
@@OLD
   private static List<InjectionPoint> injections(Object extension, List<Field> injectionPoints) 
   {
      List<InjectionPoint> result = new ArrayList<>();
      injectionPoints.stream()
         .map(field -> InjectionPointImpl.of(extension, field))
         .forEach(result::add);
      return result;
   }
@@NEW
   private static List<InjectionPoint> injections(Object extension, List<Field> injectionPoints) 
   {
      return injectionPoints.stream()
         .map(field -> InjectionPointImpl.of(extension, field))
         .collect(Collectors.toList());
   }
@@EDIT 1
@@OLD
   private static List<EventPoint> events(Object extension, List<Field> eventPoints) 
   {
      List<EventPoint> result = new ArrayList<>();
      eventPoints.stream().map(method -> EventPointImpl.of(extension, method)).forEach(result::add);
      return result;
   }
@@NEW
   private static List<EventPoint> events(Object extension, List<Field> eventPoints) 
   {
      return eventPoints.stream()
         .map(method -> EventPointImpl.of(extension, method))
         .collect(Collectors.toList());
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/core/Reflections.java
@@MD5IN 0f243c7f5b65d420ce8b671b0d0ec3a4
@@MD5OUT 36ddfbc765c126e93e30bfc81a90d62d
@@EDIT 1
@@OLD
import java.util.List;
@@NEW
import java.util.List;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   public static List<Method> getObserverMethods(Class<?> clazz)
   {
      List<Method> observerMethods = new ArrayList<>();
      if(clazz == null)
      {
         return observerMethods;
      }
      Arrays.stream(clazz.getMethods())
         .filter(method -> isObserverMethod(method))
         .forEach(observerMethods::add);
      observerMethods.addAll(getObserverMethods(clazz.getSuperclass()));
      return observerMethods;
   }
@@NEW
   public static List<Method> getObserverMethods(Class<?> clazz)
   {
      if(clazz == null)
      {
         return new ArrayList<>();
      }
      List<Method> observerMethods = Arrays.stream(clazz.getMethods())
         .filter(method -> isObserverMethod(method))
         .collect(Collectors.toCollection(ArrayList::new));
      observerMethods.addAll(getObserverMethods(clazz.getSuperclass()));
      return observerMethods;
   }
@@EDIT 1
@@OLD
   public static List<Field> getFieldInjectionPoints(Class<?> clazz)
   {
      List<Field> injectionPoints = new ArrayList<>();
      if(clazz == null)
      {
         return injectionPoints;
      }
      Arrays.stream(clazz.getDeclaredFields())
         .filter(field -> isInjectionPoint(field))
         .forEach(injectionPoints::add);
      injectionPoints.addAll(getFieldInjectionPoints(clazz.getSuperclass()));
      return injectionPoints;
   }
@@NEW
   public static List<Field> getFieldInjectionPoints(Class<?> clazz)
   {
      if(clazz == null)
      {
         return new ArrayList<>();
      }
      List<Field> injectionPoints = Arrays.stream(clazz.getDeclaredFields())
         .filter(field -> isInjectionPoint(field))
         .collect(Collectors.toCollection(ArrayList::new));
      injectionPoints.addAll(getFieldInjectionPoints(clazz.getSuperclass()));
      return injectionPoints;
   }
@@EDIT 1
@@OLD
   public static List<Field> getEventPoints(Class<?> clazz)
   {
      List<Field> eventPoints = new ArrayList<>();
      if(clazz == null)
      {
         return eventPoints;
      }
      Arrays.stream(clazz.getDeclaredFields())
         .filter(field -> isEventPoint(field))
         .forEach(eventPoints::add);
      eventPoints.addAll(getEventPoints(clazz.getSuperclass()));
      return eventPoints;
   }
@@NEW
   public static List<Field> getEventPoints(Class<?> clazz)
   {
      if(clazz == null)
      {
         return new ArrayList<>();
      }
      List<Field> eventPoints = Arrays.stream(clazz.getDeclaredFields())
         .filter(field -> isEventPoint(field))
         .collect(Collectors.toCollection(ArrayList::new));
      eventPoints.addAll(getEventPoints(clazz.getSuperclass()));
      return eventPoints;
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/enricher/resource/URLResourceProvider.java
@@MD5IN ae6c4f95771a08383042877f7df58325
@@MD5OUT a3ceb8002ecadb44da34c6584d648829
@@EDIT 1
@@OLD
import java.util.HashSet;
import java.util.List;
import java.util.Set;
@@NEW
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   private boolean allInSameContext(List<Servlet> servlets)
   {
      Set<String> context = new HashSet<>();
      servlets.stream().map(servlet -> servlet.getContextRoot()).forEach(context::add);
      return context.size() == 1;
   }
@@NEW
   private boolean allInSameContext(List<Servlet> servlets)
   {
      Set<String> context = servlets.stream()
         .map(servlet -> servlet.getContextRoot())
         .collect(Collectors.toSet());
      return context.size() == 1;
   }
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/TestClass.java
@@MD5IN 2d02bc759670b4e42b6f727c65020899
@@MD5OUT bc6ba23e2997028716d6da0ce668056b
@@EDIT 1
@@OLD
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
@@NEW
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   public Method[] getMethods(Class<? extends Annotation> annotation)
   {
      List<Method> foundMethods = new ArrayList<>();
      Method[] methods = testClass.getMethods();
      Arrays.stream(methods)
         .filter(method -> method.isAnnotationPresent(annotation))
         .forEach(foundMethods::add);
      return foundMethods.toArray(new Method[0]);
   }
@@NEW
   public Method[] getMethods(Class<? extends Annotation> annotation)
   {
      List<Method> foundMethods = Arrays.stream(testClass.getMethods())
         .filter(method -> method.isAnnotationPresent(annotation))
         .collect(Collectors.toList());
      return foundMethods.toArray(new Method[0]);
   }
@@ENDFILE
@@FILE testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/MethodParameterInjectionPoint.java
@@MD5IN d86330b86149d61a3974ff4a50123981
@@MD5OUT d08e669f3be0f292dc0ae4b0a1e88871
@@EDIT 1
@@OLD
import java.util.Set;
@@NEW
import java.util.Set;
import java.util.stream.Collectors;
@@EDIT 1
@@OLD
   public Set<Annotation> getQualifiers()
   {
      Set<Annotation> qualifiers = new HashSet<>();
      Arrays.stream(method.getParameterAnnotations()[position])
         .filter(annotation -> beanManager.isQualifier(annotation.annotationType()))
         .forEach(qualifiers::add);
      /*
       * TODO: ARQ-240 We should not add @Default by default, this should be handled by CDI. 
       * Due to bug in Weld 1.0.0.SP4(fixed in trunk). Remove this when 1.1.0 is out. 
       */
      if(qualifiers.size() == 0)
      {
         qualifiers.add(new DefaultLiteral());
      }
      return qualifiers;
   }
@@NEW
   public Set<Annotation> getQualifiers()
   {
      Set<Annotation> qualifiers = Arrays.stream(method.getParameterAnnotations()[position])
         .filter(annotation -> beanManager.isQualifier(annotation.annotationType()))
         .collect(Collectors.toCollection(HashSet::new));
      /*
       * TODO: ARQ-240 We should not add @Default by default, this should be handled by CDI. 
       * Due to bug in Weld 1.0.0.SP4(fixed in trunk). Remove this when 1.1.0 is out. 
       */
      if(qualifiers.size() == 0)
      {
         qualifiers.add(new DefaultLiteral());
      }
      return qualifiers;
   }
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/protocol/ProtocolDescription.java
@@MD5IN 6951aac7caa06bb4c311b926f38d9c19
@@MD5OUT 9cd1bb557af84daf88cca134d0041e17
@@EDIT 1
@@OLD
import org.jboss.arquillian.spi.util.Validate;
@@NEW
import java.util.Objects;

import org.jboss.arquillian.spi.util.Validate;
@@EDIT 1
@@OLD
   @Override
   public int hashCode()
   {
      final int prime = 31;
      int result = 1;
      result = prime * result + ((name == null) ? 0 : name.hashCode());
      return result;
   }
@@NEW
   @Override
   public int hashCode()
   {
      return Objects.hash(name);
   }
@@EDIT 1
@@OLD
   @Override
   public boolean equals(Object obj)
   {
      if (this == obj)
         return true;
      if (obj == null)
         return false;
      if (getClass() != obj.getClass())
         return false;
      ProtocolDescription other = (ProtocolDescription) obj;
      if (name == null)
      {
         if (other.name != null)
            return false;
      }
      else if (!name.equals(other.name))
         return false;
      return true;
   }
@@NEW
   @Override
   public boolean equals(Object obj)
   {
      if (this == obj)
         return true;
      if (obj == null)
         return false;
      if (getClass() != obj.getClass())
         return false;
      ProtocolDescription other = (ProtocolDescription) obj;
      return Objects.equals(name, other.name);
   }
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/test/DeploymentTargetDescription.java
@@MD5IN 5c169058171665fd33f333d66eb384a2
@@MD5OUT 7a4f1ed43085c5ba30b149efb83acd10
@@EDIT 1
@@OLD
package org.jboss.arquillian.spi.client.test;

/**
@@NEW
package org.jboss.arquillian.spi.client.test;

import java.util.Objects;

/**
@@EDIT 1
@@OLD
   @Override
   public int hashCode()
   {
      final int prime = 31;
      int result = 1;
      result = prime * result + ((name == null) ? 0 : name.hashCode());
      return result;
   }
@@NEW
   @Override
   public int hashCode()
   {
      return Objects.hash(name);
   }
@@EDIT 1
@@OLD
   @Override
   public boolean equals(Object obj)
   {
      if (this == obj)
         return true;
      if (obj == null)
         return false;
      if (getClass() != obj.getClass())
         return false;
      DeploymentTargetDescription other = (DeploymentTargetDescription) obj;
      if (name == null)
      {
         if (other.name != null)
            return false;
      }
      else if (!name.equals(other.name))
         return false;
      return true;
   }
@@NEW
   @Override
   public boolean equals(Object obj)
   {
      if (this == obj)
         return true;
      if (obj == null)
         return false;
      if (getClass() != obj.getClass())
         return false;
      DeploymentTargetDescription other = (DeploymentTargetDescription) obj;
      return Objects.equals(name, other.name);
   }
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/client/test/TargetDescription.java
@@MD5IN 56ea79166c92014deff9152ee2ef167a
@@MD5OUT 82f98c9562a133cb6675a8d789aa4f4e
@@EDIT 1
@@OLD
package org.jboss.arquillian.spi.client.test;

/**
@@NEW
package org.jboss.arquillian.spi.client.test;

import java.util.Objects;

/**
@@EDIT 1
@@OLD
   @Override
   public int hashCode()
   {
      final int prime = 31;
      int result = 1;
      result = prime * result + ((name == null) ? 0 : name.hashCode());
      return result;
   }
@@NEW
   @Override
   public int hashCode()
   {
      return Objects.hash(name);
   }
@@EDIT 1
@@OLD
   @Override
   public boolean equals(Object obj)
   {
      if (this == obj)
         return true;
      if (obj == null)
         return false;
      if (getClass() != obj.getClass())
         return false;
      TargetDescription other = (TargetDescription) obj;
      if (name == null)
      {
         if (other.name != null)
            return false;
      }
      else if (!name.equals(other.name))
         return false;
      return true;
   }
@@NEW
   @Override
   public boolean equals(Object obj)
   {
      if (this == obj)
         return true;
      if (obj == null)
         return false;
      if (getClass() != obj.getClass())
         return false;
      TargetDescription other = (TargetDescription) obj;
      return Objects.equals(name, other.name);
   }
@@ENDFILE
@@FILE impl-base/src/main/java/org/jboss/arquillian/impl/execution/LocalTestExecuter.java
@@MD5IN 19dd979d52e9bbc609ef3c27bbc43f91
@@MD5OUT 3a62b654ca08e6487028ac44ffc5e863
@@EDIT 1
@@OLD
import java.lang.reflect.Method;
import java.util.Collection;
@@NEW
import java.lang.reflect.Method;
import java.time.Instant;
import java.util.Collection;
@@EDIT 1
@@OLD
         result.setEnd(System.currentTimeMillis());         
@@NEW
         result.setEnd(Instant.now().toEpochMilli());
@@ENDFILE
@@FILE protocols/jmx/src/main/java/org/jboss/arquillian/protocol/jmx/JMXMethodExecutor.java
@@MD5IN ddc4370fa46a6bcb736237f9f8f6b599
@@MD5OUT d32097737ad69b0213cfc93458ef8138
@@EDIT 1
@@OLD
import java.util.ArrayList;
@@NEW
import java.time.Instant;
import java.util.ArrayList;
@@EDIT 1
@@OLD
         result.setEnd(System.currentTimeMillis());
@@NEW
         result.setEnd(Instant.now().toEpochMilli());
@@ENDFILE
@@FILE protocols/servlet/src/main/java/org/jboss/arquillian/protocol/servlet/ServletMethodExecutor.java
@@MD5IN 0dc05622a671b7a4397b5db6d6f0546c
@@MD5OUT f21ab8d99d74a20796cbc15a68817f64
@@EDIT 1
@@OLD
import java.net.URLConnection;
@@NEW
import java.net.URLConnection;
import java.time.Duration;
import java.time.Instant;
@@EDIT 1
@@OLD
      long timeoutTime = System.currentTimeMillis() + 1000;
      boolean interrupted = false;
      while (timeoutTime > System.currentTimeMillis())
@@NEW
      Instant deadline = Instant.now().plus(Duration.ofSeconds(1));
      boolean interrupted = false;
      while (Instant.now().isBefore(deadline))
@@ENDFILE
@@FILE spi/src/main/java/org/jboss/arquillian/spi/TestResult.java
@@MD5IN 5e4ff451419397f382f3503e8af28265
@@MD5OUT dc93e43139caa34a803bfbae855fe685
@@EDIT 1
@@OLD
import java.io.Serializable;
@@NEW
import java.io.Serializable;
import java.time.Instant;
@@EDIT 1
@@OLD
		this.start = System.currentTimeMillis();
@@NEW
		this.start = Instant.now().toEpochMilli();
@@ENDFILE
EDITS05

# Captured rather than streamed, because the line it prints is the assertion:
# apply8.py refuses a tree whose files do not arrive at the md5 the edits were
# computed against, and refuses a file that does not leave at the md5 the
# generator computed -- but a solve.sh that let the one summary line scroll past
# unread would still be trusting that it ran at all.
#
# `|| true` so that `set -e` does not abort on the assignment itself. Without
# it, the commonest failure -- run against a tree that is not the one step 04
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
want='apply8.py: 90 edit(s) in 28 file(s), every result md5 as computed'
if ! printf '%s\n' "${ap}" | grep -qxF "${want}"; then
   echo "solve05: the edits did not all apply" >&2
   exit 1
fi

# ==========================================================================
# 2. The compile level, which this step does NOT move.
# ==========================================================================
# Step 04 raised build.gradle to 1.8 and the class-file major to 52; the Java 8
# library asks for no more than that, so this step leaves both alone. Asserted
# and not assumed: the edit list covers 28 source files and says nothing about
# build.gradle, so a tree that arrived here with the level knocked back would
# apply all 90 edits, print the summary line, and fail an inherited gate with
# nothing pointing at this script.
for lvl in sourceCompatibility targetCompatibility; do
   n=$(grep -c -F "${lvl} = JavaVersion.VERSION_1_8" build.gradle || true)
   if [ "${n}" != 1 ]; then
      echo "solve05: build.gradle has ${n} ${lvl} line(s) at 1.8, wanted 1" >&2
      exit 1
   fi
done
if grep -qE 'JavaVersion\.VERSION_(9|1[0-9]|2[0-9])' build.gradle; then
   echo "solve05: build.gradle names a language level above 1.8" >&2
   exit 1
fi
echo "solve05: build.gradle still at 1.8, as this step intends"

echo "solve: step 05 complete"
