package arqprobe.fam.core;

/** Root of the two-level event hierarchy the {@code core} fixtures fire. */
public class BaseEvent
{
   private final String tag;

   public BaseEvent(String tag)
   {
      this.tag = tag;
   }

   public String tag()
   {
      return tag;
   }

   @Override
   public String toString()
   {
      return getClass().getSimpleName() + "(" + tag + ")";
   }
}
