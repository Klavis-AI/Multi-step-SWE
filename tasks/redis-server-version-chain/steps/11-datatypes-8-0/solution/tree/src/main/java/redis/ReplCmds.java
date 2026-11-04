package redis;

import java.util.*;

/**
 * The replication commands: REPLICAOF/SLAVEOF, SYNC, PSYNC, REPLCONF, WAIT
 * and ROLE.  Ported from redis src/replication.c.
 */
public final class ReplCmds {

    public static void register() {
        CommandTable.reg("replicaof", 3, "admin noscript stale", 0, 0, 0, ReplCmds::replicaof);
        CommandTable.reg("slaveof", 3, "admin noscript stale", 0, 0, 0, ReplCmds::replicaof);
        CommandTable.reg("sync", 1, "admin noscript", 0, 0, 0, ReplCmds::sync);
        CommandTable.reg("psync", -3, "admin noscript", 0, 0, 0, ReplCmds::sync);
        CommandTable.reg("replconf", -1, "admin noscript loading stale fast", 0, 0, 0, ReplCmds::replconf);
        CommandTable.reg("wait", 3, "blocking", 0, 0, 0, ReplCmds::wait);
        CommandTable.reg("waitaof", 4, "noscript blocking", 0, 0, 0, ReplCmds::waitaof);
        CommandTable.reg("role", 1, "loading stale fast", 0, 0, 0, ReplCmds::role);
        CommandTable.reg("monitor", 1, "admin noscript loading stale", 0, 0, 0, ReplCmds::monitor);

        Blocking.TIMEOUT.put("wait", (s, c) -> {
            long[] st = (long[]) c.blockState;
            c.addInt(st == null ? 0 : s.repl.countAcks(st[0]));
        });
        Blocking.TIMEOUT.put("waitaof", (s, c) -> {
            long[] st = (long[]) c.blockState;
            long off = st == null ? 0 : st[0];
            c.addArray(2);
            c.addInt((s.aof != null && s.aof.fsyncedReploff >= off) ? 1 : 0);
            c.addInt(s.repl.countAofAcks(off));
        });
    }

    // ------------------------------------------------------------------
    // MONITOR (replication.c monitorCommand)
    // ------------------------------------------------------------------
    static void monitor(Client c) {
        Server s = Server.I;
        /* Ignore MONITOR if the client is already a replica or a monitor. */
        if (c.isReplica || c.isMonitor) return;
        c.isMonitor = true;
        s.monitors.add(c);
        c.addOk();
    }

    // ------------------------------------------------------------------
    // REPLICAOF host port | REPLICAOF NO ONE
    // ------------------------------------------------------------------
    static void replicaof(Client c) {
        Server s = Server.I;
        Repl r = s.repl;

        if (s.config.getBool("cluster-enabled")) {
            c.addError("ERR REPLICAOF not allowed in cluster mode.");
            return;
        }
        if (!"no-failover".equals(r.failoverState)) {
            c.addError("ERR REPLICAOF not allowed while failing over.");
            return;
        }

        if (c.argIs(1, "no") && c.argIs(2, "one")) {
            if (r.masterHost != null) {
                r.unsetMaster();
                s.logNotice("MASTER MODE enabled (user request from 'id=" + c.id
                        + " addr=" + c.addr + "')");
            }
        } else {
            if (c.isReplica) {
                c.addError("ERR Command is not valid when client is a replica.");
                return;
            }
            Long port = Util.string2ll(c.argv[2]);
            if (port == null || port < 0 || port > 65535) {
                c.addError("ERR Invalid master port");
                return;
            }
            String host = c.argStr(1);
            if (r.masterHost != null && r.masterHost.equalsIgnoreCase(host)
                    && r.masterPort == port) {
                s.logNotice("REPLICAOF would result into synchronization with the master"
                        + " we are already connected with. No operation performed.");
                c.addRaw(Util.b("+OK Already connected to specified master\r\n"));
                return;
            }
            r.setMaster(host, (int) (long) port);
            s.logNotice("REPLICAOF " + host + ":" + port + " enabled (user request from 'id="
                    + c.id + " addr=" + c.addr + "')");
        }
        c.addOk();
    }

    // ------------------------------------------------------------------
    // SYNC / PSYNC <replid> <offset>
    // ------------------------------------------------------------------
    static void sync(Client c) {
        Server.I.repl.syncCommand(c);
    }

    // ------------------------------------------------------------------
    // REPLCONF <option> <value> ...
    // ------------------------------------------------------------------
    static void replconf(Client c) {
        Server.I.repl.replconfCommand(c);
    }

