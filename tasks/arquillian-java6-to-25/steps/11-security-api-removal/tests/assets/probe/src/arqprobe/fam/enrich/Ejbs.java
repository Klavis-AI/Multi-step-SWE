package arqprobe.fam.enrich;

/**
 * What the directory hands back when an EJB lookup succeeds.
 *
 * One class implements every bean interface in {@link Beans}, because the
 * enricher never asks what it got — it assigns it to the field and stops. What
 * the fixtures care about is which name produced it, and that is in the log.
 *
 * Named rather than printed by class, for the same reason the CDI stubs are:
 * see {@link Jndi#describe}.
 */
public final class Ejbs
{
   private Ejbs()
   {
   }

   public static final class Bean implements Beans.Cart, Beans.Order, Jndi.Named
   {
      private final String label;

      public Bean(String label)
      {
         this.label = label;
      }

      public String label()
      {
         return "ejb:" + label;
      }

      public void add(String item)
      {
         Log.add("cart:add " + item);
      }

      public void submit()
      {
         Log.add("order:submit");
      }

      public String toString()
      {
         return "ejb:" + label;
      }
   }
}
