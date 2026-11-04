package redis;

import java.util.ArrayList;
import java.util.List;

/**
 * Pub/Sub commands: SUBSCRIBE, UNSUBSCRIBE, PSUBSCRIBE, PUNSUBSCRIBE,
 * SSUBSCRIBE, SUNSUBSCRIBE, PUBLISH, SPUBLISH and the PUBSUB container
 * (src/pubsub.c).
 */
public final class PubSubCmds {
    private PubSubCmds() {}

    public static void register() {
        String sub = "pubsub noscript loading stale";
        CommandTable.reg("subscribe", -2, sub, 0, 0, 0, PubSubCmds::subscribe);
        CommandTable.reg("unsubscribe", -1, sub, 0, 0, 0, PubSubCmds::unsubscribe);
        CommandTable.reg("psubscribe", -2, sub, 0, 0, 0, PubSubCmds::psubscribe);
        CommandTable.reg("punsubscribe", -1, sub, 0, 0, 0, PubSubCmds::punsubscribe);
        CommandTable.reg("ssubscribe", -2, sub, 0, 0, 0, PubSubCmds::ssubscribe);
        CommandTable.reg("sunsubscribe", -1, sub, 0, 0, 0, PubSubCmds::sunsubscribe);
        CommandTable.reg("publish", 3, "pubsub loading stale fast", 0, 0, 0, PubSubCmds::publish);
        CommandTable.reg("spublish", 3, "pubsub loading stale fast", 1, 1, 1, PubSubCmds::spublish);
        CommandTable.reg("pubsub", -2, "pubsub loading stale", 0, 0, 0, PubSubCmds::pubsub);

        CommandTable.regSub("pubsub", "channels", -2, "pubsub loading stale", PubSubCmds::channels);
        CommandTable.regSub("pubsub", "numsub", -2, "pubsub loading stale", PubSubCmds::numsub);
        CommandTable.regSub("pubsub", "numpat", -2, "pubsub loading stale", PubSubCmds::numpat);
        CommandTable.regSub("pubsub", "shardchannels", -2, "pubsub loading stale", PubSubCmds::shardchannels);
        CommandTable.regSub("pubsub", "shardnumsub", -2, "pubsub loading stale", PubSubCmds::shardnumsub);
        CommandTable.regSub("pubsub", "help", -2, "pubsub loading stale", PubSubCmds::help);
    }

    // ------------------------------------------------------------------
    // subscription counters
    // ------------------------------------------------------------------
    /** pubsub.c clientSubscriptionsCount(): global channels + patterns. */
    static int normalCount(Client c) { return c.subChannels.size() + c.subPatterns.size(); }

    /** pubsub.c clientShardSubscriptionsCount(): shard channels only. */
    static int shardCount(Client c) { return c.subShard.size(); }

    /** addReplyPubsubSubscribed() / addReplyPubsubUnsubscribed(): a 3 element push. */
    static void notify(Client c, String kind, byte[] channel, int count) {
        c.addPush(3);
        c.addBulk(Util.b(kind));
        if (channel == null) c.addNil();
        else c.addBulk(channel);
        c.addInt(count);
    }

    // ------------------------------------------------------------------
    // SUBSCRIBE / UNSUBSCRIBE
    // ------------------------------------------------------------------
    static void subscribe(Client c) {
        PubSub ps = Server.I.pubsub;
        for (int j = 1; j < c.argc; j++) {
            ps.subscribe(c, c.arg(j), false);
            notify(c, "subscribe", c.arg(j), normalCount(c));
        }
    }

    static void unsubscribe(Client c) {
        PubSub ps = Server.I.pubsub;
        if (c.argc == 1) {
            List<Key> all = new ArrayList<>(c.subChannels);
            if (all.isEmpty()) {
                notify(c, "unsubscribe", null, normalCount(c));
            } else {
                for (Key k : all) {
                    ps.unsubscribe(c, k.b, false);
                    notify(c, "unsubscribe", k.b, normalCount(c));
                }
            }
        } else {
            for (int j = 1; j < c.argc; j++) {
                ps.unsubscribe(c, c.arg(j), false);
                notify(c, "unsubscribe", c.arg(j), normalCount(c));
            }
        }
    }

    static void psubscribe(Client c) {
        PubSub ps = Server.I.pubsub;
        for (int j = 1; j < c.argc; j++) {
            ps.psubscribe(c, c.arg(j));
            notify(c, "psubscribe", c.arg(j), normalCount(c));
        }
    }

    static void punsubscribe(Client c) {
        PubSub ps = Server.I.pubsub;
        if (c.argc == 1) {
            List<Key> all = new ArrayList<>(c.subPatterns);
            if (all.isEmpty()) {
                notify(c, "punsubscribe", null, normalCount(c));
            } else {
                for (Key k : all) {
                    ps.punsubscribe(c, k.b);
                    notify(c, "punsubscribe", k.b, normalCount(c));
                }
            }
        } else {
            for (int j = 1; j < c.argc; j++) {
                ps.punsubscribe(c, c.arg(j));
                notify(c, "punsubscribe", c.arg(j), normalCount(c));
            }
        }
    }

