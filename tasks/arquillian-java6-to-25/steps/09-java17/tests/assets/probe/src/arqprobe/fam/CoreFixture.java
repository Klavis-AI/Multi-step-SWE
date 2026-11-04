package arqprobe.fam;

import java.io.File;
import java.util.concurrent.atomic.AtomicReference;

import arqprobe.Fixture;
import arqprobe.Rec;
import arqprobe.fam.core.BaseEvent;
import arqprobe.fam.core.Extensions;
import arqprobe.fam.core.SubEvent;
import arqprobe.fam.core.Trace;

import org.jboss.arquillian.impl.core.ManagerBuilder;
import org.jboss.arquillian.impl.core.ManagerImpl;
import org.jboss.arquillian.impl.core.ObjectStore;
import org.jboss.arquillian.impl.core.context.ClassContextImpl;
import org.jboss.arquillian.impl.core.context.ContainerContextImpl;
import org.jboss.arquillian.impl.core.context.DeploymentContextImpl;
import org.jboss.arquillian.impl.core.context.SuiteContextImpl;
import org.jboss.arquillian.impl.core.context.TestContextImpl;
import org.jboss.arquillian.impl.core.spi.NonManagedObserver;
import org.jboss.arquillian.impl.core.spi.context.ApplicationContext;
import org.jboss.arquillian.impl.core.spi.context.ClassContext;
import org.jboss.arquillian.impl.core.spi.context.ContainerContext;
import org.jboss.arquillian.impl.core.spi.context.Context;
import org.jboss.arquillian.impl.core.spi.context.DeploymentContext;
import org.jboss.arquillian.impl.core.spi.context.SuiteContext;
import org.jboss.arquillian.impl.core.spi.context.TestContext;
import org.jboss.arquillian.spi.client.deployment.DeploymentDescription;
import org.jboss.arquillian.spi.core.annotation.ApplicationScoped;
import org.jboss.arquillian.spi.core.annotation.ClassScoped;
import org.jboss.arquillian.spi.core.annotation.ContainerScoped;
import org.jboss.arquillian.spi.core.annotation.SuiteScoped;
import org.jboss.arquillian.spi.core.annotation.TestScoped;
import org.jboss.shrinkwrap.api.ShrinkWrap;
import org.jboss.shrinkwrap.api.spec.JavaArchive;

/**
 * The event-manager microcontainer: builder, contexts, scoped binding,
 * resolution, observer dispatch, interceptors, injection, and what each of them
 * does when handed something it does not accept.
 *
 * Code-driven family — the fixture name selects the scenario; there are no
 * input files.
 */
public class CoreFixture implements Fixture
{
   private final String name;

   public CoreFixture(String name, File dir)
   {
      this.name = name;
   }

   public void run(Rec r) throws Throwable
   {
      Trace.reset();
      if (!dispatch(r))
      {
         throw new IllegalArgumentException("unknown core fixture: " + name);
      }
      r.list("trace", Trace.events());
   }

