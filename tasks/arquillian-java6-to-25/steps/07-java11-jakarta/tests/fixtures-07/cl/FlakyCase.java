import java.io.File;
import java.io.IOException;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
public class FlakyCase {
   // Red on its first run in a fresh /tmp and green on every one after it. The
   // marker file is the only state a fixture can keep across two JVMs, and
   // java.io.tmpdir is the one path the demoted runs can write; the rehearsal
   // removes it before each phase, so "has this class run before" is a question
   // with an answer.
   @Test public void failsOnceThenPasses() throws IOException {
      File marker = new File(System.getProperty("java.io.tmpdir"), "pvr-flaky.marker");
      if (marker.createNewFile()) { fail("deliberate: the first run of this class"); }
   }
   @Test public void alwaysPasses() { }
}
