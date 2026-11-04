package redis;

import java.util.*;

/**
 * Client side caching: the key -> client-id invalidation table, the BCAST
 * prefix table and the invalidation push messages (src/tracking.c).
 */
public final class Tracking {
    private Tracking() {}

    /** TrackingTable: key -> the ids of the clients that may cache it */
    static final HashMap<Key, LinkedHashSet<Long>> TABLE = new HashMap<>();

    /** the per-prefix broadcast state */
    static final class Bcast {
        /** keys modified in the current event loop cycle -> id of the client that did it */
        final LinkedHashMap<Key, Long> keys = new LinkedHashMap<>();
        final LinkedHashSet<Long> clients = new LinkedHashSet<>();
    }

    /** PrefixTable */
    static final LinkedHashMap<Key, Bcast> PREFIX = new LinkedHashMap<>();

    public static long totalItems = 0;
    public static int trackingClients = 0;

    /** server.tracking_pending_keys; a null element means "all the keys" */
    private static final List<byte[]> pending = new ArrayList<>();

    /** server.current_client, only tracked while a top level command runs */
    static Client current = null;
    private static int depth = 0;

    public static long totalKeys() { return TABLE.size(); }
    public static long totalPrefixes() { return PREFIX.size(); }

    // ------------------------------------------------------------------
    // enable / disable
    // ------------------------------------------------------------------
    public static void disableTracking(Server s, Client c) {
        if (c.trackingBcast) {
            for (byte[] p : c.trackingPrefixes) {
                Key k = new Key(p);
                Bcast bs = PREFIX.get(k);
                if (bs == null) continue;
                bs.clients.remove(c.id);
                if (bs.clients.isEmpty()) PREFIX.remove(k);
            }
        }
        c.trackingPrefixes.clear();
        if (c.tracking) trackingClients--;
        c.tracking = false;
        c.trackingBcast = false;
        c.trackingOptIn = false;
        c.trackingOptOut = false;
        c.trackingNoloop = false;
        c.trackingCachingOn = false;
        c.trackingBrokenRedir = false;
    }

    static void enableBcastForPrefix(Client c, byte[] prefix) {
        Key k = new Key(prefix);
        Bcast bs = PREFIX.computeIfAbsent(k, x -> new Bcast());
        if (bs.clients.add(c.id)) {
            boolean have = false;
            for (byte[] p : c.trackingPrefixes) if (Arrays.equals(p, prefix)) { have = true; break; }
            if (!have) c.trackingPrefixes.add(prefix);
        }
    }

    public static void enableTracking(Server s, Client c, long redirect, boolean bcast,
                                      boolean optin, boolean optout, boolean noloop,
                                      List<byte[]> prefixes) {
        if (!c.tracking) trackingClients++;
        c.tracking = true;
        c.trackingBrokenRedir = false;
        c.trackingBcast = false;
        c.trackingOptIn = false;
        c.trackingOptOut = false;
        c.trackingNoloop = false;
        c.trackingRedir = redirect;
        if (bcast) {
            c.trackingBcast = true;
            if (prefixes.isEmpty()) enableBcastForPrefix(c, Util.EMPTY);
            else for (byte[] p : prefixes) enableBcastForPrefix(c, p);
        }
        c.trackingOptIn = optin;
        c.trackingOptOut = optout;
        c.trackingNoloop = noloop;
    }

    // ------------------------------------------------------------------
    // remembering the keys served to a tracking client
    // ------------------------------------------------------------------
    public static void rememberKeys(Server s, Client tracking, Client executing, Cmd cmd) {
        boolean optin = tracking.trackingOptIn;
        boolean optout = tracking.trackingOptOut;
        boolean caching = tracking.trackingCachingOn;
        if ((optin && !caching) || (optout && caching)) return;
        if (cmd.isPubsub()) return;
        List<Integer> keys = AclState.keyPositions(cmd, executing.argv, executing.argc);
        if (keys.isEmpty()) return;
        for (int idx : keys) {
            if (idx >= executing.argc) continue;
            Key k = new Key(executing.argv[idx]);
            LinkedHashSet<Long> ids = TABLE.computeIfAbsent(k, x -> new LinkedHashSet<>());
            if (ids.add(tracking.id)) totalItems++;
        }
    }

    // ------------------------------------------------------------------
    // sending the invalidation
    // ------------------------------------------------------------------
    static void sendTrackingMessage(Server s, Client c, byte[] payload, boolean proto) {
        boolean usingRedirection = false;
        if (c.trackingRedir > 0) {
            Client redir = s.clients.get(c.trackingRedir);
            if (redir == null || redir.chan == null) {
                c.trackingBrokenRedir = true;
                if (c.resp > 2) {
                    Reply.push(c.out, 2, c.resp);
                    Reply.bulk(c.out, Util.b("tracking-redir-broken"));
                    Reply.integer(c.out, c.trackingRedir);
                }
                return;
            }
            c = redir;
            usingRedirection = true;
        }
        if (c.resp > 2) {
            Reply.push(c.out, 2, c.resp);
            Reply.bulk(c.out, Util.b("invalidate"));
        } else if (usingRedirection && (!c.subChannels.isEmpty() || !c.subPatterns.isEmpty())) {
            Reply.push(c.out, 3, c.resp);
            Reply.bulk(c.out, Util.b("message"));
            Reply.bulk(c.out, Util.b("__redis__:invalidate"));
        } else {
            /* RESP2 without redirection cannot receive push messages. */
            return;
        }
        if (proto) c.out.put(payload);
        else {
            Reply.array(c.out, 1);
            Reply.bulk(c.out, payload);
        }
    }

