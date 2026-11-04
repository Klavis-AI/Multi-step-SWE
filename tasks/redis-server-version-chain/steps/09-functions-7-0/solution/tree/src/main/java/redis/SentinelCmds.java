package redis;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The SENTINEL command family and the sentinel mode command table
 * (src/sentinel.c sentinelcmds[], sentinelCommand(), sentinelInfoCommand(),
 * sentinelRoleCommand(), sentinelSetCommand(), sentinelPublishCommand()).
 */
public final class SentinelCmds {
    private SentinelCmds() {}

    /**
     * initSentinel(): throw away the normal command table and install the
     * handful of commands a Sentinel understands.
     */
    public static void initSentinelTable() {
        CommandTable.init();
        Map<String, Cmd> keep = new LinkedHashMap<>();
        /* sentinelcmds[] in 2.8, plus the commands later releases keep reachable
         * in Sentinel mode (CMD_SENTINEL in commands.def) and CONFIG. */
        for (String n : new String[]{"ping", "subscribe", "unsubscribe", "psubscribe",
                                     "punsubscribe", "shutdown", "client", "config",
                                     "auth", "hello", "command"}) {
            Cmd c = CommandTable.TABLE.get(n);
            if (c != null) keep.put(n, c);
        }
        Map<String, Map<String, Cmd>> keepSubs = new LinkedHashMap<>();
        for (String n : new String[]{"client", "config", "command"}) {
            Map<String, Cmd> m = CommandTable.SUBS.get(n);
            if (m != null) keepSubs.put(n, m);
        }
        CommandTable.TABLE.clear();
        CommandTable.SUBS.clear();
        CommandTable.TABLE.putAll(keep);
        CommandTable.SUBS.putAll(keepSubs);

        CommandTable.reg("sentinel", -2, "admin", 0, 0, 0, SentinelCmds::sentinelCommand);
        CommandTable.reg("publish", 3, "pubsub loading stale fast", 0, 0, 0, SentinelCmds::publish);
        CommandTable.reg("info", -1, "loading stale", 0, 0, 0, SentinelCmds::info);
        CommandTable.reg("role", 1, "loading stale fast", 0, 0, 0, SentinelCmds::role);
    }

    static Sentinel sen() { return Server.I.sentinel; }

    // ------------------------------------------------------------------
    // helpers
    // ------------------------------------------------------------------
    /** sentinelGetMasterByNameOrReplyError() */
    private static Sentinel.Instance masterByNameOrReply(Client c, int idx) {
        Sentinel.Instance ri = sen().masters.get(c.argStr(idx));
        if (ri == null) {
            c.addError("ERR No such master with that name");
            return null;
        }
        return ri;
    }

    private static void numArgsErr(Client c) {
        c.addError("ERR Wrong number of arguments for 'sentinel " + c.argStr(1) + "'");
    }

    /** sentinelFailoverStateStr() */
    static String failoverStateStr(int state) {
        switch (state) {
            case Sentinel.FS_NONE: return "none";
            case Sentinel.FS_WAIT_START: return "wait_start";
            case Sentinel.FS_SELECT_SLAVE: return "select_slave";
            case Sentinel.FS_SEND_SLAVEOF_NOONE: return "send_slaveof_noone";
            case Sentinel.FS_WAIT_PROMOTION: return "wait_promotion";
            case Sentinel.FS_RECONF_SLAVES: return "reconf_slaves";
            case Sentinel.FS_UPDATE_CONFIG: return "update_config";
            default: return "unknown";
        }
    }

