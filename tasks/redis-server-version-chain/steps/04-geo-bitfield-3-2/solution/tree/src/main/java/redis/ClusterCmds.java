package redis;

import java.util.*;

/**
 * The CLUSTER command family plus ASKING / READONLY / READWRITE and
 * RESTORE-ASKING. Ported from redis 3.0.7 src/cluster.c clusterCommand(),
 * with the subcommands that only exist in later releases (SHARDS, LINKS,
 * ADDSLOTSRANGE, DELSLOTSRANGE, MYID) taken from 8.0.6 src/cluster.c.
 */
public final class ClusterCmds {
    private ClusterCmds() {}

    public static void register() {
        CommandTable.reg("cluster", -2, "admin random stale", 0, 0, 0, ClusterCmds::cluster);
        CommandTable.reg("asking", 1, "fast", 0, 0, 0, ClusterCmds::asking);
        CommandTable.reg("readonly", 1, "fast loading stale", 0, 0, 0, ClusterCmds::readonly);
        CommandTable.reg("readwrite", 1, "fast loading stale", 0, 0, 0, ClusterCmds::readwrite);
        CommandTable.reg("restore-asking", -4, "write denyoom asking", 1, 1, 1,
                ClusterCmds::restoreAsking);
    }

    static Server srv() { return Server.I; }

    static Cluster cl() {
        Cluster c = Server.I.cluster;
        if (c == null) throw new RedisError("ERR This instance has cluster support disabled");
        return c;
    }

    // ------------------------------------------------------------------
    // ASKING / READONLY / READWRITE
    // ------------------------------------------------------------------
    static void asking(Client c) {
        if (Server.I.cluster == null) {
            c.addError("ERR This instance has cluster support disabled");
            return;
        }
        c.asking = true;
        c.addOk();
    }

    static void readonly(Client c) {
        if (Server.I.cluster == null) {
            c.addError("ERR This instance has cluster support disabled");
            return;
        }
        c.readonlyMode = true;
        c.addOk();
    }

    static void readwrite(Client c) {
        c.readonlyMode = false;
        c.addOk();
    }

    /** RESTORE-ASKING is RESTORE with the CMD_ASKING flag (cluster.c). */
    static void restoreAsking(Client c) {
        Cmd restore = CommandTable.lookup("restore");
        if (restore == null) throw new RedisError("ERR unknown command");
        restore.handler.run(c);
    }

