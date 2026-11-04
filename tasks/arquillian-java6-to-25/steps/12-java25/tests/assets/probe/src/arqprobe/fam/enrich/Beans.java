package arqprobe.fam.enrich;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.util.List;

import javax.annotation.Resource;
import javax.ejb.EJB;

/**
 * The classes the enrichers are pointed at.
 *
 * Each one isolates a single decision. {@code ResourceInjectionEnricher} asks
 * four questions of every field it finds — is it annotated, is it primitive, is
 * it already set, and what name does it resolve to — and the only way to see
 * the answers separately is to give it one field at a time.
 *
 * Two rules hold throughout, and both are about determinism rather than taste:
 *
 * <ul>
 * <li>At most one annotated field per class in the hierarchy.
 * {@code SecurityActions.getFieldsWithAnnotation} walks
 * {@code getDeclaredFields()}, whose order the JVM does not specify, so a class
 * with two annotated fields would inject them in an order no transcript could
 * rely on.</li>
 * <li>At most one annotated method per class, for the same reason —
 * {@code getMethodsWithAnnotation} walks {@code getDeclaredMethods()}. Note
 * that unlike the field walk it does <em>not</em> climb to the superclass; that
 * asymmetry is a behaviour, and {@link InheritsMethod} records it.</li>
 * </ul>
 *
 * {@code javax.annotation.Resource} and {@code javax.ejb.EJB} are two of the
 * names {@code run.sh} rebinds to {@code jakarta.*} when the tree under test
 * has moved. Nothing here records the annotation's own name for that reason.
 */
public final class Beans
{
   private Beans()
   {
   }

   /**
    * A qualifier for the CDI fixtures that is not a CDI annotation.
    *
    * Whether an annotation is a qualifier is answered by the bean manager, and
    * in these fixtures the bean manager is a stub that answers however the
    * fixture told it to. So the marker does not need — and deliberately does
    * not have — {@code @Qualifier} on it: that would tie this file to a
    * namespace for no gain.
    */
   @Retention(RetentionPolicy.RUNTIME)
   @Target({ElementType.PARAMETER, ElementType.FIELD, ElementType.METHOD})
   public @interface Tagged
   {
   }

   // ------------------------------------------------------------------------
   // @Resource fields: which name a field resolves to
   // ------------------------------------------------------------------------

   /** No attributes: the name is built from the declaring class and the field. */
   public static class ResPlain
   {
      @Resource
      private String value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** {@code name} is appended to the lookup prefix. */
   public static class ResNamed
   {
      @Resource(name = "alpha")
      private String value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** {@code mappedName} is used as given, with no prefix. */
   public static class ResMapped
   {
      @Resource(mappedName = "mapped/alpha")
      private String value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** Both: one of them wins, and which one is the observable. */
   public static class ResNamedAndMapped
   {
      @Resource(name = "alpha", mappedName = "mapped/alpha")
      private String value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** Already set: an object field with a value is left alone. */
   public static class ResPreset
   {
      @Resource
      private String value = "already-here";

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** Not annotated at all. */
   public static class ResNone
   {
      private String value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   // ------------------------------------------------------------------------
   // @Resource fields: the primitive special case
   // ------------------------------------------------------------------------
   //
   // A primitive field cannot be null, so the enricher guesses from the value
   // whether it has been set, by comparing String.valueOf against "0", "0.0"
   // and "false". Each primitive width reaches that comparison differently and
   // one of them — char — misses it and is caught by a second test.

   /** {@code String.valueOf(0)} is "0": treated as unset. */
   public static class ResIntZero
   {
      @Resource(mappedName = "mapped/int")
      private int value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** A non-zero int is treated as already set. */
   public static class ResIntSet
   {
      @Resource(mappedName = "mapped/int")
      private int value = 5;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** {@code String.valueOf(0L)} is also "0". */
   public static class ResLongZero
   {
      @Resource(mappedName = "mapped/long")
      private long value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** {@code String.valueOf(0.0d)} is "0.0". */
   public static class ResDoubleZero
   {
      @Resource(mappedName = "mapped/double")
      private double value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** {@code String.valueOf(0.0f)} is "0.0" too. */
   public static class ResFloatZero
   {
      @Resource(mappedName = "mapped/float")
      private float value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** {@code String.valueOf(false)} is "false". */
   public static class ResBooleanFalse
   {
      @Resource(mappedName = "mapped/boolean")
      private boolean value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** True is indistinguishable from "already set". */
   public static class ResBooleanTrue
   {
      @Resource(mappedName = "mapped/boolean")
      private boolean value = true;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /**
    * The one primitive the string comparison misses.
    *
    * {@code String.valueOf((char) 0)} is a NUL character, not "0", so the
    * default-value test fails and a second, {@code Character}-specific test
    * catches it. Adversarial: it is the branch that exists only because the
    * first test is wrong.
    */
   public static class ResCharZero
   {
      @Resource(mappedName = "mapped/char")
      private char value;