    /** addReplySentinelRedisInstance() */
    static void addInstance(Client c, Sentinel.Instance ri) {
        List<String[]> f = new ArrayList<>();
        long now = Sentinel.mstime();

        f.add(new String[]{"name", ri.name});
        f.add(new String[]{"ip", ri.addr.ip});
        f.add(new String[]{"port", Integer.toString(ri.addr.port)});
        f.add(new String[]{"runid", ri.runid != null ? ri.runid : ""});

        StringBuilder flags = new StringBuilder();
        if ((ri.flags & Sentinel.SRI_S_DOWN) != 0) flags.append("s_down,");
        if ((ri.flags & Sentinel.SRI_O_DOWN) != 0) flags.append("o_down,");
        if ((ri.flags & Sentinel.SRI_MASTER) != 0) flags.append("master,");
        if ((ri.flags & Sentinel.SRI_SLAVE) != 0) flags.append("slave,");
        if ((ri.flags & Sentinel.SRI_SENTINEL) != 0) flags.append("sentinel,");
        if ((ri.flags & Sentinel.SRI_DISCONNECTED) != 0) flags.append("disconnected,");
        if ((ri.flags & Sentinel.SRI_MASTER_DOWN) != 0) flags.append("master_down,");
        if ((ri.flags & Sentinel.SRI_FAILOVER_IN_PROGRESS) != 0) flags.append("failover_in_progress,");
        if ((ri.flags & Sentinel.SRI_PROMOTED) != 0) flags.append("promoted,");
        if ((ri.flags & Sentinel.SRI_RECONF_SENT) != 0) flags.append("reconf_sent,");
        if ((ri.flags & Sentinel.SRI_RECONF_INPROG) != 0) flags.append("reconf_inprog,");
        if ((ri.flags & Sentinel.SRI_RECONF_DONE) != 0) flags.append("reconf_done,");
        if (flags.length() != 0) flags.setLength(flags.length() - 1);
        f.add(new String[]{"flags", flags.toString()});

        f.add(new String[]{"pending-commands", Integer.toString(ri.pendingCommands)});

        if ((ri.flags & Sentinel.SRI_FAILOVER_IN_PROGRESS) != 0)
            f.add(new String[]{"failover-state", failoverStateStr(ri.failoverState)});

        f.add(new String[]{"last-ping-sent", Long.toString(ri.lastPingTime != 0 ? now - ri.lastPingTime : 0)});
        f.add(new String[]{"last-ok-ping-reply", Long.toString(now - ri.lastAvailTime)});
        f.add(new String[]{"last-ping-reply", Long.toString(now - ri.lastPongTime)});

        if ((ri.flags & Sentinel.SRI_S_DOWN) != 0)
            f.add(new String[]{"s-down-time", Long.toString(now - ri.sDownSinceTime)});
        if ((ri.flags & Sentinel.SRI_O_DOWN) != 0)
            f.add(new String[]{"o-down-time", Long.toString(now - ri.oDownSinceTime)});

        f.add(new String[]{"down-after-milliseconds", Long.toString(ri.downAfterPeriod)});

        if ((ri.flags & (Sentinel.SRI_MASTER | Sentinel.SRI_SLAVE)) != 0) {
            f.add(new String[]{"info-refresh", Long.toString(now - ri.infoRefresh)});
            f.add(new String[]{"role-reported",
                    ri.roleReported == Sentinel.SRI_MASTER ? "master" : "slave"});
            f.add(new String[]{"role-reported-time", Long.toString(now - ri.roleReportedTime)});
        }

        if ((ri.flags & Sentinel.SRI_MASTER) != 0) {
            f.add(new String[]{"config-epoch", Long.toString(ri.configEpoch)});
            f.add(new String[]{"num-slaves", Integer.toString(ri.slaves.size())});
            f.add(new String[]{"num-other-sentinels", Integer.toString(ri.sentinels.size())});
            f.add(new String[]{"quorum", Integer.toString(ri.quorum)});
            f.add(new String[]{"failover-timeout", Long.toString(ri.failoverTimeout)});
            f.add(new String[]{"parallel-syncs", Integer.toString(ri.parallelSyncs)});
            if (ri.notificationScript != null)
                f.add(new String[]{"notification-script", ri.notificationScript});
            if (ri.clientReconfigScript != null)
                f.add(new String[]{"client-reconfig-script", ri.clientReconfigScript});
        }

        if ((ri.flags & Sentinel.SRI_SLAVE) != 0) {
            f.add(new String[]{"master-link-down-time", Long.toString(ri.masterLinkDownTime)});
            f.add(new String[]{"master-link-status",
                    ri.slaveMasterLinkStatus == Sentinel.LINK_UP ? "ok" : "err"});
            f.add(new String[]{"master-host", ri.slaveMasterHost != null ? ri.slaveMasterHost : "?"});
            f.add(new String[]{"master-port", Integer.toString(ri.slaveMasterPort)});
            f.add(new String[]{"slave-priority", Integer.toString(ri.slavePriority)});
            f.add(new String[]{"slave-repl-offset", Long.toString(ri.slaveReplOffset)});
        }

        if ((ri.flags & Sentinel.SRI_SENTINEL) != 0) {
            f.add(new String[]{"last-hello-message", Long.toString(now - ri.lastHelloTime)});
            f.add(new String[]{"voted-leader", ri.leader != null ? ri.leader : "?"});
            f.add(new String[]{"voted-leader-epoch", Long.toString(ri.leaderEpoch)});
        }

        c.addArray(f.size() * 2);
        for (String[] kv : f) { c.addBulk(kv[0]); c.addBulk(kv[1]); }
    }

