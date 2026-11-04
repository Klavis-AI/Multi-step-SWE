import org.junit.jupiter.api.Test;
public class ClassInitFails {
   static { if (Boolean.TRUE) throw new RuntimeException("static init blew up"); }
   @Test public void never() { }
}
