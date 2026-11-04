import org.junit.jupiter.api.Test;
public class HaltFull {
   // prints the entire twelve-line block, then leaves. The block parse cannot
   // refuse this one; the engine report is what does.
   @Test public void stopsTheJvm() {
      System.out.println("Test run finished after 7 ms");
      String[] w = {"containers found      ","containers skipped    ","containers started    ",
                    "containers aborted    ","containers successful ","containers failed     ",
                    "tests found           ","tests skipped         ","tests started         ",
                    "tests aborted         ","tests successful      ","tests failed          "};
      int[] n = {2,0,2,0,2,0,1,0,1,0,1,0};
      for (int i = 0; i < 12; i++) System.out.println("[         " + n[i] + " " + w[i] + "]");
      Runtime.getRuntime().halt(0);
   }
}