    static void addDictOfInstances(Client c, Map<String, Sentinel.Instance> instances) {
        c.addArray(instances.size());
        for (Sentinel.Instance ri : new ArrayList<>(instances.values())) addInstance(c, ri);
    }

    /** sentinelIsQuorumReachable() */
    static final int ISQR_OK = 0, ISQR_NOQUORUM = 1, ISQR_NOAUTH = 2;

    static int isQuorumReachable(Sentinel.Instance master, int[] usableOut) {
        int usable = 1;
        int result = ISQR_OK;
        int voters = master.sentinels.size() + 1;
        for (Sentinel.Instance ri : master.sentinels.values()) {
            if ((ri.flags & (Sentinel.SRI_S_DOWN | Sentinel.SRI_O_DOWN)) != 0) continue;
            usable++;
        }
        if (usable < master.quorum) result |= ISQR_NOQUORUM;
        if (usable < voters / 2 + 1) result |= ISQR_NOAUTH;
        if (usableOut != null) usableOut[0] = usable;
        return result;
    }

    // ------------------------------------------------------------------
    // SENTINEL
    // ------------------------------------------------------------------
    static void sentinelCommand(Client c) {
        Sentinel sen = sen();
        String sub = c.argLower(1);

        if (sub.equals("masters")) {
            if (c.argc != 2) { numArgsErr(c); return; }
            addDictOfInstances(c, sen.masters);

        } else if (sub.equals("master")) {
            if (c.argc != 3) { numArgsErr(c); return; }
            Sentinel.Instance ri = masterByNameOrReply(c, 2);
            if (ri == null) return;
            addInstance(c, ri);

        } else if (sub.equals("slaves") || sub.equals("replicas")) {
            if (c.argc != 3) { numArgsErr(c); return; }
            Sentinel.Instance ri = masterByNameOrReply(c, 2);
            if (ri == null) return;
            addDictOfInstances(c, ri.slaves);

        } else if (sub.equals("sentinels")) {
            if (c.argc != 3) { numArgsErr(c); return; }
            Sentinel.Instance ri = masterByNameOrReply(c, 2);
            if (ri == null) return;
            addDictOfInstances(c, ri.sentinels);

        } else if (sub.equals("is-master-down-by-addr")) {
            if (c.argc != 6) { numArgsErr(c); return; }
            long port, reqEpoch;
            try { port = Long.parseLong(c.argStr(3).trim()); }
            catch (NumberFormatException e) { c.addError("ERR value is not an integer or out of range"); return; }
            try { reqEpoch = Long.parseLong(c.argStr(4).trim()); }
            catch (NumberFormatException e) { c.addError("ERR value is not an integer or out of range"); return; }

            Sentinel.Instance ri = Sentinel.byAddrAndRunID(sen.masters, c.argStr(2), (int) port, null);
            boolean isdown = !sen.tilt && ri != null
                    && (ri.flags & Sentinel.SRI_S_DOWN) != 0
                    && (ri.flags & Sentinel.SRI_MASTER) != 0;

            String leader = null;
            long[] leaderEpoch = new long[1];
            if (ri != null && (ri.flags & Sentinel.SRI_MASTER) != 0 && !c.argStr(5).equals("*")) {
                leader = sen.voteLeader(ri, reqEpoch, c.argStr(5), leaderEpoch);
            }
            c.addArray(3);
            c.addInt(isdown ? 1 : 0);
            c.addBulk(leader != null ? leader : "*");
            c.addInt(leaderEpoch[0]);

        } else if (sub.equals("reset")) {
            if (c.argc != 3) { numArgsErr(c); return; }
            c.addInt(sen.resetMastersByPattern(c.arg(2), Sentinel.GENERATE_EVENT));

        } else if (sub.equals("get-master-addr-by-name")) {
            if (c.argc != 3) { numArgsErr(c); return; }
            Sentinel.Instance ri = sen.masters.get(c.argStr(2));
            if (ri == null) {
                c.addNilArray();
            } else {
                Sentinel.Addr addr = Sentinel.currentMasterAddr(ri);
                c.addArray(2);
                c.addBulk(addr.ip);
                c.addBulk(Integer.toString(addr.port));
            }

        } else if (sub.equals("failover")) {
            if (c.argc != 3) { numArgsErr(c); return; }
            Sentinel.Instance ri = masterByNameOrReply(c, 2);
            if (ri == null) return;
            if ((ri.flags & Sentinel.SRI_FAILOVER_IN_PROGRESS) != 0) {
                c.addError("INPROG Failover already in progress");
                return;
            }
            if (sen.selectSlave(ri) == null) {
                c.addError("NOGOODSLAVE No suitable slave to promote");
                return;
            }
            Server.I.logWarn("Executing user requested FAILOVER of '" + ri.name + "'");
            sen.startFailover(ri);
            ri.flags |= Sentinel.SRI_FORCE_FAILOVER;
            c.addOk();

        } else if (sub.equals("pending-scripts")) {
            if (c.argc != 2) { numArgsErr(c); return; }
            c.addArray(0);

        } else if (sub.equals("monitor")) {
            if (c.argc != 6) { numArgsErr(c); return; }
            long quorum, port;
            try { quorum = Long.parseLong(c.argStr(5).trim()); }
            catch (NumberFormatException e) { c.addError("ERR Invalid quorum"); return; }
            try { port = Long.parseLong(c.argStr(4).trim()); }
            catch (NumberFormatException e) { c.addError("ERR Invalid port"); return; }
            Sentinel.Addr probe = Sentinel.createAddr(c.argStr(3), port <= 0 ? 1 : (int) port);
            if (probe == null && (port > 0 && port <= 65535)) {
                c.addError("ERR Invalid IP address specified");
                return;
            }
            Sentinel.Instance ri = sen.createInstance(c.argStr(2), Sentinel.SRI_MASTER,
                    c.argStr(3), (int) port, (int) quorum, null);
            if (ri == null) {
                if (sen.masters.containsKey(c.argStr(2))) c.addError("ERR Duplicated master name");
                else if (port <= 0 || port > 65535) c.addError("ERR Invalid port number");
                else c.addError("ERR Unspecified error adding the instance");
            } else {
                sen.flushConfig();
                sen.event(Sentinel.WARNING, "+monitor", ri, "%@ quorum %d", ri.quorum);
                c.addOk();
            }

        } else if (sub.equals("flushconfig")) {
            if (c.argc != 2) { numArgsErr(c); return; }
            sen.flushConfig();
            c.addOk();

        } else if (sub.equals("remove")) {
            if (c.argc != 3) { numArgsErr(c); return; }
            Sentinel.Instance ri = masterByNameOrReply(c, 2);
            if (ri == null) return;
            sen.event(Sentinel.WARNING, "-monitor", ri, "%@");
            sen.masters.remove(c.argStr(2));
            sen.releaseInstance(ri);
            sen.flushConfig();
            c.addOk();

        } else if (sub.equals("ckquorum")) {
            if (c.argc != 3) { numArgsErr(c); return; }
            Sentinel.Instance ri = masterByNameOrReply(c, 2);
            if (ri == null) return;
            int[] usable = new int[1];
            int result = isQuorumReachable(ri, usable);
            if (result == ISQR_OK) {
                c.addSimple("OK " + usable[0] + " usable Sentinels. Quorum and failover authorization "
                        + "can be reached");
            } else {
                StringBuilder e = new StringBuilder("NOQUORUM " + usable[0] + " usable Sentinels. ");
                if ((result & ISQR_NOQUORUM) != 0)
                    e.append("Not enough available Sentinels to reach the specified quorum for this master");
                if ((result & ISQR_NOAUTH) != 0) {
                    if ((result & ISQR_NOQUORUM) != 0) e.append(". ");
                    e.append("Not enough available Sentinels to reach the majority and authorize a failover");
                }
                c.addError(e.toString());
            }

        } else if (sub.equals("set")) {
            if (c.argc < 3 || c.argc % 2 == 0) { numArgsErr(c); return; }
            sentinelSetCommand(c);

        } else if (sub.equals("info-cache")) {
            infoCache(c);

        } else if (sub.equals("simulate-failure")) {
            simulateFailure(c);

        } else if (sub.equals("myid") && c.argc == 2) {
            c.addBulk(sen.myid());

        } else if (sub.equals("help")) {
            c.addArray(1);
            c.addBulk("SENTINEL <subcommand> [<arg> [value] [opt] ...]");

        } else {
            c.addError("ERR Unknown sentinel subcommand '" + c.argStr(1) + "'");
        }
    }