      public String toString()
      {
         return String.valueOf((int) value);
      }
   }

   /** A set char falls through both tests. */
   public static class ResCharSet
   {
      @Resource(mappedName = "mapped/char")
      private char value = 'x';

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /**
    * {@code String.valueOf((short) 0)} is "0", like int and long.
    *
    * Kept because the three integral widths reach the same branch by three
    * different boxing paths, and a migration that replaced the string compare
    * with a typed one could easily keep two of them working.
    */
   public static class ResShortZero
   {
      @Resource(mappedName = "mapped/short")
      private short value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** The bound value has the wrong type for the field. */
   public static class ResWrongType
   {
      @Resource(mappedName = "mapped/alpha")
      private Integer value;

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   // ------------------------------------------------------------------------
   // @Resource: inheritance, and methods
   // ------------------------------------------------------------------------

   /** Superclass with the annotated field; the walk climbs to it. */
   public static class ResSuper
   {
      @Resource(mappedName = "mapped/inherited")
      private String inherited;

      public String toString()
      {
         return String.valueOf(inherited);
      }
   }

   /** Declares nothing itself. */
   public static class ResSub extends ResSuper
   {
   }

   /** A setter with a mapped name. */
   public static class ResSetter
   {
      private String value;

      @Resource(mappedName = "mapped/setter")
      public void setValue(String value)
      {
         Log.add("bean:setValue " + value);
         this.value = value;
      }

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /**
    * A setter with no name at all.
    *
    * {@code getResourceName(Resource)} returns null when neither attribute is
    * set, and — unlike the field path, which falls back to a name built from
    * the class — that null goes straight into the lookup. Adversarial: it is
    * the only path where the enricher asks the directory for nothing.
    */
   public static class ResSetterUnnamed
   {
      private String value;

      @Resource
      public void setValue(String value)
      {
         Log.add("bean:setValue " + value);
         this.value = value;
      }

      public String toString()
      {
         return String.valueOf(value);
      }
   }

   /** Two arguments: rejected before any lookup. */
   public static class ResSetterTwoArgs
   {
      @Resource(mappedName = "mapped/setter")
      public void setValue(String a, String b)
      {
         Log.add("bean:setValue " + a + "," + b);
      }
   }

   /** Right shape, wrong name: rejected before any lookup. */
   public static class ResNotASetter
   {
      @Resource(mappedName = "mapped/setter")
      public void assignValue(String a)
      {
         Log.add("bean:assignValue " + a);
      }
   }

   /** The setter itself refuses. */
   public static class ResSetterThrows
   {
      @Resource(mappedName = "mapped/setter")
      public void setValue(String value)
      {
         Log.add("bean:setValue " + value);
         throw new IllegalStateException("the setter said no");
      }
   }

   // ------------------------------------------------------------------------
   // @EJB
   // ------------------------------------------------------------------------

   /** What an EJB fixture asks for. Its simple name builds every JNDI name tried. */
   public interface Cart
   {
      void add(String item);
   }

   /** A second one, so the name list is visibly derived from the field type. */
   public interface Order
   {
      void submit();
   }

   /** No mapped name: the enricher walks its whole list of guesses. */
   public static class EjbPlain
   {
      @EJB
      private Cart cart;

      public String toString()
      {
         return String.valueOf(cart);
      }
   }

   /** A different field type, to show the list is built from the type. */
   public static class EjbOtherType
   {
      @EJB
      private Order order;

      public String toString()
      {
         return String.valueOf(order);
      }
   }

   /** A mapped name replaces the whole list with one entry. */
   public static class EjbMapped
   {
      @EJB(mappedName = "mapped/cart")
      private Cart cart;

      public String toString()
      {
         return String.valueOf(cart);
      }
   }

   /** An empty mapped name is not a mapped name. */
   public static class EjbMappedEmpty
   {
      @EJB(mappedName = "")
      private Cart cart;

      public String toString()
      {
         return String.valueOf(cart);
      }
   }

