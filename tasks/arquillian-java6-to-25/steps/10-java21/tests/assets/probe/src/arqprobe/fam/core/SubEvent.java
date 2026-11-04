package arqprobe.fam.core;

/** Subtype of {@link BaseEvent}: fired to check which observers the manager selects. */
public class SubEvent extends BaseEvent
{
   public SubEvent(String tag)
   {
      super(tag);
   }
}