    // ------------------------------------------------------------------
    // CLUSTER
    // ------------------------------------------------------------------
    static void cluster(Client c) {
        Server s = srv();
        if (s.cluster == null) {
            c.addError("ERR This instance has cluster support disabled");
            return;
        }
        Cluster cl = s.cluster;
        String sub = c.argLower(1);
        int argc = c.argc;

        if (sub.equals("meet") && argc == 4) {
            Long port = Util.string2ll(c.arg(3));
            if (port == null) {
                c.addError("ERR Invalid TCP port specified: " + c.argStr(3));
                return;
            }
            int r = cl.startHandshake(c.argStr(2), (int) (long) port);
            if (r == Cluster.HS_EINVAL) {
                c.addError("ERR Invalid node address specified: " + c.argStr(2) + ":" + c.argStr(3));
            } else {
                c.addOk();
            }
        } else if (sub.equals("nodes") && argc == 2) {
            c.addBulk(Util.b(cl.genNodesDescription(0).toString()));
        } else if (sub.equals("myid") && argc == 2) {
            c.addBulk(Util.b(cl.myself.name));
        } else if (sub.equals("slots") && argc == 2) {
            replySlots(c, cl);
        } else if (sub.equals("shards") && argc == 2) {
            replyShards(c, cl);
        } else if (sub.equals("links") && argc == 2) {
            replyLinks(c, cl);
        } else if (sub.equals("flushslots") && argc == 2) {
            if (s.dbs[0].size() != 0) {
                c.addError("ERR DB must be empty to perform CLUSTER FLUSHSLOTS.");
                return;
            }
            cl.delNodeSlots(cl.myself);
            cl.doBeforeSleep(Cluster.TODO_UPDATE_STATE | Cluster.TODO_SAVE_CONFIG);
            c.addOk();
        } else if ((sub.equals("addslots") || sub.equals("delslots")) && argc >= 3) {
            addDelSlots(c, cl, sub.equals("delslots"), false);
        } else if ((sub.equals("addslotsrange") || sub.equals("delslotsrange")) && argc >= 4) {
            if ((argc % 2) != 0) {
                c.addError("ERR wrong number of arguments for 'cluster|" + sub + "' command");
                return;
            }
            addDelSlots(c, cl, sub.equals("delslotsrange"), true);
        } else if (sub.equals("setslot") && argc >= 4) {
            setslot(c, cl);
        } else if (sub.equals("bumpepoch") && argc == 2) {
            boolean bumped = cl.bumpConfigEpochWithoutConsensus();
            c.addSimple((bumped ? "BUMPED " : "STILL ")
                    + Long.toUnsignedString(cl.myself.configEpoch));
        } else if (sub.equals("info") && argc == 2) {
            c.addBulk(Util.b(clusterInfo(cl)));
        } else if (sub.equals("saveconfig") && argc == 2) {
            int retval = cl.saveConfig(true);
            if (retval == 0) c.addOk();
            else c.addError("ERR error saving the cluster node config");
        } else if (sub.equals("keyslot") && argc == 3) {
            c.addInt(Cluster.keyHashSlot(c.arg(2)));
        } else if (sub.equals("countkeysinslot") && argc == 3) {
            Long slot = Util.string2ll(c.arg(2));
            if (slot == null) throw RedisError.notInt();
            if (slot < 0 || slot >= Cluster.SLOTS) {
                c.addError("ERR Invalid slot");
                return;
            }
            c.addInt(cl.countKeysInSlot((int) (long) slot));
        } else if (sub.equals("getkeysinslot") && argc == 4) {
            Long slot = Util.string2ll(c.arg(2));
            if (slot == null) throw RedisError.notInt();
            Long maxkeys = Util.string2ll(c.arg(3));
            if (maxkeys == null) throw RedisError.notInt();
            if (slot < 0 || slot >= Cluster.SLOTS || maxkeys < 0) {
                c.addError("ERR Invalid slot or number of keys");
                return;
            }
            List<byte[]> keys = cl.getKeysInSlot((int) (long) slot,
                    (int) Math.min(maxkeys, Integer.MAX_VALUE));
            c.addArray(keys.size());
            for (byte[] k : keys) c.addBulk(k);
        } else if (sub.equals("forget") && argc == 3) {
            Cluster.Node n = cl.lookupNode(c.argStr(2));
            if (n == null) {
                c.addError("ERR Unknown node " + c.argStr(2));
                return;
            } else if (n == cl.myself) {
                c.addError("ERR I tried hard but I can't forget myself...");
                return;
            } else if (cl.myself.isSlave() && cl.myself.slaveof == n) {
                c.addError("ERR Can't forget my master!");
                return;
            }
            cl.blacklistAddNode(n);
            cl.delNode(n);
            cl.doBeforeSleep(Cluster.TODO_UPDATE_STATE | Cluster.TODO_SAVE_CONFIG);
            c.addOk();
        } else if (sub.equals("replicate") && argc == 3) {
            Cluster.Node n = cl.lookupNode(c.argStr(2));
            if (n == null) {
                c.addError("ERR Unknown node " + c.argStr(2));
                return;
            }
            if (n == cl.myself) {
                c.addError("ERR Can't replicate myself");
                return;
            }
            if (n.isSlave()) {
                c.addError("ERR I can only replicate a master, not a slave.");
                return;
            }
            if (cl.myself.isMaster() && (cl.myself.numslots != 0 || s.dbs[0].size() != 0)) {
                c.addError("ERR To set a master the node must be empty and "
                        + "without assigned slots.");
                return;
            }
            cl.setMaster(n);
            cl.doBeforeSleep(Cluster.TODO_UPDATE_STATE | Cluster.TODO_SAVE_CONFIG);
            c.addOk();
        } else if ((sub.equals("slaves") || sub.equals("replicas")) && argc == 3) {
            Cluster.Node n = cl.lookupNode(c.argStr(2));
            if (n == null) {
                c.addError("ERR Unknown node " + c.argStr(2));
                return;
            }
            if (n.isSlave()) {
                c.addError("ERR The specified node is not a master");
                return;
            }
            c.addArray(n.slaves.size());
            for (Cluster.Node sl : n.slaves) c.addBulk(Util.b(cl.genNodeDescription(sl)));
        } else if (sub.equals("count-failure-reports") && argc == 3) {
            Cluster.Node n = cl.lookupNode(c.argStr(2));
            if (n == null) {
                c.addError("ERR Unknown node " + c.argStr(2));
                return;
            }
            c.addInt(cl.nodeFailureReportsCount(n));
        } else if (sub.equals("failover") && (argc == 2 || argc == 3)) {
            failover(c, cl);
        } else if (sub.equals("set-config-epoch") && argc == 3) {
            Long epoch = Util.string2ll(c.arg(2));
            if (epoch == null) throw RedisError.notInt();
            if (epoch < 0) {
                c.addError("ERR Invalid config epoch specified: " + epoch);
            } else if (cl.nodes.size() > 1) {
                c.addError("ERR The user can assign a config epoch only when the "
                        + "node does not know any other node.");
            } else if (cl.myself.configEpoch != 0) {
                c.addError("ERR Node config epoch is already non-zero");
            } else {
                cl.myself.configEpoch = epoch;
                s.logWarn("configEpoch set to " + Long.toUnsignedString(cl.myself.configEpoch)
                        + " via CLUSTER SET-CONFIG-EPOCH");
                if (Long.compareUnsigned(cl.currentEpoch, epoch) < 0) cl.currentEpoch = epoch;
                cl.doBeforeSleep(Cluster.TODO_UPDATE_STATE | Cluster.TODO_SAVE_CONFIG);
                c.addOk();
            }
        } else if (sub.equals("reset") && (argc == 2 || argc == 3)) {
            boolean hard = false;
            if (argc == 3) {
                if (c.argIs(2, "hard")) hard = true;
                else if (c.argIs(2, "soft")) hard = false;
                else { c.addError("ERR syntax error"); return; }
            }
            if (cl.myself.isMaster() && s.dbs[c.db].size() != 0) {
                c.addError("ERR CLUSTER RESET can't be called with master nodes containing keys");
                return;
            }
            cl.reset(hard);
            c.addOk();
        } else if (sub.equals("help")) {
            c.addError("ERR Wrong CLUSTER subcommand or number of arguments");
        } else {
            c.addError("ERR Wrong CLUSTER subcommand or number of arguments");
        }
    }