    static void ssubscribe(Client c) {
        PubSub ps = Server.I.pubsub;
        for (int j = 1; j < c.argc; j++) {
            ps.subscribe(c, c.arg(j), true);
            notify(c, "ssubscribe", c.arg(j), shardCount(c));
        }
    }

    static void sunsubscribe(Client c) {
        PubSub ps = Server.I.pubsub;
        if (c.argc == 1) {
            List<Key> all = new ArrayList<>(c.subShard);
            if (all.isEmpty()) {
                notify(c, "sunsubscribe", null, shardCount(c));
            } else {
                for (Key k : all) {
                    ps.unsubscribe(c, k.b, true);
                    notify(c, "sunsubscribe", k.b, shardCount(c));
                }
            }
        } else {
            for (int j = 1; j < c.argc; j++) {
                ps.unsubscribe(c, c.arg(j), true);
                notify(c, "sunsubscribe", c.arg(j), shardCount(c));
            }
        }
    }

    // ------------------------------------------------------------------
    // PUBLISH / SPUBLISH
    // ------------------------------------------------------------------
    static void publish(Client c) {
        int receivers = Server.I.pubsub.publishMessage(Server.I, c.arg(1), c.arg(2), false);
        /* pubsub.c publishCommand(): in cluster mode the message travels over
         * the cluster bus instead of the replication link. */
        if (Server.I.cluster != null) Server.I.cluster.propagatePublish(c.arg(1), c.arg(2));
        c.addInt(receivers);
    }

    static void spublish(Client c) {
        int receivers = Server.I.pubsub.publishMessage(Server.I, c.arg(1), c.arg(2), true);
        c.addInt(receivers);
    }

    // ------------------------------------------------------------------
    // PUBSUB
    // ------------------------------------------------------------------
    static void pubsub(Client c) {
        ConnCmds.markSub(c, "pubsub");
        CommandTable.dispatchSub(c, "pubsub");
    }

    /** pubsub.c channelList(): every registered channel, optionally glob filtered. */
    static void channelList(Client c, byte[] pat, java.util.Map<Key, java.util.LinkedHashSet<Client>> m) {
        List<Key> out = new ArrayList<>();
        for (Key k : m.keySet()) {
            if (pat == null || Util.stringmatch(pat, k.b, false)) out.add(k);
        }
        c.addArray(out.size());
        for (Key k : out) c.addBulk(k.b);
    }

    static void channels(Client c) {
        if (c.argc != 2 && c.argc != 3) throw ConnCmds.subErr(c, "pubsub");
        channelList(c, c.argc == 3 ? c.arg(2) : null, Server.I.pubsub.channels);
    }

    static void shardchannels(Client c) {
        if (c.argc != 2 && c.argc != 3) throw ConnCmds.subErr(c, "pubsub");
        channelList(c, c.argc == 3 ? c.arg(2) : null, Server.I.pubsub.shard);
    }

    static void numsub(Client c) {
        PubSub ps = Server.I.pubsub;
        c.addArray((c.argc - 2) * 2);
        for (int j = 2; j < c.argc; j++) {
            java.util.LinkedHashSet<Client> set = ps.channels.get(new Key(c.arg(j)));
            c.addBulk(c.arg(j));
            c.addInt(set == null ? 0 : set.size());
        }
    }

    static void shardnumsub(Client c) {
        PubSub ps = Server.I.pubsub;
        c.addArray((c.argc - 2) * 2);
        for (int j = 2; j < c.argc; j++) {
            java.util.LinkedHashSet<Client> set = ps.shard.get(new Key(c.arg(j)));
            c.addBulk(c.arg(j));
            c.addInt(set == null ? 0 : set.size());
        }
    }

    static void numpat(Client c) {
        if (c.argc != 2) throw ConnCmds.arity("pubsub|numpat");
        c.addInt(Server.I.pubsub.patterns.size());
    }

    static final String[] PUBSUB_HELP = {
        "CHANNELS [<pattern>]",
        "    Return the currently active channels matching a <pattern> (default: '*').",
        "NUMPAT",
        "    Return number of subscriptions to patterns.",
        "NUMSUB [<channel> ...]",
        "    Return the number of subscribers for the specified channels, excluding",
        "    pattern subscriptions(default: no channels).",
        "SHARDCHANNELS [<pattern>]",
        "    Return the currently active shard level channels matching a <pattern> (default: '*').",
        "SHARDNUMSUB [<shardchannel> ...]",
        "    Return the number of subscribers for the specified shard level channel(s)",
    };

    static void help(Client c) {
        if (c.argc != 2) throw ConnCmds.arity("pubsub|help");
        ConnCmds.addHelp(c, PUBSUB_HELP);
    }
}