   /** Already set: skipped without a lookup. */
   public static class EjbPreset
   {
      @EJB
      private Cart cart = new Ejbs.Bean("preset");

      public String toString()
      {
         return String.valueOf(cart);
      }
   }

   /** Inherited annotated field: the field walk climbs. */
   public static class EjbSuper
   {
      @EJB
      private Cart cart;

      public String toString()
      {
         return String.valueOf(cart);
      }
   }

   /** Declares nothing itself. */
   public static class EjbSub extends EjbSuper
   {
   }

   /** A setter, annotated on the method only. */
   public static class EjbSetter
   {
      private Cart cart;

      @EJB
      public void setCart(Cart cart)
      {
         Log.add("bean:setCart " + cart);
         this.cart = cart;
      }

      public String toString()
      {
         return String.valueOf(cart);
      }
   }

   /**
    * A setter whose parameter carries an annotation that is not {@code @EJB}.
    *
    * {@code injectClass} scans {@code getParameterAnnotations()[0]} for an
    * {@code @EJB} and uses its {@code mappedName} if it finds one. It never
    * will: {@code @EJB} is {@code @Target({TYPE, METHOD, FIELD})} in EJB 3.0
    * and still is in Jakarta EJB 4.0, so no compiler will let one onto a
    * parameter, and an annotation type cannot extend another so nothing else
    * can satisfy the {@code isAssignableFrom} either. The mapped name for a
    * setter is therefore always null and a setter always walks the full list.
    *
    * This fixture puts a different annotation there so the scan runs over a
    * non-empty array and rejects what it finds, which is as far into that
    * branch as the language allows anything to go.
    */
   public static class EjbSetterTaggedParameter
   {
      private Cart cart;

      @EJB
      public void setCart(@Tagged Cart cart)
      {
         Log.add("bean:setCart " + cart);
         this.cart = cart;
      }

      public String toString()
      {
         return String.valueOf(cart);
      }
   }

   /** Two arguments: rejected before any lookup. */
   public static class EjbSetterTwoArgs
   {
      @EJB
      public void setCart(Cart a, Cart b)
      {
         Log.add("bean:setCart " + a + "," + b);
      }
   }

   /** Right shape, wrong name: rejected before any lookup. */
   public static class EjbNotASetter
   {
      @EJB
      public void assignCart(Cart a)
      {
         Log.add("bean:assignCart " + a);
      }
   }

   /** The setter itself refuses. */
   public static class EjbSetterThrows
   {
      @EJB(mappedName = "mapped/cart")
      public void setCart(Cart cart)
      {
         Log.add("bean:setCart " + cart);
         throw new IllegalStateException("the setter said no");
      }
   }

   /**
    * An annotated method on the superclass, which is <em>not</em> found.
    *
    * The field walk climbs the hierarchy and the method walk does not. Nothing
    * in the codebase says so; it falls out of one helper calling
    * {@code getDeclaredFields} in a loop and its neighbour calling
    * {@code getDeclaredMethods} once.
    */
   public static class InheritsMethodSuper
   {
      private Cart cart;

      @EJB
      public void setCart(Cart cart)
      {
         Log.add("bean:setCart " + cart);
         this.cart = cart;
      }

      public String toString()
      {
         return String.valueOf(cart);
      }
   }

   /** Declares nothing itself. */
   public static class InheritsMethod extends InheritsMethodSuper
   {
   }

   // ------------------------------------------------------------------------
   // CDI
   // ------------------------------------------------------------------------

   /** What the bean manager's injection target writes into. */
   public static class CdiTarget
   {
      private String injected;

      public String toString()
      {
         return String.valueOf(injected);
      }
   }

   /** Nothing for the injection target to write into. */
   public static class CdiNoField
   {
      public String toString()
      {
         return "CdiNoField";
      }
   }

   /**
    * Method shapes for {@code resolve}.
    *
    * One method per parameter shape, each named uniquely, because the enricher
    * is handed a {@code Method} and there is no overload resolution to depend
    * on.
    */
   public static class Methods
   {
      public void none()
      {
      }

      public void one(String first)
      {
      }

      public void two(String first, int second)
      {
      }

      public void generic(List<String> first)
      {
      }

      public void tagged(@Tagged String first)
      {
      }

      public void primitive(int first)
      {
      }

      public void array(String[] first)
      {
      }

      public void twoTagged(@Tagged String first, @Tagged @Deprecated String second)
      {
      }
   }
}