    // ------------------------------------------------------------------
    // ADDSLOTS / DELSLOTS / ADDSLOTSRANGE / DELSLOTSRANGE
    // ------------------------------------------------------------------
    private static void addDelSlots(Client c, Cluster cl, boolean del, boolean ranges) {
        byte[] marks = new byte[Cluster.SLOTS];
        int step = ranges ? 2 : 1;
        for (int j = 2; j < c.argc; j += step) {
            int start = getSlotOrReply(c, j);
            if (start == -1) return;
            int end = start;
            if (ranges) {
                end = getSlotOrReply(c, j + 1);
                if (end == -1) return;
                if (start > end) {
                    c.addError("ERR start slot number " + start
                            + " is greater than end slot number " + end);
                    return;
                }
            }
            for (int slot = start; slot <= end; slot++) {
                if (del && cl.slots[slot] == null) {
                    c.addError("ERR Slot " + slot + " is already unassigned");
                    return;
                } else if (!del && cl.slots[slot] != null) {
                    c.addError("ERR Slot " + slot + " is already busy");
                    return;
                }
                if (marks[slot]++ == 1) {
                    c.addError("ERR Slot " + slot + " specified multiple times");
                    return;
                }
            }
        }
        for (int j = 0; j < Cluster.SLOTS; j++) {
            if (marks[j] == 0) continue;
            if (cl.importingSlotsFrom[j] != null) cl.importingSlotsFrom[j] = null;
            if (del) cl.delSlot(j);
            else cl.addSlot(cl.myself, j);
        }
        cl.doBeforeSleep(Cluster.TODO_UPDATE_STATE | Cluster.TODO_SAVE_CONFIG);
        c.addOk();
    }