    /** SENTINEL INFO-CACHE [name ...] */
    private static void infoCache(Client c) {
        Sentinel sen = sen();
        List<Sentinel.Instance> wanted = new ArrayList<>();
        if (c.argc == 2) {
            wanted.addAll(sen.masters.values());
        } else {
            for (int i = 2; i < c.argc; i++) {
                Sentinel.Instance ri = sen.masters.get(c.argStr(i));
                if (ri != null) wanted.add(ri);
            }
        }
        long now = Sentinel.mstime();
        /* 1) master name
         * 2) 1) 1) info cached ms  2) info from master
         *    2) 1) info cached ms  2) info from replica1 ... */
        c.addArray(wanted.size() * 2);
        for (Sentinel.Instance master : wanted) {
            c.addBulk(master.name);
            c.addArray(master.slaves.size() + 1);
            c.addArray(2);
            c.addInt(master.infoRefresh != 0 ? now - master.infoRefresh : 0);
            if (master.info != null) c.addBulk(Util.b(master.info)); else c.addNil();
            for (Sentinel.Instance slave : master.slaves.values()) {
                c.addArray(2);
                c.addInt(master.infoRefresh != 0 ? now - slave.infoRefresh : 0);
                if (slave.info != null) c.addBulk(Util.b(slave.info)); else c.addNil();
            }
        }
    }

