import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
public class FailCase {
   @Test public void passes() { assertTrue(true); }
   @Test public void failsHere() { assertEquals(1, 2, "deliberate"); }
   @Test public void alsoFails() { throw new IllegalStateException("boom"); }
}