    /** cluster.c getSlotOrReply(): -1 means the error was already sent. */
    private static int getSlotOrReply(Client c, int idx) {
        Long slot = Util.string2ll(c.arg(idx));
        if (slot == null || slot < 0 || slot >= Cluster.SLOTS) {
            c.addError("ERR Invalid or out of range slot");
            return -1;
        }
        return (int) (long) slot;
    }

    // ------------------------------------------------------------------
    // SETSLOT
    // ------------------------------------------------------------------
    private static void setslot(Client c, Cluster cl) {
        if (cl.myself.isSlave()) {
            c.addError("ERR Please use SETSLOT only with masters.");
            return;
        }
        int slot = getSlotOrReply(c, 2);
        if (slot == -1) return;

        if (c.argIs(3, "migrating") && c.argc == 5) {
            if (cl.slots[slot] != cl.myself) {
                c.addError("ERR I'm not the owner of hash slot " + slot);
                return;
            }
            Cluster.Node n = cl.lookupNode(c.argStr(4));
            if (n == null) {
                c.addError("ERR I don't know about node " + c.argStr(4));
                return;
            }
            cl.migratingSlotsTo[slot] = n;
        } else if (c.argIs(3, "importing") && c.argc == 5) {
            if (cl.slots[slot] == cl.myself) {
                c.addError("ERR I'm already the owner of hash slot " + slot);
                return;
            }
            Cluster.Node n = cl.lookupNode(c.argStr(4));
            if (n == null) {
                c.addError("ERR I don't know about node " + c.argStr(3));
                return;
            }
            cl.importingSlotsFrom[slot] = n;
        } else if (c.argIs(3, "stable") && c.argc == 4) {
            cl.importingSlotsFrom[slot] = null;
            cl.migratingSlotsTo[slot] = null;
        } else if (c.argIs(3, "node") && c.argc == 5) {
            Cluster.Node n = cl.lookupNode(c.argStr(4));
            if (n == null) {
                c.addError("ERR Unknown node " + c.argStr(4));
                return;
            }
            if (cl.slots[slot] == cl.myself && n != cl.myself) {
                if (cl.countKeysInSlot(slot) != 0) {
                    c.addError("ERR Can't assign hashslot " + slot + " to a different node "
                            + "while I still hold keys for this hash slot.");
                    return;
                }
            }
            if (cl.countKeysInSlot(slot) == 0 && cl.migratingSlotsTo[slot] != null)
                cl.migratingSlotsTo[slot] = null;
            if (n == cl.myself && cl.importingSlotsFrom[slot] != null) {
                if (cl.bumpConfigEpochWithoutConsensus())
                    Server.I.logWarn("configEpoch updated after importing slot " + slot);
                cl.importingSlotsFrom[slot] = null;
            }
            cl.delSlot(slot);
            cl.addSlot(n, slot);
        } else {
            c.addError("ERR Invalid CLUSTER SETSLOT action or number of arguments");
            return;
        }
        cl.doBeforeSleep(Cluster.TODO_SAVE_CONFIG | Cluster.TODO_UPDATE_STATE);
        c.addOk();
    }

