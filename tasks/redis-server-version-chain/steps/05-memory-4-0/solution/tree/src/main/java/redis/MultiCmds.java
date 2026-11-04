package redis;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/** MULTI, EXEC, DISCARD, WATCH and UNWATCH (src/multi.c). */
public final class MultiCmds {
    private MultiCmds() {}

    /** Subscribe/unsubscribe commands are refused inside a transaction. */
    private static final Set<String> NOT_IN_MULTI = new HashSet<>(Arrays.asList(
            "subscribe", "unsubscribe", "psubscribe", "punsubscribe",
            "ssubscribe", "sunsubscribe"));

    public static void register() {
        CommandTable.reg("multi", 1, "noscript loading stale fast", 0, 0, 0, MultiCmds::multi);
        CommandTable.reg("exec", 1, "noscript loading stale", 0, 0, 0, MultiCmds::exec);
        CommandTable.reg("discard", 1, "noscript loading stale fast", 0, 0, 0, MultiCmds::discard);
        CommandTable.reg("watch", -2, "noscript loading stale fast", 1, -1, 1, MultiCmds::watch);
        CommandTable.reg("unwatch", 1, "noscript loading stale fast", 0, 0, 0, MultiCmds::unwatch);
    }

    // ------------------------------------------------------------------
    // MULTI / DISCARD
    // ------------------------------------------------------------------
    static void multi(Client c) {
        if (c.inMulti) {
            c.addError("ERR MULTI calls can not be nested");
            return;
        }
        c.inMulti = true;
        c.multiQueue.clear();
        c.multiError = false;
        c.addOk();
    }

    static void discard(Client c) {
        if (!c.inMulti) {
            c.addError("ERR DISCARD without MULTI");
            return;
        }
        Multi.discard(Server.I, c);
        c.addOk();
    }

    // ------------------------------------------------------------------
    // WATCH / UNWATCH
    // ------------------------------------------------------------------
    static void watch(Client c) {
        Server s = Server.I;
        if (c.inMulti) {
            c.addError("ERR WATCH inside MULTI is not allowed");
            return;
        }
        for (int j = 1; j < c.argc; j++) Multi.watchKey(s, c, c.arg(j));
        c.addOk();
    }

    static void unwatch(Client c) {
        Multi.unwatchAll(Server.I, c);
        c.addOk();
    }

    // ------------------------------------------------------------------
    // EXEC
    // ------------------------------------------------------------------
    static void exec(Client c) {
        Server s = Server.I;
        if (!c.inMulti) {
            c.addError("ERR EXEC without MULTI");
            return;
        }

        /* Abort because a WATCHed key was touched, or because queueing failed.
         * The first case replies a null array, the second an EXECABORT error. */
        if (c.dirtyCas || c.multiError) {
            if (c.multiError) c.addError("EXECABORT Transaction discarded because of previous errors.");
            else c.addNilArray();
            Multi.discard(s, c);
            return;
        }

        /* Unwatch ASAP, otherwise we'll waste CPU cycles. */
        Multi.unwatchAll(s, c);

        List<byte[][]> queued = new ArrayList<>(c.multiQueue);
        byte[][] origArgv = c.argv;
        int origArgc = c.argc;

        s.execDepth++;
        c.addArray(queued.size());
        boolean mustPropagate = false;
        try {
            for (byte[][] argv : queued) {
                c.argv = argv;
                c.argc = argv.length;
                String name = c.cmdName();
                Cmd cmd = CommandTable.lookup(name);
                if (cmd == null) {
                    c.addError("ERR unknown command '" + Util.s(argv[0]) + "'");
                    continue;
                }
                if ((cmd.arity > 0 && cmd.arity != c.argc) || (c.argc < -cmd.arity)) {
                    c.addError("ERR wrong number of arguments for '" + cmd.name + "' command");
                    continue;
                }
                if (NOT_IN_MULTI.contains(name)) {
                    c.addError("ERR " + name.toUpperCase(Locale.US) + " is not allowed in transactions");
                    continue;
                }
                /* multi.c: the ACLs are re-checked at execution time in case they
                 * changed after the commands were queued. */
                if (Acl.enabled(s)) {
                    String aclErr = AclState.checkExec(s, c, cmd);
                    if (aclErr != null) { c.addError(aclErr); continue; }
                }
                /* Propagate a MULTI once the first write command is reached, so
                 * that the AOF and the replicas see the whole block atomically. */
                if (!mustPropagate && cmd.isWrite()) {
                    /* propagatePendingCommands(): the MULTI itself carries no
                     * database, the SELECT is emitted by the first command
                     * inside the transaction. */
                    s.propagateExtra(-1, new byte[][]{Util.b("MULTI")});
                    mustPropagate = true;
                }
                try {
                    s.call(c, cmd);
                } catch (RedisError e) {
                    c.addErr(e.msg);
                } catch (RuntimeException e) {
                    c.addError("ERR internal error: " + e);
                }
                /* Blocking is denied inside MULTI; never leave the client blocked. */
                if (c.blocked) s.unblockClient(c);
            }
        } finally {
            c.argv = origArgv;
            c.argc = origArgc;
            s.execDepth--;
            Multi.discard(s, c);
            /* Make sure the EXEC is propagated as well when MULTI was. */
            if (mustPropagate) s.dirty++;
        }
    }
}
