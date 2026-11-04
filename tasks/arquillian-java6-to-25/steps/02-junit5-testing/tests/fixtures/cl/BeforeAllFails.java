import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeAll;
public class BeforeAllFails {
   @BeforeAll static void setup() { throw new RuntimeException("container down"); }
   @Test public void a() { }
}