    // ------------------------------------------------------------------
    // FAILOVER
    // ------------------------------------------------------------------
    private static void failover(Client c, Cluster cl) {
        boolean force = false, takeover = false;
        if (c.argc == 3) {
            if (c.argIs(2, "force")) {
                force = true;
            } else if (c.argIs(2, "takeover")) {
                takeover = true;
                force = true;
            } else {
                c.addError("ERR syntax error");
                return;
            }
        }
        if (cl.myself.isMaster()) {
            c.addError("ERR You should send CLUSTER FAILOVER to a slave");
            return;
        } else if (cl.myself.slaveof == null) {
            c.addError("ERR I'm a slave but my master is unknown to me");
            return;
        } else if (!force && (cl.myself.slaveof.failed() || cl.myself.slaveof.link == null)) {
            c.addError("ERR Master is down or failed, please use CLUSTER FAILOVER FORCE");
            return;
        }
        cl.resetManualFailover();
        cl.mfEnd = Cluster.now() + Cluster.MF_TIMEOUT;

        if (takeover) {
            Server.I.logWarn("Taking over the master (user request).");
            cl.bumpConfigEpochWithoutConsensus();
            cl.failoverReplaceYourMaster();
        } else if (force) {
            Server.I.logWarn("Forced failover user request accepted.");
            cl.mfCanStart = true;
        } else {
            Server.I.logWarn("Manual failover user request accepted.");
            cl.sendMFStart(cl.myself.slaveof);
        }
        c.addOk();
    }

    // ------------------------------------------------------------------
    // CLUSTER INFO
    // ------------------------------------------------------------------
    private static String clusterInfo(Cluster cl) {
        String[] statestr = {"ok", "fail", "needhelp"};
        int slotsAssigned = 0, slotsOk = 0, slotsPfail = 0, slotsFail = 0;
        for (int j = 0; j < Cluster.SLOTS; j++) {
            Cluster.Node n = cl.slots[j];
            if (n == null) continue;
            slotsAssigned++;
            if (n.failed()) slotsFail++;
            else if (n.timedOut()) slotsPfail++;
            else slotsOk++;
        }
        long myepoch = (cl.myself.isSlave() && cl.myself.slaveof != null)
                ? cl.myself.slaveof.configEpoch : cl.myself.configEpoch;
        StringBuilder info = new StringBuilder(512);
        info.append("cluster_state:").append(statestr[cl.state]).append("\r\n");
        info.append("cluster_slots_assigned:").append(slotsAssigned).append("\r\n");
        info.append("cluster_slots_ok:").append(slotsOk).append("\r\n");
        info.append("cluster_slots_pfail:").append(slotsPfail).append("\r\n");
        info.append("cluster_slots_fail:").append(slotsFail).append("\r\n");
        info.append("cluster_known_nodes:").append(cl.nodes.size()).append("\r\n");
        info.append("cluster_size:").append(cl.size).append("\r\n");
        info.append("cluster_current_epoch:").append(Long.toUnsignedString(cl.currentEpoch)).append("\r\n");
        info.append("cluster_my_epoch:").append(Long.toUnsignedString(myepoch)).append("\r\n");
        long totSent = 0, totRecv = 0;
        for (int i = 0; i < cl.statsSent.length; i++) {
            if (cl.statsSent[i] == 0) continue;
            totSent += cl.statsSent[i];
            info.append("cluster_stats_messages_").append(Cluster.MSG_NAMES[i])
                .append("_sent:").append(cl.statsSent[i]).append("\r\n");
        }
        info.append("cluster_stats_messages_sent:").append(totSent).append("\r\n");
        for (int i = 0; i < cl.statsReceived.length; i++) {
            if (cl.statsReceived[i] == 0) continue;
            totRecv += cl.statsReceived[i];
            info.append("cluster_stats_messages_").append(Cluster.MSG_NAMES[i])
                .append("_received:").append(cl.statsReceived[i]).append("\r\n");
        }
        info.append("cluster_stats_messages_received:").append(totRecv).append("\r\n");
        info.append("total_cluster_links_buffer_limit_exceeded:0\r\n");
        return info.toString();
    }