    /** SENTINEL SIMULATE-FAILURE <flag> ... */
    private static void simulateFailure(Client c) {
        Sentinel sen = sen();
        sen.simFailCrashAfterElection = false;
        sen.simFailCrashAfterPromotion = false;
        for (int i = 2; i < c.argc; i++) {
            String f = c.argLower(i);
            if (f.equals("crash-after-election")) {
                sen.simFailCrashAfterElection = true;
                Server.I.logWarn("Failure simulation: this Sentinel will crash after being "
                        + "successfully elected as failover leader");
            } else if (f.equals("crash-after-promotion")) {
                sen.simFailCrashAfterPromotion = true;
                Server.I.logWarn("Failure simulation: this Sentinel will crash after promoting "
                        + "the selected replica to master");
            } else if (f.equals("help")) {
                c.addArray(2);
                c.addBulk("crash-after-election");
                c.addBulk("crash-after-promotion");
                return;
            } else {
                c.addError("ERR Unknown failure simulation specified");
                return;
            }
        }
        c.addOk();
    }

    /** sentinelSetCommand(): SENTINEL SET <name> <option> <value> ... */
    static void sentinelSetCommand(Client c) {
        Sentinel sen = sen();
        Sentinel.Instance ri = masterByNameOrReply(c, 2);
        if (ri == null) return;
        int changes = 0;

        for (int j = 3; j < c.argc; j += 2) {
            String option = c.argLower(j);
            String value = c.argStr(j + 1);
            Long ll = Util.string2ll(c.arg(j + 1));

            if (option.equals("down-after-milliseconds")) {
                if (ll == null || ll <= 0) { badfmt(c, changes, value, c.argStr(j)); return; }
                ri.downAfterPeriod = ll;
                sen.propagateDownAfterPeriod(ri);
                changes++;
            } else if (option.equals("failover-timeout")) {
                if (ll == null || ll <= 0) { badfmt(c, changes, value, c.argStr(j)); return; }
                ri.failoverTimeout = ll;
                changes++;
            } else if (option.equals("parallel-syncs")) {
                if (ll == null || ll <= 0) { badfmt(c, changes, value, c.argStr(j)); return; }
                ri.parallelSyncs = (int) (long) ll;
                changes++;
            } else if (option.equals("notification-script")) {
                ri.notificationScript = value.isEmpty() ? null : value;
                changes++;
            } else if (option.equals("client-reconfig-script")) {
                ri.clientReconfigScript = value.isEmpty() ? null : value;
                changes++;
            } else if (option.equals("auth-pass")) {
                ri.authPass = value.isEmpty() ? null : value;
                changes++;
            } else if (option.equals("quorum")) {
                if (ll == null || ll <= 0) { badfmt(c, changes, value, c.argStr(j)); return; }
                ri.quorum = (int) (long) ll;
                changes++;
            } else {
                c.addError("ERR Unknown option '" + c.argStr(j) + "' for SENTINEL SET");
                if (changes > 0) sen.flushConfig();
                return;
            }
            sen.event(Sentinel.WARNING, "+set", ri, "%@ %s %s", option, value);
        }
        if (changes > 0) sen.flushConfig();
        c.addOk();
    }

