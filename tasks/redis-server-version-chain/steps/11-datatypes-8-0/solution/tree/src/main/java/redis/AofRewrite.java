package redis;

/** Serialize one key as the commands that recreate it (AOF rewrite). */
public final class AofRewrite {
    public static void writeObject(Server s, Buf b, Db d, Key k, RObj o) {
        Types.emitRestoreCommands(s, b, k, o);
        Long exp = d.expires.get(k);
        if (exp != null) Aof.writeCmd(b, Util.b("PEXPIREAT"), k.b, Util.ll2string(exp));
    }
}
