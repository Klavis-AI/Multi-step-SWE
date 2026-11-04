package arqprobe;

/**
 * One scored input. Implementations must be deterministic: no wall-clock, no
 * identity hash codes, no filesystem paths, no iteration over unordered
 * collections without sorting. Everything they emit goes through {@link Rec},
 * which normalizes what is left.
 */
public interface Fixture
{
   void run(Rec r) throws Throwable;
}