    private static void badfmt(Client c, int changes, String value, String option) {
        if (changes > 0) sen().flushConfig();
        c.addError("ERR Invalid argument '" + value + "' for SENTINEL SET '" + option + "'");
    }

    // ------------------------------------------------------------------
    // PUBLISH (sentinel version): only hello messages are accepted
    // ------------------------------------------------------------------
    static void publish(Client c) {
        if (!c.argStr(1).equals(Sentinel.HELLO_CHANNEL)) {
            c.addError("ERR Only HELLO messages are accepted by Sentinel instances.");
            return;
        }
        sen().processHelloMessage(c.argStr(2));
        c.addInt(1);
    }

    // ------------------------------------------------------------------
    // ROLE
    // ------------------------------------------------------------------
    static void role(Client c) {
        Sentinel sen = sen();
        c.addArray(2);
        c.addBulk("sentinel");
        c.addArray(sen.masters.size());
        for (Sentinel.Instance ri : sen.masters.values()) c.addBulk(ri.name);
    }

    // ------------------------------------------------------------------
    // INFO
    // ------------------------------------------------------------------
    static void info(Client c) {
        if (c.argc > 2) { c.addError("ERR syntax error"); return; }
        String section = c.argc == 2 ? c.argLower(1) : "default";
        boolean def = section.equals("default") || section.equals("all") || section.equals("everything");
        StringBuilder info = new StringBuilder();
        int sections = 0;
        Server s = Server.I;

        if (section.equals("server") || def) {
            if (sections++ > 0) info.append("\r\n");
            long uptime = (System.currentTimeMillis() - s.startTime) / 1000L;
            info.append("# Server\r\n");
            kv(info, "redis_version", Server.VERSION);
            kv(info, "redis_git_sha1", "00000000");
            kv(info, "redis_git_dirty", "0");
            kv(info, "redis_build_id", "0000000000000000");
            kv(info, "redis_mode", "sentinel");
            kv(info, "os", System.getProperty("os.name", "Linux"));
            kv(info, "arch_bits", "64");
            kv(info, "multiplexing_api", "epoll");
            kv(info, "gcc_version", "0.0.0");
            kv(info, "process_id", Long.toString(ProcessHandle.current().pid()));
            kv(info, "run_id", s.runId);
            kv(info, "tcp_port", Integer.toString(s.port));
            kv(info, "uptime_in_seconds", Long.toString(uptime));
            kv(info, "uptime_in_days", Long.toString(uptime / (3600 * 24)));
            kv(info, "hz", "10");
            kv(info, "lru_clock", Long.toString((System.currentTimeMillis() / 1000L) & 0xffffffL));
            kv(info, "config_file", s.config.configFile == null ? "" : s.config.configFile);
        }

        if (section.equals("sentinel") || def) {
            if (sections++ > 0) info.append("\r\n");
            Sentinel sen = sen();
            info.append("# Sentinel\r\n");
            kv(info, "sentinel_masters", Integer.toString(sen.masters.size()));
            kv(info, "sentinel_tilt", sen.tilt ? "1" : "0");
            kv(info, "sentinel_running_scripts", "0");
            kv(info, "sentinel_scripts_queue_length", "0");
            int masterId = 0;
            for (Sentinel.Instance ri : sen.masters.values()) {
                String status = "ok";
                if ((ri.flags & Sentinel.SRI_O_DOWN) != 0) status = "odown";
                else if ((ri.flags & Sentinel.SRI_S_DOWN) != 0) status = "sdown";
                info.append(String.format(Locale.US,
                        "master%d:name=%s,status=%s,address=%s:%d,slaves=%d,sentinels=%d\r\n",
                        masterId++, ri.name, status, ri.addr.ip, ri.addr.port,
                        ri.slaves.size(), ri.sentinels.size() + 1));
            }
        }
        c.addBulk(Util.b(info.toString()));
    }

    private static void kv(StringBuilder sb, String k, String v) {
        sb.append(k).append(':').append(v).append("\r\n");
    }
}