   private boolean dispatch(Rec r) throws Throwable
   {
      if (builderFixture(r) || contextFixture(r) || bindFixture(r)
            || observerFixture(r) || interceptorFixture(r) || failureFixture(r)
            || injectionFixture(r))
      {
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // builder and manager lifecycle
   // ------------------------------------------------------------------------

   private boolean builderFixture(Rec r)
   {
      if ("empty-manager".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().create();
         r.value("resolve.Object", () -> m.resolve(Object.class));
         r.value("resolve.null", () -> m.resolve(null));
         r.value("context.Suite", () -> m.getContext(SuiteContext.class));
         r.value("context.Application", () -> nameOf(m.getContext(ApplicationContext.class)));
         r.value("extension.Recorder", () -> m.getExtension(Extensions.Recorder.class));
         r.attempt("shutdown", () -> m.shutdown());
         return true;
      }
      if ("builder-null-context".equals(name))
      {
         r.attempt("context(null)", () -> ManagerBuilder.from().context(null));
         return true;
      }
      if ("builder-null-extension".equals(name))
      {
         r.attempt("extension(null)", () -> ManagerBuilder.from().extension(null));
         r.attempt("extensions(null)", () -> ManagerBuilder.from().extensions((Class<?>[]) null));
         return true;
      }
      if ("builder-empty-extensions".equals(name))
      {
         r.attempt("extensions()", () -> ManagerBuilder.from().extensions().create().shutdown());
         return true;
      }
      if ("builder-duplicate-extension".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .extension(Extensions.Recorder.class)
               .extension(Extensions.Recorder.class)
               .create();
         r.attempt("fire", () -> m.fire("dup"));
         m.shutdown();
         return true;
      }
      if ("builder-duplicate-context".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .context(SuiteContextImpl.class)
               .context(SuiteContextImpl.class)
               .create();
         SuiteContext ctx = m.getContext(SuiteContext.class);
         ctx.activate();
         r.value("bind-then-resolve", () -> {
            m.bind(SuiteScoped.class, StringBuilder.class, new StringBuilder("v"));
            return str(m.resolve(StringBuilder.class));
         });
         ctx.deactivate();
         ctx.destroy();
         m.shutdown();
         return true;
      }
      if ("all-contexts-registered".equals(name))
      {
         ManagerImpl m = allContexts();
         r.kv("suite.scope", scopeOf(m.getContext(SuiteContext.class)));
         r.kv("class.scope", scopeOf(m.getContext(ClassContext.class)));
         r.kv("container.scope", scopeOf(m.getContext(ContainerContext.class)));
         r.kv("deployment.scope", scopeOf(m.getContext(DeploymentContext.class)));
         r.kv("test.scope", scopeOf(m.getContext(TestContext.class)));
         r.kv("application.scope", scopeOf(m.getContext(ApplicationContext.class)));
         r.kv("application.active", Boolean.valueOf(m.getContext(ApplicationContext.class).isActive()));
         r.kv("suite.active", Boolean.valueOf(m.getContext(SuiteContext.class).isActive()));
         m.shutdown();
         return true;
      }
      if ("shutdown-then-fire".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .context(SuiteContextImpl.class)
               .extension(Extensions.Recorder.class)
               .create();
         m.shutdown();
         r.attempt("fire-after-shutdown", () -> m.fire("after"));
         r.value("resolve-after-shutdown", () -> m.resolve(Object.class));
         r.value("context-after-shutdown", () -> m.getContext(SuiteContext.class));
         r.value("extension-after-shutdown", () -> m.getExtension(Extensions.Recorder.class));
         return true;
      }
      if ("shutdown-twice".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(SuiteContextImpl.class).create();
         r.attempt("shutdown.1", () -> m.shutdown());
         r.attempt("shutdown.2", () -> m.shutdown());
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // contexts and the object store
   // ------------------------------------------------------------------------

   private boolean contextFixture(Rec r)
   {
      if ("suite-context-lifecycle".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(SuiteContextImpl.class).create();
         SuiteContext c = m.getContext(SuiteContext.class);
         r.kv("active.initial", Boolean.valueOf(c.isActive()));
         c.activate();
         r.kv("active.afterActivate", Boolean.valueOf(c.isActive()));
         c.getObjectStore().add(StringBuilder.class, new StringBuilder("in-suite"));
         r.kv("store.afterAdd", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         r.kv("active.afterDeactivate", Boolean.valueOf(c.isActive()));
         c.activate();
         r.kv("store.afterReactivate", str(c.getObjectStore().get(StringBuilder.class)));
         c.destroy();
         r.kv("active.afterDestroy", Boolean.valueOf(c.isActive()));
         c.activate();
         r.kv("store.afterDestroyAndReactivate", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         c.destroy();
         m.shutdown();
         return true;
      }
      if ("class-context-two-ids".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(ClassContextImpl.class).create();
         ClassContext c = m.getContext(ClassContext.class);
         c.activate(String.class);
         c.getObjectStore().add(StringBuilder.class, new StringBuilder("for-String"));
         r.kv("store.String", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         c.activate(Integer.class);
         r.kv("store.Integer", str(c.getObjectStore().get(StringBuilder.class)));
         c.getObjectStore().add(StringBuilder.class, new StringBuilder("for-Integer"));
         r.kv("store.Integer.afterAdd", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         c.activate(String.class);
         r.kv("store.String.again", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         c.destroy(String.class);
         c.activate(String.class);
         r.kv("store.String.afterDestroy", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         c.activate(Integer.class);
         r.kv("store.Integer.afterOtherDestroy", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         m.shutdown();
         return true;
      }
      if ("context-store-when-inactive".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(ClassContextImpl.class).create();
         ClassContext c = m.getContext(ClassContext.class);
         r.value("store.beforeActivate", () -> c.getObjectStore());
         c.activate(String.class);
         c.deactivate();
         r.value("store.afterDeactivate", () -> c.getObjectStore());
         m.shutdown();
         return true;
      }
      if ("context-deactivate-without-activate".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(SuiteContextImpl.class).create();
         SuiteContext c = m.getContext(SuiteContext.class);
         r.attempt("deactivate", () -> c.deactivate());
         r.kv("active", Boolean.valueOf(c.isActive()));
         m.shutdown();
         return true;
      }
      if ("context-destroy-unknown-id".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(ClassContextImpl.class).create();
         ClassContext c = m.getContext(ClassContext.class);
         r.attempt("destroy.never-activated", () -> c.destroy(String.class));
         c.activate(Integer.class);
         r.attempt("destroy.other-while-active", () -> c.destroy(String.class));
         r.kv("active.afterOtherDestroy", Boolean.valueOf(c.isActive()));
         r.attempt("destroy.active", () -> c.destroy(Integer.class));
         r.kv("active.afterOwnDestroy", Boolean.valueOf(c.isActive()));
         m.shutdown();
         return true;
      }
      if ("context-activate-null-id".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(ClassContextImpl.class).create();
         ClassContext c = m.getContext(ClassContext.class);
         r.attempt("activate(null)", () -> c.activate(null));
         r.kv("active", Boolean.valueOf(c.isActive()));
         r.value("store", () -> c.getObjectStore());
         m.shutdown();
         return true;
      }
      if ("context-nested-activate-same-id".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(ClassContextImpl.class).create();
         ClassContext c = m.getContext(ClassContext.class);
         c.activate(String.class);
         c.getObjectStore().add(StringBuilder.class, new StringBuilder("first"));
         c.activate(String.class);
         r.kv("store.afterReactivate", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         r.kv("active.afterOneDeactivate", Boolean.valueOf(c.isActive()));
         m.shutdown();
         return true;
      }
      if ("context-clear-all".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(ClassContextImpl.class).create();
         ClassContext c = m.getContext(ClassContext.class);
         c.activate(String.class);
         c.getObjectStore().add(StringBuilder.class, new StringBuilder("a"));
         c.deactivate();
         c.activate(Integer.class);
         c.getObjectStore().add(StringBuilder.class, new StringBuilder("b"));
         c.deactivate();
         c.clearAll();
         c.activate(String.class);
         r.kv("store.String", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         c.activate(Integer.class);
         r.kv("store.Integer", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         m.shutdown();
         return true;
      }
      if ("objectstore-basics".equals(name))
      {
         ObjectStore store = new ObjectStore();
         r.kv("get.missing", str(store.get(StringBuilder.class)));
         store.add(StringBuilder.class, new StringBuilder("one"));
         r.kv("get.present", str(store.get(StringBuilder.class)));
         store.add(StringBuilder.class, new StringBuilder("two"));
         r.kv("get.replaced", str(store.get(StringBuilder.class)));
         r.value("get.subtype", () -> store.get(CharSequence.class));
         store.clear();
         r.kv("get.afterClear", str(store.get(StringBuilder.class)));
         return true;
      }
      if ("objectstore-nulls".equals(name))
      {
         ObjectStore store = new ObjectStore();
         r.attempt("add(null,value)", () -> store.add(null, new StringBuilder()));
         r.attempt("add(type,null)", () -> store.add(StringBuilder.class, null));
         r.attempt("get(null)", () -> store.get(null));
         return true;
      }
      if ("context-thread-isolation".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(SuiteContextImpl.class).create();
         final SuiteContext c = m.getContext(SuiteContext.class);
         c.activate();
         c.getObjectStore().add(StringBuilder.class, new StringBuilder("main-thread"));
         r.kv("active.thisThread", Boolean.valueOf(c.isActive()));

         final AtomicReference<String> other = new AtomicReference<String>();
         Thread t = new Thread(() -> {
            StringBuilder sb = new StringBuilder();
            sb.append("active=").append(c.isActive()).append(" store=");
            try
            {
               sb.append(str(c.getObjectStore().get(StringBuilder.class)));
            }
            catch (Throwable ex)
            {
               sb.append("<threw:").append(ex.getClass().getName()).append('>');
            }
            other.set(sb.toString());
         });
         t.start();
         join(t);
         r.kv("otherThread", other.get());
         c.deactivate();
         c.destroy();
         m.shutdown();
         return true;
      }
      if ("deployment-context-id".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(DeploymentContextImpl.class).create();
         DeploymentContext c = m.getContext(DeploymentContext.class);
         DeploymentDescription one = new DeploymentDescription(
               "one", ShrinkWrap.create(JavaArchive.class, "one.jar"));
         DeploymentDescription two = new DeploymentDescription(
               "two", ShrinkWrap.create(JavaArchive.class, "two.jar"));
         c.activate(one);
         c.getObjectStore().add(StringBuilder.class, new StringBuilder("in-one"));
         c.deactivate();
         c.activate(two);
         r.kv("store.two", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         c.activate(one);
         r.kv("store.one", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();

         DeploymentDescription equalToOne = new DeploymentDescription(
               "one", ShrinkWrap.create(JavaArchive.class, "one.jar"));
         c.activate(equalToOne);
         r.kv("store.equalToOne", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         m.shutdown();
         return true;
      }
      if ("container-context-id".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(ContainerContextImpl.class).create();
         ContainerContext c = m.getContext(ContainerContext.class);
         c.activate("alpha");
         c.getObjectStore().add(StringBuilder.class, new StringBuilder("in-alpha"));
         c.deactivate();
         c.activate("beta");
         r.kv("store.beta", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         c.activate(new String("alpha"));
         r.kv("store.alpha.viaEqualString", str(c.getObjectStore().get(StringBuilder.class)));
         c.deactivate();
         m.shutdown();
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // bind and resolve
   // ------------------------------------------------------------------------

   private boolean bindFixture(Rec r)
   {
      if ("bind-suite-scoped".equals(name))
      {
         ManagerImpl m = allContexts();
         SuiteContext suite = m.getContext(SuiteContext.class);
         ClassContext klass = m.getContext(ClassContext.class);
         suite.activate();
         klass.activate(CoreFixture.class);
         m.bind(SuiteScoped.class, StringBuilder.class, new StringBuilder("bound"));
         r.kv("suite.store", str(suite.getObjectStore().get(StringBuilder.class)));
         r.kv("class.store", str(klass.getObjectStore().get(StringBuilder.class)));
         r.kv("resolve", str(m.resolve(StringBuilder.class)));
         klass.deactivate();
         klass.destroy(CoreFixture.class);
         suite.deactivate();
         suite.destroy();
         m.shutdown();
         return true;
      }
      if ("bind-every-scope".equals(name))
      {
         ManagerImpl m = allContexts();
         SuiteContext suite = m.getContext(SuiteContext.class);
         ClassContext klass = m.getContext(ClassContext.class);
         ContainerContext container = m.getContext(ContainerContext.class);
         TestContext test = m.getContext(TestContext.class);
         suite.activate();
         klass.activate(CoreFixture.class);
         container.activate("c");
         test.activate("t");
         m.bind(ApplicationScoped.class, StringBuilder.class, new StringBuilder("app"));
         r.kv("resolve.app", str(m.resolve(StringBuilder.class)));
         m.bind(SuiteScoped.class, StringBuilder.class, new StringBuilder("suite"));
         r.kv("resolve.suite", str(m.resolve(StringBuilder.class)));
         m.bind(ClassScoped.class, StringBuilder.class, new StringBuilder("class"));
         r.kv("resolve.class", str(m.resolve(StringBuilder.class)));
         m.bind(ContainerScoped.class, StringBuilder.class, new StringBuilder("container"));
         r.kv("resolve.container", str(m.resolve(StringBuilder.class)));
         m.bind(TestScoped.class, StringBuilder.class, new StringBuilder("test"));
         r.kv("resolve.test", str(m.resolve(StringBuilder.class)));
         test.deactivate();
         r.kv("resolve.afterTestDeactivate", str(m.resolve(StringBuilder.class)));
         container.deactivate();
         r.kv("resolve.afterContainerDeactivate", str(m.resolve(StringBuilder.class)));
         klass.deactivate();
         r.kv("resolve.afterClassDeactivate", str(m.resolve(StringBuilder.class)));
         suite.deactivate();
         r.kv("resolve.afterSuiteDeactivate", str(m.resolve(StringBuilder.class)));
         m.shutdown();
         return true;
      }
      if ("bind-inactive-context".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(SuiteContextImpl.class).create();
         r.attempt("bind", () -> m.bind(SuiteScoped.class, StringBuilder.class, new StringBuilder("x")));
         m.shutdown();
         return true;
      }
      if ("bind-unregistered-scope".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(SuiteContextImpl.class).create();
         m.getContext(SuiteContext.class).activate();
         r.attempt("bind.ClassScoped", () -> m.bind(ClassScoped.class, StringBuilder.class, new StringBuilder("x")));
         m.shutdown();
         return true;
      }
      if ("bind-nulls".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(SuiteContextImpl.class).create();
         m.getContext(SuiteContext.class).activate();
         r.attempt("bind(null,type,value)", () -> m.bind(null, StringBuilder.class, new StringBuilder()));
         r.attempt("bind(scope,null,value)", () -> m.bind(SuiteScoped.class, null, new StringBuilder()));
         r.attempt("bind(scope,type,null)", () -> m.bind(SuiteScoped.class, StringBuilder.class, null));
         r.attempt("bind(notAScope,...)", () -> m.bind(Override.class, StringBuilder.class, new StringBuilder()));
         m.shutdown();
         return true;
      }
      if ("bind-and-fire".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .context(SuiteContextImpl.class)
               .extension(Extensions.ObjectObserver.class)
               .create();
         m.getContext(SuiteContext.class).activate();
         r.attempt("bindAndFire", () -> m.bindAndFire(SuiteScoped.class, BaseEvent.class, new BaseEvent("bf")));
         r.kv("resolve", str(m.resolve(BaseEvent.class)));
         m.shutdown();
         return true;
      }
      if ("resolve-subtype".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(SuiteContextImpl.class).create();
         m.getContext(SuiteContext.class).activate();
         m.bind(SuiteScoped.class, SubEvent.class, new SubEvent("s"));
         r.kv("resolve.exact", str(m.resolve(SubEvent.class)));
         r.kv("resolve.supertype", str(m.resolve(BaseEvent.class)));
         m.shutdown();
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // observer dispatch
   // ------------------------------------------------------------------------

   private boolean observerFixture(Rec r)
   {
      if ("observer-single".equals(name))
      {
         fireOn(r, "hello", Extensions.Recorder.class);
         return true;
      }
      if ("observer-two-extensions".equals(name))
      {
         fireOn(r, "hello", Extensions.Recorder.class, Extensions.SecondRecorder.class);
         return true;
      }
      if ("observer-two-extensions-reversed".equals(name))
      {
         fireOn(r, "hello", Extensions.SecondRecorder.class, Extensions.Recorder.class);
         return true;
      }
      if ("observer-precedence-order".equals(name))
      {
         fireOn(r, "p", Extensions.PrecedenceLow.class, Extensions.PrecedenceZero.class,
               Extensions.PrecedenceHigh.class);
         return true;
      }
      if ("observer-precedence-order-reversed".equals(name))
      {
         fireOn(r, "p", Extensions.PrecedenceHigh.class, Extensions.PrecedenceZero.class,
               Extensions.PrecedenceLow.class);
         return true;
      }
      if ("observer-object-catch-all".equals(name))
      {
         fireOn(r, "any", Extensions.ObjectObserver.class);
         return true;
      }
      if ("observer-hierarchy-base".equals(name))
      {
         fireOn(r, new BaseEvent("b"), Extensions.BaseObserver.class, Extensions.SubObserver.class);
         return true;
      }
      if ("observer-hierarchy-sub".equals(name))
      {
         fireOn(r, new SubEvent("s"), Extensions.BaseObserver.class, Extensions.SubObserver.class);
         return true;
      }
      if ("observer-inherited-methods".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .extension(Extensions.InheritedObserverChild.class).create();
         r.attempt("fire", () -> m.fire("inh"));
         // Class.getMethods() order is unspecified, and this extension has two
         // matching methods, so only the multiset is stable.
         r.sorted("observers", Trace.events());
         Trace.reset();
         m.shutdown();
         return true;
      }
      if ("observer-static-method".equals(name))
      {
         fireOn(r, "st", Extensions.StaticObserver.class);
         return true;
      }
      if ("observer-private-method".equals(name))
      {
         fireOn(r, "pv", Extensions.PrivateObserver.class);
         return true;
      }
      if ("observer-protected-method".equals(name))
      {
         fireOn(r, "pt", Extensions.ProtectedObserver.class);
         return true;
      }
      if ("observer-extra-parameter".equals(name))
      {
         fireOn(r, "xp", Extensions.ExtraParameterObserver.class);
         return true;
      }
      if ("observer-unannotated".equals(name))
      {
         fireOn(r, "ua", Extensions.UnannotatedMethod.class);
         return true;
      }
      if ("observer-extra-parameter-resolved".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .context(SuiteContextImpl.class)
               .extension(Extensions.ResolvedParameterObserver.class)
               .create();
         m.getContext(SuiteContext.class).activate();
         r.attempt("fire.beforeBind", () -> m.fire("xr"));
         m.bind(SuiteScoped.class, StringBuilder.class, new StringBuilder("resolved"));
         r.attempt("fire.afterBind", () -> m.fire("xr"));
         m.shutdown();
         return true;
      }
      if ("fire-null-event".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().extension(Extensions.Recorder.class).create();
         r.attempt("fire(null)", () -> m.fire(null));
         r.attempt("fire(null,observer)", () -> m.fire(null, null));
         m.shutdown();
         return true;
      }
      if ("fire-no-observers".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().create();
         r.attempt("fire", () -> m.fire("nobody-listening"));
         m.shutdown();
         return true;
      }
      if ("fire-event-of-manager-type".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().extension(Extensions.ObjectObserver.class).create();
         r.attempt("fire", () -> m.fire(new int[] {1, 2}));
         m.shutdown();
         return true;
      }
      if ("nonmanaged-observer".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().extension(Extensions.Recorder.class).create();
         NonManagedObserver<String> observer = event -> Trace.add("nonManaged:" + event);
         r.attempt("fire", () -> m.fire("nm", observer));
         m.shutdown();
         return true;
      }
      if ("nonmanaged-observer-throws".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().extension(Extensions.Recorder.class).create();
         NonManagedObserver<String> observer = event -> {
            Trace.add("nonManaged.entered");
            throw new IllegalStateException("nm-boom");
         };
         r.attempt("fire", () -> m.fire("nm", observer));
         m.shutdown();
         return true;
      }
      if ("nonmanaged-observer-null".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().extension(Extensions.Recorder.class).create();
         r.attempt("fire", () -> m.fire("nm", null));
         m.shutdown();
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // interceptors
   // ------------------------------------------------------------------------

   private boolean interceptorFixture(Rec r)
   {
      if ("interceptor-around".equals(name))
      {
         fireOn(r, "i", Extensions.Interceptor.class, Extensions.Recorder.class);
         return true;
      }
      if ("interceptor-no-proceed".equals(name))
      {
         fireOn(r, "i", Extensions.InterceptorNoProceed.class, Extensions.Recorder.class);
         return true;
      }
      if ("interceptor-proceed-twice".equals(name))
      {
         fireOn(r, "i", Extensions.InterceptorProceedTwice.class, Extensions.Recorder.class);
         return true;
      }
      if ("interceptor-chain".equals(name))
      {
         fireOn(r, "i", Extensions.Interceptor.class, Extensions.InterceptorNoProceed.class,
               Extensions.Recorder.class);
         return true;
      }
      if ("interceptor-does-not-see-other-event".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .extension(Extensions.Interceptor.class)
               .extension(Extensions.BaseObserver.class)
               .create();
         r.attempt("fire", () -> m.fire(new BaseEvent("not-a-string")));
         m.shutdown();
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // failure modes
   // ------------------------------------------------------------------------

   private boolean failureFixture(Rec r)
   {
      if ("observer-throws-runtime".equals(name))
      {
         fireOn(r, "t", Extensions.ThrowsRuntime.class);
         return true;
      }
      if ("observer-throws-checked".equals(name))
      {
         fireOn(r, "t", Extensions.ThrowsChecked.class);
         return true;
      }
      if ("observer-throws-error".equals(name))
      {
         fireOn(r, "t", Extensions.ThrowsError.class);
         return true;
      }
      if ("observer-throws-then-later-observer".equals(name))
      {
         fireOn(r, "t", Extensions.ThrowsRuntime.class, Extensions.Recorder.class);
         return true;
      }
      if ("observer-throws-with-handler".equals(name))
      {
         fireOn(r, "t", Extensions.ThrowsRuntime.class, Extensions.IllegalStateHandler.class);
         return true;
      }
      if ("observer-throws-inside-interceptor".equals(name))
      {
         fireOn(r, "t", Extensions.Interceptor.class, Extensions.ThrowsRuntime.class);
         return true;
      }
      if ("observer-throws-invocation-exception".equals(name))
      {
         fireOn(r, "t", Extensions.ThrowsInvocationException.class);
         return true;
      }
      if ("producer-without-scope".equals(name))
      {
         r.attempt("create", () -> ManagerBuilder.from()
               .context(SuiteContextImpl.class)
               .extension(Extensions.UnscopedProducer.class).create());
         return true;
      }
      if ("inject-on-plain-field".equals(name))
      {
         fireOn(r, "pf", Extensions.InjectOnPlainField.class);
         return true;
      }
      if ("extension-no-default-constructor".equals(name))
      {
         r.attempt("create", () -> ManagerBuilder.from()
               .extension(Extensions.NoDefaultConstructor.class).create());
         return true;
      }
      if ("extension-private-constructor".equals(name))
      {
         ManagerImpl m = (ManagerImpl) r.call("create", () -> ManagerBuilder.from()
               .extension(Extensions.PrivateConstructor.class).create());
         if (m != null)
         {
            r.attempt("fire", () -> m.fire("pc"));
            m.shutdown();
         }
         return true;
      }
      if ("extension-throwing-constructor".equals(name))
      {
         r.attempt("create", () -> ManagerBuilder.from()
               .extension(Extensions.ThrowingConstructor.class).create());
         return true;
      }
      if ("extension-interface".equals(name))
      {
         r.attempt("create", () -> ManagerBuilder.from().extension(CharSequence.class).create());
         return true;
      }
      if ("extension-primitive".equals(name))
      {
         r.attempt("create", () -> ManagerBuilder.from().extension(int.class).create());
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // injection
   // ------------------------------------------------------------------------

   private boolean injectionFixture(Rec r)
   {
      if ("inject-instance-unbound".equals(name))
      {
         fireOn(r, "iu", Extensions.InstanceConsumer.class);
         return true;
      }
      if ("inject-instance-bound".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .context(SuiteContextImpl.class)
               .extension(Extensions.InstanceConsumer.class)
               .create();
         m.getContext(SuiteContext.class).activate();
         m.bind(SuiteScoped.class, StringBuilder.class, new StringBuilder("bound-value"));
         r.attempt("fire", () -> m.fire("ib"));
         m.shutdown();
         return true;
      }
      if ("producer-suite-scoped".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .context(SuiteContextImpl.class)
               .extension(Extensions.SuiteProducer.class)
               .extension(Extensions.InstanceConsumer.class)
               .create();
         SuiteContext c = m.getContext(SuiteContext.class);
         c.activate();
         r.attempt("fire", () -> m.fire("ps"));
         r.kv("suite.store", str(c.getObjectStore().get(StringBuilder.class)));
         r.kv("resolve", str(m.resolve(StringBuilder.class)));
         m.shutdown();
         return true;
      }
      if ("producer-class-scoped".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .context(SuiteContextImpl.class)
               .context(ClassContextImpl.class)
               .extension(Extensions.ClassProducer.class)
               .create();
         SuiteContext suite = m.getContext(SuiteContext.class);
         ClassContext klass = m.getContext(ClassContext.class);
         suite.activate();
         klass.activate(CoreFixture.class);
         r.attempt("fire", () -> m.fire("pc"));
         r.kv("class.store", str(klass.getObjectStore().get(StringBuilder.class)));
         r.kv("suite.store", str(suite.getObjectStore().get(StringBuilder.class)));
         klass.deactivate();
         r.kv("resolve.afterClassDeactivate", str(m.resolve(StringBuilder.class)));
         m.shutdown();
         return true;
      }
      if ("producer-application-scoped".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .extension(Extensions.ApplicationProducer.class)
               .create();
         r.attempt("fire", () -> m.fire("pa"));
         r.kv("resolve", str(m.resolve(StringBuilder.class)));
         m.shutdown();
         return true;
      }
      if ("producer-unregistered-scope".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .extension(Extensions.UnregisteredScopeProducer.class)
               .create();
         r.attempt("fire", () -> m.fire("pu"));
         r.kv("resolve", str(m.resolve(StringBuilder.class)));
         m.shutdown();
         return true;
      }
      if ("producer-inactive-scope".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from()
               .context(ClassContextImpl.class)
               .extension(Extensions.ClassProducer.class)
               .create();
         r.attempt("fire", () -> m.fire("pi"));
         m.shutdown();
         return true;
      }
      if ("inject-event-fires".equals(name))
      {
         fireOn(r, "ev", Extensions.EventFirer.class, Extensions.BaseObserver.class);
         return true;
      }
      if ("inject-event-nested-into-throwing".equals(name))
      {
         fireOn(r, "ev", Extensions.EventFirer.class, Extensions.ThrowsBaseEvent.class);
         return true;
      }
      if ("inject-manager".equals(name))
      {
         fireOn(r, "mg", Extensions.ManagerInjected.class);
         return true;
      }
      if ("self-firing-observer".equals(name))
      {
         fireOn(r, "sf", Extensions.SelfFirer.class);
         return true;
      }
      if ("manager-inject-plain-object".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().context(SuiteContextImpl.class).create();
         m.getContext(SuiteContext.class).activate();
         m.bind(SuiteScoped.class, StringBuilder.class, new StringBuilder("for-target"));
         Extensions.InjectionTarget target = new Extensions.InjectionTarget();
         r.value("instance.beforeInject", () -> target.instance());
         r.attempt("inject", () -> m.inject(target));
         r.value("instance.afterInject", () -> str(target.instance().get()));
         m.shutdown();
         return true;
      }
      if ("manager-inject-null".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().create();
         r.attempt("inject(null)", () -> m.inject(null));
         m.shutdown();
         return true;
      }
      if ("manager-inject-no-injection-points".equals(name))
      {
         ManagerImpl m = ManagerBuilder.from().create();
         r.attempt("inject", () -> m.inject(new StringBuilder()));
         m.shutdown();
         return true;
      }
      return false;
   }

   // ------------------------------------------------------------------------
   // helpers
   // ------------------------------------------------------------------------

   /** Build a manager with every id-bound and non-id-bound context registered. */
   private static ManagerImpl allContexts(Class<?>... extensions)
   {
      ManagerBuilder b = ManagerBuilder.from()
            .context(SuiteContextImpl.class)
            .context(ClassContextImpl.class)
            .context(ContainerContextImpl.class)
            .context(DeploymentContextImpl.class)
            .context(TestContextImpl.class);
      for (Class<?> extension : extensions)
      {
         b.extension(extension);
      }
      return b.create();
   }

   /** Register the extensions in the order given, fire one event, shut down. */
   private static void fireOn(Rec r, final Object event, Class<?>... extensions)
   {
      ManagerBuilder b = ManagerBuilder.from().context(SuiteContextImpl.class);
      for (Class<?> extension : extensions)
      {
         b.extension(extension);
      }
      final ManagerImpl m = b.create();
      m.getContext(SuiteContext.class).activate();
      r.attempt("fire", () -> m.fire(event));
      m.shutdown();
   }

   private static String scopeOf(Context c)
   {
      return c == null ? "<null>" : c.getScope().getName();
   }

   private static String nameOf(Object o)
   {
      return o == null ? "<null>" : o.getClass().getName();
   }

   private static String str(Object o)
   {
      return o == null ? "<null>" : o.toString();
   }

   private static void join(Thread t)
   {
      try
      {
         t.join(60000);
      }
      catch (InterruptedException e)
      {
         Thread.currentThread().interrupt();
      }
   }
}