    // ------------------------------------------------------------------
    // WAIT numreplicas timeout
    // ------------------------------------------------------------------
    static void wait(Client c) {
        Server s = Server.I;
        Repl r = s.repl;

        if (r.masterHost != null) {
            c.addError("ERR WAIT cannot be used with replica instances. Please also note that"
                    + " since Redis 4.0 if a replica is configured to be writable (which is not"
                    + " the default) writes to replicas are just local and are not propagated.");
            return;
        }

        Long numreplicas = Util.string2ll(c.argv[1]);
        if (numreplicas == null) {
            c.addError("ERR value is not an integer or out of range");
            return;
        }
        Long timeout = Util.string2ll(c.argv[2]);
        if (timeout == null) {
            c.addError("ERR timeout is not an integer or out of range");
            return;
        }
        if (timeout < 0) {
            c.addError("ERR timeout is negative");
            return;
        }
        if (timeout > 0 && timeout > Long.MAX_VALUE - System.currentTimeMillis()) {
            c.addError("ERR timeout is out of range");
            return;
        }

        long offset = s.masterReplOffset;
        int ack = r.countAcks(offset);
        if (ack >= numreplicas || s.execDepth > 0 || c.fake) {
            c.addInt(ack);
            return;
        }

        // Ask every replica for an ACK as soon as possible, then block.
        r.requestAckFromReplicas();
        long capped = Math.min(timeout, Long.MAX_VALUE / 4);
        Blocking.blockOnKeys(s, c, new ArrayList<byte[]>(), capped / 1000.0, "wait",
                new long[]{offset, numreplicas});
    }

    // ------------------------------------------------------------------
    // WAITAOF numlocal numreplicas timeout (7.2, replication.c)
    // ------------------------------------------------------------------
    static void waitaof(Client c) {
        Server s = Server.I;
        Repl r = s.repl;

        Long numlocal = Util.string2ll(c.argv[1]);
        if (numlocal == null) {
            c.addError("ERR value is not an integer or out of range");
            return;
        }
        if (numlocal < 0 || numlocal > 1) {
            c.addError("ERR value is out of range, must be between 0 and 1 inclusive");
            return;
        }
        Long numreplicas = Util.string2ll(c.argv[2]);
        if (numreplicas == null) {
            c.addError("ERR value is not an integer or out of range");
            return;
        }
        if (numreplicas < 0) {
            c.addError("ERR value is out of range, must be positive");
            return;
        }
        Long timeout = Util.string2ll(c.argv[3]);
        if (timeout == null) {
            c.addError("ERR timeout is not an integer or out of range");
            return;
        }
        if (timeout < 0) {
            c.addError("ERR timeout is negative");
            return;
        }
        if (timeout > 0 && timeout > Long.MAX_VALUE - System.currentTimeMillis()) {
            c.addError("ERR timeout is out of range");
            return;
        }

        if (r.masterHost != null) {
            c.addError("ERR WAITAOF cannot be used with replica instances. Please also note that "
                    + "writes to replicas are just local and are not propagated.");
            return;
        }
        if (numlocal != 0 && !s.config.getBool("appendonly")) {
            c.addError("ERR WAITAOF cannot be used when numlocal is set but appendonly is disabled.");
            return;
        }

        long offset = c.woff;
        int ackreplicas = r.countAofAcks(offset);
        long acklocal = (s.aof != null && s.aof.fsyncedReploff >= offset) ? 1 : 0;
        if ((ackreplicas >= numreplicas && acklocal >= numlocal) || s.execDepth > 0 || c.fake) {
            c.addArray(2);
            c.addInt(acklocal);
            c.addInt(ackreplicas);
            return;
        }

        r.requestAckFromReplicas();
        long capped = Math.min(timeout, Long.MAX_VALUE / 4);
        Blocking.blockOnKeys(s, c, new ArrayList<byte[]>(), capped / 1000.0, "waitaof",
                new long[]{offset, numreplicas, numlocal});
    }

    // ------------------------------------------------------------------
    // ROLE
    // ------------------------------------------------------------------
    static void role(Client c) {
        Server s = Server.I;
        Repl r = s.repl;

        if (r.masterHost == null) {
            List<Client> online = new ArrayList<>();
            for (Client rep : s.replicas) {
                if ("online".equals(rep.replState)) online.add(rep);
            }
            c.addArray(3);
            c.addBulk("master");
            c.addInt(s.masterReplOffset);
            c.addArray(online.size());
            for (Client rep : online) {
                Repl.ReplicaState st = r.rstate.get(rep.id);
                String ip = (st != null && st.announceIp != null) ? st.announceIp : Repl.ipOf(rep);
                c.addArray(3);
                c.addBulk(ip);
                c.addBulk(Long.toString(rep.replAckPort));
                c.addBulk(Long.toString(rep.replAckOffset));
            }
        } else {
            c.addArray(5);
            c.addBulk("slave");
            c.addBulk(r.masterHost);
            c.addInt(r.masterPort);
            c.addBulk(r.replStateName());
            c.addInt(r.masterClient != null ? r.replOffset : -1);
        }
    }
}
