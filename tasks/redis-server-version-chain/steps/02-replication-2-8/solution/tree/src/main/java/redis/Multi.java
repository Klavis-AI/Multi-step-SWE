package redis;

import java.util.*;

/** MULTI/EXEC/DISCARD/WATCH/UNWATCH. */
public final class Multi {
    public static void watchKey(Server s, Client c, byte[] key) {
        Key k = new Key(key);
        for (int i = 0; i < c.watched.size(); i++)
            if (c.watched.get(i).equals(k) && c.watchedDbs.get(i) == c.db) return;
        c.watched.add(k);
        c.watchedDbs.add(c.db);
        s.dbs[c.db].watchedKeys.computeIfAbsent(k, x -> new ArrayList<>()).add(c);
    }

    public static void unwatchAll(Server s, Client c) {
        for (int i = 0; i < c.watched.size(); i++) {
            Key k = c.watched.get(i);
            int dbid = c.watchedDbs.get(i);
            List<Client> l = s.dbs[dbid].watchedKeys.get(k);
            if (l != null) { l.remove(c); if (l.isEmpty()) s.dbs[dbid].watchedKeys.remove(k); }
        }
        c.watched.clear();
        c.watchedDbs.clear();
        c.dirtyCas = false;
    }

    public static void discard(Server s, Client c) {
        c.multiQueue.clear();
        c.inMulti = false;
        c.multiError = false;
        unwatchAll(s, c);
    }
}