    static byte[] nullPayload(int resp) {
        return resp >= 3 ? Util.b("_\r\n") : Util.b("*-1\r\n");
    }

    static void rememberKeyToBroadcast(Client c, byte[] key) {
        for (Map.Entry<Key, Bcast> e : PREFIX.entrySet()) {
            byte[] p = e.getKey().b;
            if (p.length > key.length) continue;
            boolean match = true;
            for (int i = 0; i < p.length; i++) if (p[i] != key[i]) { match = false; break; }
            if (!match) continue;
            e.getValue().keys.put(new Key(key), c == null ? null : c.id);
        }
    }

    /** tracking.c trackingInvalidateKey() */
    public static void invalidateKey(Server s, Client c, byte[] key, boolean bcast) {
        if (trackingClients == 0 && TABLE.isEmpty() && PREFIX.isEmpty()) return;
        if (bcast && !PREFIX.isEmpty()) rememberKeyToBroadcast(c, key);
        Key k = new Key(key);
        LinkedHashSet<Long> ids = TABLE.remove(k);
        if (ids == null) return;
        totalItems -= ids.size();
        for (long id : ids) {
            Client target = s.clients.get(id);
            if (target == null || !target.tracking || target.trackingBcast) continue;
            if (target.trackingNoloop && target == current) continue;
            if (target == current && depth > 0) pending.add(key);
            else sendTrackingMessage(s, target, key, false);
        }
    }

    public static void handlePendingKeyInvalidations(Server s) {
        if (pending.isEmpty()) return;
        List<byte[]> copy = new ArrayList<>(pending);
        pending.clear();
        Client cur = current;
        if (cur == null) return;
        for (byte[] key : copy) {
            if (key != null) sendTrackingMessage(s, cur, key, false);
            else sendTrackingMessage(s, cur, nullPayload(cur.resp), true);
        }
    }

    /** tracking.c trackingInvalidateKeysOnFlush() */
    public static void invalidateKeysOnFlush(Server s) {
        if (trackingClients > 0) {
            for (Client c : new ArrayList<>(s.clients.values())) {
                if (!c.tracking) continue;
                if (c == current) pending.add(null);
                else sendTrackingMessage(s, c, nullPayload(c.resp), true);
            }
        }
        TABLE.clear();
        totalItems = 0;
    }

    /** tracking.c trackingLimitUsedSlots() */
    public static void limitUsedSlots(Server s) {
        if (trackingClients == 0) return;
        long max = s.config.getLong("tracking-table-max-keys");
        if (max == 0) return;
        if (TABLE.size() <= max) return;
        Iterator<Key> it = TABLE.keySet().iterator();
        List<byte[]> victims = new ArrayList<>();
        while (it.hasNext() && TABLE.size() - victims.size() > max) victims.add(it.next().b);
        for (byte[] v : victims) invalidateKey(s, null, v, false);
    }

    /** tracking.c trackingBuildBroadcastReply() */
    static byte[] buildBroadcastReply(Long excludeId, Bcast bs) {
        int count = 0;
        for (Map.Entry<Key, Long> e : bs.keys.entrySet())
            if (excludeId == null || !excludeId.equals(e.getValue())) count++;
        if (excludeId != null && count == 0) return null;
        Buf b = new Buf(64);
        Reply.array(b, count);
        for (Map.Entry<Key, Long> e : bs.keys.entrySet()) {
            if (excludeId != null && excludeId.equals(e.getValue())) continue;
            Reply.bulk(b, e.getKey().b);
        }
        return b.toBytes();
    }

    /** tracking.c trackingBroadcastInvalidationMessages(), from beforeSleep() */
    public static void broadcastInvalidationMessages(Server s) {
        if (PREFIX.isEmpty() || trackingClients == 0) return;
        for (Bcast bs : new ArrayList<>(PREFIX.values())) {
            if (!bs.keys.isEmpty()) {
                byte[] proto = buildBroadcastReply(null, bs);
                for (long id : new ArrayList<>(bs.clients)) {
                    Client c = s.clients.get(id);
                    if (c == null) continue;
                    if (c.trackingNoloop) {
                        byte[] adhoc = buildBroadcastReply(id, bs);
                        if (adhoc != null) sendTrackingMessage(s, c, adhoc, true);
                    } else {
                        sendTrackingMessage(s, c, proto, true);
                    }
                }
            }
            bs.keys.clear();
        }
    }

    // ------------------------------------------------------------------
    // dispatcher hooks
    // ------------------------------------------------------------------
    public static void beforeCall(Server s, Client c) {
        if (depth == 0 && !c.fake) current = c;
        depth++;
    }

    public static void afterCall(Server s, Client c, Cmd cmd) {
        Client cur = current;
        if (cmd.isReadonly() && cur != null && cur.tracking && !cur.trackingBcast) {
            String n = cmd.name;
            if (!n.equals("eval_ro") && !n.equals("evalsha_ro") && !n.equals("fcall_ro")) {
                try {
                    rememberKeys(s, cur, c, cmd);
                } catch (RuntimeException e) {
                    /* never let key extraction take a command down */
                }
            }
        }
        depth--;
        if (depth == 0) {
            handlePendingKeyInvalidations(s);
            current = null;
        }
    }

    public static void beforeSleep(Server s) {
        broadcastInvalidationMessages(s);
    }

    /** called from Server.freeClient() */
    public static void onClientFree(Server s, Client c) {
        if (current == c) current = null;
        if (c.tracking) disableTracking(s, c);
    }
}