    // ------------------------------------------------------------------
    // CLUSTER SLOTS / SHARDS / LINKS
    // ------------------------------------------------------------------
    /** [start, end] ranges of the slots served by 'node'. */
    private static List<int[]> slotRanges(Cluster.Node node) {
        List<int[]> out = new ArrayList<>();
        int start = -1;
        for (int j = 0; j < Cluster.SLOTS; j++) {
            boolean bit = Cluster.nodeGetSlotBit(node, j);
            if (bit && start == -1) start = j;
            if (start != -1 && (!bit || j == Cluster.SLOTS - 1)) {
                int end = bit && j == Cluster.SLOTS - 1 ? j : j - 1;
                out.add(new int[]{start, end});
                start = -1;
            }
        }
        return out;
    }

    private static void replySlots(Client c, Cluster cl) {
        List<Cluster.Node> masters = new ArrayList<>();
        int total = 0;
        for (Cluster.Node node : cl.nodes.values()) {
            if (!node.isMaster() || node.numslots == 0) continue;
            masters.add(node);
            total += slotRanges(node).size();
        }
        c.addArray(total);
        for (Cluster.Node node : masters) {
            List<Cluster.Node> okSlaves = new ArrayList<>();
            for (Cluster.Node sl : node.slaves) if (!sl.failed()) okSlaves.add(sl);
            for (int[] range : slotRanges(node)) {
                c.addArray(3 + okSlaves.size());
                c.addInt(range[0]);
                c.addInt(range[1]);
                c.addArray(2);
                c.addBulk(Util.b(node.ip));
                c.addInt(node.port);
                for (Cluster.Node sl : okSlaves) {
                    c.addArray(2);
                    c.addBulk(Util.b(sl.ip));
                    c.addInt(sl.port);
                }
            }
        }
    }

    private static void replyShards(Client c, Cluster cl) {
        /* One shard per master; its replicas follow it in the node list. */
        List<Cluster.Node> masters = new ArrayList<>();
        for (Cluster.Node node : cl.nodes.values()) if (node.isMaster()) masters.add(node);
        c.addArray(masters.size());
        for (Cluster.Node m : masters) {
            c.addMap(2);
            c.addBulk("slots");
            List<int[]> ranges = slotRanges(m);
            c.addArray(ranges.size() * 2);
            for (int[] r : ranges) { c.addInt(r[0]); c.addInt(r[1]); }
            c.addBulk("nodes");
            c.addArray(1 + m.slaves.size());
            addShardNode(c, cl, m);
            for (Cluster.Node sl : m.slaves) addShardNode(c, cl, sl);
        }
    }

    private static void addShardNode(Client c, Cluster cl, Cluster.Node node) {
        long offset;
        if (node == cl.myself) {
            offset = node.isSlave() ? cl.replicationGetSlaveOffset() : Server.I.masterReplOffset;
        } else {
            offset = node.replOffset;
        }
        c.addMap(7);
        c.addBulk("id");
        c.addBulk(Util.b(node.name));
        c.addBulk("port");
        c.addInt(node.port);
        c.addBulk("ip");
        c.addBulk(Util.b(node.ip));
        c.addBulk("endpoint");
        c.addBulk(Util.b(node.ip));
        c.addBulk("role");
        c.addBulk(node.isSlave() ? "replica" : "master");
        c.addBulk("replication-offset");
        c.addInt(offset);
        c.addBulk("health");
        c.addBulk(node.failed() ? "fail" : (node.isSlave() && offset == 0 ? "loading" : "online"));
    }

    private static void replyLinks(Client c, Cluster cl) {
        List<Cluster.Link> links = new ArrayList<>();
        for (Cluster.Node node : cl.nodes.values()) if (node.link != null) links.add(node.link);
        c.addArray(links.size());
        for (Cluster.Link l : links) {
            c.addMap(6);
            c.addBulk("direction");
            c.addBulk(l.inbound ? "from" : "to");
            c.addBulk("node");
            c.addBulk(Util.b(l.node == null ? "" : l.node.name));
            c.addBulk("create-time");
            c.addInt(l.ctime);
            c.addBulk("events");
            c.addBulk("r");
            c.addBulk("send-buffer-allocated");
            c.addInt(0);
            c.addBulk("send-buffer-used");
            c.addInt(0);
        }
    }
}
