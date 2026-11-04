package redis;

/** A protocol-level error reply, thrown out of a command implementation. */
public class RedisError extends RuntimeException {
    public final byte[] msg;
    public RedisError(String m) { super(m); this.msg = Util.b(m); }
    public RedisError(byte[] m) { super(Util.s(m)); this.msg = m; }

    public static RedisError wrongType() {
        return new RedisError("WRONGTYPE Operation against a key holding the wrong kind of value");
    }
    public static RedisError notInt() { return new RedisError("ERR value is not an integer or out of range"); }
    public static RedisError notFloat() { return new RedisError("ERR value is not a valid float"); }
    public static RedisError syntax() { return new RedisError("ERR syntax error"); }
    public static RedisError outOfRange() { return new RedisError("ERR value is out of range"); }
    public static RedisError wrongArgs(String cmd) {
        return new RedisError("ERR wrong number of arguments for '" + cmd + "' command");
    }
    public static RedisError noSuchKey() { return new RedisError("ERR no such key"); }
    public static RedisError indexOutOfRange() { return new RedisError("ERR index out of range"); }
}
