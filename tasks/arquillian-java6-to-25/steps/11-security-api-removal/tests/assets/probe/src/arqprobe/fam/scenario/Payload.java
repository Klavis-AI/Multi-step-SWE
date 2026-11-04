package arqprobe.fam.scenario;

/**
 * A single class with no nested types, used as archive payload.
 *
 * ShrinkWrap's {@code addClass} pulls in a type's inner classes too, so adding
 * the fixture class itself would make every archive listing grow whenever a new
 * fixture is written.
 */
public class Payload
{
   public String value()
   {
      return "payload";
   }
}
