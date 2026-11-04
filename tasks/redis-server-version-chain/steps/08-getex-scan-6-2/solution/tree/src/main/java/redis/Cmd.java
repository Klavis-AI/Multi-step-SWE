package redis;

/** One entry of the command table. */
public final class Cmd {
    public interface Handler { void run(Client c) throws RedisError; }

    public final String name;
    public final String fullName;      // "config|get" for subcommands
    public final int arity;            // negative = "at least |arity|"
    public final String flags;         // space-separated redis command flags
    public final int firstKey, lastKey, keyStep;
    public final Handler handler;
    public final int sinceMajor;       // first redis version exposing it (major*100+minor)

    public Cmd(String name, int arity, String flags, int firstKey, int lastKey, int keyStep, Handler h) {
        this(name, name, arity, flags, firstKey, lastKey, keyStep, h, 0);
    }

    public Cmd(String name, String fullName, int arity, String flags,
               int firstKey, int lastKey, int keyStep, Handler h, int since) {
        this.name = name; this.fullName = fullName; this.arity = arity; this.flags = flags;
        this.firstKey = firstKey; this.lastKey = lastKey; this.keyStep = keyStep;
        this.handler = h; this.sinceMajor = since;
    }

    public boolean isWrite() { return has("write"); }
    public boolean isReadonly() { return has("readonly"); }
    public boolean isAdmin() { return has("admin"); }
    public boolean isDenyOom() { return has("denyoom"); }
    public boolean isNoScript() { return has("noscript"); }
    public boolean isLoading() { return has("loading"); }
    public boolean isStale() { return has("stale"); }
    public boolean isFast() { return has("fast"); }
    public boolean isPubsub() { return has("pubsub"); }

    public boolean has(String f) {
        return (" " + flags + " ").contains(" " + f + " ");
    }
}
