package redis;

import java.util.*;

/** Channel, pattern and shard-channel pub/sub registries. */
public final class PubSub {
    public final HashMap<Key, LinkedHashSet<Client>> channels = new HashMap<>();
    public final LinkedHashMap<Key, LinkedHashSet<Client>> patterns = new LinkedHashMap<>();
    public final HashMap<Key, LinkedHashSet<Client>> shard = new HashMap<>();

    public int subscribe(Client c, byte[] chan, boolean sh) {
        Key k = new Key(chan);
        Map<Key, LinkedHashSet<Client>> m = sh ? shard : channels;
        LinkedHashSet<Client> set = c.resp >= 0 ? m.computeIfAbsent(k, x -> new LinkedHashSet<>()) : null;
        set.add(c);
        (sh ? c.subShard : c.subChannels).add(k);
        return subCount(c);
    }

    public int unsubscribe(Client c, byte[] chan, boolean sh) {
        Key k = new Key(chan);
        Map<Key, LinkedHashSet<Client>> m = sh ? shard : channels;
        LinkedHashSet<Client> set = m.get(k);
        if (set != null) { set.remove(c); if (set.isEmpty()) m.remove(k); }
        (sh ? c.subShard : c.subChannels).remove(k);
        return subCount(c);
    }

    public int psubscribe(Client c, byte[] pat) {
        Key k = new Key(pat);
        patterns.computeIfAbsent(k, x -> new LinkedHashSet<>()).add(c);
        c.subPatterns.add(k);
        return subCount(c);
    }

    public int punsubscribe(Client c, byte[] pat) {
        Key k = new Key(pat);
        LinkedHashSet<Client> set = patterns.get(k);
        if (set != null) { set.remove(c); if (set.isEmpty()) patterns.remove(k); }
        c.subPatterns.remove(k);
        return subCount(c);
    }

    public int subCount(Client c) { return c.subChannels.size() + c.subPatterns.size() + c.subShard.size(); }

    public void unsubscribeAll(Server s, Client c) {
        for (Key k : new ArrayList<>(c.subChannels)) unsubscribe(c, k.b, false);
        for (Key k : new ArrayList<>(c.subShard)) unsubscribe(c, k.b, true);
        for (Key k : new ArrayList<>(c.subPatterns)) punsubscribe(c, k.b);
    }

    public int publishMessage(Server s, byte[] chan, byte[] msg, boolean sh) {
        int receivers = 0;
        Key k = new Key(chan);
        LinkedHashSet<Client> set = (sh ? shard : channels).get(k);
        if (set != null) {
            for (Client c : new ArrayList<>(set)) {
                Reply.push(c.out, 3, c.resp);
                Reply.bulk(c.out, Util.b(sh ? "smessage" : "message"));
                Reply.bulk(c.out, chan);
                Reply.bulk(c.out, msg);
                receivers++;
            }
        }
        if (!sh) {
            for (Map.Entry<Key, LinkedHashSet<Client>> e : patterns.entrySet()) {
                if (!Util.stringmatch(e.getKey().b, chan, false)) continue;
                for (Client c : new ArrayList<>(e.getValue())) {
                    Reply.push(c.out, 4, c.resp);
                    Reply.bulk(c.out, Util.b("pmessage"));
                    Reply.bulk(c.out, e.getKey().b);
                    Reply.bulk(c.out, chan);
                    Reply.bulk(c.out, msg);
                    receivers++;
                }
            }
        }
        return receivers;
    }
}
